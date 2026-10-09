#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "jamrack_git_hash.h"   // generated at build time (plugin/cmake/git_hash.cmake)

#include "midpluck/jsmath.h"

#include <cmath>
#include <new>

using namespace midpluck;

namespace
{
    int clampInt (int v, int lo, int hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

    /** Plain value of an int / choice / bool parameter (APVTS raw values are denormalised). */
    int readInt (const std::atomic<float>* p) noexcept
    {
        const float v = p->load (std::memory_order_relaxed);
        return std::isfinite (v) ? static_cast<int> (std::lround (v)) : 0;
    }

    /** clamp(x, lo, hi) for a JS-rounded value; NaN -> lo. */
    int clampRounded (double x, int lo, int hi) noexcept
    {
        if (! (x >= lo)) return lo;
        if (x > hi) return hi;
        return static_cast<int> (x);
    }
}

//==============================================================================
juce::AudioProcessor::BusesProperties MidPluckProcessor::makeBuses()
{
    // mono or stereo (isBusesLayoutSupported), same layout in and out: a pass-through
    return BusesProperties()
               .withInput ("Input", juce::AudioChannelSet::stereo(), true)
               .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

MidPluckProcessor::MidPluckProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "MIDPLUCK", params::createLayout())
{
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (params::id::bypass));
    jassert (bypassParam != nullptr);
    pGain = apvts.getRawParameterValue (params::id::gain);
    pSens = apvts.getRawParameterValue (params::id::sens);
    pDecay = apvts.getRawParameterValue (params::id::decay);
    pDyn = apvts.getRawParameterValue (params::id::dyn);
    pBend = apvts.getRawParameterValue (params::id::bend);
    pRange = apvts.getRawParameterValue (params::id::range);
    pOctave = apvts.getRawParameterValue (params::id::octave);
    pTranspose = apvts.getRawParameterValue (params::id::transpose);
    pMode = apvts.getRawParameterValue (params::id::mode);
    pChannel = apvts.getRawParameterValue (params::id::channel);
    pBypass = apvts.getRawParameterValue (params::id::bypass);

    // MONO is small (a few kB): built now at a default rate so that it is never
    // null; prepareToPlay rebuilds it at the host rate. POLY is built by the
    // builder thread from the first prepareToPlay on (an instance that is only
    // scanned never renders a bank).
    monoApplied = monoParamsNow();
    appliedOctave = static_cast<int> (monoApplied.octave);
    appliedTranspose = static_cast<int> (monoApplied.transpose);
    mono = std::make_unique<MonoTracker> (hostRate, monoApplied);
    monoRate = hostRate;

    startTimer (100);
}

MidPluckProcessor::~MidPluckProcessor()
{
    stopTimer();
    builder.signalThreadShouldExit();
    builder.notify();
    builder.stopThread (10000);
    drainRetired();
    delete polyNext.exchange (nullptr, std::memory_order_acq_rel);
    delete polyCurrent;
    polyCurrent = nullptr;
}

juce::String MidPluckProcessor::buildLabel()
{
    return juce::String (JucePlugin_Name) + " " + JucePlugin_VersionString + " (" + JAMRACK_GIT_HASH + ")";
}

//==============================================================================
MonoParams MidPluckProcessor::monoParamsNow() const noexcept
{
    MonoParams p;   // fmin, fmax, a4: DEFAULT_PARAMS (not plugin parameters: setParams never allocates)
    p.sens = params::sliderValue (pSens->load (std::memory_order_relaxed));
    p.release = params::sliderValue (pDecay->load (std::memory_order_relaxed));
    p.dyn = params::sliderValue (pDyn->load (std::memory_order_relaxed));
    p.bend = pBend->load (std::memory_order_relaxed) >= 0.5f;
    p.octave = clampInt (readInt (pOctave), -2, 2);
    p.transpose = clampInt (readInt (pTranspose), -12, 12);
    return p;
}

void MidPluckProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    hostRate = newSampleRate > 0 ? newSampleRate : 48000.0;
    hostBlock = samplesPerBlock > 0 ? samplesPerBlock : 512;

    // MONO: decimation round(sr / 24000) and the biquads depend on the rate.
    const MonoParams mp = monoParamsNow();
    if (mono == nullptr || monoRate != hostRate)
    {
        mono = std::make_unique<MonoTracker> (hostRate, mp);
        monoRate = hostRate;
    }
    else
    {
        mono->setParams (mp);
        mono->reset();
    }
    monoApplied = mp;
    appliedOctave = static_cast<int> (mp.octave);
    appliedTranspose = static_cast<int> (mp.transpose);

    // POLY: only the resampler depends on the host rate; the bank is kept
    // (plan 3.1) and this never waits for the builder.
    ecoBudgetMs = PolyEngineState::ecoBudgetMs (hostRate, hostBlock);
    polyWantedRate.store (hostRate, std::memory_order_release);
    if (polyCurrent == nullptr)
        polyCurrent = polyNext.exchange (nullptr, std::memory_order_acq_rel);   // adopt a finished build
    if (polyCurrent != nullptr)
    {
        if (polyCurrent->rate != hostRate)
        {
            polyCurrent->state.setSampleRate (hostRate);   // allocates: fine here, never on the audio thread
            polyCurrent->rate = hostRate;
        }
        polyCurrent->state.reset();
        polyCurrent->state.setBudgetMs (ecoBudgetMs);
        polyCurrent->state.setParams (appliedOctave, appliedTranspose);
    }
    else
    {
        requestPolyBuild();
    }
    if (! builder.isThreadRunning())
        builder.startThread (juce::Thread::Priority::normal);   // ~10-20 ms of work; "low" may park it on efficiency cores

    // what the host still holds is released at the start of the next block; with
    // nothing held, the wheel reset of that flush goes to the channel in use
    if (const int ch = clampInt (readInt (pChannel), 1, 16); sounding.empty() && ch != curChannel)
    {
        curChannel = ch;
        lastWheel = -1;
    }
    shellFlushPending.store (true, std::memory_order_release);
    setLatencySamples (0);    // detection is not a compensable delay (plan 3.2)
}

void MidPluckProcessor::releaseResources()
{
    engineResetPending.store (true, std::memory_order_release);
}

// May be called on the audio thread (VST3 setProcessing(false)): only a flag.
void MidPluckProcessor::reset()
{
    engineResetPending.store (true, std::memory_order_release);
}

bool MidPluckProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    return inOk && out == in;    // pass-through: same layout in and out
}

//==============================================================================
// POLY state life cycle (rule 3.3-5)

void MidPluckProcessor::PolyBuilder::run()
{
    while (! threadShouldExit())
    {
        if (! owner.polyBuildRequested.exchange (false, std::memory_order_acq_rel))
        {
            wait (-1);
            continue;
        }
        const double rate = owner.polyWantedRate.load (std::memory_order_acquire);
        if (rate <= 0)
            continue;

        PolyHolder* h = nullptr;
        try { h = new PolyHolder (rate); }   // bank + CSR + decomposer + rule + tracker: tens of ms
        catch (const std::bad_alloc&) { h = nullptr; }
        if (h == nullptr)
            continue;
        if (threadShouldExit())
        {
            delete h;
            break;
        }
        // A state still in polyNext was never taken by the audio thread (it
        // takes with exchange(nullptr)): nobody else can hold it, delete it.
        delete owner.polyNext.exchange (h, std::memory_order_acq_rel);
    }
}

void MidPluckProcessor::requestPolyBuild()
{
    polyBuildRequested.store (true, std::memory_order_release);
    builder.notify();
}

bool MidPluckProcessor::retire (PolyHolder* h) noexcept
{
    if (h == nullptr)
        return true;
    const auto scope = retireFifo.write (1);
    if (scope.blockSize1 > 0) { retireSlots[scope.startIndex1] = h; return true; }
    if (scope.blockSize2 > 0) { retireSlots[scope.startIndex2] = h; return true; }
    return false;
}

void MidPluckProcessor::drainRetired()
{
    const auto scope = retireFifo.read (retireFifo.getNumReady());
    scope.forEach ([this] (int i)
    {
        delete retireSlots[i];
        retireSlots[i] = nullptr;
    });
}

void MidPluckProcessor::timerCallback()
{
    drainRetired();
    if (polyRebuildNeeded.exchange (false, std::memory_order_acq_rel))
        requestPolyBuild();
}

void MidPluckProcessor::installPolyState (bool emit) noexcept
{
    PolyHolder* h = polyNext.exchange (nullptr, std::memory_order_acq_rel);
    if (h == nullptr)
        return;

    if (h->rate != hostRate)
    {
        // built for a rate the host left meanwhile: never used, rebuilt by the timer
        if (! retire (h))
            jassertfalse;   // retire queue full (31 states pending): leaked, never freed on this thread
        polyRebuildNeeded.store (true, std::memory_order_release);
        return;
    }

    h->state.setBudgetMs (ecoBudgetMs);
    h->state.setParams (appliedOctave, appliedTranspose);
    if (polyCurrent != nullptr)
    {
        if (activeMode == params::modePoly)
        {
            if (emit) flushActive (0);
            else flushSilently();
        }
        if (! retire (polyCurrent))
            jassertfalse;   // see above
    }
    polyCurrent = h;
}

//==============================================================================
// MIDI out (contract 3.2)

