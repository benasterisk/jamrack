// vst3_probe: a minimal command-line VST3 host that loads an INSTALLED .vst3
// bundle, feeds it a WAV file block by block like a DAW would, and writes
// every MIDI message the plugin emits, stamped to the sample, as JSON.
//
//   vst3_probe --plugin "D:/VST3/MidPluck.vst3" --wav plucks.wav --out events.json
//              [--block 128] [--channel 5] [--bypass-from 2.3] [--bypass-to 3.0]
//              [--reprepare-at 2.8 [--reprepare-rate 48000]] [--tail 1.0] [--warmup-ms 500] [--realtime]
//              [--param NAME=VALUE]... [--param-norm NAME=X]...
//              [--param-at T:NAME=VALUE]... [--param-norm-at T:NAME=X]...
//   vst3_probe --plugin "D:/VST3/MidPluck.vst3" --state-test --out state.json
//   vst3_probe --plugin "D:/VST3/MidPluck.vst3" --list-params
//
// Parameters (MidPluck names: GAIN, SENS, DECAY, DYN, BEND, BEND RANGE,
// OCTAVE, TRANSPOSE, MODE, MIDI CH, BYPASS; matched without regard to case,
// "channel" and "range" also accepted):
//   --param NAME=VALUE        PLAIN value, parsed by the plugin's own text
//                             parser exactly as a host's text entry would be
//                             (VST3 getParamValueByString): "mode=poly",
//                             "octave=-1", "BEND RANGE=12", "bend=off"
//   --param-norm NAME=X       NORMALISED value 0..1, sent as is
//   --param-at T:NAME=VALUE   plain value applied at the first block that
//   --param-norm-at T:NAME=X  starts at or after T seconds (mid-run changes)
// --param / --param-norm are applied before prepareToPlay; a hosted VST3
// parameter change reaches the plugin at its next process call.
// --warmup-ms waits after prepareToPlay before the first block, so that work
// a plugin does on a background thread (MidPluck renders its POLY engine
// there) is ready, as it would be after the first ~100 ms in a DAW.
// --realtime paces the blocks at the audio rate (a block is not sent before
// its time has come), as a DAW does; without it the file runs as fast as the
// plugin allows.
// --state-test: sets every parameter to a non-default value on one instance,
// saves the state, loads it into a NEW instance and compares every parameter.
//
// It answers, without a DAW and without a person at the screen, what a host
// sees: which notes come out and at which sample, the pitch-wheel values,
// the channel, what happens around a host bypass, a parameter change or a
// deactivate/reactivate (releaseResources + prepareToPlay, what a host does
// when a device is switched off and on), and whether the audio passes through
// (effect) or stays silent (instrument). Every input bus is enabled and fed the
// WAV, so an instrument whose only input is a side-chain receives it on that bus.
// Limits: JUCE hosting passes the same buffer as input and output, and does
// not reproduce Live's own routing, tap point or level.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

namespace
{
    struct ParamSet
    {
        juce::String name, value;
        bool normalised = false;
        double at = -1.0;             // seconds; < 0: before prepareToPlay
        bool done = false;
    };

    struct Args
    {
        juce::String plugin, wav, out;
        int block = 128;
        int channel = 0;              // 0 = leave the plugin's default
        double bypassFrom = -1.0, bypassTo = -1.0;
        double reprepareAt = -1.0;
        double reprepareRate = 0.0;   // 0 = the WAV's rate (a host switching the device off and on)
        double tail = 1.0;            // seconds of silence after the file
        int warmupMs = 0;
        bool stateTest = false, listParams = false, realtime = false;
        std::vector<ParamSet> sets;
    };

    bool parseSet (const juce::String& spec, bool normalised, bool timed, Args& a)
    {
        ParamSet s;
        s.normalised = normalised;
        juce::String rest = spec;
        if (timed)
        {
            if (! rest.containsChar (':')) return false;
            s.at = rest.upToFirstOccurrenceOf (":", false, false).getDoubleValue();
            rest = rest.fromFirstOccurrenceOf (":", false, false);
        }
        if (! rest.containsChar ('=')) return false;
        s.name = rest.upToFirstOccurrenceOf ("=", false, false).trim();
        s.value = rest.fromFirstOccurrenceOf ("=", false, false).trim();
        if (s.name.isEmpty()) return false;
        a.sets.push_back (s);
        return true;
    }

