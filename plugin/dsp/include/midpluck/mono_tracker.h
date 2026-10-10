// MidPluck MONO engine: a line-by-line C++ port of js/audio/guitar/tracker.js
// (GuitarTracker). The JavaScript is canonical (docs/plugin-plan.md, section 0):
// same evaluation order, double arithmetic everywhere, float32 storage exactly
// where the JS uses a Float32Array (rmsHist, hfHist, midiHist, ring, win, d,
// cmnd; rule 3.3-4), JS number semantics (Math.round, NaN-propagating
// Math.max/min, ToInt32; rule 3.3-9). Never "improve" the algorithm here: an
// improvement goes into the JS first, then into this port.
//
// Per sample:  DC block -> level (RMS) and high-band level (pick transients)
//              -> 2x low-pass 3 kHz -> /D (~24 kHz)
// Per frame (64 decimated samples ~ 2.7 ms): onset, YIN pitch, note state
// machine -> note-on / note-off / pitch-bend / meter events.
//
// Threading (plugin shell): process(), flush() and reset() never allocate and
// may run on the audio thread. The constructor allocates; setParams()
// allocates ONLY when fmin or fmax change (it rebuilds the YIN buffers, like
// _setupYin in the JS): call it with new fmin/fmax on the message thread while
// the audio thread is not inside process() (the v1 shell never changes them:
// they are not plugin parameters, plan 3.1). setParams() with the same
// fmin/fmax only rewrites a few doubles and is safe at the top of processBlock.
#pragma once

#include "midpluck/events.h"

#include <cstdint>
#include <vector>

namespace midpluck
{
    /** DEFAULT_PARAMS of tracker.js. */
    struct MonoParams
    {
        double sens = 0.5;       // 0..1 onset sensitivity: gate level and required rise
        double release = 0.5;    // 0..1 how far a note may decay before note-off (15..45 dB)
        double dyn = 0.7;        // 0..1 velocity dynamics (0 = every note at full velocity)
        bool bend = true;        // true: pitch bend follows bends/vibrato; false: chromatic
        double octave = 0;       // added to the detected note (x12; JS "| 0", so truncated to int)
        double transpose = 0;    // semitones added to the detected note (JS "| 0")
        double fmin = 70;        // Hz: drop D is 73.4; ~30 for a bass guitar
        double fmax = 1400;      // Hz: 24th fret of the high E is 1319
        double a4 = 440;         // Hz: tuning reference (must be > 0)
    };

    /**
     * TUNING of tracker.js: decision thresholds, same names, same defaults,
     * mutable at run time (the evaluation harness ablates them). Every field is
     * a double because every JS number is one (a non-integer override behaves
     * exactly as in the JS, e.g. HOLD = 6.5 loops while i < 6.5).
     */
    struct Tuning
    {
        double YIN_THRESHOLD = 0.15;   // first dip below this = the period
        double CONF_ON = 0.85;         // confidence (1 - dip depth) to start a note
        double CONF_TRACK = 0.7;       // confidence to keep tracking a sounding note
        double JUMP = 0.5;             // semitones within JUMP_SPAN frames = hammer-on, not a bend
        double JUMP_SPAN = 3;          // frames (~8 ms): faster than any finger can bend
        double HOLD = 6;               // frames (~16 ms) of peak-hold for the level
        double BEND_RANGE = 2;         // semitones (what the synth engines accept)
        double BEND_RETRIG = 2.35;     // beyond this the note is re-struck (slide)
        double REFRACTORY = 10;        // frames (~27 ms): one onset per pluck
        double PEND_MAX = 20;          // frames (~53 ms) to find a pitch after an onset
        double LOWCONF_MAX = 12;       // frames (~32 ms) without a period = string muted
        double SOFT_AGREE = 4;         // frames of stable pitch for an onset-less (swell) note
        double ONSET_AGREE = 3;        // frames agreeing on the pitch before a plucked note starts
        double LEGATO_AGREE = 4;       // frames out of the last LEGATO_WINDOW voting for the new note
        double LEGATO_WINDOW = 6;      //   before a hammer-on / pull-off / slide retriggers
        double OCTAVE_AGREE = 6;       // ... for an octave change without a pluck
        double GRID = 0.35;            // semitones: a note must sit this close to the grid to be named
        double SAME_NOTE_HOLDOFF = 19; // frames (~50 ms): a note cannot be re-picked right after it started
        double EARLY_FIX = 22;         // frames (~60 ms): a different stable pitch this soon after a pluck is corrected
        double HARMONIC_GUARD = 0;     // 1: also check 1.5x/3x the period (2x only by default)
    };

    /** Sets one Tuning field by its JS name ("ONSET_AGREE", ...). Returns false for an unknown name. */
    bool setTuningValue (Tuning& t, const char* key, double value) noexcept;

    /** The object tracker.js passes to its optional onFrame hook, once per frame. */
    struct MonoFrameTrace
    {
        long long k = 0;
        double rmsDb = 0, hfDb = 0, ref = 0, hfRef = 0, lvl = 0;
        bool onset = false, strong = false;
        double hz = 0, conf = 0, midiF = 0;
        int note = -1;            // raw sounding note (before octave/transpose) or -1
        bool pend = false;        // an onset is waiting for its pitch
    };

    using MonoTraceFn = void (*) (const MonoFrameTrace& frame, void* user);

    namespace mono
    {
        /** RBJ-cookbook biquad (low-pass or high-pass), transposed direct form II. Doubles throughout. */
        struct Biquad
        {
            double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
            double z1 = 0, z2 = 0;

            Biquad() = default;
            Biquad (bool lowPass, double sr, double fc, double q) noexcept;

