// test/poly-engine.test.mjs, test 4: "decomposer: a single template spectrum
// activates its own pitch, and almost nothing else" (dense, the Decomposer
// defaults), plus the same with the shipped sparse setting.
#include "poly_test_util.h"
#include "midpluck/poly/bank.h"
#include "midpluck/poly/nmf.h"

#include <vector>

int main()
{
    using namespace polytest;
    using namespace midpluck::poly;

    const Bank bank = buildBank();
    for (int pass = 0; pass < 2; pass++)
    {
        const DecomposerParams params = pass == 0 ? DecomposerParams {} : DECOMPOSER_SHIPPED;
        Decomposer dec (bank, params);
        const int k = 2 * 20 + 5;                 // D string, fret 5 = G3 (midi 55)
        std::vector<double> v (NBINS);
        for (int b = 0; b < NBINS; b++) v[static_cast<size_t> (b)] = 0.05 * bank.W[static_cast<size_t> (k) * NBINS + b];
        const double* P = nullptr;
        for (int i = 0; i < 6; i++) P = dec.step (v.data());   // warm start settles in a few hops
        const int j = 55 - PITCH_LO;
        double total = 0;
        for (int m = 0; m < N_PITCH; m++) total += P[m];
        PT_CHECK (P[j] > 0.8 * total, "%s: G3 holds %.0f %% of the activation", pass ? "sparse" : "dense", 100 * P[j] / total);
        PT_CHECK (dec.nAct >= 60 && dec.nAct <= 62, "%s: active set %d", pass ? "sparse" : "dense", dec.nAct);
        if (pass == 1) PT_CHECK (dec.nnz > 0 && dec.nnz < bank.K * NBINS / 2, "CSR keeps %d of %d entries", dec.nnz, bank.K * NBINS);
    }
    return finish ("test_poly_nmf");
}