void MidPluckProcessor::sendWheel (int value, int at) noexcept
{
    if (value == lastWheel)
        return;
    midiOut->addEvent (juce::MidiMessage::pitchWheel (curChannel, value), at);
    lastWheel = value;
}

void MidPluckProcessor::releaseAll (int at) noexcept
{
    sounding.releaseAll ([this, at] (int midi, int channel)
    {
        midiOut->addEvent (juce::MidiMessage::noteOff (channel, midi), at);
    });
}

void MidPluckProcessor::dispatch (const EventList& list, int base) noexcept
{
    const int last = blockSamples > 0 ? blockSamples - 1 : 0;
    const bool isMono = activeMode == params::modeMono;
    for (int i = 0; i < list.count; ++i)
    {
        const Event& e = list.items[i];
        const int at = clampInt (base + e.sampleOffset, 0, last);
        switch (e.type)
        {
            case EventType::NoteOn:
            {
                const int m = clampInt (e.midi, 0, 127);
                const int v = clampRounded (js::round (e.vel * 127), 1, 127);
                if (isMono) sendWheel (8192, at);
                midiOut->addEvent (juce::MidiMessage::noteOn (curChannel, m, static_cast<juce::uint8> (v)), at);
                sounding.add (m, curChannel);
                meters.notesSent.fetch_add (1, std::memory_order_relaxed);
                break;
            }
            case EventType::NoteOff:
            {
                const int m = clampInt (e.midi, 0, 127);
                if (sounding.has (m))
                {
                    const int channel = sounding.remove (m);
                    midiOut->addEvent (juce::MidiMessage::noteOff (channel, m), at);
                }
                else if (isMono && ! sounding.empty())
                {
                    releaseAll (at);   // a shift change mislabelled the note-off: release what is held
                }
                if (isMono) sendWheel (8192, at);
                break;
            }
            case EventType::Bend:
            {
                if (isMono)
                    sendWheel (clampRounded (8192 + js::round (e.semis / bendRange * 8192), 0, 16383), at);
                break;
            }
            case EventType::Meter:
            {
                meters.db.store (static_cast<float> (e.db), std::memory_order_relaxed);
                if (isMono)
                {
                    meters.midiF.store (static_cast<float> (e.midiF), std::memory_order_relaxed);
                    meters.latMs.store (static_cast<float> (e.latMs), std::memory_order_relaxed);
                }
                else
                {
                    meters.voices.store (e.voices, std::memory_order_relaxed);
                    meters.hopMs.store (static_cast<float> (e.hopMs), std::memory_order_relaxed);
                    meters.eco.store (e.eco, std::memory_order_relaxed);
                }
                break;
            }
        }
    }
}

void MidPluckProcessor::flushActive (int at) noexcept
{
    events.clear();
    if (activeMode == params::modeMono) mono->flush (events);
    else if (polyCurrent != nullptr) polyCurrent->state.flush (events);
    dispatch (events, at);
    events.clear();
    releaseAll (at);
    sendWheel (8192, at);
}

void MidPluckProcessor::flushSilently() noexcept
{
    events.clear();
    if (activeMode == params::modeMono) mono->flush (events);
    else if (polyCurrent != nullptr) polyCurrent->state.flush (events);
    events.clear();
    sounding.clear();
}

void MidPluckProcessor::resetActive() noexcept
{
    if (activeMode == params::modeMono) mono->reset();
    else if (polyCurrent != nullptr) polyCurrent->state.reset();
}

void MidPluckProcessor::resetEngines() noexcept
{
    mono->reset();
    if (polyCurrent != nullptr) polyCurrent->state.reset();
}

