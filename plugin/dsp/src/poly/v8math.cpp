// fdlibm sin / cos / log / log10 as V8 compiles them (src/base/ieee754.cc), and
// V8's Math.hypot builtin. See midpluck/poly/v8math.h for why and how verified.
//
// fdlibm: Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
// Developed at SunSoft, a Sun Microsystems, Inc. business. Permission to use,
// copy, modify, and distribute this software is freely granted, provided that
// this notice is preserved.

#include "midpluck/poly/v8math.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace midpluck::v8math
{
    namespace
    {
        inline std::int32_t highWord (double x) noexcept
        {
            std::uint64_t u; std::memcpy (&u, &x, 8);
            return static_cast<std::int32_t> (static_cast<std::uint32_t> (u >> 32));
        }
        inline std::uint32_t lowWord (double x) noexcept
        {
            std::uint64_t u; std::memcpy (&u, &x, 8);
            return static_cast<std::uint32_t> (u);
        }
        inline double fromWords (std::uint32_t hi, std::uint32_t lo) noexcept
        {
            const std::uint64_t u = (static_cast<std::uint64_t> (hi) << 32) | lo;
            double x; std::memcpy (&x, &u, 8);
            return x;
        }
        inline double setHighWord (double x, std::uint32_t hi) noexcept { return fromWords (hi, lowWord (x)); }

        // ---- k_sin.c
        double kernelSin (double x, double y, int iy) noexcept
        {
            constexpr double half = 5.00000000000000000000e-01,
                S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03,
                S3 = -1.98412698298579493134e-04, S4 = 2.75573137070700676789e-06,
                S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;
            const std::int32_t ix = highWord (x) & 0x7fffffff;
            if (ix < 0x3e400000)                      // |x| < 2**-27
                if (static_cast<int> (x) == 0) return x;
            const double z = x * x;
            const double v = z * x;
            const double r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
            if (iy == 0) return x + v * (S1 + z * r);
            return x - ((z * (half * y - v * r) - y) - v * S1);
        }

        // ---- k_cos.c (fdlibm 5.3 form, as in V8)
        double kernelCos (double x, double y) noexcept
        {
            constexpr double one = 1.0,
                C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03,
                C3 = 2.48015872894767294178e-05, C4 = -2.75573143513906633035e-07,
                C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;
            const std::int32_t ix = highWord (x) & 0x7fffffff;
            if (ix < 0x3e400000)                      // |x| < 2**-27
                if (static_cast<int> (x) == 0) return one;
            const double z = x * x;
            const double r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
            if (ix < 0x3fd33333)                      // |x| < 0.3
                return one - (0.5 * z - (z * r - x * y));
            double qx;
            if (ix > 0x3fe90000) qx = 0.28125;        // |x| > 0.78125
            else qx = fromWords (static_cast<std::uint32_t> (ix - 0x00200000), 0);   // x/4
            const double iz = 0.5 * z - qx;
            const double a = one - qx;
            return a - (iz - (z * r - x * y));
        }

        // ---- e_rem_pio2.c, small and medium arguments (|x| <= 2^19 * pi/2).
        // Returns false for larger arguments (the caller falls back).
        bool remPio2 (double x, double* y, int& nOut) noexcept
        {
            static const std::int32_t npio2_hw[] = {
                0x3FF921FB, 0x400921FB, 0x4012D97C, 0x401921FB, 0x401F6A7A, 0x4022D97C,
                0x4025FDBB, 0x402921FB, 0x402C463A, 0x402F6A7A, 0x4031475C, 0x4032D97C,
                0x40346B9C, 0x4035FDBB, 0x40378FDB, 0x403921FB, 0x403AB41B, 0x403C463A,
                0x403DD85A, 0x403F6A7A, 0x40407E4C, 0x4041475C, 0x4042106C, 0x4042D97C,
                0x4043A28C, 0x40446B9C, 0x404534AC, 0x4045FDBB, 0x4046C6CB, 0x40478FDB,
                0x404858EB, 0x404921FB,
            };
            constexpr double half = 5.00000000000000000000e-01,
                invpio2 = 6.36619772367581382433e-01,
                pio2_1 = 1.57079632673412561417e+00, pio2_1t = 6.07710050650619224932e-11,
                pio2_2 = 6.07710050630396597660e-11, pio2_2t = 2.02226624879595063154e-21,
                pio2_3 = 2.02226624871116645580e-21, pio2_3t = 8.47842766036889956997e-32;

            const std::int32_t hx = highWord (x);
            const std::int32_t ix = hx & 0x7fffffff;
            if (ix <= 0x3fe921fb) { y[0] = x; y[1] = 0; nOut = 0; return true; }
            if (ix < 0x4002d97c)                      // |x| < 3pi/4, n = +-1
            {
                if (hx > 0)
                {
                    double z = x - pio2_1;
                    if (ix != 0x3ff921fb) { y[0] = z - pio2_1t; y[1] = (z - y[0]) - pio2_1t; }
                    else { z -= pio2_2; y[0] = z - pio2_2t; y[1] = (z - y[0]) - pio2_2t; }
                    nOut = 1;
                }
                else
                {
                    double z = x + pio2_1;
                    if (ix != 0x3ff921fb) { y[0] = z + pio2_1t; y[1] = (z - y[0]) + pio2_1t; }
                    else { z += pio2_2; y[0] = z + pio2_2t; y[1] = (z - y[0]) + pio2_2t; }
                    nOut = -1;
                }
                return true;
            }
            if (ix <= 0x413921fb)                     // |x| ~<= 2^19 * (pi/2)
            {
                double t = std::fabs (x);
                const std::int32_t n = static_cast<std::int32_t> (t * invpio2 + half);
                const double fn = static_cast<double> (n);
                double r = t - fn * pio2_1;
                double w = fn * pio2_1t;              // 1st round good to 85 bits
                if (n < 32 && ix != npio2_hw[n - 1])
                {
                    y[0] = r - w;                     // quick check: no cancellation
                }
                else
                {
                    const std::int32_t j = ix >> 20;
                    y[0] = r - w;
                    std::uint32_t high = static_cast<std::uint32_t> (highWord (y[0]));
                    std::int32_t i = j - static_cast<std::int32_t> ((high >> 20) & 0x7ff);
                    if (i > 16)                       // 2nd iteration, good to 118 bits
                    {
                        t = r;
                        w = fn * pio2_2;
                        r = t - w;
                        w = fn * pio2_2t - ((t - r) - w);
                        y[0] = r - w;
                        high = static_cast<std::uint32_t> (highWord (y[0]));
                        i = j - static_cast<std::int32_t> ((high >> 20) & 0x7ff);
                        if (i > 49)                   // 3rd iteration, 151 bits
                        {
                            t = r;
                            w = fn * pio2_3;
                            r = t - w;
                            w = fn * pio2_3t - ((t - r) - w);
                            y[0] = r - w;
                        }
                    }
                }
                y[1] = (r - y[0]) - w;
                if (hx < 0) { y[0] = -y[0]; y[1] = -y[1]; nOut = -n; }
                else nOut = n;
                return true;
            }
            return false;
        }
    }

    double sin (double x) noexcept
    {
        const std::int32_t ix = highWord (x) & 0x7fffffff;
        if (ix <= 0x3fe921fb) return kernelSin (x, 0.0, 0);
        if (ix >= 0x7ff00000) return x - x;
        double y[2]; int n = 0;
        if (! remPio2 (x, y, n)) return std::sin (x);
        switch (n & 3)
        {
            case 0:  return kernelSin (y[0], y[1], 1);
            case 1:  return kernelCos (y[0], y[1]);
            case 2:  return -kernelSin (y[0], y[1], 1);
            default: return -kernelCos (y[0], y[1]);
        }
    }

    double cos (double x) noexcept
    {
        const std::int32_t ix = highWord (x) & 0x7fffffff;
        if (ix <= 0x3fe921fb) return kernelCos (x, 0.0);
        if (ix >= 0x7ff00000) return x - x;
        double y[2]; int n = 0;
        if (! remPio2 (x, y, n)) return std::cos (x);
        switch (n & 3)
        {
            case 0:  return kernelCos (y[0], y[1]);
            case 1:  return -kernelSin (y[0], y[1], 1);
            case 2:  return -kernelCos (y[0], y[1]);
            default: return kernelSin (y[0], y[1], 1);
        }
    }

    double log (double x) noexcept
    {
        constexpr double ln2_hi = 6.93147180369123816490e-01, ln2_lo = 1.90821492927058770002e-10,
            two54 = 1.80143985094819840000e+16,
            Lg1 = 6.666666666666735130e-01, Lg2 = 3.999999999940941908e-01,
            Lg3 = 2.857142874366239149e-01, Lg4 = 2.222219843214978396e-01,
            Lg5 = 1.818357216161805012e-01, Lg6 = 1.531383769920937332e-01,
            Lg7 = 1.479819860511658591e-01;

        std::int32_t hx = highWord (x);
        const std::uint32_t lx = lowWord (x);
        std::int32_t k = 0;
        if (hx < 0x00100000)                          // x < 2**-1022
        {
            if (((hx & 0x7fffffff) | static_cast<std::int32_t> (lx)) == 0)
                return -std::numeric_limits<double>::infinity();         // log(+-0) = -inf
            if (hx < 0) return std::numeric_limits<double>::quiet_NaN(); // log(-#) = NaN
            k -= 54;
            x *= two54;                               // subnormal: scale up
            hx = highWord (x);
        }
        if (hx >= 0x7ff00000) return x + x;
        k += (hx >> 20) - 1023;
        hx &= 0x000fffff;
        std::int32_t i = (hx + 0x95f64) & 0x100000;
        x = setHighWord (x, static_cast<std::uint32_t> (hx | (i ^ 0x3ff00000)));   // normalise x or x/2
        k += (i >> 20);
        const double f = x - 1.0;
        if ((0x000fffff & (2 + hx)) < 3)              // -2**-20 <= f < 2**-20
        {
            if (f == 0.0)
            {
                if (k == 0) return 0.0;
                const double dk = static_cast<double> (k);
                return dk * ln2_hi + dk * ln2_lo;
            }
            const double R = f * f * (0.5 - 0.33333333333333333 * f);
            if (k == 0) return f - R;
            const double dk = static_cast<double> (k);
            return dk * ln2_hi - ((R - dk * ln2_lo) - f);
        }
        const double s = f / (2.0 + f);
        const double dk = static_cast<double> (k);
        const double z = s * s;
        i = hx - 0x6147a;
        const double w = z * z;
        const std::int32_t j = 0x6b851 - hx;
        const double t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
        const double t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
        i |= j;
        const double R = t2 + t1;
        if (i > 0)
        {
            const double hfsq = 0.5 * f * f;
            if (k == 0) return f - (hfsq - s * (hfsq + R));
            return dk * ln2_hi - ((hfsq - (s * (hfsq + R) + dk * ln2_lo)) - f);
        }
        if (k == 0) return f - s * (f - R);
        return dk * ln2_hi - ((s * (f - R) - dk * ln2_lo) - f);
    }

    double log10 (double x) noexcept
    {
        constexpr double two54 = 1.80143985094819840000e+16,
            ivln10 = 4.34294481903251816668e-01,
            log10_2hi = 3.01029995663611771306e-01,
            log10_2lo = 3.69423907715893078616e-13;

        std::int32_t hx = highWord (x);
        const std::uint32_t lx = lowWord (x);
        std::int32_t k = 0;
        if (hx < 0x00100000)                          // x < 2**-1022
        {
            if (((hx & 0x7fffffff) | static_cast<std::int32_t> (lx)) == 0)
                return -std::numeric_limits<double>::infinity();
            if (hx < 0) return std::numeric_limits<double>::quiet_NaN();
            k -= 54;
            x *= two54;
            hx = highWord (x);
        }
        if (hx >= 0x7ff00000) return x + x;
        if (hx == 0x3ff00000 && lowWord (x) == 0) return 0.0;   // log10(1) = +0
        k += (hx >> 20) - 1023;
        const std::int32_t i = static_cast<std::int32_t> ((static_cast<std::uint32_t> (k) & 0x80000000u) >> 31);
        hx = (hx & 0x000fffff) | ((0x3ff - i) << 20);
        const double y = static_cast<double> (k + i);
        x = setHighWord (x, static_cast<std::uint32_t> (hx));
        const double z = y * log10_2lo + ivln10 * log (x);
        return z + y * log10_2hi;
    }

    double hypot (double a, double b) noexcept
    {
        const bool nan = std::isnan (a) || std::isnan (b);
        const double xa = std::fabs (a), xb = std::fabs (b);
        double mx = 0;
        if (! std::isnan (a) && xa > mx) mx = xa;
        if (! std::isnan (b) && xb > mx) mx = xb;
        if (mx == std::numeric_limits<double>::infinity()) return mx;
        if (nan) return std::numeric_limits<double>::quiet_NaN();
        if (mx == 0) return 0.0;
        // Kahan summation of the squares, normalised by the largest (V8 math.tq)
        double sum = 0, compensation = 0;
        const double xs[2] = { xa, xb };
        for (double v : xs)
        {
            const double n = v / mx;
            const double summand = n * n - compensation;
            const double preliminary = sum + summand;
            compensation = (preliminary - sum) - summand;
            sum = preliminary;
        }
        return std::sqrt (sum) * mx;
    }
}
