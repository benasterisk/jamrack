// MONO closed test: the RBJ biquads of the front end (tracker.js Biquad).
// Their coefficients, impulse response and steady-state response to a 1 kHz
// cosine must match the closed-form RBJ-cookbook filter, computed here
// independently (long double, which MSVC makes a double; direct form I for
// the impulse response, H(e^jw) for the 1 kHz response), to 1e-9.
#include "mono_test_util.h"

#include <complex>

using namespace midpluck;

namespace
{
    struct Ref { long double b0, b1, b2, a1, a2; };

    // RBJ cookbook, normalised by a0 (Robert Bristow-Johnson, "Audio EQ Cookbook").
    Ref rbj (bool lowPass, long double sr, long double fc, long double q)
    {
        const long double w0 = 2.0L * 3.141592653589793238462643383279L * fc / sr;
        const long double cs = std::cos (w0), alpha = std::sin (w0) / (2.0L * q);
        const long double a0 = 1.0L + alpha;
        Ref r {};
        if (lowPass) { r.b0 = (1 - cs) / 2 / a0; r.b1 = (1 - cs) / a0; }
        else { r.b0 = (1 + cs) / 2 / a0; r.b1 = -(1 + cs) / a0; }
        r.b2 = r.b0;
        r.a1 = -2 * cs / a0;
        r.a2 = (1 - alpha) / a0;
        return r;
    }

    void checkFilter (const char* label, mono::Biquad f, bool lowPass, double sr, double fc, double q)
    {
        const Ref r = rbj (lowPass, sr, fc, q);
        const double tolCoef = 1e-12;
        MT_CHECK (std::fabs (f.b0 - (double) r.b0) < tolCoef, "%s b0 %.17g vs %.17Lg", label, f.b0, r.b0);
        MT_CHECK (std::fabs (f.b1 - (double) r.b1) < tolCoef, "%s b1 %.17g vs %.17Lg", label, f.b1, r.b1);
        MT_CHECK (std::fabs (f.b2 - (double) r.b2) < tolCoef, "%s b2 %.17g vs %.17Lg", label, f.b2, r.b2);
        MT_CHECK (std::fabs (f.a1 - (double) r.a1) < tolCoef, "%s a1 %.17g vs %.17Lg", label, f.a1, r.a1);
        MT_CHECK (std::fabs (f.a2 - (double) r.a2) < tolCoef, "%s a2 %.17g vs %.17Lg", label, f.a2, r.a2);

        // impulse response vs the direct-form-I recursion on the closed-form coefficients
        {
            mono::Biquad g = f;
            g.z1 = g.z2 = 0;
            long double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            double worst = 0;
            for (int n = 0; n < 2000; n++)
            {
                const double x = n == 0 ? 1.0 : 0.0;
                const double y = g.run (x);
                const long double yr = r.b0 * x + r.b1 * x1 + r.b2 * x2 - r.a1 * y1 - r.a2 * y2;
                x2 = x1; x1 = x; y2 = y1; y1 = yr;
                worst = std::max (worst, (double) std::fabs ((long double) y - yr));
            }
            MT_CHECK (worst <= 1e-9, "%s impulse response differs by %.3g", label, worst);
            std::printf ("%-22s impulse response max |error| %.3g\n", label, worst);
        }

        // steady state at 1 kHz: y[n] = |H| cos(w n + arg H) once the transient has died
        {
            mono::Biquad g = f;
            g.z1 = g.z2 = 0;
            const long double w = 2.0L * 3.141592653589793238462643383279L * 1000.0L / sr;
            const std::complex<long double> z1 = std::polar (1.0L, -w), z2 = std::polar (1.0L, -2 * w);
            const std::complex<long double> H = (r.b0 + r.b1 * z1 + r.b2 * z2) / (1.0L + r.a1 * z1 + r.a2 * z2);
            const long double mag = std::abs (H), ph = std::arg (H);
            double worst = 0;
            const int settle = static_cast<int> (sr * 0.1), total = static_cast<int> (sr * 0.5);
            for (int n = 0; n < total; n++)
            {
                const double x = static_cast<double> (std::cos (w * n));
                const double y = g.run (x);
                if (n >= settle)
                {
                    const long double yr = mag * std::cos (w * n + ph);
                    worst = std::max (worst, (double) std::fabs ((long double) y - yr));
                }
            }
            MT_CHECK (worst <= 1e-9, "%s 1 kHz response differs by %.3g", label, worst);
            std::printf ("%-22s 1 kHz |H| = %.12Lf (%.2Lf dB), max |error| %.3g\n", label, mag, 20 * std::log10 (mag), worst);
        }
    }
}

int main()
{
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        char label[64];
        // the filters the tracker builds (tracker.js constructor)
        MonoTracker tr (sr);
        std::snprintf (label, sizeof label, "lp 3 kHz @ %.0f", sr);
        checkFilter (label, tr.lowPass1(), true, sr, 3000, 0.7);
        std::snprintf (label, sizeof label, "lp2 3 kHz @ %.0f", sr);
        checkFilter (label, tr.lowPass2(), true, sr, 3000, 0.7);
        std::snprintf (label, sizeof label, "hp 2.5 kHz @ %.0f", sr);
        checkFilter (label, tr.highPass(), false, sr, 2500, 0.7);
    }
    return monotest::finish ("test_mono_biquad");
}
