// Port of js/audio/guitar/poly/nmf.js (see midpluck/poly/nmf.h).

#include "midpluck/poly/nmf.h"
#include "midpluck/jsmath.h"

#include <algorithm>
#include <cmath>

namespace midpluck::poly
{
    namespace
    {
        constexpr double H_FLOOR = 1e-6;
        constexpr double EPS = 1e-9;

        /** Stable sort of a[0..n) by key[a[i]] descending, ties in input order:
         *  Array.prototype.sort((a, b) => key[b] - key[a]) of V8 (stable TimSort)
         *  for finite keys. Bottom-up merge sort into the fixed buffer tmp (>= n);
         *  no allocation, O(n log n) on <= 122 entries. */
        void stableSortDesc (int* a, int n, const double* key, int* tmp) noexcept
        {
            int* src = a;
            int* dst = tmp;
            for (int width = 1; width < n; width *= 2)
            {
                for (int lo = 0; lo < n; lo += 2 * width)
                {
                    const int mid = std::min (lo + width, n), hi = std::min (lo + 2 * width, n);
                    int i = lo, j = mid, k = lo;
                    while (i < mid && j < hi)
                    {
                        // the right element goes first only if strictly greater (stability)
                        if (key[src[j]] > key[src[i]]) dst[k++] = src[j++];
                        else dst[k++] = src[i++];
                    }
                    while (i < mid) dst[k++] = src[i++];
                    while (j < hi) dst[k++] = src[j++];
                }
                std::swap (src, dst);
            }
            if (src != a) std::copy (src, src + n, a);
        }
    }

    Decomposer::Decomposer (const Bank& b, const DecomposerParams& o)
        : active (o.active), iter (o.iter), lambda (o.lambda), initSal (o.initSal), sparse (o.sparse),
          K (b.K), bank (b)
    {
        for (int i = 0; i < K; i++)
            if (bank.meta[static_cast<size_t> (i)].midi < 0) noise.push_back (i);
        const size_t k = static_cast<size_t> (K);
        h.assign (k, 0.0);           // activations carried from hop to hop
        sal.assign (k, 0.0);
        ha.assign (k, 0.0);
        act.assign (k, 0);
        order.assign (k, 0);
        sortBuf.assign (k, 0);
        sortTmp.assign (k, 0);
        inAct.assign (k, 0);
        y.assign (NBINS, 0.0);
        g1.assign (NBINS, 0.0);      // v * y^-1.5
        g2.assign (NBINS, 0.0);      // y^-0.5
        P.assign (N_PITCH, 0.0);
        if (sparse > 0) buildSparse (sparse);
    }

    void Decomposer::buildSparse (double tau)
    {
        const float* W = bank.W.data();
        sPtr.assign (static_cast<size_t> (K) + 1, 0);
        sIdx.clear();
        sVal.clear();
        for (int k = 0; k < K; k++)
        {
            double mx = 0;
            for (int b = 0; b < NBINS; b++)
                if (W[k * NBINS + b] > mx) mx = W[k * NBINS + b];
            const double thr = tau * mx;
            for (int b = 0; b < NBINS; b++)
            {
                const double w = W[k * NBINS + b];
                if (w >= thr) { sIdx.push_back (b); sVal.push_back (W[k * NBINS + b]); }
            }
            sPtr[static_cast<size_t> (k) + 1] = static_cast<int> (sIdx.size());
        }
        nnz = static_cast<int> (sIdx.size());
    }

    void Decomposer::reset() noexcept
    {
        std::fill (h.begin(), h.end(), 0.0);
    }

