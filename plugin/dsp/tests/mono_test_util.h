// Shared helpers for the MONO closed tests (dsp/tests/test_mono_*.cpp):
// a tiny assertion macro and a C++ copy of the synthetic plucked string of
// test/plucks.mjs (pluck, silence, concat), so the tests need no JS run.
#pragma once

#include "midpluck/events.h"
#include "midpluck/mono_tracker.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace monotest
{
    inline int& failures() { static int n = 0; return n; }

#define MT_CHECK(cond, ...)                                                    \
    do {                                                                       \
        if (! (cond)) {                                                        \
            std::fprintf (stderr, "FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); \
            std::fprintf (stderr, __VA_ARGS__);                                \
            std::fprintf (stderr, "\n");                                       \
            ++monotest::failures();                                            \
        }                                                                      \
    } while (0)

    inline int finish (const char* name)
    {
        if (failures() == 0) { std::printf ("%s: all checks passed\n", name); return 0; }
        std::fprintf (stderr, "%s: %d check(s) failed\n", name, failures());
        return 1;
    }

    constexpr double kPi = 3.141592653589793;

    inline double midiToHz (double m) { return 440 * std::pow (2.0, (m - 69) / 12); }

    /** makeRng of test/plucks.mjs (deterministic LCG in -1..1). */
    struct Rng
    {
        std::uint32_t s;
        explicit Rng (std::uint32_t seed) : s (seed) {}
        double operator()()
        {
            s = s * 1664525u + 1013904223u;
            return s / 4294967296.0 * 2 - 1;
        }
    };

    struct PluckOpts
    {
        double midi = 60, cents = 0, dur = 1, amp = 0.3, fundamental = 1, decay = 1;
        int harmonics = 14;
        std::uint32_t seed = 7;
        double stiffness = 2e-4;
        double cut = 0;          // seconds, 0 = none
    };

    /** pluck() of test/plucks.mjs without bends / hammer-ons: Float32 output like the JS. */
    inline std::vector<float> pluck (double sr, const PluckOpts& o)
    {
        const double f0 = midiToHz (o.midi) * std::pow (2.0, o.cents / 1200);
        const long n = std::lround (sr * o.dur);
        std::vector<float> out (static_cast<size_t> (n), 0.0f);
        Rng rng (o.seed);
        std::vector<double> phase (static_cast<size_t> (o.harmonics + 1), 0.0);
        const long cutN = o.cut > 0 ? std::lround (o.cut * sr) : n;
        for (long i = 0; i < std::min (n, cutN); i++)
        {
            const double t = i / sr;
            const double f = f0;
            const double attack = std::min (1.0, t / 0.0015);
            double s = 0;
            for (int k = 1; k <= o.harmonics; k++)
            {
                const double fk = k * f * std::sqrt (1 + o.stiffness * k * k);
                phase[static_cast<size_t> (k)] += 2 * kPi * fk / sr;
                const double a = (k == 1 ? o.fundamental : 1.0 / k) * std::exp (-t * o.decay * (0.3 + 1.2 * k));
                s += a * std::sin (phase[static_cast<size_t> (k)]);
            }
            const double pick = std::exp (-t / 0.0025) * rng() * 0.8;
            out[static_cast<size_t> (i)] = static_cast<float> (o.amp * attack * (s * 0.55 + pick) + rng() * 1e-4);
        }
        return out;
    }

    inline std::vector<float> silence (double sr, double dur, double noiseAmp = 1e-4, std::uint32_t seed = 3)
    {
        Rng rng (seed);
        std::vector<float> out (static_cast<size_t> (std::lround (sr * dur)));
        for (float& v : out) v = static_cast<float> (rng() * noiseAmp);
        return out;
    }

    inline std::vector<float> concat (std::initializer_list<std::vector<float>> parts)
    {
        std::vector<float> out;
        for (const auto& p : parts) out.insert (out.end(), p.begin(), p.end());
        return out;
    }

    /** An event with the absolute time (ms) of the end of its block, like run() of test/plucks.mjs. */
    struct TimedEvent
    {
        midpluck::Event e;
        long long block = 0;      // start sample of the block
        double ms = 0;
    };

    /** Feeds the signal in blocks; collects every event (meters included). */
    inline std::vector<TimedEvent> run (midpluck::MonoTracker& tr, double sr, const std::vector<float>& sig, int block = 128)
    {
        std::vector<TimedEvent> all;
        auto list = std::make_unique<midpluck::EventList>();
        const long long n = static_cast<long long> (sig.size());
        for (long long o = 0; o < n; o += block)
        {
            const int len = static_cast<int> (std::min<long long> (block, n - o));
            list->clear();
            tr.process (sig.data() + o, len, *list);
            for (int i = 0; i < list->count; i++)
                all.push_back ({ list->items[i], o, (o + len) / sr * 1000 });
        }
        return all;
    }

    inline std::vector<TimedEvent> only (const std::vector<TimedEvent>& ev, midpluck::EventType t)
    {
        std::vector<TimedEvent> r;
        for (const auto& e : ev) if (e.e.type == t) r.push_back (e);
        return r;
    }

    inline std::string describe (const std::vector<TimedEvent>& ev)
    {
        std::string s;
        char buf[96];
        for (const auto& t : ev)
        {
            if (t.e.type == midpluck::EventType::NoteOn) std::snprintf (buf, sizeof buf, "on%d@%.0f ", t.e.midi, t.ms);
            else if (t.e.type == midpluck::EventType::NoteOff) std::snprintf (buf, sizeof buf, "off%d@%.0f ", t.e.midi, t.ms);
            else continue;
            s += buf;
        }
        return s.empty() ? std::string ("(no notes)") : s;
    }

    /** Bitwise equality of two doubles (NaN equals NaN). */
    inline bool sameBits (double a, double b)
    {
        if (std::isnan (a) && std::isnan (b)) return true;
        return std::memcmp (&a, &b, sizeof a) == 0;
    }

    inline bool sameEvent (const midpluck::Event& a, const midpluck::Event& b)
    {
        return a.type == b.type && a.sampleOffset == b.sampleOffset && a.midi == b.midi
            && sameBits (a.vel, b.vel) && std::string (a.why) == std::string (b.why)
            && sameBits (a.semis, b.semis) && a.frame == b.frame
            && sameBits (a.db, b.db) && sameBits (a.hz, b.hz) && sameBits (a.midiF, b.midiF)
            && sameBits (a.conf, b.conf) && sameBits (a.latMs, b.latMs) && a.note == b.note;
    }
}
