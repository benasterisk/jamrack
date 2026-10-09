// Port of js/audio/guitar/poly/bank.js (see midpluck/poly/bank.h).

#include "midpluck/poly/bank.h"
#include "midpluck/poly/fft.h"
#include "midpluck/poly/v8math.h"
#include "midpluck/jsmath.h"

#include <algorithm>
#include <cmath>

namespace midpluck::poly
{
    namespace
    {
        constexpr double kPi = 3.141592653589793;   // Math.PI
        constexpr double F_MAX = 10000;
        constexpr int OVERSAMPLE = 32;
    }

    double midiToHz (double m) noexcept
    {
        return 440 * std::pow (2.0, (m - 69) / 12);
    }

    std::vector<double> hannWindow (int n)
    {
        std::vector<double> w (static_cast<size_t> (n), 0.0);
        double sum = 0;
        for (int i = 0; i < n; i++)
        {
            w[static_cast<size_t> (i)] = 0.5 - 0.5 * v8math::cos (2 * kPi * (i + 0.5) / n);
            sum += w[static_cast<size_t> (i)];
        }
        for (int i = 0; i < n; i++) w[static_cast<size_t> (i)] *= 2 / sum;
        return w;
    }

    std::vector<double> windowKernel (const std::vector<double>& w)
    {
        const int n = NFFT * OVERSAMPLE;
        RealFFT fft (n);
        std::vector<double> mag (static_cast<size_t> (n / 2 + 1), 0.0);
        fft.magnitude (w.data(), static_cast<int> (w.size()), mag.data());
        const double k0 = mag[0];
        for (double& m : mag) m /= k0;
        return mag;
    }

    void renderProfile (double f0, double B, const double* amps, int nAmps,
                        const std::vector<double>& kern, double* spec)
    {
        const int last = static_cast<int> (kern.size()) - 2;
        const double* k = kern.data();
        for (int n = 1; n <= nAmps; n++)
        {
            const double a = amps[n - 1];
            const double fn = n * f0 * std::sqrt (1 + B * n * n);
            if (fn > F_MAX || a <= 0) continue;
            const double centre = fn / BIN_HZ;
            for (int b = 0; b < NBINS; b++)
            {
                const double off = std::fabs (b - centre) * OVERSAMPLE;
                double idx = std::floor (off);
                if (idx > last) idx = last;
                const double frac = off - idx;
                const int i = static_cast<int> (idx);
                spec[b] += a * (k[i] * (1 - frac) + k[i + 1] * frac);
            }
        }
    }

    Bank buildBank (BankWindow win, const Profile& profile)
    {
        const int n = win == BankWindow::Medium ? WINDOW_MEDIUM : WINDOW_SHORT;
        Bank bank;
        bank.window = hannWindow (n);
        const std::vector<double> kern = windowKernel (bank.window);
        bank.K = BANK_K;
        bank.W.assign (static_cast<size_t> (bank.K) * NBINS, 0.0f);
        bank.meta.reserve (static_cast<size_t> (bank.K));
        std::vector<double> col (NBINS, 0.0);
        int k = 0;
        auto store = [&]
        {
            double ss = 0;
            for (int b = 0; b < NBINS; b++) ss += col[static_cast<size_t> (b)] * col[static_cast<size_t> (b)];
            const double inv = 1 / std::sqrt (ss);
            const size_t base = static_cast<size_t> (k) * NBINS;
            for (int b = 0; b < NBINS; b++)
                bank.W[base + static_cast<size_t> (b)] = static_cast<float> (col[static_cast<size_t> (b)] * inv);
            std::fill (col.begin(), col.end(), 0.0);
            k++;
        };
        for (int s = 0; s < 6; s++)
        {
            const double bOpen = profile.bLaw[s][0], slope = profile.bLaw[s][1];
            for (int f = 0; f < FRETS; f++)
            {
                const int midi = OPEN_MIDI[s] + f;
                const double B = bOpen * std::pow (2.0, f * slope / 6);
                renderProfile (midiToHz (midi), B, profile.prof[s], PROFILE_PARTIALS, kern, col.data());
                bank.meta.push_back ({ s, f, midi, "" });
                store();
            }
        }
        // noise templates: white, and pink (1/sqrt f, floored at 50 Hz)
        std::fill (col.begin(), col.end(), 1.0);
        bank.meta.push_back ({ -1, -1, -1, "noise-white" });
        store();
        for (int b = 0; b < NBINS; b++) col[static_cast<size_t> (b)] = 1 / std::sqrt (js::max (b * BIN_HZ, 50.0));
        bank.meta.push_back ({ -1, -1, -1, "noise-pink" });
        store();
        for (int j = 0; j < N_PITCH; j++)
        {
            int c = 0;
            for (int i = 0; i < bank.K; i++)
                if (bank.meta[static_cast<size_t> (i)].midi == PITCH_LO + j && c < MAX_COLS_PER_PITCH)
                    bank.pitchCols[j][c++] = i;
            bank.pitchCount[j] = c;
        }
        return bank;
    }
}
