// POLY engine (PolyEngineState) contracts for the plugin shell:
//   - a single pluck gives its note once (sanity, as test/poly-engine.test.mjs)
//   - flush() releases every sounding voice, a second flush() is silent
//   - the note-off carries the midi of its note-on when the shift changes mid-note
//   - reset() then the same input gives the same events (JS reset semantics:
//     the resampler history and the AGC envelope are kept, so the input before
//     the reset ends with enough digital silence for both to settle)
//   - process(), flush() and reset() never allocate (global operator new counted
//     while armed)
//   - ECO budget of rule 3.3-6 and the ECO switch
#include "poly_test_util.h"
#include "midpluck/events.h"
#include "midpluck/poly/engine.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

// ---- allocation counter (armed only around the calls under test)
namespace
{
    std::atomic<bool> g_armed { false };
    std::atomic<long> g_allocs { 0 };

    void* countedAlloc (std::size_t n)
    {
        if (g_armed.load (std::memory_order_relaxed)) g_allocs.fetch_add (1, std::memory_order_relaxed);
        void* p = std::malloc (n ? n : 1);
        if (! p) throw std::bad_alloc();
        return p;
    }
}

void* operator new (std::size_t n) { return countedAlloc (n); }
void* operator new[] (std::size_t n) { return countedAlloc (n); }
void* operator new (std::size_t n, const std::nothrow_t&) noexcept
{
    try { return countedAlloc (n); } catch (...) { return nullptr; }
}
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept
{
    try { return countedAlloc (n); } catch (...) { return nullptr; }
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
    using namespace polytest;
    using namespace midpluck;
    using namespace midpluck::poly;

    struct Rec
    {
        EventType type;
        int midi, block, sampleOffset, voices;
        long long frame;
        double vel, db, gainDb;
        bool operator== (const Rec& o) const
        {
            // velocities, levels and gains compared bit for bit
            return type == o.type && midi == o.midi && block == o.block && sampleOffset == o.sampleOffset
                && frame == o.frame && voices == o.voices
                && std::memcmp (&vel, &o.vel, 8) == 0 && std::memcmp (&db, &o.db, 8) == 0
                && std::memcmp (&gainDb, &o.gainDb, 8) == 0;
        }
    };

    /** Feeds sig in 128-sample blocks; returns the note and meter events (meter: deterministic fields only). */
    std::vector<Rec> run (PolyEngineState& st, const std::vector<float>& sig, EventList& ev, int block = 128)
    {
        std::vector<Rec> out;
        int bi = 0;
        for (size_t o = 0; o < sig.size(); o += static_cast<size_t> (block), bi++)
        {
            const int n = static_cast<int> (std::min (sig.size() - o, static_cast<size_t> (block)));
            ev.clear();
            st.process (sig.data() + o, n, ev);
            PT_CHECK (ev.dropped == 0, "dropped %d events", ev.dropped);
            for (int i = 0; i < ev.count; i++)
            {
                const Event& e = ev.items[i];
                out.push_back ({ e.type, e.midi, bi, e.sampleOffset, e.voices, e.frame, e.vel, e.db, e.gainDb });
            }
        }
        return out;
    }

    std::vector<Rec> flushed (PolyEngineState& st, EventList& ev)
    {
        std::vector<Rec> out;
        ev.clear();
        st.flush (ev);
        for (int i = 0; i < ev.count; i++)
        {
            const Event& e = ev.items[i];
            out.push_back ({ e.type, e.midi, -1, e.sampleOffset, e.voices, e.frame, e.vel, e.db, e.gainDb });
        }
        return out;
    }

    int count (const std::vector<Rec>& v, EventType t, int midi = -1)
    {
        int n = 0;
        for (const Rec& r : v) if (r.type == t && (midi < 0 || r.midi == midi)) n++;
        return n;
    }

    PolyParams quiet()
    {
        PolyParams p;
        p.autoEco = false;   // timing must not change the decisions in a test
        return p;
    }
}