    bool parse (int argc, char** argv, Args& a)
    {
        for (int i = 1; i < argc; ++i)
        {
            const juce::String k (argv[i]);
            auto next = [&] { return i + 1 < argc ? juce::String (argv[++i]) : juce::String(); };
            if (k == "--plugin") a.plugin = next();
            else if (k == "--wav") a.wav = next();
            else if (k == "--out") a.out = next();
            else if (k == "--block") a.block = next().getIntValue();
            else if (k == "--channel") a.channel = next().getIntValue();
            else if (k == "--bypass-from") a.bypassFrom = next().getDoubleValue();
            else if (k == "--bypass-to") a.bypassTo = next().getDoubleValue();
            else if (k == "--reprepare-at") a.reprepareAt = next().getDoubleValue();
            else if (k == "--reprepare-rate") a.reprepareRate = next().getDoubleValue();
            else if (k == "--tail") a.tail = next().getDoubleValue();
            else if (k == "--warmup-ms") a.warmupMs = next().getIntValue();
            else if (k == "--state-test") a.stateTest = true;
            else if (k == "--realtime") a.realtime = true;
            else if (k == "--list-params") a.listParams = true;
            else if (k == "--param") { if (! parseSet (next(), false, false, a)) return false; }
            else if (k == "--param-norm") { if (! parseSet (next(), true, false, a)) return false; }
            else if (k == "--param-at") { if (! parseSet (next(), false, true, a)) return false; }
            else if (k == "--param-norm-at") { if (! parseSet (next(), true, true, a)) return false; }
            else { std::fprintf (stderr, "unknown option %s\n", argv[i]); return false; }
        }
        if (a.plugin.isEmpty() || a.block <= 0) return false;
        if (a.listParams) return true;
        if (a.stateTest) return a.out.isNotEmpty();
        return a.wav.isNotEmpty() && a.out.isNotEmpty();
    }

    juce::String num (double v) { return std::isfinite (v) ? juce::String (v, 9) : juce::String ("null"); }

    juce::String jsonString (const juce::String& s)
    {
        return "\"" + s.replace ("\\", "\\\\").replace ("\"", "\\\"") + "\"";
    }

    juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& plugin, const juce::String& name)
    {
        static const std::map<juce::String, juce::String> aliases = {
            { "channel", "MIDI CH" }, { "range", "BEND RANGE" }
        };
        juce::String wanted = name;
        if (auto it = aliases.find (name.toLowerCase()); it != aliases.end())
            wanted = it->second;
        for (auto* p : plugin.getParameters())
            if (p->getName (64).equalsIgnoreCase (wanted))
                return p;
        return nullptr;
    }

    /** Applies one --param*; returns false (and says why) if the name is unknown. */
    bool applySet (juce::AudioPluginInstance& plugin, const ParamSet& s)
    {
        auto* p = findParam (plugin, s.name);
        if (p == nullptr)
        {
            std::fprintf (stderr, "no parameter named \"%s\" (try --list-params)\n", s.name.toRawUTF8());
            return false;
        }
        const float norm = s.normalised ? juce::jlimit (0.0f, 1.0f, (float) s.value.getDoubleValue())
                                        : p->getValueForText (s.value);
        p->setValueNotifyingHost (norm);
        std::printf ("%s%s = %s (normalised %.6f)\n", s.at >= 0 ? ("[t=" + juce::String (s.at) + " s] ").toRawUTF8() : "",
                     p->getName (64).toRawUTF8(), p->getText (p->getValue(), 64).toRawUTF8(), (double) p->getValue());
        return true;
    }

    juce::String paramsJson (juce::AudioPluginInstance& plugin)
    {
        juce::String j;
        int k = 0;
        for (auto* p : plugin.getParameters())
            j << (k++ ? ",\n  " : "  ") << "{\"name\":" << jsonString (p->getName (64))
              << ",\"text\":" << jsonString (p->getText (p->getValue(), 64))
              << ",\"value\":" << num (p->getValue())
              << ",\"default\":" << num (p->getDefaultValue()) << "}";
        return j;
    }

    void processSilence (juce::AudioPluginInstance& plugin, int block, int blocks)
    {
        const int nCh = juce::jmax (plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels(), 1);
        juce::AudioBuffer<float> buf (nCh, block);
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            midi.clear();
            plugin.processBlock (buf, midi);
        }
    }

    /** --state-test: values -> getStateInformation -> NEW instance -> setStateInformation -> compare. */
    int runStateTest (juce::AudioPluginFormatManager& fm, const juce::PluginDescription& desc, const Args& a)
    {
        const double sr = 48000.0;
        juce::String error;
        auto first = fm.createPluginInstance (desc, sr, a.block, error);
        if (first == nullptr) { std::fprintf (stderr, "load failed: %s\n", error.toRawUTF8()); return 1; }
        first->prepareToPlay (sr, a.block);

        // a deterministic non-default value for every parameter (the bypass
        // included), on a legal step for the stepped ones (bool, choice, int,
        // float with an interval), so that the plugin stores what was asked
        int i = 0;
        for (auto* p : first->getParameters())
        {
            double v = std::fmod (0.37 + 0.29 * i++, 1.0);
            const int steps = p->getNumSteps();
            if (steps >= 2 && steps <= 100000)
            {
                const int last = steps - 1;
                int k = (int) std::lround (v * last);
                if (std::abs ((double) k / last - p->getDefaultValue()) < 1e-6) k = (k + 1) % steps;
                v = (double) k / last;
            }
            else if (std::abs (v - p->getDefaultValue()) < 0.05)
            {
                v = std::fmod (v + 0.5, 1.0);
            }
            p->setValueNotifyingHost ((float) v);
        }
        for (const auto& s : a.sets)
            if (! applySet (*first, s)) return 1;
        processSilence (*first, a.block, 4);    // the changes reach the processor

        juce::MemoryBlock state;
        first->getStateInformation (state);

        auto second = fm.createPluginInstance (desc, sr, a.block, error);
        if (second == nullptr) { std::fprintf (stderr, "second load failed: %s\n", error.toRawUTF8()); return 1; }
        second->setStateInformation (state.getData(), (int) state.getSize());
        second->prepareToPlay (sr, a.block);
        processSilence (*second, a.block, 4);

        const auto& pa = first->getParameters();
        const auto& pb = second->getParameters();
        int mismatches = 0, changed = 0;
        juce::String rows;
        for (int k = 0; k < juce::jmin (pa.size(), pb.size()); ++k)
        {
            const float va = pa[k]->getValue(), vb = pb[k]->getValue();
            const juce::String ta = pa[k]->getText (va, 64), tb = pb[k]->getText (vb, 64);
            // The host caches the normalised value it sent; the plugin stores it
            // snapped to the parameter's interval (an int, a 0.01 step). What the
            // first instance really holds is its text, read back by the plugin's
            // own parser: the second instance must hold exactly that.
            const float expected = pa[k]->getValueForText (ta);
            const bool same = ta == tb && std::abs ((double) vb - (double) expected) <= 1e-6;
            if (! same) ++mismatches;
            if (ta != pa[k]->getText (pa[k]->getDefaultValue(), 64)) ++changed;
            rows << (k ? ",\n  " : "  ") << "{\"name\":" << jsonString (pa[k]->getName (64))
                 << ",\"saved\":" << jsonString (ta) << ",\"loaded\":" << jsonString (tb)
                 << ",\"savedValue\":" << num (va) << ",\"expectedValue\":" << num (expected) << ",\"loadedValue\":" << num (vb)
                 << ",\"default\":" << num (pa[k]->getDefaultValue())
                 << ",\"same\":" << (same ? "true" : "false") << "}";
        }
        if (pa.size() != pb.size()) ++mismatches;

        // the second instance, saved again, must give back the same bytes
        juce::MemoryBlock again;
        second->getStateInformation (again);
        const bool identical = again == state;
        first->releaseResources();
        second->releaseResources();
        const bool ok = mismatches == 0 && changed == pa.size() && identical;

        juce::String json;
        json << "{\n \"plugin\": " << jsonString (first->getName()) << ",\n"
             << " \"stateBytes\": " << (int) state.getSize() << ",\n"
             << " \"resavedStateIdentical\": " << (identical ? "true" : "false") << ",\n"
             << " \"parameters\": " << pa.size() << ",\n"
             << " \"changedFromDefault\": " << changed << ",\n"
             << " \"mismatches\": " << mismatches << ",\n"
             << " \"ok\": " << (ok ? "true" : "false") << ",\n"
             << " \"rows\": [\n" << rows << "\n ]\n}\n";
        if (! juce::File (a.out).replaceWithText (json))
        {
            std::fprintf (stderr, "cannot write %s\n", a.out.toRawUTF8());
            return 1;
        }
        std::printf ("state test: %d parameters, %d changed from default, %d mismatches after reload, "
                     "re-saved state %s (%d bytes): %s\n",
                     pa.size(), changed, mismatches, identical ? "identical" : "DIFFERENT", (int) state.getSize(),
                     ok ? "OK" : "FAILED");
        first.reset();
        second.reset();
        return ok ? 0 : 1;
    }
}

