// Port of js/audio/guitar/poly/engine.js: POLY engine, polyphonic guitar ->
// note tracker.
//
// Per input sample: polyphase resampling to 24 kHz (resample.h).
// Per hop of 64 analysis samples (2.67 ms):
//   1. level: RMS of the hop, in dBFS
//   2. two newest-anchored Hann windows (1024 and 512 samples), 2048-point FFT
//   3. onset feature: log-spectral flux of the short window
//   4. octave guards from the medium spectrum (odd / even, low / high partials)
//   5. AGC on the spectrum, then the decomposer (nmf.h)
//   6. note rule -> note-on / note-off (notes.h)
//
// Events (midpluck/events.h): NoteOn / NoteOff carry the hop that decided them
// in `frame` and the input sample that completed that hop in `sampleOffset`;
// a Meter event every 8 hops (db, nmfMs, hopMs, voices, eco, gainDb).
//
// Real-time contract (docs/plugin-plan.md 3.3): the constructors allocate,
// process() / pushAnalysisRate() / flush() / reset() never do. The cost meter
// uses std::chrono::steady_clock like the JS uses performance.now(); the ECO
// load control that it drives can be switched off (autoEco = false) so that an
// offline run is deterministic.
#pragma once

#include "midpluck/events.h"
#include "midpluck/poly/bank.h"
#include "midpluck/poly/fft.h"
#include "midpluck/poly/nmf.h"
#include "midpluck/poly/notes.h"
#include "midpluck/poly/resample.h"

#include <vector>

namespace midpluck::poly
{
    /** ECO of engine.js: the decomposer setting used while the audio thread saturates. */
    struct EcoParams
    {
        double iter = 5;
        double active = 40;
    };

    /** Everything a PolyTracker is built with (POLY_DEFAULTS, DECOMPOSER, ECO, NOTE_RULE, options). */
    struct PolyParams
    {
        double octave = 0;              // POLY_DEFAULTS: added (x 12) to the detected note
        double transpose = 0;           // POLY_DEFAULTS: semitones added
        DecomposerParams decomposer = DECOMPOSER_SHIPPED;
        EcoParams eco {};
        NoteRuleParams rule {};
        bool agc = true;                // params.agc !== false
        bool autoEco = true;            // params.autoEco !== false (off for deterministic offline runs)
    };

    class PolyTracker
    {
    public:
        /** Allocates every buffer; keeps a reference to `bank` (built once, shared). */
        PolyTracker (double sampleRate, const Bank& bank, const PolyParams& params = {});

        /** Feeds a block at the input rate; appends the events to out. No allocation. */
        void process (const float* input, int n, EventList& out) noexcept;

        /** Feeds samples already at the analysis rate (tests, offline harnesses). */
        void pushAnalysisRate (const double* samples, int n, EventList& out) noexcept;

        /** Releases every sounding note (input stopped). */
        void flush (EventList& out) noexcept;

        /** Forgets the past exactly like engine.js reset(): ring, hop counter, flux
         *  history, activations, voices. Like the JS, it keeps the resampler state,
         *  the AGC envelope, the ECO state and the cost averages. */
        void reset() noexcept;

        /** setParams({ octave, transpose }): shift = 12 * (octave | 0) + (transpose | 0). */
        void setParams (double octave, double transpose) noexcept;

        /** New host rate (plan 3.1, prepareToPlay): rebuilds only the resampler, the
         *  bank and every other state are kept. ALLOCATES: never on the audio thread.
         *  Not in the JS (a page builds a new tracker per AudioContext). */
        void setSampleRate (double sampleRate);

        /** Delay added by the resampler, in seconds (0.42 ms at the usual rates). */
        double delaySeconds() const noexcept { return rs.delaySeconds(); }

        void setEco (bool on) noexcept;
        bool eco() const noexcept { return ecoOn; }

        /** Hop-cost budget of the ECO control, in ms. Default 1000 * HOP / SR (2.667 ms, as the JS). */
        void setBudgetMs (double ms) noexcept { budget = ms; }
        double budgetMs() const noexcept { return budget; }

        /** Rule 3.3-6: the budget for a host block size (hops that may complete in one callback). */
        static double ecoBudgetMs (double sampleRate, int blockSize) noexcept;

        // Cost meter (ms): exponential averages as in the JS, plus the raw last values.
        double nmfMs = 0, hopMs = 0, maxHopMs = 0;
        double lastHopMs = 0, lastNmfMs = 0;
        double envDb, gainDb = 0;
        long long hop = 0;
        bool agc, autoEco;
        int shift = 0;

        Decomposer decomposer;
        NoteRule rule;

    private:
        void sample (double s, int offset, EventList& out) noexcept;
        void doHop (int offset, EventList& out) noexcept;
        void loadControl() noexcept;
        void emit (const RuleEventList& raw, int offset, EventList& out) noexcept;

        double sr;
        Resampler rs;
        const Bank& bank;
        RealFFT fft;
        EcoParams ecoParams;
        double fullIter, fullActive;
        double budget;
        bool ecoOn = false;
        int ecoCount = 0;

        std::vector<double> winM, winS, ring, frameM, frameS, V, Vs, dbHist, odd, low;
        int nM, nS;
        int wi = 0, since = 0;
        double sumSq = 0;
        double pk[6] = {};
        int pbin[N_PITCH * 6] = {};
        int sent[N_PITCH];                // raw pitch -> midi actually sent (-1 = none)
        RuleEventList raw;
    };

    /**
     * The whole POLY engine as the plugin shell holds it (rule 3.3-5): bank,
     * decomposer (CSR built), note rule and tracker, built entirely in the
     * constructor (allocates, ~tens of ms: on a background thread), then used
     * on the audio thread without allocating. Not copyable, not movable.
     */
    struct PolyEngineState
    {
        explicit PolyEngineState (double sampleRate, const PolyParams& params = {});
        PolyEngineState (const PolyEngineState&) = delete;
        PolyEngineState& operator= (const PolyEngineState&) = delete;

        /** Input-rate block; sampleOffset = the input sample that completed the hop. */
        void process (const float* in, int n, EventList& out) noexcept { tracker.process (in, n, out); }
        void pushAnalysisRate (const double* s, int n, EventList& out) noexcept { tracker.pushAnalysisRate (s, n, out); }
        void flush (EventList& out) noexcept { tracker.flush (out); }
        void reset() noexcept { tracker.reset(); }
        void setParams (int octave, int transpose) noexcept { tracker.setParams (octave, transpose); }
        /** prepareToPlay: new host rate, the bank is kept. Allocates (not on the audio thread). */
        void setSampleRate (double sampleRate) { tracker.setSampleRate (sampleRate); }
        void setBudgetMs (double ms) noexcept { tracker.setBudgetMs (ms); }
        bool eco() const noexcept { return tracker.eco(); }

        static double delaySeconds (double sampleRate) noexcept;
        static double ecoBudgetMs (double sampleRate, int blockSize) noexcept { return PolyTracker::ecoBudgetMs (sampleRate, blockSize); }

        const Bank bank;
        PolyTracker tracker;
    };
}