int main()
{
    const double SR48 = 48000;
    auto ev = std::make_unique<EventList>();

    // ---- a single pluck: its note, once; flush releases it
    {
        PolyEngineState st (SR48, quiet());
        PluckOpts o; o.midi = 55; o.dur = 0.6; o.stiffness = 1e-4;   // no cut: still sounding at the end
        const auto sig = concat ({ silence (SR48, 0.1), pluck (SR48, o) });
        const auto evs = run (st, sig, *ev);
        const int ons = count (evs, EventType::NoteOn), offs = count (evs, EventType::NoteOff);
        PT_CHECK (count (evs, EventType::NoteOn, 55) == 1, "expected one G3, got %d note-ons (%d total)", count (evs, EventType::NoteOn, 55), ons);
        PT_CHECK (ons <= 2, "%d note-ons", ons);
        for (const Rec& r : evs)
            if (r.type == EventType::NoteOn && r.midi == 55)
            {
                const double ms = (r.frame + 1) * 1000.0 * HOP / SR + 1000 * PolyEngineState::delaySeconds (SR48);
                PT_CHECK (ms - 100 > 0 && ms - 100 < 60, "note-on latency %.1f ms", ms - 100);
                PT_CHECK (r.sampleOffset >= 0 && r.sampleOffset < 128, "sampleOffset %d", r.sampleOffset);
                PT_CHECK (r.vel >= 0.1 && r.vel <= 1, "velocity %g", r.vel);
            }
        PT_CHECK (ons - offs >= 1, "the note should still sound before the flush (%d on, %d off)", ons, offs);
        const auto fl = flushed (st, *ev);
        PT_CHECK (count (fl, EventType::NoteOff) == ons - offs, "flush released %d of %d sounding notes", count (fl, EventType::NoteOff), ons - offs);
        PT_CHECK (count (fl, EventType::NoteOn) == 0, "flush emitted a note-on");
        PT_CHECK (st.tracker.rule.voiceCount() == 0, "%d voices left after flush", st.tracker.rule.voiceCount());
        PT_CHECK (flushed (st, *ev).empty(), "second flush is not silent");
    }

    // ---- shift change mid-note: the note-off names the midi that was sent
    {
        PolyEngineState st (SR48, quiet());
        st.setParams (1, 2);   // +14
        PluckOpts o; o.midi = 50; o.dur = 0.5; o.stiffness = 1e-4;
        const auto evs = run (st, concat ({ silence (SR48, 0.1), pluck (SR48, o) }), *ev);
        st.setParams (0, 0);
        const auto fl = flushed (st, *ev);
        PT_CHECK (count (evs, EventType::NoteOn, 64) == 1, "D3 + 14 = 64 expected once, got %d", count (evs, EventType::NoteOn, 64));
        PT_CHECK (count (fl, EventType::NoteOff, 64) == 1, "the flush must release 64 (the shifted midi), got %d", count (fl, EventType::NoteOff, 64));
    }

    // ---- reset then the same input: the same events
    {
        PluckOpts a; a.midi = 45; a.dur = 0.5; a.cut = 0.4; a.stiffness = 1e-4; a.seed = 2;
        PluckOpts b; b.midi = 59; b.dur = 0.6; b.stiffness = 1e-4; b.seed = 5;
        const auto X = concat ({ silence (SR48, 0.1), pluck (SR48, a), silence (SR48, 0.05), pluck (SR48, b), silence (SR48, 0.3) });
        PluckOpts c; c.midi = 48; c.dur = 0.6; c.stiffness = 1e-4; c.seed = 9;
        // 12 s of digital silence: the AGC envelope falls back to its -70 dB floor
        // and the resampler history to zeros, the two states reset() keeps (as the JS)
        const auto Y = concat ({ silence (SR48, 0.1), pluck (SR48, c), silence (SR48, 12.0, 0.0) });

        PolyEngineState fresh (SR48, quiet());
        auto ref = run (fresh, X, *ev);
        const auto refFlush = flushed (fresh, *ev);
        ref.insert (ref.end(), refFlush.begin(), refFlush.end());

        PolyEngineState used (SR48, quiet());
        run (used, Y, *ev);
        flushed (used, *ev);
        used.reset();
        auto again = run (used, X, *ev);
        const auto againFlush = flushed (used, *ev);
        again.insert (again.end(), againFlush.begin(), againFlush.end());

        PT_CHECK (count (ref, EventType::NoteOn) >= 2, "X should give its two notes, got %d", count (ref, EventType::NoteOn));
        PT_CHECK (ref.size() == again.size(), "%zu events fresh, %zu after reset", ref.size(), again.size());
        size_t same = 0;
        for (size_t i = 0; i < ref.size() && i < again.size(); i++) if (ref[i] == again[i]) same++;
        PT_CHECK (same == ref.size() && same == again.size(), "%zu of %zu events identical after reset", same, ref.size());
    }

    // ---- setSampleRate (prepareToPlay): a 48 kHz state moved to 44.1 kHz behaves like a 44.1 kHz one
    {
        const double SR44 = 44100;
        PluckOpts a; a.midi = 52; a.dur = 0.5; a.stiffness = 1e-4;
        const auto X = concat ({ silence (SR44, 0.1), pluck (SR44, a), silence (SR44, 0.2) });
        PolyEngineState native (SR44, quiet());
        PolyEngineState moved (SR48, quiet());
        moved.setSampleRate (SR44);
        const auto r1 = run (native, X, *ev);
        const auto r2 = run (moved, X, *ev);
        PT_CHECK (r1.size() == r2.size() && std::equal (r1.begin(), r1.end(), r2.begin()), "setSampleRate(44100): %zu vs %zu events", r1.size(), r2.size());
        PT_CHECK (moved.tracker.delaySeconds() == PolyEngineState::delaySeconds (SR44), "delay after setSampleRate");
    }

    // ---- no allocation in process(), flush(), reset() (44.1 kHz: the 80/147 polyphase path)
    {
        const double SR44 = 44100;
        PolyEngineState st (SR44, PolyParams {});   // autoEco on: the timing path is exercised too
        PluckOpts a; a.midi = 40; a.dur = 1.0; a.stiffness = 1e-4;
        PluckOpts b; b.midi = 64; b.dur = 1.0; b.stiffness = 1e-4; b.seed = 4;
        auto sig = concat ({ silence (SR44, 0.1), pluck (SR44, a) });
        const auto pb = pluck (SR44, b);
        for (size_t i = 0; i < pb.size(); i++) sig[i + 4410] += 0.8f * pb[i];
        std::vector<float> chunk (512);
        long notesSeen = 0;
        for (int block : { 32, 128, 512, 127 })
        {
            for (size_t o = 0; o < sig.size(); o += static_cast<size_t> (block))
            {
                const int n = static_cast<int> (std::min (sig.size() - o, static_cast<size_t> (block)));
                ev->clear();
                g_armed = true;
                st.process (sig.data() + o, n, *ev);
                g_armed = false;
                for (int i = 0; i < ev->count; i++) if (ev->items[i].type == EventType::NoteOn) notesSeen++;
            }
            ev->clear();
            g_armed = true;
            st.flush (*ev);
            st.reset();
            g_armed = false;
        }
        PT_CHECK (g_allocs.load() == 0, "%ld allocation(s) inside process/flush/reset", g_allocs.load());
        PT_CHECK (notesSeen >= 4, "the allocation run should play notes (%ld note-ons)", notesSeen);
        // the engine's own building allocates (it is built on a background thread in the shell)
        g_armed = true;
        { PolyEngineState other (SR44, PolyParams {}); (void) other; }
        g_armed = false;
        PT_CHECK (g_allocs.load() > 0, "the allocation counter is not wired (constructor counted 0)");
    }

    // ---- ECO budget (rule 3.3-6) and switch
    {
        struct Case { double sr; int block; double ms; };
        for (const Case& c : { Case { 48000, 64, 1.3333 }, Case { 48000, 128, 2.6667 }, Case { 48000, 256, 2.6667 },
                               Case { 44100, 64, 1.4512 }, Case { 44100, 128, 1.4512 }, Case { 44100, 256, 1.9349 } })
        {
            const double b = PolyEngineState::ecoBudgetMs (c.sr, c.block);
            PT_CHECK (std::fabs (b - c.ms) < 1e-3, "budget %g Hz / %d: %.4f ms, expected %.4f", c.sr, c.block, b, c.ms);
        }
        PolyEngineState st (SR48, quiet());
        PT_CHECK (! st.eco(), "ECO on at start");
        PT_CHECK (st.tracker.decomposer.iter == 8 && st.tracker.decomposer.active == 60, "shipped decomposer");
        st.tracker.setEco (true);
        PT_CHECK (st.eco() && st.tracker.decomposer.iter == 5 && st.tracker.decomposer.active == 40, "ECO setting");
        st.tracker.setEco (false);
        PT_CHECK (! st.eco() && st.tracker.decomposer.iter == 8 && st.tracker.decomposer.active == 60, "back to full");
        st.setBudgetMs (1.25);
        PT_CHECK (st.tracker.budgetMs() == 1.25, "setBudgetMs");
    }

    return finish ("test_poly_engine");
}