int main (int argc, char** argv)
{
    Args a;
    if (! parse (argc, argv, a))
    {
        std::fprintf (stderr, "usage: vst3_probe --plugin <x.vst3> --wav <in.wav> --out <events.json> "
                              "[--block 128] [--channel N] [--bypass-from s --bypass-to s] [--reprepare-at s [--reprepare-rate Hz]] "
                              "[--tail s] [--warmup-ms N] [--realtime] [--param NAME=VALUE] [--param-norm NAME=X] "
                              "[--param-at T:NAME=VALUE] [--param-norm-at T:NAME=X]\n"
                              "       vst3_probe --plugin <x.vst3> --state-test --out <state.json>\n"
                              "       vst3_probe --plugin <x.vst3> --list-params\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    // ---- the plugin, through the same VST3 hosting code JUCE-based DAWs use
    juce::AudioPluginFormatManager fm;
    fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* f : fm.getFormats())
        f->findAllTypesForFile (types, a.plugin);
    if (types.isEmpty()) { std::fprintf (stderr, "no VST3 plugin in %s\n", a.plugin.toRawUTF8()); return 1; }

    if (a.stateTest)
        return runStateTest (fm, *types[0], a);

    if (a.listParams)
    {
        juce::String error;
        auto plugin = fm.createPluginInstance (*types[0], 48000.0, a.block, error);
        if (plugin == nullptr) { std::fprintf (stderr, "load failed: %s\n", error.toRawUTF8()); return 1; }
        for (auto* p : plugin->getParameters())
            std::printf ("%-12s default %-14s (normalised %.4f, %d steps)%s\n", p->getName (64).toRawUTF8(),
                         p->getText (p->getDefaultValue(), 64).toRawUTF8(), (double) p->getDefaultValue(),
                         p->getNumSteps(), p == plugin->getBypassParameter() ? "  [bypass parameter]" : "");
        return 0;
    }

    // ---- the WAV (first channel, as the plugins read channel 0)
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (a.wav)));
    if (reader == nullptr) { std::fprintf (stderr, "cannot read %s\n", a.wav.toRawUTF8()); return 1; }
    const double sr = reader->sampleRate;
    const int total = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> wav (1, total);
    reader->read (&wav, 0, total, 0, true, false);

    juce::String error;
    auto plugin = fm.createPluginInstance (*types[0], sr, a.block, error);
    if (plugin == nullptr) { std::fprintf (stderr, "load failed: %s\n", error.toRawUTF8()); return 1; }

    // every bus enabled in stereo (the instrument's only input is its side-chain)
    auto layout = plugin->getBusesLayout();
    for (auto& b : layout.inputBuses)  b = juce::AudioChannelSet::stereo();
    for (auto& b : layout.outputBuses) b = juce::AudioChannelSet::stereo();
    const bool layoutOk = plugin->setBusesLayout (layout);

    if (a.channel > 0)
        for (auto* p : plugin->getParameters())
            if (p->getName (32) == "MIDI CH")
            {
                // hosted parameters are normalised: 1..16 maps to 0..1
                p->setValueNotifyingHost ((float) (a.channel - 1) / 15.0f);
                std::printf ("MIDI CH set to %d (plugin reads \"%s\")\n", a.channel,
                             p->getText (p->getValue(), 16).toRawUTF8());
            }
    for (auto& s : a.sets)
        if (s.at < 0)
        {
            if (! applySet (*plugin, s)) return 1;
            s.done = true;
        }

    auto* bypass = plugin->getBypassParameter();
    plugin->setRateAndBufferSizeDetails (sr, a.block);
    plugin->prepareToPlay (sr, a.block);
    if (a.warmupMs > 0)
        juce::Thread::sleep (a.warmupMs);

    const int nIn = plugin->getTotalNumInputChannels();
    const int nOut = plugin->getTotalNumOutputChannels();
    const int nCh = juce::jmax (nIn, nOut, 1);
    juce::AudioBuffer<float> buf (nCh, a.block);
    juce::MidiBuffer midi;

    juce::String ev;
    int nEvents = 0;
    double passMaxDiff = 0.0, outMaxAbs = 0.0;
    double processMs = 0.0, maxBlockMs = 0.0;   // wall time inside processBlock (the whole VST3 call)
    long long blocks = 0;
    long long passMismatches = 0, passCompared = 0;
    bool bypassed = false;
    const int bypassFrom = a.bypassFrom >= 0 ? (int) std::llround (a.bypassFrom * sr) : -1;
    const int bypassTo = a.bypassTo >= 0 ? (int) std::llround (a.bypassTo * sr) : -1;
    const int reprepareAt = a.reprepareAt >= 0 ? (int) std::llround (a.reprepareAt * sr) : -1;
    bool reprepared = false;
    juce::String changes;
    int nChanges = 0;

    // some silence after the file lets the last note end
    const int tail = (int) (juce::jmax (0.0, a.tail) * sr);
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    for (int start = 0; start < total + tail; start += a.block)
    {
        const int n = juce::jmin (a.block, total + tail - start);
        if (a.realtime)
            while (juce::Time::getMillisecondCounterHiRes() - t0 < 1000.0 * start / sr)
                juce::Thread::sleep (1);
        buf.setSize (nCh, n, false, false, true);
        buf.clear();
        for (int ch = 0; ch < nIn; ++ch)
            for (int s = 0; s < n; ++s)
                buf.setSample (ch, s, start + s < total ? wav.getSample (0, start + s) : 0.0f);

        if (reprepareAt >= 0 && ! reprepared && start >= reprepareAt)
        {
            // --reprepare-rate: the host changes its sample rate (the probe keeps
            // feeding the same samples: only the plugin's handling is tested)
            const double newRate = a.reprepareRate > 0 ? a.reprepareRate : sr;
            plugin->releaseResources();
            plugin->setRateAndBufferSizeDetails (newRate, a.block);
            plugin->prepareToPlay (newRate, a.block);
            reprepared = true;
        }

        for (auto& s : a.sets)
            if (! s.done && start >= (int) std::llround (s.at * sr))
            {
                if (! applySet (*plugin, s)) return 1;
                s.done = true;
                auto* p = findParam (*plugin, s.name);
                changes << (nChanges++ ? ",\n  " : "  ") << "{\"sample\":" << start << ",\"name\":" << jsonString (p->getName (64))
                        << ",\"text\":" << jsonString (p->getText (p->getValue(), 64)) << "}";
            }

        const bool wantBypass = bypassFrom >= 0 && start >= bypassFrom && (bypassTo < 0 || start < bypassTo);
        if (bypass != nullptr && wantBypass != bypassed)
        {
            bypass->setValueNotifyingHost (wantBypass ? 1.0f : 0.0f);
            bypassed = wantBypass;
            changes << (nChanges++ ? ",\n  " : "  ") << "{\"sample\":" << start << ",\"name\":\"bypass\",\"text\":"
                    << (bypassed ? "\"on\"" : "\"off\"") << "}";
        }

        midi.clear();
        const double c0 = juce::Time::getMillisecondCounterHiRes();
        if (bypassed && bypass == nullptr)
            plugin->processBlockBypassed (buf, midi);
        else
            plugin->processBlock (buf, midi);
        const double blockMs = juce::Time::getMillisecondCounterHiRes() - c0;
        processMs += blockMs;
        maxBlockMs = juce::jmax (maxBlockMs, blockMs);
        ++blocks;

        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            const int at = start + meta.samplePosition;
            juce::String e = "{\"sample\":" + juce::String (at) + ",\"t\":" + num (at / sr)
                           + ",\"ch\":" + juce::String (m.getChannel());
            if (m.isNoteOn()) e << ",\"type\":\"on\",\"note\":" << m.getNoteNumber() << ",\"vel\":" << (int) m.getVelocity();
            else if (m.isNoteOff()) e << ",\"type\":\"off\",\"note\":" << m.getNoteNumber();
            else if (m.isPitchWheel()) e << ",\"type\":\"wheel\",\"value\":" << m.getPitchWheelValue();
            else e << ",\"type\":\"other\",\"desc\":\"" << m.getDescription() << "\"";
            e << ",\"bypassed\":" << (bypassed ? "true" : "false") << "}";
            ev << (nEvents++ ? ",\n  " : "  ") << e;
        }

        // audio checks: effect = pass-through of the input on every output channel, instrument = silence
        for (int ch = 0; ch < nOut; ++ch)
            for (int s = 0; s < n; ++s)
            {
                const float x = start + s < total ? wav.getSample (0, start + s) : 0.0f;
                const float y = buf.getSample (ch, s);
                outMaxAbs = juce::jmax (outMaxAbs, (double) std::abs (y));
                if (! std::isfinite (x))
                    continue;    // a NaN/Inf test sample cannot be compared
                ++passCompared;
                if (y != x || std::signbit (y) != std::signbit (x)) ++passMismatches;
                passMaxDiff = juce::jmax (passMaxDiff, (double) std::abs (y - x));
            }
    }
    plugin->releaseResources();

    juce::String json;
    json << "{\n \"plugin\": " << jsonString (plugin->getName()) << ",\n"
         << " \"wav\": " << jsonString (a.wav.replace ("\\", "/")) << ",\n"
         << " \"sampleRate\": " << sr << ",\n \"block\": " << a.block << ",\n"
         << " \"fileSamples\": " << total << ",\n \"tailSamples\": " << tail << ",\n"
         << " \"warmupMs\": " << a.warmupMs << ",\n \"realtime\": " << (a.realtime ? "true" : "false") << ",\n"
         << " \"isInstrument\": " << (types[0]->isInstrument ? "true" : "false") << ",\n"
         << " \"layoutAccepted\": " << (layoutOk ? "true" : "false") << ",\n"
         << " \"inputChannels\": " << nIn << ",\n \"outputChannels\": " << nOut << ",\n"
         << " \"latencySamples\": " << plugin->getLatencySamples() << ",\n"
         << " \"producesMidi\": " << (plugin->producesMidi() ? "true" : "false") << ",\n"
         << " \"acceptsMidi\": " << (plugin->acceptsMidi() ? "true" : "false") << ",\n"
         << " \"hasBypassParameter\": " << (bypass != nullptr ? "true" : "false") << ",\n"
         << " \"passThroughMaxDiff\": " << num (passMaxDiff) << ",\n"
         << " \"passThroughMismatchedSamples\": " << (juce::int64) passMismatches << ",\n"
         << " \"passThroughComparedSamples\": " << (juce::int64) passCompared << ",\n"
         << " \"outputMaxAbs\": " << num (outMaxAbs) << ",\n"
         << " \"processSeconds\": " << num (processMs / 1000.0) << ",\n"
         << " \"audioSeconds\": " << num ((total + tail) / sr) << ",\n"
         << " \"cpuPercentOfOneCore\": " << num (100.0 * processMs / 1000.0 / ((total + tail) / sr)) << ",\n"
         << " \"meanBlockMs\": " << num (blocks > 0 ? processMs / (double) blocks : 0.0) << ",\n"
         << " \"maxBlockMs\": " << num (maxBlockMs) << ",\n"
         << " \"blockBudgetMs\": " << num (1000.0 * a.block / sr) << ",\n"
         << " \"params\": [\n" << paramsJson (*plugin) << "\n ],\n"
         << " \"changes\": [\n" << changes << "\n ],\n"
         << " \"events\": [\n" << ev << "\n ]\n}\n";

    if (! juce::File (a.out).replaceWithText (json))
    {
        std::fprintf (stderr, "cannot write %s\n", a.out.toRawUTF8());
        return 1;
    }
    std::printf ("%s: %d MIDI events, %d input / %d output channels, latency %d, pass-through mismatches %lld, "
                 "processBlock %.2f %% of one core (mean %.3f ms, max %.3f ms per block of %.3f ms)\n",
                 plugin->getName().toRawUTF8(), nEvents, nIn, nOut, plugin->getLatencySamples(), passMismatches,
                 100.0 * processMs / 1000.0 / ((total + tail) / sr), blocks > 0 ? processMs / (double) blocks : 0.0,
                 maxBlockMs, 1000.0 * a.block / sr);
    plugin.reset();
    return 0;
}
