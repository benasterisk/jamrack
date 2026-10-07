// vst3_probe: a minimal command-line VST3 host that loads an INSTALLED .vst3
// bundle, feeds it a WAV file block by block like a DAW would, and writes
// every MIDI message the plugin emits, stamped to the sample, as JSON.
//
//   vst3_probe --plugin "D:/VST3/JAMRACK GTM Fx.vst3" --wav plucks.wav --out events.json
//              [--block 128] [--channel 5] [--bypass-from 2.3] [--bypass-to 3.0]
//              [--reprepare-at 2.8]
//
// It answers, without a DAW and without a person at the screen, what a host
// sees: which notes come out and at which sample, the pitch-wheel values,
// the channel, what happens around a host bypass or a deactivate/reactivate
// (releaseResources + prepareToPlay, what a host does when a device is
// switched off and on), and whether the audio passes through (effect) or
// stays silent (instrument). Every input bus is enabled and fed the WAV, so
// an instrument whose only input is a side-chain receives it on that bus.
// Limits: JUCE hosting passes the same buffer as input and output, and does
// not reproduce Live's own routing, tap point or level.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <cstdio>
#include <map>

namespace
{
    struct Args
    {
        juce::String plugin, wav, out;
        int block = 128;
        int channel = 0;              // 0 = leave the plugin's default
        double bypassFrom = -1.0, bypassTo = -1.0;
        double reprepareAt = -1.0;
    };

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
            else { std::fprintf (stderr, "unknown option %s\n", argv[i]); return false; }
        }
        return a.plugin.isNotEmpty() && a.wav.isNotEmpty() && a.out.isNotEmpty() && a.block > 0;
    }

    juce::String num (double v) { return juce::String (v, 9); }
}

int main (int argc, char** argv)
{
    Args a;
    if (! parse (argc, argv, a))
    {
        std::fprintf (stderr, "usage: vst3_probe --plugin <x.vst3> --wav <in.wav> --out <events.json> "
                              "[--block 128] [--channel N] [--bypass-from s --bypass-to s] [--reprepare-at s]\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    // ---- the WAV (first channel, as the plugins read channel 0)
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (a.wav)));
    if (reader == nullptr) { std::fprintf (stderr, "cannot read %s\n", a.wav.toRawUTF8()); return 1; }
    const double sr = reader->sampleRate;
    const int total = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> wav (1, total);
    reader->read (&wav, 0, total, 0, true, false);

    // ---- the plugin, through the same VST3 hosting code JUCE-based DAWs use
    juce::AudioPluginFormatManager fm;
    fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* f : fm.getFormats())
        f->findAllTypesForFile (types, a.plugin);
    if (types.isEmpty()) { std::fprintf (stderr, "no VST3 plugin in %s\n", a.plugin.toRawUTF8()); return 1; }

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

    auto* bypass = plugin->getBypassParameter();
    plugin->setRateAndBufferSizeDetails (sr, a.block);
    plugin->prepareToPlay (sr, a.block);

    const int nIn = plugin->getTotalNumInputChannels();
    const int nOut = plugin->getTotalNumOutputChannels();
    const int nCh = juce::jmax (nIn, nOut, 1);
    juce::AudioBuffer<float> buf (nCh, a.block);
    juce::MidiBuffer midi;

    juce::String ev;
    int nEvents = 0;
    double passMaxDiff = 0.0, outMaxAbs = 0.0;
    bool bypassed = false;
    const int bypassFrom = a.bypassFrom >= 0 ? (int) std::llround (a.bypassFrom * sr) : -1;
    const int bypassTo = a.bypassTo >= 0 ? (int) std::llround (a.bypassTo * sr) : -1;
    const int reprepareAt = a.reprepareAt >= 0 ? (int) std::llround (a.reprepareAt * sr) : -1;
    bool reprepared = false;

    // a few seconds of silence after the file let the last note end
    const int tail = (int) (1.0 * sr);
    for (int start = 0; start < total + tail; start += a.block)
    {
        const int n = juce::jmin (a.block, total + tail - start);
        buf.setSize (nCh, n, false, false, true);
        buf.clear();
        for (int ch = 0; ch < nIn; ++ch)
            for (int s = 0; s < n; ++s)
                buf.setSample (ch, s, start + s < total ? wav.getSample (0, start + s) : 0.0f);

        if (reprepareAt >= 0 && ! reprepared && start >= reprepareAt)
        {
            plugin->releaseResources();
            plugin->prepareToPlay (sr, a.block);
            reprepared = true;
        }

        const bool wantBypass = bypassFrom >= 0 && start >= bypassFrom && (bypassTo < 0 || start < bypassTo);
        if (bypass != nullptr && wantBypass != bypassed)
        {
            bypass->setValueNotifyingHost (wantBypass ? 1.0f : 0.0f);
            bypassed = wantBypass;
        }

        midi.clear();
        if (bypassed && bypass == nullptr)
            plugin->processBlockBypassed (buf, midi);
        else
            plugin->processBlock (buf, midi);

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

        // audio checks: effect = pass-through of channel 0, instrument = silence
        for (int s = 0; s < n && nOut > 0; ++s)
        {
            const double x = start + s < total ? wav.getSample (0, start + s) : 0.0;
            const double y = buf.getSample (0, s);
            passMaxDiff = juce::jmax (passMaxDiff, std::abs (y - x));
            outMaxAbs = juce::jmax (outMaxAbs, std::abs (y));
        }
    }
    plugin->releaseResources();

    juce::String json;
    json << "{\n \"plugin\": \"" << plugin->getName() << "\",\n"
         << " \"wav\": \"" << a.wav.replace ("\\", "/") << "\",\n"
         << " \"sampleRate\": " << sr << ",\n \"block\": " << a.block << ",\n"
         << " \"isInstrument\": " << (types[0]->isInstrument ? "true" : "false") << ",\n"
         << " \"layoutAccepted\": " << (layoutOk ? "true" : "false") << ",\n"
         << " \"inputChannels\": " << nIn << ",\n \"outputChannels\": " << nOut << ",\n"
         << " \"latencySamples\": " << plugin->getLatencySamples() << ",\n"
         << " \"producesMidi\": " << (plugin->producesMidi() ? "true" : "false") << ",\n"
         << " \"hasBypassParameter\": " << (bypass != nullptr ? "true" : "false") << ",\n"
         << " \"passThroughMaxDiff\": " << num (passMaxDiff) << ",\n"
         << " \"outputMaxAbs\": " << num (outMaxAbs) << ",\n"
         << " \"events\": [\n" << ev << "\n ]\n}\n";

    if (! juce::File (a.out).replaceWithText (json))
    {
        std::fprintf (stderr, "cannot write %s\n", a.out.toRawUTF8());
        return 1;
    }
    std::printf ("%s: %d MIDI events, %d input / %d output channels, latency %d\n",
                 plugin->getName().toRawUTF8(), nEvents, nIn, nOut, plugin->getLatencySamples());
    plugin.reset();
    return 0;
}
