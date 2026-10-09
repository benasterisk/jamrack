// Port of js/audio/guitar/poly/fft.js (see midpluck/poly/fft.h).

#include "midpluck/poly/fft.h"
#include "midpluck/poly/v8math.h"

#include <stdexcept>
#include <string>

namespace midpluck::poly
{
    namespace { constexpr double kPi = 3.141592653589793; }   // Math.PI

    RealFFT::RealFFT (int size) : n (size)
    {
        if (n < 2 || (n & (n - 1)) != 0)
            throw std::invalid_argument ("RealFFT: size " + std::to_string (n) + " is not a power of two");
        re.assign (static_cast<size_t> (n), 0.0);
        im.assign (static_cast<size_t> (n), 0.0);
        cosT.assign (static_cast<size_t> (n / 2), 0.0);
        sinT.assign (static_cast<size_t> (n / 2), 0.0);
        for (int i = 0; i < n / 2; i++)
        {
            const double a = -2 * kPi * i / n;
            cosT[static_cast<size_t> (i)] = v8math::cos (a);
            sinT[static_cast<size_t> (i)] = v8math::sin (a);
        }
        rev.assign (static_cast<size_t> (n), 0u);
        int bits = 0;                                  // Math.log2(n), exact for a power of two
        while ((1 << bits) < n) bits++;
        for (int i = 0; i < n; i++)
        {
            std::uint32_t r = 0;
            for (int b = 0; b < bits; b++)
                r |= ((static_cast<std::uint32_t> (i) >> b) & 1u) << (bits - 1 - b);
            rev[static_cast<size_t> (i)] = r;
        }
    }

    void RealFFT::magnitude (const double* x, int xLen, double* out) noexcept
    {
        const int len = xLen < n ? xLen : n;
        double* r = re.data();
        double* m = im.data();
        const std::uint32_t* rv = rev.data();
        for (int i = 0; i < n; i++)
        {
            const int j = static_cast<int> (rv[i]);
            r[i] = j < len ? x[j] : 0.0;
            m[i] = 0.0;
        }
        transform();
        for (int k = 0; k <= n / 2; k++) out[k] = v8math::hypot (r[k], m[k]);
    }

    void RealFFT::transform() noexcept
    {
        double* r = re.data();
        double* m = im.data();
        const double* c = cosT.data();
        const double* s = sinT.data();
        for (int size = 2; size <= n; size <<= 1)
        {
            const int half = size >> 1, step = n / size;
            for (int start = 0; start < n; start += size)
            {
                for (int j = 0, t = 0; j < half; j++, t += step)
                {
                    const double wr = c[t], wi = s[t];
                    const int a = start + j, b = a + half;
                    const double xr = r[b] * wr - m[b] * wi;
                    const double xi = r[b] * wi + m[b] * wr;
                    r[b] = r[a] - xr; m[b] = m[a] - xi;
                    r[a] += xr; m[a] += xi;
                }
            }
        }
    }
}
