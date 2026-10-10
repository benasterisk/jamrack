#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "jamrack_git_hash.h"   // generated at build time (plugin/cmake/git_hash.cmake)

#include <cmath>

namespace
{
    // Wheel position for a time t (samples) inside the bend window:
    // 8192 -> 12288 over [rampStart, rampPeak), back to 8192 over [rampPeak, rampEnd).
    int wheelAt (int64_t t, int64_t rampStart, int64_t rampPeak, int64_t rampEnd) noexcept
    {
        if (t < rampStart || t >= rampEnd)
            return 8192;
        if (t < rampPeak)
            return 8192 + (int) std::lround (4096.0 * (double) (t - rampStart) / (double) (rampPeak - rampStart));
        return 12288 - (int) std::lround (4096.0 * (double) (t - rampPeak) / (double) (rampEnd - rampPeak));
    }
}

// BusesProperties is protected in AudioProcessor: built by a static member.
juce::AudioProcessor::BusesProperties GtmPrototypeProcessor::makeBuses()
{
   #if JAMRACK_IS_INST
    // One auxiliary input (see NoMainInput) and a silent stereo output.
    return BusesProperties()
               .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)
               .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
   #else
    return BusesProperties()
               .withInput ("Input", juce::AudioChannelSet::stereo(), true)
               .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
   #endif
}

GtmPrototypeProcessor::GtmPrototypeProcessor()
    : AudioProcessor (makeBuses()),
      params (*this, nullptr, "JAMRACK_M0", createLayout())
{
    channelParam = params.getRawParameterValue ("channel");
}

juce::AudioProcessorValueTreeState::ParameterLayout GtmPrototypeProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "channel", 1 }, "MIDI CH", 1, 16, 1));
    return layout;
}

juce::String GtmPrototypeProcessor::buildLabel()
{
    return juce::String (JucePlugin_Name) + "  " + JucePlugin_VersionString + "  (" + JAMRACK_GIT_HASH + ")";
}

void GtmPrototypeProcessor::resetState() noexcept
{
    if (noteOn)
        flushPending = true;      // its note-off goes out at the start of the next block
    armed = false;                // envelopes kept: a string still ringing re-arms, never triggers
    noteOn = false;
    sinceOn = 0;
    samplesToNextBend = 0;
}

void GtmPrototypeProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate > 0 ? newSampleRate : 44100.0;
    fastCoef = 1.0 - std::exp (-1.0 / (fastSeconds * sampleRate));
    slowCoef = 1.0 - std::exp (-1.0 / (slowSeconds * sampleRate));
    rampStart  = (int64_t) std::llround (0.100 * sampleRate);
    rampPeak   = (int64_t) std::llround (0.250 * sampleRate);
    rampEnd    = (int64_t) std::llround (0.400 * sampleRate);
    noteLength = (int64_t) std::llround (0.600 * sampleRate);
    resetState();
    setLatencySamples (0);    // detection is not a compensable delay (plan 3.2)
}

void GtmPrototypeProcessor::releaseResources() { resetState(); }

void GtmPrototypeProcessor::reset() { resetState(); }

bool GtmPrototypeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
   #if JAMRACK_IS_INST
    // the instrument's only input bus is the side-chain (index 0, auxiliary to the host)
    const auto side = layouts.inputBuses.isEmpty() ? juce::AudioChannelSet() : layouts.inputBuses.getReference (0);
    const bool sideOk = side.isDisabled() || side == juce::AudioChannelSet::mono() || side == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    juce::ignoreUnused (in);
    return sideOk && outOk;
   #else
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    return inOk && out == in;    // pass-through: same layout in and out
   #endif
}

void GtmPrototypeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();

    const int n = buffer.getNumSamples();
    const int channel = juce::jlimit (1, 16, (int) channelParam->load());

    if (flushPending)
    {
        midi.addEvent (juce::MidiMessage::noteOff (noteChannel, note), 0);
        midi.addEvent (juce::MidiMessage::pitchWheel (noteChannel, 8192), 0);
        lastWheel = 8192;
        flushPending = false;
    }

   #if JAMRACK_IS_INST
    auto side = getBusBuffer (buffer, true, 0);
    const float* in = side.getNumChannels() > 0 ? side.getReadPointer (0) : nullptr;
   #else
    const float* in = buffer.getNumChannels() > 0 ? buffer.getReadPointer (0) : nullptr;
   #endif

    const double triggerMs = std::pow (10.0, triggerDb / 10.0);   // mean-square threshold

    // the envelopes survive reset and bypass: one NaN/Inf sample must not stop the detector for good
    if (! std::isfinite (envFast) || ! std::isfinite (envSlow))
        envFast = envSlow = 0.0;

    for (int s = 0; s < n; ++s)
    {
        const double x = in != nullptr ? (double) in[s] : 0.0;
        envFast += fastCoef * (x * x - envFast);
        envSlow += slowCoef * (x * x - envSlow);
        const bool rising = envFast > triggerMs && envFast > riseRatio * envSlow;
        const bool settled = ! (envFast > triggerMs && envFast > rearmRatio * envSlow);

        if (noteOn)
        {
            ++sinceOn;
            if (sinceOn >= noteLength)
            {
                midi.addEvent (juce::MidiMessage::noteOff (noteChannel, note), s);
                midi.addEvent (juce::MidiMessage::pitchWheel (noteChannel, 8192), s);
                lastWheel = 8192;
                noteOn = false;
            }
            else if (--samplesToNextBend <= 0)
            {
                samplesToNextBend = bendEvery;
                const int w = wheelAt (sinceOn, rampStart, rampPeak, rampEnd);
                if (w != lastWheel || (sinceOn >= rampStart && sinceOn < rampEnd))
                {
                    midi.addEvent (juce::MidiMessage::pitchWheel (noteChannel, w), s);
                    lastWheel = w;
                }
            }
        }

        // Re-arm only once the note is over AND the rise has died down: an
        // attack during the held note is ignored, never replayed late at the
        // note-off; a string that keeps ringing does not block the next pluck.
        if (! armed && ! noteOn && settled)
            armed = true;

        if (armed && ! noteOn && rising)
        {
            armed = false;
            noteOn = true;
            noteChannel = channel;
            sinceOn = 0;
            samplesToNextBend = bendEvery;
            midi.addEvent (juce::MidiMessage::pitchWheel (noteChannel, 8192), s);
            midi.addEvent (juce::MidiMessage::noteOn (noteChannel, note, (juce::uint8) velocity), s);
            lastWheel = 8192;
            notesEmitted.fetch_add (1, std::memory_order_relaxed);
        }
    }

   #if JAMRACK_IS_INST
    // the instrument produces no sound of its own
    for (int ch = 0; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, n);
   #endif
    // Fx: the audio buffer is left untouched (pass-through)
}

void GtmPrototypeProcessor::trackEnvelope (const float* in, int n) noexcept
{
    if (! std::isfinite (envFast) || ! std::isfinite (envSlow))
        envFast = envSlow = 0.0;
    for (int s = 0; s < n; ++s)
    {
        const double x = in != nullptr ? (double) in[s] : 0.0;
        envFast += fastCoef * (x * x - envFast);
        envSlow += slowCoef * (x * x - envSlow);
    }
}

// Host bypass (the VST3 bypass parameter JUCE exposes): the sounding note is
// released at once (otherwise it would hang, and its timeline would resume
// late after un-bypass), the envelopes keep following the input and the
// detector stays disarmed, so a string still ringing at un-bypass plays
// nothing; the Fx passes the audio through and the Inst stays silent (JUCE's
// default bypass would copy the side-chain guitar to the instrument's output).
void GtmPrototypeProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    if (noteOn || flushPending)
    {
        midi.addEvent (juce::MidiMessage::noteOff (noteChannel, note), 0);
        midi.addEvent (juce::MidiMessage::pitchWheel (noteChannel, 8192), 0);
        lastWheel = 8192;
    }
    flushPending = false;
    noteOn = false;
    armed = false;
    sinceOn = 0;
    samplesToNextBend = 0;
   #if JAMRACK_IS_INST
    auto side = getBusBuffer (buffer, true, 0);
    trackEnvelope (side.getNumChannels() > 0 ? side.getReadPointer (0) : nullptr, buffer.getNumSamples());
    for (int ch = 0; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
   #else
    trackEnvelope (buffer.getNumChannels() > 0 ? buffer.getReadPointer (0) : nullptr, buffer.getNumSamples());
    // Fx: audio passes through unchanged
   #endif
}

juce::AudioProcessorEditor* GtmPrototypeProcessor::createEditor()
{
    return new GtmPrototypeEditor (*this);
}

void GtmPrototypeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = params.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void GtmPrototypeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (params.state.getType()))
            params.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GtmPrototypeProcessor();
}