//==============================================================================
void MidPluckProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    midiOut = &midi;
    const int n = buffer.getNumSamples();
    blockSamples = n;

    const bool bypass = pBypass->load (std::memory_order_relaxed) >= 0.5f;
    const int mode = readInt (pMode) == params::modePoly ? params::modePoly : params::modeMono;
    const int channel = clampInt (readInt (pChannel), 1, 16);
    const MonoParams mp = monoParamsNow();
    const int octave = static_cast<int> (mp.octave);
    const int transpose = static_cast<int> (mp.transpose);
    bendRange = params::rangeSemis[clampInt (readInt (pRange), 0, 3)];

    // 1. entering bypass: release everything once, then nothing more is sent
    if (bypass && ! wasBypassed)
    {
        flushActive (0);
        wasBypassed = true;
    }

    // 2. a POLY state finished by the builder (its predecessor is flushed if active)
    installPolyState (! bypass);

    // 3. prepareToPlay / reset / releaseResources asked for a flush (and a reset)
    const bool doReset = engineResetPending.exchange (false, std::memory_order_acq_rel);
    if (shellFlushPending.exchange (false, std::memory_order_acq_rel) || doReset)
    {
        if (bypass) flushSilently();
        else flushActive (0);
    }
    if (doReset)
        resetEngines();

    meters.polyReady.store (polyCurrent != nullptr, std::memory_order_relaxed);
    meters.budgetMs.store (static_cast<float> (ecoBudgetMs), std::memory_order_relaxed);

    if (bypass)
    {
        // audio unchanged, no engine fed, no event; parameter changes are
        // taken silently (nothing sounds), the engine is reset at the exit
        activeMode = mode;
        if (channel != curChannel) { curChannel = channel; lastWheel = -1; }
        if (octave != appliedOctave || transpose != appliedTranspose)
        {
            appliedOctave = octave;
            appliedTranspose = transpose;
            if (polyCurrent != nullptr) polyCurrent->state.setParams (octave, transpose);
        }
        if (mp.sens != monoApplied.sens || mp.release != monoApplied.release || mp.dyn != monoApplied.dyn
            || mp.bend != monoApplied.bend || mp.octave != monoApplied.octave || mp.transpose != monoApplied.transpose)
        {
            mono->setParams (mp);
            monoApplied = mp;
        }
        meters.mode.store (activeMode, std::memory_order_relaxed);
        meters.bypassed.store (true, std::memory_order_relaxed);
        midiOut = nullptr;
        return;
    }

    // 4. leaving bypass: the active engine starts from a clean state
    if (wasBypassed)
    {
        wasBypassed = false;
        resetActive();
    }

    // 5. mode switch: flush the outgoing engine, reset the incoming one (plan 3.2)
    if (mode != activeMode)
    {
        flushActive (0);
        activeMode = mode;
        resetActive();
    }

    // 6. MIDI channel change: notes and wheel of the old channel released first
    if (channel != curChannel)
    {
        flushActive (0);
        curChannel = channel;
        lastWheel = -1;
    }

    // 7. octave / transpose change: flush with the old shift, then apply
    if (octave != appliedOctave || transpose != appliedTranspose)
    {
        flushActive (0);
        appliedOctave = octave;
        appliedTranspose = transpose;
        if (polyCurrent != nullptr) polyCurrent->state.setParams (octave, transpose);
    }
    if (mp.sens != monoApplied.sens || mp.release != monoApplied.release || mp.dyn != monoApplied.dyn
        || mp.bend != monoApplied.bend || mp.octave != monoApplied.octave || mp.transpose != monoApplied.transpose)
    {
        mono->setParams (mp);   // same fmin/fmax: rewrites a few doubles, no allocation
        monoApplied = mp;
    }

    // 8. the engine, in chunks of at most maxChunk samples
    PolyHolder* poly = polyCurrent;
    const bool isMono = activeMode == params::modeMono;
    const float* in = (getTotalNumInputChannels() > 0 && buffer.getNumChannels() > 0) ? buffer.getReadPointer (0) : nullptr;
    if (in != nullptr && (isMono || poly != nullptr))
    {
        // the gain as the 0.01-step value it shows (exactly 1 at the default, whatever
        // rounding the host's normalised value went through)
        const float g = static_cast<float> (params::sliderValue (pGain->load (std::memory_order_relaxed)));
        for (int pos = 0; pos < n; pos += maxChunk)
        {
            const int len = juce::jmin (maxChunk, n - pos);
            const float* src = in + pos;
            if (g != 1.0f)
            {
                for (int i = 0; i < len; ++i) scratch[i] = src[i] * g;
                src = scratch;
            }
            events.clear();
            if (isMono) mono->process (src, len, events);
            else poly->state.process (src, len, events);
            if (events.dropped > 0)
                meters.droppedEvents.fetch_add (events.dropped, std::memory_order_relaxed);
            dispatch (events, pos);
        }
        events.clear();
    }

    meters.mode.store (activeMode, std::memory_order_relaxed);
    meters.bypassed.store (false, std::memory_order_relaxed);
    midiOut = nullptr;
    // the audio buffer is left untouched: pass-through
}

//==============================================================================
juce::AudioProcessorEditor* MidPluckProcessor::createEditor()
{
    return new MidPluckEditor (*this);
}

void MidPluckProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MidPluckProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

    // juce::AudioParameterBool keeps the raw normalised value a host sent (0.54
    // reads as "on"), and the APVTS does not rewrite a parameter whose snapped
    // value already equals the state's: a reload could leave 0.54 where 1 was
    // saved (pluginval, strictness 10, "Plugin state restoration"). Snap them.
    for (auto* p : getParameters())
        if (auto* b = dynamic_cast<juce::AudioParameterBool*> (p))
        {
            const float v = b->get() ? 1.0f : 0.0f;
            if (p->getValue() != v)
                p->setValueNotifyingHost (v);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MidPluckProcessor();
}
