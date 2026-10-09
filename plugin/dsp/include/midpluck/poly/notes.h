// Port of js/audio/guitar/poly/notes.js: per-hop note rule of the POLY engine.
// Turns the pitch activations of the decomposer, the level and the spectral
// flux into note-on / note-off decisions (causal port of test/poly/notes.py
// with the thresholds of test/poly/params-merged.json "best").
//
// Real-time port (rule 3.3-3): the JS Map of voices becomes a fixed table of
// N_PITCH slots plus an insertion-ordered list (Map iteration order, which the
// weakest-voice search and flush depend on through ties); events go to a
// fixed-capacity list. No allocation in step() / flush().
#pragma once

#include "midpluck/poly/bank.h"

#include <cstdint>

namespace midpluck::poly
{
    /** NOTE_RULE of notes.js (params-merged.json "best"); every field mutable at run time. */
    struct NoteRuleParams
    {
        double absOn = 0.008;
        double frac = 0.12;
        double riseX = 1.2;
        double fluxThr = 2.0;
        double riseDb = 6.0;
        double releaseDb = 20.0;
        double offHops = 9;
        double windowHops = 15;
        double refractory = 10;
        double octOdd = 0.25;
        double octUp = 0.5;
        double maxVoices = 6;
        double agree = 2;
        double minDelay = 0;
        double lowGuard = 0.0;
        double harmUp = 1.0;
        // Not in the prototype: no note-on while the hop level is under gateDb,
        // and every voice is released after silenceHops hops under it.
        double gateDb = -60;
        double silenceHops = 190;   // ~0.5 s
    };

    /** A decision of the rule: { t: 'on', midi, vel, why: 'poly', hop } or { t: 'off', midi, hop }. */
    struct RuleEvent
    {
        bool on = false;
        int midi = 0;           // raw pitch (m + PITCH_LO), before the octave / transpose shift
        double vel = 0;
        long long hop = 0;
    };

    struct RuleEventList
    {
        // per hop at most: N_PITCH silence offs + N_PITCH decay offs + 3 x N_PITCH
        // note-on path events (repick off, weakest off, on)
        static constexpr int capacity = 5 * N_PITCH + 8;
        RuleEvent items[capacity];
        int count = 0;
        int dropped = 0;
        void clear() noexcept { count = 0; dropped = 0; }
        void push (const RuleEvent& e) noexcept
        {
            if (count < capacity) items[count++] = e;
            else ++dropped;
        }
    };

    class NoteRule
    {
    public:
        explicit NoteRule (const NoteRuleParams& params = {}) noexcept;

        void reset() noexcept;

        /** One hop. P: pitch activations (N_PITCH), level in dBFS, flux, odd and
         *  low partial ratios per pitch. Appends to out. */
        void step (const double* P, double level, double flux, const double* odd, const double* low,
                   RuleEventList& out) noexcept;

        /** Releases every voice at hop `hop` (Math.max(0, hop)), in insertion order. */
        void flush (RuleEventList& out, long long hop) noexcept;
        void flush (RuleEventList& out) noexcept { flush (out, t - 1); }

        int voiceCount() const noexcept { return nVoices; }

        NoteRuleParams p;
        long long t = 0;

    private:
        struct Voice { long long on = 0; double peak = 0; int below = 0; double vel = 0; };

        bool has (int m) const noexcept { return m >= 0 && m < N_PITCH && live[m]; }
        void addVoice (int m, const Voice& v) noexcept;
        void removeVoice (int m) noexcept;
        void noteOff (int m, long long hop, RuleEventList& out) noexcept;

        static constexpr int P_RING = 8;   // hops of P kept for the "before the onset" reference
        static constexpr int B_RING = 4;   // hops of the base condition kept for `agree`

        double levels[8];
        double Phist[P_RING * N_PITCH];
        std::uint8_t baseHist[B_RING * N_PITCH];
        double before[N_PITCH];
        std::uint8_t fired[N_PITCH];
        std::uint8_t cond[N_PITCH];
        double lastOnset = -1e9;
        double lastFlux = 0;
        double onsetHop = -1e6;
        long long quietHops = 0;

        // Map pitch index -> voice, insertion-ordered
        Voice voices[N_PITCH];
        bool live[N_PITCH];
        int orderList[N_PITCH];
        int nVoices = 0;
    };
}
