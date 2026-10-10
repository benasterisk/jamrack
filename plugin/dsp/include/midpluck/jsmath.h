// JavaScript number semantics for the port (docs/plugin-plan.md, rule 3.3-9).
// Every JS number is an IEEE double; a Float32Array store rounds to float and a
// load promotes back to double. These helpers keep the C++ bit-compatible with
// V8 where the C++ standard library differs.
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace midpluck::js
{
    constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

    /** Math.round: halves round towards +infinity (-2.5 -> -2), exact for 0.49999999999999994. */
    inline double round (double x) noexcept
    {
        if (std::isnan (x) || std::isinf (x)) return x;
        const double r = std::floor (x);
        return (x - r >= 0.5) ? r + 1.0 : r;
    }

    /** Math.max of two numbers: NaN if either is NaN; +0 beats -0. */
    inline double max (double a, double b) noexcept
    {
        if (std::isnan (a) || std::isnan (b)) return NaN;
        if (a == b) return std::signbit (a) ? b : a;
        return a > b ? a : b;
    }

    /** Math.min of two numbers: NaN if either is NaN; -0 beats +0. */
    inline double min (double a, double b) noexcept
    {
        if (std::isnan (a) || std::isnan (b)) return NaN;
        if (a == b) return std::signbit (a) ? a : b;
        return a < b ? a : b;
    }

    /** x | 0 (ToInt32): truncation towards zero, modulo 2^32, NaN/Inf -> 0. */
    inline std::int32_t toInt32 (double x) noexcept
    {
        if (std::isnan (x) || std::isinf (x)) return 0;
        const double t = std::trunc (x);
        const double m = std::fmod (t, 4294967296.0);
        double u = m < 0 ? m + 4294967296.0 : m;
        if (u >= 2147483648.0) u -= 4294967296.0;
        return static_cast<std::int32_t> (u);
    }

    /** Store into a Float32Array element and read it back. */
    inline double f32 (double x) noexcept { return static_cast<double> (static_cast<float> (x)); }
}
