// Port of js/audio/guitar/poly/fft.js: real-input FFT for the POLY engine,
// magnitude spectrum of a zero-padded frame.
//
// Radix-2 iterative complex FFT with precomputed twiddles and bit-reversal
// table; a real frame is fed with a zero imaginary part. Same loops, same
// evaluation order and the same twiddle values as the JavaScript (fdlibm cos /
// sin, V8 hypot: midpluck/poly/v8math.h), so the spectra are bit-identical.
#pragma once

#include <cstdint>
#include <vector>

namespace midpluck::poly
{
    class RealFFT
    {
    public:
        /** `n` must be a power of two (throws std::invalid_argument otherwise). Allocates. */
        explicit RealFFT (int n);

        int size() const noexcept { return n; }

        /** Magnitude |X[k]| for k = 0..n/2 of the real signal x[0..len) (zero-padded
         *  at the end, len clipped to n), written into out[0..n/2]. No allocation. */
        void magnitude (const double* x, int len, double* out) noexcept;

    private:
        void transform() noexcept;

        int n;
        std::vector<double> re, im, cosT, sinT;
        std::vector<std::uint32_t> rev;
    };
}