    const double* Decomposer::step (const double* v) noexcept
    {
        double* const hp = h.data();
        double* const salp = sal.data();
        double* const hap = ha.data();
        int* const actp = act.data();
        double* const yp = y.data();
        double* const g1p = g1.data();
        double* const g2p = g2.data();
        const double lam = lambda;
        const bool useSparse = sparse > 0 && ! sPtr.empty();
        const float* const W = bank.W.data();
        const int* const ptr = sPtr.data();
        const int* const sidx = sIdx.data();
        const float* const sval = sVal.data();

        // 1. salience and the frame maximum
        double vmax = 0;
        for (int b = 0; b < NBINS; b++) if (v[b] > vmax) vmax = v[b];
        if (useSparse)
        {
            for (int k = 0; k < K; k++)
            {
                double s = 0;
                for (int i = ptr[k], e = ptr[k + 1]; i < e; i++) s += static_cast<double> (sval[i]) * v[sidx[i]];
                salp[k] = s;
            }
        }
        else
        {
            for (int k = 0; k < K; k++)
            {
                double s = 0;
                const float* col = W + static_cast<size_t> (k) * NBINS;
                for (int b = 0; b < NBINS; b++) s += static_cast<double> (col[b]) * v[b];
                salp[k] = s;
            }
        }

        // 2. active set
        const double nmax = js::min (active, static_cast<double> (K));
        std::uint8_t* const in = inAct.data();
        std::fill (inAct.begin(), inAct.end(), static_cast<std::uint8_t> (0));
        int n = 0;
        if (nmax >= K)
        {
            for (int k = 0; k < K; k++) actp[n++] = k;
        }
        else
        {
            int* const ord = order.data();
            int ns = 0;
            for (int k = 0; k < K; k++) if (hp[k] > H_FLOOR) ord[ns++] = k;
            if (ns >= nmax)
            {
                // the strongest nmax of the sounding templates
                int* const sub = sortBuf.data();
                std::copy (ord, ord + ns, sub);
                stableSortDesc (sub, ns, hp, sortTmp.data());
                for (int i = 0; i < nmax; i++) { actp[n++] = sub[i]; in[sub[i]] = 1; }
            }
            else
            {
                for (int i = 0; i < ns; i++) { actp[n++] = ord[i]; in[ord[i]] = 1; }
                // fill with the most salient of the rest
                int* const rest = sortBuf.data();
                int nr = 0;
                for (int k = 0; k < K; k++) if (! in[k]) rest[nr++] = k;
                stableSortDesc (rest, nr, salp, sortTmp.data());
                const double need = nmax - ns;
                for (int i = 0; i < need; i++) { actp[n++] = rest[i]; in[rest[i]] = 1; }
            }
            for (int k : noise) if (! in[k]) { actp[n++] = k; in[k] = 1; }
            // ascending column order, like np.union1d (summation order only): act
            // holds each marked column exactly once, so the ascending sort of act
            // is the ascending scan of the marks
            int m = 0;
            for (int k = 0; k < K; k++) if (in[k]) actp[m++] = k;
        }
        nAct = n;

        // 3. warm start
        const double floor = 1e-4 * js::max (vmax, 1e-6);
        for (int i = 0; i < n; i++)
        {
            const int k = actp[i];
            double start = js::max (hp[k], floor);
            if (initSal > 0) start = js::max (start, initSal * salp[k]);
            hap[i] = start;
        }

        // 4. multiplicative updates on the active columns
        for (int it = 0; it < iter; it++)
        {
            std::fill (y.begin(), y.end(), EPS);
            if (useSparse)
            {
                for (int i = 0; i < n; i++)
                {
                    const double a = hap[i];
                    if (a == 0) continue;
                    const int k = actp[i];
                    for (int j = ptr[k], e = ptr[k + 1]; j < e; j++) yp[sidx[j]] += a * static_cast<double> (sval[j]);
                }
            }
            else
            {
                for (int i = 0; i < n; i++)
                {
                    const double a = hap[i];
                    if (a == 0) continue;
                    const float* col = W + static_cast<size_t> (actp[i]) * NBINS;
                    for (int b = 0; b < NBINS; b++) yp[b] += a * static_cast<double> (col[b]);
                }
            }
            for (int b = 0; b < NBINS; b++)
            {
                const double r = 1 / std::sqrt (yp[b]);   // y^-0.5
                g2p[b] = r;
                g1p[b] = v[b] * r / yp[b];                 // v * y^-1.5
            }
            if (useSparse)
            {
                for (int i = 0; i < n; i++)
                {
                    const int k = actp[i];
                    double num = 0, den = 0;
                    for (int j = ptr[k], e = ptr[k + 1]; j < e; j++)
                    {
                        const double w = sval[j];
                        const int b = sidx[j];
                        num += w * g1p[b];
                        den += w * g2p[b];
                    }
                    hap[i] *= num / (den + lam);
                }
            }
            else
            {
                for (int i = 0; i < n; i++)
                {
                    const float* col = W + static_cast<size_t> (actp[i]) * NBINS;
                    double num = 0, den = 0;
                    for (int b = 0; b < NBINS; b++)
                    {
                        const double w = col[b];
                        num += w * g1p[b];
                        den += w * g2p[b];
                    }
                    hap[i] *= num / (den + lam);
                }
            }
        }

        // 5. write back, pitch activations
        std::fill (h.begin(), h.end(), 0.0);
        for (int i = 0; i < n; i++) hp[actp[i]] = hap[i];
        double* const Pp = P.data();
        for (int j = 0; j < N_PITCH; j++)
        {
            double s = 0;
            const int* c = bank.pitchCols[j];
            for (int i = 0, e = bank.pitchCount[j]; i < e; i++) s += hp[c[i]];
            Pp[j] = s;
        }
        return Pp;
    }
}
