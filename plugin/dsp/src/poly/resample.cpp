// Port of js/audio/guitar/poly/resample.js (see midpluck/poly/resample.h).

#include "midpluck/poly/resample.h"
#include "midpluck/poly/v8math.h"
#include "midpluck/jsmath.h"

#include <cmath>

namespace midpluck::poly
{
    namespace
    {
        constexpr double kPi = 3.141592653589793;   // Math.PI

        long long gcd (long long a, long long b)
        {
            while (b != 0) { const long long t = a % b; a = b; b = t; }
            return a;
        }

        /** Modified Bessel function of the first kind, order 0 (series). */
        double besselI0 (double x)
        {
            double sum = 1, term = 1;
            const double y = x * x / 4;
            for (int k = 1; k < 60; k++)
            {
                term *= y / (k * k);
                sum += term;
                if (term < sum * 1e-17) break;
            }
            return sum;
        }

        struct Ratio { long long up, down, halfLen; };

        Ratio ratioFor (double srIn, double srOut, double factor)
        {
            const long long a = static_cast<long long> (js::round (srIn));
            const long long b = static_cast<long long> (js::round (srOut));
            const long long g = gcd (a, b);
            Ratio r;
            r.up = b / g;
            r.down = a / g;
            const long long maxRate = r.up > r.down ? r.up : r.down;
            r.halfLen = static_cast<long long> (js::round (factor * static_cast<double> (maxRate)));
            return r;
        }
    }

    std::vector<double> firwinKaiser (int numtaps, double cutoff, double beta)
    {
        std::vector<double> h (static_cast<size_t> (numtaps), 0.0);
        const double mid = (numtaps - 1) / 2.0;
        const double i0b = besselI0 (beta);
        double sum = 0;
        for (int i = 0; i < numtaps; i++)
        {
            const double m = i - mid;
            const double sinc = m == 0 ? 1.0 : v8math::sin (kPi * cutoff * m) / (kPi * cutoff * m);
            const double r = 2.0 * i / (numtaps - 1) - 1;
            const double kaiser = besselI0 (beta * std::sqrt (js::max (0.0, 1 - r * r))) / i0b;
            h[static_cast<size_t> (i)] = cutoff * sinc * kaiser;
            sum += h[static_cast<size_t> (i)];
        }
        for (int i = 0; i < numtaps; i++) h[static_cast<size_t> (i)] /= sum;   // unit gain at DC
        return h;
    }

    Resampler::Resampler (double srInHz, double srOutHz, double halfLenFactor)
        : srIn (srInHz), srOut (srOutHz)
    {
        const Ratio r = ratioFor (srIn, srOut, halfLenFactor);
        up = r.up;
        down = r.down;
        halfLen = r.halfLen;
        const long long maxRate = up > down ? up : down;
        const int taps = static_cast<int> (2 * halfLen + 1);
        std::vector<double> h = firwinKaiser (taps, 1.0 / static_cast<double> (maxRate), 5.0);
        for (int i = 0; i < taps; i++) h[static_cast<size_t> (i)] *= static_cast<double> (up);
        // Polyphase split: output sample n uses phase (n * down) mod up and
        // input index floor(n * down / up), looking back over `perPhase` inputs.
        perPhase = static_cast<int> (std::ceil (static_cast<double> (taps) / static_cast<double> (up)));
        phases.assign (static_cast<size_t> (up * perPhase), 0.0);
        for (long long p = 0; p < up; p++)
            for (int j = 0; j < perPhase; j++)
            {
                const long long k = p + j * up;    // tap index for input j steps back
                phases[static_cast<size_t> (p * perPhase + j)] = k < taps ? h[static_cast<size_t> (k)] : 0.0;
            }
        int size = 64;
        while (size < 2 * perPhase + 16) size *= 2;
        hist.assign (static_cast<size_t> (size), 0.0);
        mask = size - 1;
        identity = up == 1 && down == 1;
    }

    double resamplerDelaySeconds (double sampleRate, double srOut)
    {
        const Ratio r = ratioFor (sampleRate, srOut, 10);
        return static_cast<double> (r.halfLen) / static_cast<double> (r.up) / sampleRate;
    }
}
