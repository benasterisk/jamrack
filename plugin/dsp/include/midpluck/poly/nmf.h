// Port of js/audio/guitar/poly/nmf.js: sparse beta-divergence NMF step of the
// POLY engine. Explains one magnitude spectrum v (1025 bins) as W h with the
// fixed template bank W, h >= 0 (beta 0.5, multiplicative updates, L1 penalty,
// warm start from the previous hop, active set).
//
// Per hop: salience W^T v; active set (sounding templates, strongest `active`,
// or completed by salience; noise templates always in); warm start floored at
// 1e-4 max(v); `iter` updates; pitch activations P[m]. Two storages of W:
// dense (exact prototype arithmetic) and sparse CSR (bins >= sparse x peak).
//
// Real-time port (rules 3.3-1/2/4): no allocation in step(); the three per-hop
// Array.from().sort() of the JS are preallocated index arrays sorted fully and
// stably (bottom-up merge sort into a fixed scratch buffer) by (value desc,
// index asc), exactly like V8's stable TimSort on an ascending index list; the
// ascending re-sort is kept (it fixes the summation order). W and its CSR copy
// stay float32, every sum is double.
#pragma once

#include "midpluck/poly/bank.h"

#include <cstdint>
#include <vector>

namespace midpluck::poly
{
    /** Decomposer options; defaults = the Decomposer constructor's (nmf.js). */
    struct DecomposerParams
    {
        double active = 60;    // active-set size
        double iter = 15;      // multiplicative updates per hop
        double lambda = 400;   // L1 penalty
        double initSal = 0;    // warm start from salience (0 = off)
        double sparse = 0;     // 0 = dense, else CSR threshold relative to each column's peak
    };

    /** DECOMPOSER of engine.js: the shipped setting (sparse 1e-3, 8 iterations). */
    constexpr DecomposerParams DECOMPOSER_SHIPPED { 60, 8, 400, 0, 1e-3 };

    class Decomposer
    {
    public:
        /** Allocates every buffer (and the CSR copy if o.sparse > 0). Keeps a reference to bank. */
        Decomposer (const Bank& bank, const DecomposerParams& o = {});

        /** One hop. v: magnitude spectrum (NBINS). Returns P (N_PITCH, reused). No allocation. */
        const double* step (const double* v) noexcept;

        void reset() noexcept;

        // Mutable at run time like the JS fields (ECO switches active / iter).
        double active, iter, lambda, initSal, sparse;

        int K;
        int nAct = 0;
        int nnz = 0;                         // CSR entries (sparse mode)

        const double* activations() const noexcept { return h.data(); }   // h, carried hop to hop
        const double* pitch() const noexcept { return P.data(); }

    private:
        void buildSparse (double tau);

        const Bank& bank;
        std::vector<int> noise;
        std::vector<double> h, sal, ha, y, g1, g2, P;
        std::vector<int> act, order, sortBuf, sortTmp;
        std::vector<std::uint8_t> inAct;
        std::vector<int> sPtr, sIdx;
        std::vector<float> sVal;
    };
}
