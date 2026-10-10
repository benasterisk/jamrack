// Port of js/audio/guitar/poly/notes.js (see midpluck/poly/notes.h).

#include "midpluck/poly/notes.h"
#include "midpluck/poly/v8math.h"
#include "midpluck/jsmath.h"

#include <cmath>
#include <limits>

namespace midpluck::poly
{
    namespace
    {
        constexpr int HARMONIC_INTERVALS[5] = { 12, 19, 24, 28, 31 };
        constexpr double INF = std::numeric_limits<double>::infinity();
    }

    NoteRule::NoteRule (const NoteRuleParams& params) noexcept : p (params)
    {
        for (int m = 0; m < N_PITCH; m++) { live[m] = false; orderList[m] = 0; cond[m] = 0; }
        reset();
    }

    void NoteRule::reset() noexcept
    {
        t = 0;
        for (double& l : levels) l = INF;
        for (double& x : Phist) x = 0;
        for (std::uint8_t& x : baseHist) x = 0;
        for (double& x : before) x = 0;
        for (std::uint8_t& x : fired) x = 0;
        lastOnset = -1e9;
        lastFlux = 0;
        onsetHop = -1e6;
        quietHops = 0;
        for (int m = 0; m < N_PITCH; m++) live[m] = false;
        nVoices = 0;
    }

    void NoteRule::addVoice (int m, const Voice& v) noexcept
    {
        // Map.set on an absent key: appended at the end of the iteration order
        voices[m] = v;
        live[m] = true;
        orderList[nVoices++] = m;
    }

    void NoteRule::removeVoice (int m) noexcept
    {
        if (! has (m)) return;
        live[m] = false;
        int i = 0;
        while (i < nVoices && orderList[i] != m) i++;
        for (; i + 1 < nVoices; i++) orderList[i] = orderList[i + 1];
        nVoices--;
    }

    void NoteRule::noteOff (int m, long long hop, RuleEventList& out) noexcept
    {
        removeVoice (m);
        RuleEvent e;
        e.on = false;
        e.midi = m + PITCH_LO;
        e.hop = hop;
        out.push (e);
    }

    void NoteRule::flush (RuleEventList& out, long long hop) noexcept
    {
        const long long h = hop > 0 ? hop : 0;     // Math.max(0, hop)
        while (nVoices > 0) noteOff (orderList[0], h, out);
    }

