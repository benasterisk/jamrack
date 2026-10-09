// Port of js/audio/guitar/poly/engine.js (see midpluck/poly/engine.h).

#include "midpluck/poly/engine.h"
#include "midpluck/poly/v8math.h"
#include "midpluck/jsmath.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace midpluck::poly
{
    namespace
    {
        // Load control: the hop cost (exponential average) against the hop budget.
        constexpr double ECO_IN = 0.80, ECO_OUT = 0.45;
        constexpr int ECO_HOLD = 375;                       // 375 hops = 1 s

        // Input-level normalisation of the spectrum fed to the decomposer.
        constexpr double AGC_REF_DB = -17;
        constexpr double AGC_MAX_DB = 30;
        constexpr double AGC_DECAY_DB_PER_HOP = 6.0 / 375;  // the envelope falls 6 dB per second
        constexpr double AGC_FLOOR_DB = -70;                // below this nothing drives the envelope

        constexpr int FLUX_LAG = 3;
        constexpr int FLUX_LO = 4, FLUX_HI = 427;           // bins: 47 Hz .. 5 kHz
        constexpr int FLUX_BINS = FLUX_HI - FLUX_LO;
        constexpr int METER_EVERY = 8;
        constexpr double EPS = 1e-9;
        constexpr int RING = 2048;                          // >= medium window

        using Clock = std::chrono::steady_clock;
        inline double msBetween (Clock::time_point a, Clock::time_point b) noexcept
        {
            return std::chrono::duration<double, std::milli> (b - a).count();
        }
    }

    PolyTracker::PolyTracker (double sampleRate, const Bank& b, const PolyParams& params)
        : envDb (AGC_FLOOR_DB),
          agc (params.agc),
          autoEco (params.autoEco),
          decomposer (b, params.decomposer),
          rule (params.rule),
          sr (sampleRate),
          rs (sampleRate, SR),
          bank (b),
          fft (NFFT),
          ecoParams (params.eco),
          fullIter (params.decomposer.iter),
          fullActive (params.decomposer.active),
          budget (1000.0 * HOP / SR)
    {
        winM = bank.window;
        winS = hannWindow (WINDOW_SHORT);
        nM = WINDOW_MEDIUM;
        nS = WINDOW_SHORT;
        ring.assign (RING, 0.0);
        frameM.assign (static_cast<size_t> (nM), 0.0);
        frameS.assign (static_cast<size_t> (nS), 0.0);
        V.assign (NBINS, 0.0);
        Vs.assign (NBINS, 0.0);
        dbHist.assign (static_cast<size_t> (FLUX_LAG + 1) * FLUX_BINS, 0.0);
        odd.assign (N_PITCH, 0.0);
        low.assign (N_PITCH, 0.0);
        // bins of partials 1..6 of every pitch (centre of the +-1-bin max)
        for (int j = 0; j < N_PITCH; j++)
        {
            const double f0 = midiToHz (PITCH_LO + j);
            for (int n = 1; n <= 6; n++)
            {
                const double r = js::round (n * f0 / BIN_HZ) - 1;
                pbin[j * 6 + n - 1] = static_cast<int> (js::min (NBINS - 3, js::max (0, r))) + 1;
            }
        }
        for (int& s : sent) s = -1;
        setParams (params.octave, params.transpose);
    }

    void PolyTracker::setParams (double octave, double transpose) noexcept
    {
        shift = 12 * js::toInt32 (octave) + js::toInt32 (transpose);
    }

    void PolyTracker::setSampleRate (double sampleRate)
    {
        sr = sampleRate;
        rs = Resampler (sampleRate, SR);
    }

    double PolyTracker::ecoBudgetMs (double sampleRate, int blockSize) noexcept
    {
        const double hopIn = static_cast<double> (HOP) * sampleRate / SR;
        const double hopsMax = std::ceil (blockSize / hopIn);
        return 1000.0 * blockSize / sampleRate / (hopsMax > 0 ? hopsMax : 1);
    }

    void PolyTracker::process (const float* input, int n, EventList& out) noexcept
    {
        for (int i = 0; i < n; i++)
            rs.pushSample (static_cast<double> (input[i]), [this, i, &out] (double y) { sample (y, i, out); });
    }

    void PolyTracker::pushAnalysisRate (const double* samples, int n, EventList& out) noexcept
    {
        for (int i = 0; i < n; i++) sample (samples[i], i, out);
    }

    void PolyTracker::flush (EventList& out) noexcept
    {
        raw.clear();
        rule.flush (raw, hop - 1);
        emit (raw, 0, out);
    }

    void PolyTracker::reset() noexcept
    {
        std::fill (ring.begin(), ring.end(), 0.0);
        wi = 0; since = 0; sumSq = 0; hop = 0;
        std::fill (dbHist.begin(), dbHist.end(), 0.0);
        decomposer.reset();
        rule.reset();
    }

    void PolyTracker::sample (double s, int offset, EventList& out) noexcept
    {
        ring[static_cast<size_t> (wi)] = s;
        wi = (wi + 1) & (RING - 1);
        sumSq += s * s;
        if (++since >= HOP)
        {
            since = 0;
            doHop (offset, out);
        }
    }

    void PolyTracker::doHop (int offset, EventList& out) noexcept
    {
        const Clock::time_point t0 = Clock::now();
        const long long t = hop;
        const double level = 20 * v8math::log10 (std::sqrt (sumSq / HOP) + EPS);
        sumSq = 0;

        // windows anchored on the newest sample
        const double* rg = ring.data();
        const int mask = RING - 1;
        double* fM = frameM.data();
        const double* wM = winM.data();
        for (int i = 0; i < nM; i++) fM[i] = rg[(wi - nM + i) & mask] * wM[i];
        fft.magnitude (fM, nM, V.data());
        double* fS = frameS.data();
        const double* wS = winS.data();
        for (int i = 0; i < nS; i++) fS[i] = rg[(wi - nS + i) & mask] * wS[i];
        fft.magnitude (fS, nS, Vs.data());

        // spectral flux of the short window
        const int row = static_cast<int> (t % (FLUX_LAG + 1)) * FLUX_BINS;
        const int old = static_cast<int> ((t + 1) % (FLUX_LAG + 1)) * FLUX_BINS;   // the row of hop t - 3
        double* db = dbHist.data();
        const double* vs = Vs.data();
        double flux = 0;
        for (int b = 0; b < FLUX_BINS; b++)
        {
            const double d = 20 * v8math::log10 (js::max (vs[FLUX_LO + b], 1e-4));
            if (t >= FLUX_LAG)
            {
                const double r = d - db[old + b];
                flux += r <= 0 ? 0 : (r >= 20 ? 20 : r);
            }
            db[row + b] = d;
        }
        flux = t >= FLUX_LAG ? flux / FLUX_BINS : 0;

        // partial ratios (sub-octave and sub-harmonic guards)
        double* v = V.data();
        for (int j = 0; j < N_PITCH; j++)
        {
            for (int n = 0; n < 6; n++)
            {
                const int c = pbin[j * 6 + n];
                const double a = v[c - 1], b2 = v[c], c2 = v[c + 1];
                pk[n] = a > b2 ? (a > c2 ? a : c2) : (b2 > c2 ? b2 : c2);
            }
            odd[static_cast<size_t> (j)] = (pk[0] + pk[2] + pk[4]) / (pk[1] + pk[3] + pk[5] + EPS);
            low[static_cast<size_t> (j)] = (pk[0] + pk[1]) / (pk[2] + pk[3] + pk[4] + pk[5] + EPS);
        }

        // input-level normalisation (see AGC_REF_DB)
        if (agc)
        {
            envDb = js::max (js::max (level, envDb - AGC_DECAY_DB_PER_HOP), AGC_FLOOR_DB);
            gainDb = js::min (AGC_MAX_DB, js::max (0.0, AGC_REF_DB - envDb));
            if (gainDb > 0)
            {
                const double g = std::pow (10.0, gainDb / 20);
                for (int b = 0; b < NBINS; b++) v[b] *= g;
            }
        }

        // decomposer + note rule
        const Clock::time_point t1 = Clock::now();
        const double* P = decomposer.step (v);
        const Clock::time_point t2 = Clock::now();
        raw.clear();
        rule.step (P, level, flux, odd.data(), low.data(), raw);
        emit (raw, offset, out);

        const double hMs = msBetween (t0, Clock::now()), nMs = msBetween (t1, t2);
        lastHopMs = hMs;
        lastNmfMs = nMs;
        nmfMs += (nMs - nmfMs) * 0.05;
        hopMs += (hMs - hopMs) * 0.05;
        if (hMs > maxHopMs) maxHopMs = hMs;
        if (autoEco) loadControl();
        if (t % METER_EVERY == 0)
        {
            Event e;
            e.type = EventType::Meter;
            e.sampleOffset = offset;
            e.frame = t;
            e.db = level;
            e.nmfMs = nmfMs;
            e.hopMs = hopMs;
            e.voices = rule.voiceCount();
            e.eco = ecoOn;
            e.gainDb = gainDb;
            out.push (e);
        }
        hop = t + 1;
    }

    void PolyTracker::loadControl() noexcept
    {
        const double ratio = hopMs / budget;
        if (! ecoOn && ratio > ECO_IN)
        {
            if (++ecoCount >= ECO_HOLD) setEco (true);
        }
        else if (ecoOn && ratio < ECO_OUT)
        {
            if (++ecoCount >= ECO_HOLD) setEco (false);
        }
        else
        {
            ecoCount = 0;
        }
    }

    void PolyTracker::setEco (bool on) noexcept
    {
        ecoOn = on;
        ecoCount = 0;
        decomposer.iter = on ? ecoParams.iter : fullIter;
        decomposer.active = on ? ecoParams.active : fullActive;
    }

    // The octave / transpose shift is applied here. A note-off must carry the
    // same midi as its note-on even if the shift changed in between, or the
    // host would keep the first note sounding forever: remember what was sent.
    void PolyTracker::emit (const RuleEventList& rl, int offset, EventList& out) noexcept
    {
        for (int i = 0; i < rl.count; i++)
        {
            const RuleEvent& r = rl.items[i];
            const int slot = r.midi - PITCH_LO;
            const bool inRange = slot >= 0 && slot < N_PITCH;
            if (r.on)
            {
                const int midi = r.midi + shift;
                if (midi < 0 || midi > 127) continue;
                if (inRange) sent[slot] = midi;
                Event e;
                e.type = EventType::NoteOn;
                e.sampleOffset = offset;
                e.midi = midi;
                e.vel = r.vel;
                e.why = "poly";
                e.frame = r.hop;
                out.push (e);
            }
            else
            {
                if (! inRange || sent[slot] < 0) continue;     // _sent.get() undefined
                const int midi = sent[slot];
                sent[slot] = -1;
                Event e;
                e.type = EventType::NoteOff;
                e.sampleOffset = offset;
                e.midi = midi;
                e.frame = r.hop;
                out.push (e);
            }
        }
    }

    PolyEngineState::PolyEngineState (double sampleRate, const PolyParams& params)
        : bank (buildBank (BankWindow::Medium, genericProfile())),
          tracker (sampleRate, bank, params)
    {
    }

    double PolyEngineState::delaySeconds (double sampleRate) noexcept
    {
        return resamplerDelaySeconds (sampleRate, SR);
    }
}