            double run (double x) noexcept
            {
                const double y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }
        };

        struct YinResult
        {
            double hz = 0;
            double conf = 0;
        };
    }

    class MonoTracker
    {
    public:
        static constexpr int HOP = 64;            // analysis frame, decimated samples
        static constexpr int METER_EVERY = 8;     // frames between meter events (~21 ms)

        explicit MonoTracker (double sampleRate, const MonoParams& params = {});

        /** Changes the parameters (all of them: the C++ struct has no "undefined" field). */
        void setParams (const MonoParams& params);
        const MonoParams& params() const noexcept { return p_; }

        /** Decision thresholds; read on every frame, may be changed between process() calls. */
        Tuning tuning;

        /**
         * Feeds n mono samples and APPENDS the events they produced to out
         * (out is never cleared here). Each event's sampleOffset is the index,
         * in this block, of the input sample whose decimated sample completed
         * the analysis frame that decided it; frame is that frame's index k.
         */
        void process (const float* in, int n, EventList& out) noexcept;

        /** Releases a sounding note (when the input stops). Appends its events with sampleOffset 0. */
        void flush (EventList& out) noexcept;

        /**
         * Back to the state of a freshly constructed object with the same
         * parameters and tuning (plan 3.2): ring and YIN buffers zeroed, level
         * histories at -200 dB, pitch history at NaN, filters, DC blocker,
         * counters and note state cleared. No allocation, no event.
         */
        void reset() noexcept;

        /** Onset -> note-on time of the last note, in ms (lastLatMs in the JS). */
        double lastLatMs() const noexcept { return lastLatMs_; }
        double lastRmsDb() const noexcept { return lastRmsDb_; }

        /** Optional per-frame hook (onFrame in the JS), null by default. Called from process(). */
        void setTrace (MonoTraceFn fn, void* user) noexcept { traceFn_ = fn; traceUser_ = user; }

        // ---- read-only geometry (tests, editor)
        int decimation() const noexcept { return D_; }
        double decimatedRate() const noexcept { return srD_; }
        double frameMs() const noexcept { return frameMs_; }
        int tmin() const noexcept { return tmin_; }
        int tmax() const noexcept { return tmax_; }
        int window() const noexcept { return W_; }
        int ringSize() const noexcept { return static_cast<int> (ring_.size()); }
        long long frameCount() const noexcept { return frame_; }
        int soundingNote() const noexcept { return note_; }   // raw, before octave/transpose; -1 if none
        const mono::Biquad& lowPass1() const noexcept { return lp1_; }
        const mono::Biquad& lowPass2() const noexcept { return lp2_; }
        const mono::Biquad& highPass() const noexcept { return hp_; }

        /**
         * The period search of the JS (_yin) on the newest decimated samples,
         * exposed for the closed tests. It overwrites the YIN scratch buffers,
         * which the next frame rewrites anyway.
         */
        mono::YinResult yin (int tLo, int tHi, int W, int searchHi) noexcept;
        mono::YinResult yin (int tLo, int tHi, int W) noexcept { return yin (tLo, tHi, W, tHi); }

    private:
        enum class Why : std::uint8_t { None, Pluck, Legato, Swell, Fix };
        static const char* whyName (Why w) noexcept;

        struct Pending
        {
            bool active = false;
            long long k = 0;
            double peak = -200;
            int cand = -1;
            int agree = 0;
            bool strong = false;
        };

        void derive() noexcept;
        void setupYin();
        void frame (EventList& out) noexcept;
        double vel (double db) const noexcept;
        void noteOn (EventList& out, int m, double v, double peakDb, Why why) noexcept;
        void noteOff (EventList& out) noexcept;
        void bendTo (EventList& out, double dev) noexcept;
        mono::YinResult pitchAtOnset() noexcept;
        mono::YinResult track() noexcept;

        void emit (EventList& out, Event e) const noexcept;

        // ---- constants of the object (constructor)
        double sr_ = 48000;
        int D_ = 2;
        double srD_ = 24000;
        double frameMs_ = 0;
        MonoParams p_;

        // ---- derived from the parameters (_derive)
        double gateDb_ = 0, riseDb_ = 0, relDb_ = 0;
        int shift_ = 0;

        // ---- YIN geometry and buffers (_setupYin)
        int tmin_ = 2, tmax_ = 2, W_ = 512;
        std::vector<float> ring_;
        int mask_ = 0;
        int wi_ = 0;
        std::vector<float> win_, d_, cmnd_;

        // ---- per-sample front end
        mono::Biquad lp1_, lp2_, hp_;
        double dcX_ = 0, dcY_ = 0;
        int decPhase_ = 0;
        int since_ = 0;
        double sumSq_ = 0, sumHf_ = 0;
        int nSq_ = 0;

        // ---- frame state
        long long frame_ = 0;
        float rmsHist_[16];
        float hfHist_[16];
        long long lastOnset_ = -1000000000LL;
        double lastOnsetPeak_ = -200;
        int note_ = -1;
        double noteVel_ = 0;
        long long noteK_ = -1000000000LL;
        Why noteWhy_ = Why::None;
        double peakDb_ = -200;
        Pending pend_;
        int lowConf_ = 0, lowLevel_ = 0;
        int legCand_ = -1, legAgree_ = 0;
        int softCand_ = -1, softAgree_ = 0;
        bool armed_ = true;
        double bendOut_ = 0;
        double bendSmooth_ = 0;
        float midiHist_[8];
        double lastLatMs_ = 0;
        double lastRmsDb_ = -200;

        // ---- event stamping and tracing
        int curOffset_ = 0;
        MonoTraceFn traceFn_ = nullptr;
        void* traceUser_ = nullptr;
    };
}
