// dump_events: runs a MidPluck engine (MONO or POLY) on WAV files and writes
// the same JSON as test/dump-events.mjs / test/poly-dump-events.mjs, so that
// test/diff-events.mjs and test/score.py compare C++ and JS note for note
// (docs/plugin-plan.md, sections 2 and 4).
//
// Split across files so the pieces can be written independently:
//   dump_events.cpp  main(): options, take loop, JSON, cost report
//   dump_common.cpp  WAV reader, takes list reader, JSON writer
//   dump_mono.cpp    trackMono()  (MONO engine, dsp/mono_tracker)
//   dump_poly.cpp    trackPoly()  (POLY engine, dsp/poly)
#pragma once

#include <string>
#include <vector>

namespace midpluck::dump
{
    struct Wav
    {
        int rate = 0;
        std::vector<float> samples;   // first channel, int16 / 32768 (exactly like readWav in test/dump-events.mjs)
    };

    /** RIFF/WAVE PCM 16-bit reader, first channel. Throws std::runtime_error on anything else. */
    Wav readWav (const std::string& path);

    /** One line per take name; ignores a UTF-8/UTF-16 BOM, CR and empty lines. */
    std::vector<std::string> readTakes (const std::string& path);

    struct Note
    {
        double onset = 0, offset = -1;   // seconds; offset < 0 while open
        int midi = 0;
        double velocity = 0;             // 0..1, not quantised
        std::string why;
    };

    struct Take
    {
        std::string name;
        double duration = 0;
        std::vector<Note> events;
    };

    /** Per-hop/per-frame cost of the engine on one take (milliseconds), for the report. */
    struct Cost
    {
        std::vector<double> hopMs;       // POLY: whole-hop cost per hop (engine's own metering or timed)
        std::vector<double> nmfMs;       // POLY: decomposer cost per hop
        double totalSeconds = 0;         // wall time spent in the engine for the take
    };

    struct Options
    {
        std::string mode = "mono";       // mono | poly
        int block = 128;                 // host block size fed to the engine
        std::string stamp = "block";     // MONO: block (end of the 128 block, like the JS) | sample (decision sample)
        std::string align = "live";      // POLY: live (causal stream) | scipy (drop the resampler delay)
        bool dense = false;              // POLY: dense decomposer (sparse 0, iter 15), the prototype's exact arithmetic
        std::string tuning;              // MONO TUNING overrides  "KEY=value,KEY=value"
        std::string decomp;              // POLY DECOMPOSER overrides "key=value,..."
        std::string rule;                // POLY NOTE_RULE overrides  "key=value,..."
        std::string label;               // JSON label (default: "mono" / "poly")
        std::string guitarset, mixdir, set = "solo", takes, out;
        std::vector<std::string> positional;
    };

    /** Runs the MONO engine on one WAV the way test/dump-events.mjs trackNotes() does. */
    Take trackMono (const Wav& wav, const Options& opt, Cost& cost);

    /** Runs the POLY engine on one WAV the way test/poly-dump-events.mjs trackNotesPoly() does. */
    Take trackPoly (const Wav& wav, const Options& opt, Cost& cost);

    /** Writes { label, source, set?, align?, takes: { name: { duration, events: [...] } } } with %.17g numbers. */
    void writeJson (const std::string& path, const Options& opt, const std::vector<Take>& takes);
}
