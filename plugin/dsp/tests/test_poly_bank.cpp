// test/poly-engine.test.mjs, test 3: "bank: 122 unit-L2 columns, every open
// string peaks at its fundamental", plus: the generated profile header holds
// exactly the doubles V8 parsed from js/audio/guitar/poly/profile.js.
#include "poly_test_util.h"
#include "midpluck/poly/bank.h"
#include "../src/poly/profile_data.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{
    template <size_t R, size_t C>
    int countBitMismatches (const double (&v)[R][C], const std::uint64_t (&bits)[R][C])
    {
        int bad = 0;
        for (size_t r = 0; r < R; r++)
            for (size_t c = 0; c < C; c++)
            {
                std::uint64_t u;
                std::memcpy (&u, &v[r][c], 8);
                if (u != bits[r][c]) bad++;
            }
        return bad;
    }
}

int main()
{
    using namespace polytest;
    using namespace midpluck::poly;
    namespace g = midpluck::poly::generated;

    PT_CHECK (countBitMismatches (g::B_LAW, g::B_LAW_BITS) == 0, "B_LAW literals differ from V8's doubles");
    PT_CHECK (countBitMismatches (g::PROF_ATT, g::PROF_ATT_BITS) == 0, "PROF_ATT literals differ from V8's doubles");
    PT_CHECK (countBitMismatches (g::PROF_DEC, g::PROF_DEC_BITS) == 0, "PROF_DEC literals differ from V8's doubles");

    const Bank bank = buildBank();
    PT_CHECK (bank.K == 122, "K = %d", bank.K);
    for (int k = 0; k < bank.K; k++)
    {
        double ss = 0;
        for (int b = 0; b < NBINS; b++)
        {
            const double w = bank.W[static_cast<size_t> (k) * NBINS + b];
            ss += w * w;
        }
        PT_CHECK (std::fabs (ss - 1) < 1e-5, "column %d norm %.9g", k, ss);
    }
    for (int s = 0; s < 6; s++)
    {
        const int k = s * 20;                     // fret 0
        int best = 0;
        for (int b = 1; b < NBINS; b++)
            if (bank.W[static_cast<size_t> (k) * NBINS + b] > bank.W[static_cast<size_t> (k) * NBINS + best]) best = b;
        const double f0 = midiToHz (OPEN_MIDI[s]);
        PT_CHECK (std::fabs (best * BIN_HZ - f0) <= BIN_HZ, "string %d: peak at %.0f Hz, f0 %.0f", s, best * BIN_HZ, f0);
    }
    // the window of the bank is the 1024-sample analysis window
    PT_CHECK (bank.window.size() == 1024, "window length %zu", bank.window.size());
    double wsum = 0;
    for (double w : hannWindow (1024)) wsum += w;
    PT_CHECK (std::fabs (wsum - 2) < 1e-9, "window sum %.17g", wsum);

    // pitch columns: 44 pitches, the 120 string columns each listed once, noise in none
    int listed = 0;
    for (int j = 0; j < N_PITCH; j++)
        for (int i = 0; i < bank.pitchCount[j]; i++)
        {
            const int c = bank.pitchCols[j][i];
            PT_CHECK (bank.meta[static_cast<size_t> (c)].midi == PITCH_LO + j, "pitch %d lists column %d", j, c);
            if (i > 0) PT_CHECK (bank.pitchCols[j][i - 1] < c, "pitch %d columns not ascending", j);
            listed++;
        }
    PT_CHECK (listed == 120, "%d columns listed", listed);
    return finish ("test_poly_bank");
}