    void NoteRule::step (const double* P, double level, double flux, const double* odd, const double* low,
                         RuleEventList& out) noexcept
    {
        const long long tt = t;
        const double td = static_cast<double> (tt);

        // ---- level gate (see gateDb)
        const bool gated = level < p.gateDb;
        if (gated)
        {
            if (++quietHops >= p.silenceHops && nVoices > 0) flush (out, tt);
        }
        else
        {
            quietHops = 0;
        }

        // ---- onset detection
        levels[tt & 7] = level;
        double past = INF;
        for (int d = 3; d <= 6; d++)
            if (tt - d >= 0) past = js::min (past, levels[(tt - d) & 7]);
        const double rise = level - past;
        const bool raw = (flux >= p.fluxThr && rise >= p.riseDb) || flux >= 2 * p.fluxThr;
        // P of this hop into the ring before reading "4 hops ago"
        const int pRow = static_cast<int> (tt % P_RING) * N_PITCH;
        for (int m = 0; m < N_PITCH; m++) Phist[pRow + m] = P[m];
        if (raw && (td - lastOnset >= p.refractory || (td - lastOnset <= 4 && flux >= 1.5 * lastFlux)))
        {
            lastOnset = td;
            lastFlux = flux;
            onsetHop = td;
            for (std::uint8_t& f : fired) f = 0;
            const long long bh = tt - 4 > 0 ? tt - 4 : 0;
            const int bRow0 = static_cast<int> (bh % P_RING) * N_PITCH;
            for (int m = 0; m < N_PITCH; m++) before[m] = Phist[bRow0 + m];
        }

        // ---- candidate condition
        double S = 0;
        for (int m = 0; m < N_PITCH; m++) S += P[m];
        const int bRow = static_cast<int> (tt % B_RING) * N_PITCH;
        for (int m = 0; m < N_PITCH; m++) baseHist[bRow + m] = (P[m] > p.absOn && P[m] > p.frac * S) ? 1 : 0;
        const double since = td - onsetHop;
        const bool inWin = ! gated && onsetHop >= 0 && since <= p.windowHops && since >= p.minDelay;
        bool any = false;
        if (inWin)
        {
            const double riseFloor = p.absOn / 4;
            for (int m = 0; m < N_PITCH; m++)
            {
                std::uint8_t c = 1;
                for (int d = 0; d < p.agree && c; d++)
                {
                    if (tt - d < 0) { c = 0; break; }
                    c = baseHist[static_cast<int> ((tt - d) % B_RING) * N_PITCH + m];
                }
                if (c && p.lowGuard > 0 && ! (low[m] >= p.lowGuard)) c = 0;
                if (c && ! (P[m] >= p.riseX * js::max (before[m], riseFloor))) c = 0;
                if (c && ! (odd[m] >= p.octOdd)) c = 0;
                cond[m] = c;
                if (c) any = true;
            }
        }
        else
        {
            for (std::uint8_t& c : cond) c = 0;
        }

        // ---- note-off (before note-on, like the prototype)
        if (nVoices > 0)
        {
            const double offGain = std::pow (10.0, -p.releaseDb / 20);
            const double floor = p.absOn / 4;
            int i = 0;
            while (i < nVoices)
            {
                const int m = orderList[i];
                Voice& v = voices[m];
                const double x = P[m];
                if (x > v.peak) v.peak = x;
                bool removed = false;
                if (x < v.peak * offGain || x < floor)
                {
                    if (++v.below >= p.offHops) { noteOff (m, tt, out); removed = true; }
                }
                else
                {
                    v.below = 0;
                }
                if (! removed) i++;   // a deleted entry: the Map iterator moves on to the next one
            }
        }

        // ---- note-on
        if (any)
        {
            for (int m = 0; m < N_PITCH; m++)
            {
                if (! cond[m] || fired[m]) continue;
                const bool hasV = live[m];
                if (hasV && td - static_cast<double> (voices[m].on) < p.refractory) continue;
                if (m - 12 >= 0 && p.octUp > 0 && live[m - 12] && P[m] < p.octUp * P[m - 12]) continue;
                if (p.harmUp > 0)
                {
                    bool ghost = false;
                    for (int d : HARMONIC_INTERVALS)
                    {
                        const int q = m - d;
                        if (q >= 0 && (live[q] || cond[q]) && P[q] > p.harmUp * P[m]) { ghost = true; break; }
                    }
                    if (ghost) continue;
                }
                if (hasV) noteOff (m, tt, out);              // repick
                if (nVoices >= p.maxVoices)
                {
                    int weakest = -1;
                    double wval = INF;
                    for (int i = 0; i < nVoices; i++)
                    {
                        const int q = orderList[i];
                        if (P[q] < wval) { wval = P[q]; weakest = q; }
                    }
                    if (wval >= P[m]) continue;
                    noteOff (weakest, tt, out);
                }
                const double vel = js::min (1.0, js::max (0.1, (20 * v8math::log10 (js::max (P[m], 1e-9)) + 50) / 30));
                Voice nv;
                nv.on = tt;
                nv.peak = P[m];
                nv.below = 0;
                nv.vel = vel;
                addVoice (m, nv);
                fired[m] = 1;
                RuleEvent e;
                e.on = true;
                e.midi = m + PITCH_LO;
                e.vel = vel;
                e.hop = tt;
                out.push (e);
            }
        }
        t = tt + 1;
    }
}
