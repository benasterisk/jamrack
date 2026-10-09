// Transcendental functions with the exact results of V8 (Node, Chrome), for the
// POLY port (docs/plugin-plan.md, rule 3.3-9).
//
// Measured on this PC (Node 25 / V8 against the MSVC 14.44 CRT, 872 592 inputs
// taken from the engine's own call sites): std::cos and std::sin differ from
// Math.cos / Math.sin by one ulp on 2.6 % of the twiddle, window and kernel
// arguments, std::log10 from Math.log10 on 7.5 % of levels, and std::hypot
// from Math.hypot on 42 % of FFT bins. V8 computes sin, cos, log and log10 with
// the fdlibm algorithms and Math.hypot with its own scaled Kahan sum; the
// functions below are those algorithms, bit for bit (verified on the same
// inputs: 0 mismatches). std::pow matched Math.pow on all 200 249 inputs
// tried, and sqrt, floor, fabs are exact everywhere, so they stay std::.
//
// fdlibm: Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
// Developed at SunSoft, a Sun Microsystems, Inc. business. Permission to use,
// copy, modify, and distribute this software is freely granted, provided that
// this notice is preserved.
#pragma once

namespace midpluck::v8math
{
    /** Math.sin. Arguments up to 2^19 * pi/2 in magnitude (the engine stays under 10 pi);
     *  beyond that falls back to std::sin, never reached by the engine. */
    double sin (double x) noexcept;

    /** Math.cos (same range note as sin). */
    double cos (double x) noexcept;

    /** Math.log (natural logarithm, fdlibm e_log.c). */
    double log (double x) noexcept;

    /** Math.log10 (fdlibm e_log10.c). */
    double log10 (double x) noexcept;

    /** Math.hypot(a, b): V8's builtin, max-scaled Kahan sum of squares. */
    double hypot (double a, double b) noexcept;
}
