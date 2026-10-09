// Port of js/audio/guitar/poly/bank.js: generic template bank of the POLY
// engine ("fitatt" in the prototype): one magnitude-spectrum template per
// (string, fret) rendered from the measured per-string partial profile and
// inharmonicity law (profile.h), plus two noise templates for the pick
// transient. Columns are unit L2, stored float32 like the JS Float32Array
// (rule 3.3-4). Building takes tens of milliseconds and allocates: never on
// the audio thread (rule 3.3-5).
#pragma once

#include "midpluck/poly/profile.h"

#include <vector>

namespace midpluck::poly
{
    constexpr int SR = 24000;                 // analysis rate
    constexpr int HOP = 64;                   // samples per hop (2.67 ms)
    constexpr int NFFT = 2048;
    constexpr int NBINS = NFFT / 2 + 1;       // 1025
    constexpr double BIN_HZ = static_cast<double> (SR) / NFFT;   // 11.71875 Hz
    constexpr int WINDOW_MEDIUM = 1024;       // WINDOWS.medium
    constexpr int WINDOW_SHORT = 512;         // WINDOWS.short
    constexpr int OPEN_MIDI[6] = { 40, 45, 50, 55, 59, 64 };    // E2 A2 D3 G3 B3 E4
    constexpr int FRETS = 20;
    constexpr int PITCH_LO = 40, N_PITCH = 44;                  // E2 .. B5 (midi 40..83)
    constexpr int BANK_K = 6 * FRETS + 2;     // 122 columns
    constexpr int MAX_COLS_PER_PITCH = 6;     // one per string at most

    double midiToHz (double m) noexcept;

    /** Analysis window of n samples, index n-1 = newest sample, scaled so a
     *  full-scale sine shows a peak magnitude of 1 in the spectrum. */
    std::vector<double> hannWindow (int n);

    /** |W(f)| of the window on a grid of 1/32 bin, normalised to W(0) = 1. */
    std::vector<double> windowKernel (const std::vector<double>& w);

    /** Adds the partials of one note (amplitudes amps[0..nAmps)) into spec (NBINS). */
    void renderProfile (double f0, double B, const double* amps, int nAmps,
                        const std::vector<double>& kern, double* spec);

    struct ColumnMeta
    {
        int string = -1, fret = -1, midi = -1;   // midi -1 for the noise columns
        const char* name = "";                   // "noise-white" | "noise-pink" | ""
    };

    struct Bank
    {
        int K = 0;
        std::vector<float> W;                    // column-major: column k at W[k * NBINS .. (k + 1) * NBINS)
        std::vector<ColumnMeta> meta;
        std::vector<double> window;              // the analysis window of the bank (medium: 1024)
        int pitchCols[N_PITCH][MAX_COLS_PER_PITCH] = {};   // columns of pitch PITCH_LO + j, ascending
        int pitchCount[N_PITCH] = {};
    };

    enum class BankWindow { Medium, Short };

    /** buildBank(winName, profile) of bank.js. Allocates; ~tens of ms. */
    Bank buildBank (BankWindow win = BankWindow::Medium, const Profile& profile = genericProfile());
}
