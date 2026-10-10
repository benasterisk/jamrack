// MONO closed test: synthetic plucked strings (the generator of
// test/plucks.mjs, decaying inharmonic partials plus a noisy pick) give one
// note-on with the right note and one note-off, as in the first test of
// test/guitar-tracker.test.mjs; plus the noise floor and octave/transpose.
#include "mono_test_util.h"

using namespace midpluck;
using monotest::concat;
using monotest::only;
using monotest::pluck;
using monotest::PluckOpts;
using monotest::silence;

int main()
{
    struct S { const char* label; int midi; };
    const S strings[] = {
        { "D2 (drop D)", 38 }, { "E2", 40 }, { "A2", 45 }, { "D3", 50 }, { "G3", 55 },
        { "B3", 59 }, { "E4", 64 }, { "A4 (fret 5, high E)", 69 }, { "E5 (fret 12)", 76 }, { "A5 (fret 17)", 81 },
    };
    // 48 and 96 kHz: every string gives exactly one note-on and one note-off.
    // 44.1 kHz: the JS engine itself (node, same generator, 8 October 2026)
    // gives one note for eight strings, and for B3 and E4 the sequences below;
    // the port must reproduce them, not "fix" them (the JS is canonical).
    struct Known { int midi; const char* seq; };
    const Known js441[] = {
        { 59, "on60@174 off60@186 on59@186 off59@624 " },   // wrong first estimate, corrected ('fix')
        { 64, "on64@116 off64@610 on64@610 off64@624 " },   // the hard mute re-triggers the note
    };
    for (const double sr : { 48000.0, 44100.0, 96000.0 })
    {
        for (const S& s : strings)
        {
            MonoTracker tr (sr);
            PluckOpts o;
            o.midi = s.midi; o.dur = 0.6; o.cut = 0.5;
            // pluck at 100 ms, hard mute at 600 ms
            const std::vector<float> sig = concat ({ silence (sr, 0.1), pluck (sr, o), silence (sr, 0.2) });
            const auto ev = monotest::run (tr, sr, sig);
            const auto on = only (ev, EventType::NoteOn), off = only (ev, EventType::NoteOff);
            const double lat = on.empty() ? NAN : on[0].ms - 100;
            const double offLat = off.empty() ? NAN : off.back().ms - 600;
            const char* known = nullptr;
            if (sr == 44100.0)
                for (const Known& k : js441) if (k.midi == s.midi) known = k.seq;
            if (known != nullptr)
            {
                MT_CHECK (monotest::describe (ev) == known, "%.0f Hz %s: got %s, the JS gives %s", sr, s.label, monotest::describe (ev).c_str(), known);
                continue;
            }
            MT_CHECK (on.size() == 1 && on[0].e.midi == s.midi, "%.0f Hz %s: expected one note-on %d, got %s", sr, s.label, s.midi, monotest::describe (ev).c_str());
            MT_CHECK (off.size() == 1 && off[0].e.midi == s.midi, "%.0f Hz %s: expected one note-off, got %s", sr, s.label, monotest::describe (ev).c_str());
            MT_CHECK (lat > 0 && lat < 45, "%.0f Hz %s: note-on latency %.1f ms", sr, s.label, lat);
            MT_CHECK (offLat > 0 && offLat < 40, "%.0f Hz %s: note-off %.1f ms after the mute", sr, s.label, offLat);
            MT_CHECK (! on.empty() && std::string (on[0].e.why) == "pluck", "%.0f Hz %s: why = %s", sr, s.label, on.empty() ? "-" : on[0].e.why);
            MT_CHECK (! on.empty() && on[0].e.vel >= 0.05 && on[0].e.vel <= 1, "%.0f Hz %s: velocity %.3f", sr, s.label, on.empty() ? -1.0 : on[0].e.vel);
            if (sr == 48000.0)
                std::printf ("%-22s %-28s on latency %5.1f ms, off after mute %5.1f ms, vel %.2f, lastLatMs %.1f\n",
                             s.label, monotest::describe (ev).c_str(), lat, offLat, on.empty() ? 0.0 : on[0].e.vel, tr.lastLatMs());
        }
    }

    // noise floor and digital silence never trigger
    {
        const double sr = 48000;
        MonoTracker tr (sr);
        const auto ev = monotest::run (tr, sr, concat ({ silence (sr, 1.0, 0.002), silence (sr, 0.5, 0.0) }));
        MT_CHECK (only (ev, EventType::NoteOn).empty() && only (ev, EventType::NoteOff).empty(), "hiss gave %s", monotest::describe (ev).c_str());
    }

    // octave and transpose shift the emitted notes, not the tuner
    {
        const double sr = 48000;
        MonoParams p;
        p.octave = 1; p.transpose = -2;
        MonoTracker tr (sr, p);
        PluckOpts o;
        o.midi = 52; o.dur = 0.4;
        const auto ev = monotest::run (tr, sr, concat ({ silence (sr, 0.1), pluck (sr, o) }));
        const auto on = only (ev, EventType::NoteOn);
        MT_CHECK (on.size() == 1 && on[0].e.midi == 52 + 12 - 2, "octave/transpose: got %s", monotest::describe (ev).c_str());
        double lastMidiF = NAN;
        for (const auto& m : only (ev, EventType::Meter)) if (! std::isnan (m.e.midiF)) lastMidiF = m.e.midiF;
        MT_CHECK (std::fabs (lastMidiF - 52) < 0.1, "tuner read %.3f, expected ~52", lastMidiF);
    }
    return monotest::finish ("test_mono_pluck");
}
