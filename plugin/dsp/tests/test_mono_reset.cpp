// MONO closed test: flush() and reset() (plan 3.2).
//  - flush() releases a sounding note (note-off of that note, then only a
//    zeroing bend if any), and a second flush() emits nothing;
//  - reset() makes the object behave exactly like a freshly constructed one
//    with the same parameters: the same input then gives bit-identical events
//    (types, offsets, frames, velocities, bends, meters), whatever state the
//    object was in (idle, mid-note, after a parameter change that rebuilt the
//    YIN buffers).
#include "mono_test_util.h"

using namespace midpluck;
using monotest::concat;
using monotest::only;
using monotest::pluck;
using monotest::PluckOpts;
using monotest::silence;

namespace
{
    std::vector<Event> plain (const std::vector<monotest::TimedEvent>& ev)
    {
        std::vector<Event> r;
        for (const auto& t : ev) r.push_back (t.e);
        return r;
    }

    bool sameEvents (const std::vector<Event>& a, const std::vector<Event>& b, size_t& firstDiff)
    {
        const size_t n = std::min (a.size(), b.size());
        for (size_t i = 0; i < n; i++)
            if (! monotest::sameEvent (a[i], b[i])) { firstDiff = i; return false; }
        firstDiff = n;
        return a.size() == b.size();
    }

    std::vector<float> phrase (double sr)
    {
        // a pluck, a bend, a re-pick, a softer note and silence: every path of the state machine
        PluckOpts a; a.midi = 45; a.dur = 0.5; a.seed = 1;
        PluckOpts b; b.midi = 57; b.dur = 0.4; b.seed = 2;
        PluckOpts c; c.midi = 57; c.dur = 0.3; c.seed = 3; c.amp = 0.1;
        PluckOpts d; d.midi = 64; d.dur = 0.6; d.seed = 4; d.decay = 2;
        return concat ({ silence (sr, 0.1), pluck (sr, a), pluck (sr, b), pluck (sr, c), silence (sr, 0.05, 1e-4, 9), pluck (sr, d), silence (sr, 0.3) });
    }
}

int main()
{
    // ---- flush() releases a sounding note
    {
        const double sr = 48000;
        MonoTracker tr (sr);
        PluckOpts o; o.midi = 45; o.dur = 0.3;
        monotest::run (tr, sr, concat ({ silence (sr, 0.1), pluck (sr, o) }));
        MT_CHECK (tr.soundingNote() == 45, "before flush: sounding note %d", tr.soundingNote());
        auto list = std::make_unique<EventList>();
        tr.flush (*list);
        MT_CHECK (list->count >= 1 && list->items[0].type == EventType::NoteOff && list->items[0].midi == 45,
                  "flush: first event is not note-off 45 (%d events)", list->count);
        for (int i = 1; i < list->count; i++)
            MT_CHECK (list->items[i].type == EventType::Bend && list->items[i].semis == 0, "flush: event %d is not a zero bend", i);
        for (int i = 0; i < list->count; i++)
            MT_CHECK (list->items[i].sampleOffset == 0, "flush: event %d has sampleOffset %d", i, list->items[i].sampleOffset);
        MT_CHECK (tr.soundingNote() == -1, "after flush: sounding note %d", tr.soundingNote());
        list->clear();
        tr.flush (*list);
        MT_CHECK (list->count == 0, "second flush emitted %d events", list->count);
    }

    // ---- reset() == fresh object, for several parameter sets and rates
    struct Case { double sr; MonoParams p; const char* label; };
    std::vector<Case> cases;
    cases.push_back ({ 48000, MonoParams {}, "48 kHz defaults" });
    {
        MonoParams p; p.sens = 0.8; p.release = 0.2; p.dyn = 1; p.octave = -1; p.transpose = 3;
        cases.push_back ({ 44100, p, "44.1 kHz sens/release/dyn/shift" });
    }
    {
        MonoParams p; p.bend = false; p.fmin = 60; p.fmax = 1200;   // fmin/fmax: YIN buffers rebuilt by setParams
        cases.push_back ({ 96000, p, "96 kHz chromatic, fmin 60, fmax 1200" });
    }

    for (const Case& c : cases)
    {
        const std::vector<float> sig = phrase (c.sr);

        MonoTracker fresh (c.sr, c.p);
        const auto ref = plain (monotest::run (fresh, c.sr, sig));
        const int nOn = static_cast<int> (std::count_if (ref.begin(), ref.end(), [] (const Event& e) { return e.type == EventType::NoteOn; }));
        const int nBend = static_cast<int> (std::count_if (ref.begin(), ref.end(), [] (const Event& e) { return e.type == EventType::Bend; }));
        MT_CHECK (nOn >= 2, "%s: the phrase gave only %d note-on(s)", c.label, nOn);

        // 1. same object, reset after a full run (idle at the end)
        MonoTracker a (c.sr, c.p);
        monotest::run (a, c.sr, sig);
        a.reset();
        size_t at = 0;
        const auto again = plain (monotest::run (a, c.sr, sig));
        MT_CHECK (sameEvents (ref, again, at), "%s: reset after a full run differs at event %zu of %zu/%zu", c.label, at, ref.size(), again.size());

        // 2. reset in the middle of a sounding note, with a pending bend
        MonoTracker b (c.sr, c.p);
        const std::vector<float> half (sig.begin(), sig.begin() + static_cast<long> (c.sr * 0.35));
        monotest::run (b, c.sr, half, 100);
        b.reset();
        const auto afterMid = plain (monotest::run (b, c.sr, sig));
        MT_CHECK (sameEvents (ref, afterMid, at), "%s: reset mid-note differs at event %zu of %zu/%zu", c.label, at, ref.size(), afterMid.size());

        // 3. a parameter change that rebuilds YIN, then back, then reset
        MonoTracker d (c.sr, c.p);
        MonoParams other = c.p; other.fmin = 40; other.fmax = 1000; other.sens = 0.1;
        d.setParams (other);
        monotest::run (d, c.sr, sig, 64);
        d.setParams (c.p);
        d.reset();
        const auto afterParams = plain (monotest::run (d, c.sr, sig));
        MT_CHECK (sameEvents (ref, afterParams, at), "%s: reset after setParams differs at event %zu of %zu/%zu", c.label, at, ref.size(), afterParams.size());
        MT_CHECK (d.tmin() == fresh.tmin() && d.tmax() == fresh.tmax() && d.window() == fresh.window() && d.ringSize() == fresh.ringSize(),
                  "%s: YIN geometry not restored", c.label);
        MT_CHECK (d.lastLatMs() == fresh.lastLatMs(), "%s: lastLatMs %.3f vs %.3f", c.label, d.lastLatMs(), fresh.lastLatMs());

        std::printf ("%-38s %zu events (%d note-ons, %d bends): reset == fresh in the three scenarios\n", c.label, ref.size(), nOn, nBend);
    }

    // ---- tuning names map to the right fields
    {
        Tuning t;
        MT_CHECK (setTuningValue (t, "ONSET_AGREE", 2) && t.ONSET_AGREE == 2, "ONSET_AGREE not set");
        MT_CHECK (setTuningValue (t, "HARMONIC_GUARD", 1) && t.HARMONIC_GUARD == 1, "HARMONIC_GUARD not set");
        MT_CHECK (! setTuningValue (t, "NOT_A_KEY", 1), "unknown key accepted");
    }
    return monotest::finish ("test_mono_reset");
}
