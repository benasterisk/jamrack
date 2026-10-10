// Port of js/audio/guitar/poly/resample.js: streaming polyphase resampler to
// the POLY analysis rate (24 kHz), with the filter of scipy.signal.resample_poly
// (Kaiser beta 5 windowed sinc, cutoff 1 / max(up, down) of Nyquist, half-length
// 10 x max(up, down) taps on the upsampled grid, DC gain `up`).
//
// The output is causal: output sample n corresponds to the input seen
// delay() input samples ago (0.42 ms at the usual rates).
#pragma once

#include <vector>

namespace midpluck::poly
{
    /** scipy.signal.firwin(numtaps, cutoff, window=('kaiser', beta)), cutoff in units of Nyquist. */
    std::vector<double> firwinKaiser (int numtaps, double cutoff, double beta);

    class Resampler
    {
    public:
        /** Allocates (kernel and history). halfLenFactor 10 = scipy. */
        Resampler (double srIn, double srOut, double halfLenFactor = 10);

        /** Group delay of the filter in INPUT samples. */
        double delay() const noexcept { return static_cast<double> (halfLen) / static_cast<double> (up); }

        /** Same delay in seconds. */
        double delaySeconds() const noexcept { return delay() / srIn; }

        /** Pushes one input sample; calls onSample(y) for every output sample it
         *  completes (0, 1 or more). No allocation. */
        template <class F>
        void pushSample (double x, F&& onSample) noexcept
        {
            if (identity) { onSample (x); return; }
            double* h = hist.data();
            h[wi] = x;
            wi = (wi + 1) & mask;
            count++;
            // every output whose anchor input has arrived
            while (true)
            {
                const long long num = nextOut * down;        // position in the upsampled grid
                const long long anchor = num / up;           // Math.floor(num / up), num >= 0
                if (anchor >= count) break;
                const long long phase = num - anchor * up;   // (n * down) mod up
                const double* ph = phases.data() + phase * perPhase;
                double acc = 0;
                // input j steps back from `anchor`: hist index of input (anchor - j)
                int idx = static_cast<int> ((static_cast<long long> (wi) - 1 - (count - 1 - anchor)) & mask);
                for (int j = 0; j < perPhase; j++)
                {
                    acc += ph[j] * h[idx];
                    idx = (idx - 1) & mask;
                }
                onSample (acc);
                nextOut++;
            }
        }

        long long up = 1, down = 1;
        double srIn = 0, srOut = 0;
        long long halfLen = 0;
        int perPhase = 0;
        bool identity = false;

    private:
        std::vector<double> phases;      // up x perPhase, phase-major
        std::vector<double> hist;        // input history, newest at wi - 1
        int mask = 0;
        int wi = 0;
        long long count = 0;             // input samples consumed so far
        long long nextOut = 0;           // output sample index to produce next
    };

    /** Delay of Resampler(sampleRate, srOut) in seconds, without building it. */
    double resamplerDelaySeconds (double sampleRate, double srOut);
}
