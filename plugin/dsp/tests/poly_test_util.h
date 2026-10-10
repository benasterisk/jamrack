// Shared helpers for the POLY closed tests (dsp/tests/test_poly_*.cpp): an
// assertion macro and a C++ copy of the synthetic plucked string of
// test/plucks.mjs (pluck, silence, concat), so the tests need no JS run.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <vector>

namespace polytest
{
    inline int& failures() { static int n = 0; return n; }

#define PT_CHECK(cond, ...)                                                    \
    do {                                                                       \
        if (! (cond)) {                                                        \
            std::fprintf (stderr, "FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); \
            std::fprintf (stderr, __VA_ARGS__);                                \
            std::fprintf (stderr, "\n");                                       \
            ++polytest::failures();                                            \
        }                                                                      \
    } while (0)

    inline int finish (const char* name)
    {
        if (failures() == 0) { std::printf ("%s: all checks passed\n", name); return 0; }
        std::fprintf (stderr, "%s: %d check(s) failed\n", name, failures());
        return 1;
    }

    constexpr double kPi = 3.141592653589793;

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
        double midi = 60, dur = 1, amp = 0.3, cut = 0, stiffness = 2e-4;
        int harmonics = 14;
        std::uint32_t seed = 7;
    };

    /** pluck() of test/plucks.mjs without bend / hammer (float32 output like the JS). */
    inline std::vector<float> pluck (double sr, const PluckOpts& o)
    {
        const double f0 = 440 * std::pow (2.0, (o.midi - 69) / 12);
        const int n = static_cast<int> (std::floor (sr * o.dur + 0.5));
        std::vector<float> out (static_cast<size_t> (n), 0.0f);
        const int K = o.harmonics;
        Rng rng (o.seed);
        const double B = o.stiffness;
        std::vector<double> phase (static_cast<size_t> (K + 1), 0.0);
        const int cutN = o.cut > 0 ? static_cast<int> (std::floor (o.cut * sr + 0.5)) : n;
        for (int i = 0; i < (n < cutN ? n : cutN); i++)
        {
            const double t = i / sr;
            const double attack = t / 0.0015 < 1 ? t / 0.0015 : 1;
            double s = 0;
            for (int k = 1; k <= K; k++)
            {
                const double fk = k * f0 * std::sqrt (1 + B * k * k);
                phase[static_cast<size_t> (k)] += 2 * kPi * fk / sr;
                const double a = (k == 1 ? 1.0 : 1.0 / k) * std::exp (-t * (0.3 + 1.2 * k));
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
        std::vector<float> out (static_cast<size_t> (std::floor (sr * dur + 0.5)), 0.0f);
        for (float& x : out) x = static_cast<float> (rng() * noiseAmp);
        return out;
    }

    inline std::vector<float> concat (std::initializer_list<std::vector<float>> parts)
    {
        std::vector<float> out;
        for (const auto& p : parts) out.insert (out.end(), p.begin(), p.end());
        return out;
    }
}
