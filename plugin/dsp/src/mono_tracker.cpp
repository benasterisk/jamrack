// MidPluck MONO engine: line-by-line port of js/audio/guitar/tracker.js.
// See mono_tracker.h for the rules this file follows; the comments below are
// the JS comments, kept next to the lines they explain so the two files can be
// read side by side.
//
// Transcendental functions: V8's own algorithms, so that the port is bit for
// bit the JS (measured on this PC against Node 25.1 / V8 14.1, 8 October 2026):
//   Math.log10 (dB levels)        v8math::log10   std::log10 differs by 1 ulp on
//                                                 ~6 % of the frame levels
//   Math.cos / Math.sin (biquads) v8math::cos/sin std:: differs on 1 of the 28
//                                                 coefficient arguments of 7 rates
//   Math.log2 (midiF)             log2V8 below    std::log2 differs on 0.6 % of
//                                                 2.4 M inputs, 0 for this port
//   Math.pow (midiToHz)           std::pow        0 differences on m = -20..160
// With std:: everywhere the notes were already the same; the ulps showed in
// velocities (107 of 3 361 GuitarSet notes, <= 3.3e-16) and pitch bends.
//
// log2V8: fdlibm (FreeBSD e_log2.c + k_log.h) as V8 compiles it (src/base/ieee754.cc).
// fdlibm: Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
// Developed at SunSoft, a Sun Microsystems, Inc. business. Permission to use,
// copy, modify, and distribute this software is freely granted, provided that
// this notice is preserved.
#include "midpluck/mono_tracker.h"
#include "midpluck/jsmath.h"
#include "midpluck/poly/v8math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace midpluck
{
    namespace
    {
        constexpr double kPi = 3.141592653589793;     // Math.PI
        constexpr double VEL_LO_DB = -48, VEL_HI_DB = -12;   // RMS dBFS -> velocity 0..1
        constexpr long long kFar = -1000000000LL;     // -1e9 in the JS

        // Math.log2 (V8 ieee754::log2): verified bit-identical on 2.4 M inputs, the
        // 18 459 Math.log2 arguments of two GuitarSet takes included.
        double log2V8 (double x) noexcept
        {
            constexpr double two54 = 1.80143985094819840000e+16,     // 0x43500000, 0x00000000
                             ivln2hi = 1.44269504072144627571e+00,   // 0x3ff71547, 0x65200000
                             ivln2lo = 1.67517131648865118353e-10;   // 0x3de705fc, 0x2eefa200
            constexpr double Lg1 = 6.666666666666735130e-01, Lg2 = 3.999999999940941908e-01,
                             Lg3 = 2.857142874366239149e-01, Lg4 = 2.222219843214978396e-01,
                             Lg5 = 1.818357216161805012e-01, Lg6 = 1.531383769920937332e-01,
                             Lg7 = 1.479819860511658591e-01;
            std::uint64_t u;
            std::memcpy (&u, &x, 8);
            std::int32_t hx = static_cast<std::int32_t> (static_cast<std::uint32_t> (u >> 32));
            const std::uint32_t lx = static_cast<std::uint32_t> (u);
            std::int32_t k = 0;
            if (hx < 0x00100000)                                     // x < 2**-1022
            {
                if (((hx & 0x7fffffff) | static_cast<std::int32_t> (lx)) == 0)
                    return -std::numeric_limits<double>::infinity();   // log(+-0) = -inf
                if (hx < 0) return std::numeric_limits<double>::quiet_NaN();   // log(-#) = NaN
                k -= 54;
                x *= two54;                                          // subnormal number, scale up x
                std::memcpy (&u, &x, 8);
                hx = static_cast<std::int32_t> (static_cast<std::uint32_t> (u >> 32));
            }
            if (hx >= 0x7ff00000) return x + x;
            if (hx == 0x3ff00000 && lx == 0) return 0.0;              // log(1) = +0
            k += (hx >> 20) - 1023;
            hx &= 0x000fffff;
            const std::int32_t i = (hx + 0x95f64) & 0x100000;
            std::memcpy (&u, &x, 8);                                 // normalize x or x/2
            u = (u & 0xffffffffULL) | (static_cast<std::uint64_t> (static_cast<std::uint32_t> (hx | (i ^ 0x3ff00000))) << 32);
            std::memcpy (&x, &u, 8);
            k += (i >> 20);
            const double y = static_cast<double> (k);
            const double f = x - 1.0;
            const double hfsq = 0.5 * f * f;
            // k_log1p(f): log(1+f) - f for 1+f in ~[sqrt(2)/2, sqrt(2)]
            const double s = f / (2.0 + f);
            const double z = s * s;
            const double w = z * z;
            const double t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
            const double t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
            const double R = t2 + t1;
            const double r = s * (0.5 * f * f + R);
            double hi = f - hfsq;
            std::memcpy (&u, &hi, 8);                                // SET_LOW_WORD(hi, 0)
            u &= 0xffffffff00000000ULL;
            std::memcpy (&hi, &u, 8);
            const double lo = (f - hi) - hfsq + r;
            double val_hi = hi * ivln2hi;
            double val_lo = (lo + hi) * ivln2lo + lo * ivln2hi;
            const double ww = y + val_hi;                            // spadd(val_hi, val_lo, y)
            val_lo += (y - ww) + val_hi;
            val_hi = ww;
            return val_lo + val_hi;
        }

        // const dB = x => 20 * Math.log10(Math.max(x, 1e-9));
        inline double dB (double x) noexcept { return 20 * v8math::log10 (js::max (x, 1e-9)); }

        // const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
        inline double clamp (double v, double lo, double hi) noexcept { return js::min (hi, js::max (lo, v)); }

        // const midiToHz = m => 440 * Math.pow(2, (m - 69) / 12);
        inline double midiToHz (double m) noexcept { return 440 * std::pow (2.0, (m - 69) / 12); }

        // JS truthiness of a number (T.HARMONIC_GUARD ? ... : ...)
        inline bool truthy (double x) noexcept { return x != 0 && ! std::isnan (x); }

        // Same number as far as the JS "fmin + ':' + fmax" string comparison goes.
        inline bool sameNumber (double a, double b) noexcept { return a == b || (std::isnan (a) && std::isnan (b)); }

        // Index into a power-of-two history with a (possibly negative) frame
        // difference: two's complement like the JS 32-bit "&".
        inline int hidx (long long v, int mask) noexcept { return static_cast<int> (v & static_cast<long long> (mask)); }

        // Float32Array store
        inline float f32 (double x) noexcept { return static_cast<float> (x); }
    }

    bool setTuningValue (Tuning& t, const char* key, double value) noexcept
    {
        struct Field { const char* name; double Tuning::* member; };
        static const Field fields[] = {
            { "YIN_THRESHOLD", &Tuning::YIN_THRESHOLD }, { "CONF_ON", &Tuning::CONF_ON },
            { "CONF_TRACK", &Tuning::CONF_TRACK }, { "JUMP", &Tuning::JUMP },
            { "JUMP_SPAN", &Tuning::JUMP_SPAN }, { "HOLD", &Tuning::HOLD },
            { "BEND_RANGE", &Tuning::BEND_RANGE }, { "BEND_RETRIG", &Tuning::BEND_RETRIG },
            { "REFRACTORY", &Tuning::REFRACTORY }, { "PEND_MAX", &Tuning::PEND_MAX },
            { "LOWCONF_MAX", &Tuning::LOWCONF_MAX }, { "SOFT_AGREE", &Tuning::SOFT_AGREE },
            { "ONSET_AGREE", &Tuning::ONSET_AGREE }, { "LEGATO_AGREE", &Tuning::LEGATO_AGREE },
            { "LEGATO_WINDOW", &Tuning::LEGATO_WINDOW }, { "OCTAVE_AGREE", &Tuning::OCTAVE_AGREE },
            { "GRID", &Tuning::GRID }, { "SAME_NOTE_HOLDOFF", &Tuning::SAME_NOTE_HOLDOFF },
            { "EARLY_FIX", &Tuning::EARLY_FIX }, { "HARMONIC_GUARD", &Tuning::HARMONIC_GUARD },
        };
        if (key == nullptr) return false;
        for (const auto& f : fields)
        {
            if (std::strcmp (f.name, key) == 0)
            {
                t.*(f.member) = value;
                return true;
            }
        }
        return false;
    }

    // ------------------------------------------------------------------ Biquad

    mono::Biquad::Biquad (bool lowPass, double sr, double fc, double q) noexcept
    {
        const double w0 = 2 * kPi * fc / sr;
        const double cs = v8math::cos (w0), alpha = v8math::sin (w0) / (2 * q);
        const double a0 = 1 + alpha;
        if (lowPass)
        {
            b0 = (1 - cs) / 2 / a0; b1 = (1 - cs) / a0;
        }
        else
        {
            b0 = (1 + cs) / 2 / a0; b1 = -(1 + cs) / a0;
        }
        b2 = b0;
        a1 = -2 * cs / a0;
        a2 = (1 - alpha) / a0;
        z1 = 0; z2 = 0;
    }

    // ------------------------------------------------------------- MonoTracker

    MonoTracker::MonoTracker (double sampleRate, const MonoParams& params)
    {
        sr_ = sampleRate;
        // Decimate to ~24 kHz: every pitch of interest sits below 1.5 kHz, and the
        // cost of YIN grows with the square of the sample rate.
        D_ = static_cast<int> (js::max (1, js::round (sampleRate / 24000)));
        srD_ = sampleRate / D_;
        frameMs_ = HOP / srD_ * 1000;
        p_ = MonoParams {};
        derive();
        setupYin();
        setParams (params);

        lp1_ = mono::Biquad (true, sampleRate, 3000, 0.7);
        lp2_ = mono::Biquad (true, sampleRate, 3000, 0.7);
        hp_ = mono::Biquad (false, sampleRate, 2500, 0.7);
        reset();
    }

    void MonoTracker::setParams (const MonoParams& params)
    {
        const double fmin0 = p_.fmin, fmax0 = p_.fmax;
        p_ = params;
        derive();
        if (! sameNumber (p_.fmin, fmin0) || ! sameNumber (p_.fmax, fmax0)) setupYin();
    }

    void MonoTracker::derive() noexcept
    {
        const MonoParams& p = p_;
        gateDb_ = -36 - 24 * clamp (p.sens, 0, 1);    // -36 .. -60 dBFS
        riseDb_ = 12 - 6 * clamp (p.sens, 0, 1);      // 12 .. 6 dB
        relDb_ = 15 + 30 * clamp (p.release, 0, 1);   // 15 .. 45 dB below the peak
        shift_ = 12 * js::toInt32 (p.octave) + js::toInt32 (p.transpose);
    }

    void MonoTracker::setupYin()
    {
        const MonoParams& p = p_;
        tmin_ = static_cast<int> (js::max (2, std::floor (srD_ / p.fmax)));
        tmax_ = static_cast<int> (std::ceil (srD_ / p.fmin));
        // The integration window must cover at least one period of the lowest
        // note; 1.5 periods keeps the dips sharp while the note decays.
        W_ = static_cast<int> (js::max (512, js::round (1.5 * tmax_)));
        const int need = W_ + tmax_;
        int size = 1024;
        while (size < 2 * need) size *= 2;
        ring_.assign (static_cast<size_t> (size), 0.0f);
        mask_ = size - 1;
        wi_ = 0;
        win_.assign (static_cast<size_t> (need), 0.0f);
        d_.assign (static_cast<size_t> (tmax_ + 1), 0.0f);
        cmnd_.assign (static_cast<size_t> (tmax_ + 1), 0.0f);
    }

    void MonoTracker::reset() noexcept
    {
        // what _setupYin leaves behind (fresh, zero-filled typed arrays)
        std::fill (ring_.begin(), ring_.end(), 0.0f);
        wi_ = 0;
        std::fill (win_.begin(), win_.end(), 0.0f);
        std::fill (d_.begin(), d_.end(), 0.0f);
        std::fill (cmnd_.begin(), cmnd_.end(), 0.0f);

        // what the constructor sets after _setupYin / setParams
        lp1_.z1 = lp1_.z2 = 0;
        lp2_.z1 = lp2_.z2 = 0;
        hp_.z1 = hp_.z2 = 0;
        dcX_ = 0; dcY_ = 0;
        decPhase_ = 0;
        since_ = 0;
        sumSq_ = 0; sumHf_ = 0; nSq_ = 0;

        frame_ = 0;
        for (float& v : rmsHist_) v = -200.0f;
        for (float& v : hfHist_) v = -200.0f;
        lastOnset_ = kFar;
        lastOnsetPeak_ = -200;
        note_ = -1;
        noteVel_ = 0;
        noteK_ = kFar;
        noteWhy_ = Why::None;
        peakDb_ = -200;
        pend_ = Pending {};
        lowConf_ = 0; lowLevel_ = 0;
        legCand_ = -1; legAgree_ = 0;
        softCand_ = -1; softAgree_ = 0;
        armed_ = true;
        bendOut_ = 0;
        bendSmooth_ = 0;
        for (float& v : midiHist_) v = static_cast<float> (js::NaN);
        lastLatMs_ = 0;
        lastRmsDb_ = -200;
        curOffset_ = 0;
    }

    const char* MonoTracker::whyName (Why w) noexcept
    {
        switch (w)
        {
            case Why::Pluck: return "pluck";
            case Why::Legato: return "legato";
            case Why::Swell: return "swell";
            case Why::Fix: return "fix";
            case Why::None: break;
        }
        return "";
    }

    void MonoTracker::emit (EventList& out, Event e) const noexcept
    {
        e.sampleOffset = curOffset_;
        e.frame = frame_;
        out.push (e);
    }

    // Feeds a block of mono samples (process(input) in the JS).
    void MonoTracker::process (const float* input, int n, EventList& out) noexcept
    {
        float* const ring = ring_.data();
        const int mask = mask_, D = D_;
        for (int i = 0; i < n; i++)
        {
            const double x = input[i];
            // DC blocker (one pole at ~40 Hz)
            const double y = x - dcX_ + 0.995 * dcY_;
            dcX_ = x; dcY_ = y;
            sumSq_ += y * y; nSq_++;
            const double h = hp_.run (y);
            sumHf_ += h * h;
            const double f = lp2_.run (lp1_.run (y));
            if (++decPhase_ >= D)
            {
                decPhase_ = 0;
                ring[wi_] = f32 (f);
                wi_ = (wi_ + 1) & mask;
                if (++since_ >= HOP)
                {
                    since_ = 0;
                    curOffset_ = i;
                    frame (out);
                }
            }
        }
    }

    // Releases a sounding note (when the input stops).
    void MonoTracker::flush (EventList& out) noexcept
    {
        curOffset_ = 0;
        if (note_ >= 0) noteOff (out);
        pend_.active = false;
    }

    // ------------------------------------------------------------------ frame

    void MonoTracker::frame (EventList& out) noexcept
    {
        const MonoParams& p = p_;
        const Tuning& T = tuning;
        const double n = js::max (1, nSq_);
        const double rmsDb = dB (std::sqrt (sumSq_ / n));
        const double hfDb = dB (std::sqrt (sumHf_ / n));
        sumSq_ = 0; sumHf_ = 0; nSq_ = 0;
        lastRmsDb_ = rmsDb;
        const long long k = ++frame_;
        float* const H = rmsHist_;
        float* const HF = hfHist_;
        const int HM = 16 - 1;
        // The level 8-16 ms ago: just before a pluck, after the previous one has
        // settled. Comparing with the immediate past would miss fast repicking.
        double ref = -200, hfRef = -200;
        for (int i = 3; i <= 6; i++)
        {
            ref = js::max (ref, H[hidx (k - i, HM)]);
            hfRef = js::max (hfRef, HF[hidx (k - i, HM)]);
        }
        H[hidx (k, HM)] = f32 (rmsDb);
        HF[hidx (k, HM)] = f32 (hfDb);
        // A frame is shorter than one period of the low strings, so the frame
        // RMS swings with the waveform: hold the peak over one period for every
        // decision about the sustained level.
        double lvl = rmsDb;
        for (int i = 1; i < T.HOLD; i++) lvl = js::max (lvl, H[hidx (k - i, HM)]);

        const double gate = gateDb_, rise = riseDb_;
        bool onset = false, strong = false;
        // One onset per pluck (the refractory period) - unless a louder attack
        // follows: the pick touching the string scratches a few ms before it
        // releases it, and that scratch must not mask the real attack.
        const bool isFree = static_cast<double> (k - lastOnset_) >= T.REFRACTORY || rmsDb >= lastOnsetPeak_ + 3;
        if (rmsDb > gate && rmsDb > ref - 6 && isFree)
        {
            // a pick transient is shorter than a frame and may straddle two
            const double broad = rmsDb - ref, high = js::max (hfDb, HF[hidx (k - 1, HM)]) - hfRef;
            // The high band is nearly empty while a string sustains, so a pick
            // shows there with a smaller rise than in the full band.
            if (broad >= rise || (high >= rise - 3 && hfDb > gate - 12))
            {
                onset = true;
                lastOnset_ = k;
                lastOnsetPeak_ = rmsDb;
                // A re-strike of the note already sounding needs a convincing attack,
                // or a finger squeak (all high band) would double-trigger it.
                strong = broad >= rise || high >= rise + 6;
            }
        }

        if (onset)
        {
            // A pluck: the note starts once its pitch is known. The velocity is the
            // loudest of the first frames (the pick transient peaks within ~10 ms).
            pend_.active = true;
            pend_.k = k;
            pend_.peak = rmsDb;
            pend_.cand = -1;
            pend_.agree = 0;
            pend_.strong = strong;
            armed_ = true;
            legAgree_ = 0;
            softAgree_ = 0;
        }

        // ---- pitch (skipped in silence, so an idle rack costs nothing)
        double hz = 0, conf = 0, midiF = js::NaN;
        if (pend_.active)
        {
            const mono::YinResult r = pitchAtOnset();
            hz = r.hz; conf = r.conf;
            // A re-struck string breaks its own periodicity (the new pick restarts
            // it at an unrelated phase), which the window following the sounding
            // note sees as a collapse of confidence; a squeak leaves it intact.
            if (note_ >= 0 && k - pend_.k <= 4 && ! pend_.strong)
            {
                if (track().conf < 0.6) pend_.strong = true;
            }
        }
        else if (note_ < 0 && rmsDb > gate - 6)
        {
            const mono::YinResult r = yin (tmin_, tmax_, W_);
            hz = r.hz; conf = r.conf;
        }
        else if (note_ >= 0)
        {
            const mono::YinResult r = track();
            hz = r.hz; conf = r.conf;
        }
        if (hz > 0) midiF = 69 + 12 * log2V8 (hz / p.a4);
        const bool confident = conf >= T.CONF_TRACK && ! std::isnan (midiF);
        const int m = confident ? static_cast<int> (js::round (midiF)) : -1;

        if (pend_.active)
        {
            Pending& pd = pend_;
            if (k - pd.k <= 4)
            {
                pd.peak = js::max (pd.peak, rmsDb);
                lastOnsetPeak_ = js::max (lastOnsetPeak_, rmsDb);
                // A re-struck string is damped by the pick first: a dip well under
                // the sustain, then a sharp rise. A squeak has no such dip. (The dip
                // is read over two frames so the waveform of a low note, whose
                // period is longer than a frame, cannot fake one.)
                double dip = 200;
                for (int i = 1; i <= 3; i++)
                    dip = js::min (dip, js::max (H[hidx (k - i, HM)], H[hidx (k - i - 1, HM)]));
                if (rmsDb - dip >= rise && dip <= ref - 6) pd.strong = true;
            }
            // the very first frame is the pick transient: noise, not pitch
            // (a transient's estimate wanders between semitones; a note sits on the grid)
            if (k - pd.k >= 1 && conf >= T.CONF_ON && m >= 0 && lvl > gate && std::fabs (midiF - m) <= T.GRID)
            {
                pd.agree = (m == pd.cand) ? pd.agree + 1 : 1;
                pd.cand = m;
                if (pd.agree >= T.ONSET_AGREE)
                {
                    if (m != note_ || (pd.strong && static_cast<double> (k - noteK_) >= T.SAME_NOTE_HOLDOFF))
                    {
                        lastLatMs_ = static_cast<double> (k - pd.k + 1) * frameMs_;
                        noteOn (out, m, vel (pd.peak), js::max (pd.peak, rmsDb), Why::Pluck);
                    }
                    pend_.active = false;
                }
            }
            else if (static_cast<double> (k - pd.k) > T.PEND_MAX)
            {
                pend_.active = false;   // a knock or a scrape, not a note
            }
        }
        else if (note_ >= 0)
        {
            // ---- a note is sounding: follow it
            if (confident)
            {
                lowConf_ = 0;
                const double dev = midiF - note_;
                const bool onGrid = std::fabs (midiF - m) <= T.GRID;
                bool jump = false;
                for (int i = 1; i <= T.JUMP_SPAN; i++)
                {
                    const double past = midiHist_[hidx (k - i, 7)];
                    if (! std::isnan (past) && std::fabs (midiF - past) >= T.JUMP) jump = true;
                }
                // Right after a pluck, a stable pitch on another semitone means the
                // attack transient fooled the onset search: correct the note rather
                // than letting the pitch bend quietly absorb a wrong note.
                const bool early = noteWhy_ == Why::Pluck && static_cast<double> (k - noteK_) <= T.EARLY_FIX;
                const bool octave = std::fabs (dev) >= 11.5 && std::fabs (dev) <= 12.5;
                if (m != note_ && onGrid && (jump || early || ! p.bend || std::fabs (dev) > T.BEND_RETRIG))
                {
                    // Hammer-on, pull-off or slide: a new note without a new pluck.
                    // Enough recent frames must vote for the new note so a transition
                    // cannot leave ghost notes (the vote tolerates a flapping frame).
                    const double need = octave ? T.OCTAVE_AGREE : T.LEGATO_AGREE;
                    const double span = octave ? T.OCTAVE_AGREE + 2 : T.LEGATO_WINDOW;
                    int votes = 1;
                    for (int i = 1; i < span; i++)
                    {
                        const double past = midiHist_[hidx (k - i, 7)];
                        if (! std::isnan (past) && js::round (past) == m && std::fabs (past - m) <= T.GRID) votes++;
                    }
                    if (votes >= need)
                    {
                        lastLatMs_ = need * frameMs_;
                        noteOn (out, m, js::max (vel (rmsDb), noteVel_ * 0.6), rmsDb, early ? Why::Fix : Why::Legato);
                    }
                }
                else if (p.bend)
                {
                    bendTo (out, early ? clamp (dev, -0.5, 0.5) : dev);
                }
            }
            else if (++lowConf_ >= T.LOWCONF_MAX)
            {
                noteOff (out);   // no period left: the string was muted
            }
        }
        else if (armed_ && lvl > gate && conf >= T.CONF_ON && m >= 0 && std::fabs (midiF - m) <= T.GRID)
        {
            // ---- idle: a note that swelled in without a detectable attack
            // (volume pedal, very soft touch). Needs a longer agreement than a pluck.
            softAgree_ = (m == softCand_) ? softAgree_ + 1 : 1;
            softCand_ = m;
            if (softAgree_ >= T.SOFT_AGREE)
            {
                lastLatMs_ = T.SOFT_AGREE * frameMs_;
                noteOn (out, m, vel (rmsDb), rmsDb, Why::Swell);
            }
        }
        else
        {
            softAgree_ = 0;
        }

        // ---- level-based note-off, whatever else is going on
        if (note_ >= 0)
        {
            peakDb_ = js::max (peakDb_, lvl);
            if (lvl < js::max (peakDb_ - relDb_, gate - 6))
            {
                if (++lowLevel_ >= 2) noteOff (out);
            }
            else
            {
                lowLevel_ = 0;
            }
        }
        // Silence re-arms the onset-less path (it is disarmed by every note-off,
        // otherwise a decayed string would retrigger the moment its note ends).
        if (lvl < gate - 3) armed_ = true;

        midiHist_[hidx (k, 7)] = f32 (confident ? midiF : js::NaN);

        // Optional per-frame hook for tuning; unset in production.
        if (traceFn_ != nullptr)
        {
            MonoFrameTrace tr;
            tr.k = k; tr.rmsDb = rmsDb; tr.hfDb = hfDb; tr.ref = ref; tr.hfRef = hfRef; tr.lvl = lvl;
            tr.onset = onset; tr.strong = strong; tr.hz = hz; tr.conf = conf; tr.midiF = midiF;
            tr.note = note_; tr.pend = pend_.active;
            traceFn_ (tr, traceUser_);
        }

        if (k % METER_EVERY == 0)
        {
            Event e;
            e.type = EventType::Meter;
            e.db = rmsDb;
            e.hz = hz;
            e.midiF = confident ? midiF : js::NaN;
            e.conf = conf;
            e.note = note_ >= 0 ? note_ + shift_ : -1;
            e.latMs = lastLatMs_;
            emit (out, e);
        }
    }

    double MonoTracker::vel (double db) const noexcept
    {
        const double raw = clamp ((db - VEL_LO_DB) / (VEL_HI_DB - VEL_LO_DB), 0, 1);
        return clamp (1 - p_.dyn + p_.dyn * raw, 0.05, 1);
    }

    void MonoTracker::noteOn (EventList& out, int m, double v, double peakDb, Why why) noexcept
    {
        if (note_ >= 0)
        {
            Event off;
            off.type = EventType::NoteOff;
            off.midi = note_ + shift_;
            emit (out, off);
        }
        noteK_ = frame_;
        noteWhy_ = why;
        if (bendOut_ != 0)
        {
            Event b;
            b.type = EventType::Bend;
            b.semis = 0;
            emit (out, b);
        }
        bendOut_ = 0; bendSmooth_ = 0;
        note_ = m;
        noteVel_ = v;
        peakDb_ = peakDb;
        lowConf_ = 0; lowLevel_ = 0;
        legAgree_ = 0; softAgree_ = 0;
        for (float& h : midiHist_) h = static_cast<float> (js::NaN);
        Event on;
        on.type = EventType::NoteOn;
        on.midi = m + shift_;
        on.vel = v;
        on.why = whyName (why);
        emit (out, on);
    }

    void MonoTracker::noteOff (EventList& out) noexcept
    {
        Event off;
        off.type = EventType::NoteOff;
        off.midi = note_ + shift_;
        emit (out, off);
        if (bendOut_ != 0)
        {
            Event b;
            b.type = EventType::Bend;
            b.semis = 0;
            emit (out, b);
        }
        bendOut_ = 0; bendSmooth_ = 0;
        note_ = -1;
        armed_ = false;
        legAgree_ = 0;
    }

    void MonoTracker::bendTo (EventList& out, double dev) noexcept
    {
        // Light smoothing kills estimator jitter without lagging a real bend.
        bendSmooth_ += (dev - bendSmooth_) * 0.35;
        const double b = clamp (bendSmooth_, -tuning.BEND_RANGE, tuning.BEND_RANGE);
        if (std::fabs (b - bendOut_) > 0.03)
        {
            bendOut_ = b;
            Event e;
            e.type = EventType::Bend;
            e.semis = b;
            emit (out, e);
        }
    }

    // -------------------------------------------------------------------- YIN

    // Pitch of a note that has just started. A fresh note is only visible to
    // the lags it is older than, so the shortest window that is confident wins:
    // the high strings are named after a few milliseconds instead of waiting
    // for the long window the low E needs. Each window searches only the lags
    // it can also verify at twice the period (the octave guard).
    mono::YinResult MonoTracker::pitchAtOnset() noexcept
    {
        static constexpr int windows[2] = { 128, 256 };
        for (const int w : windows)
        {
            if (w >= W_) break;
            const int tHi = static_cast<int> (js::min (tmax_, js::round (w * 1.33)));
            const mono::YinResult r = yin (tmin_, tHi, w, static_cast<int> (std::floor (tHi / 2.0)));
            if (r.conf >= tuning.CONF_ON) return r;
        }
        return yin (tmin_, tmax_, W_);
    }

    // Follows the sounding note: a short window (3 periods), +-14 semitones.
    mono::YinResult MonoTracker::track() noexcept
    {
        const double tau0 = srD_ / midiToHz (note_);
        return yin (
            static_cast<int> (js::max (tmin_, std::floor (tau0 / 2.2))),
            static_cast<int> (js::min (tmax_, std::ceil (tau0 * 2.2))),
            static_cast<int> (clamp (js::round (3 * tau0), 96, W_)));
    }

    // Period search: the newest W decimated samples are compared with the
    // same samples a lag earlier (this way a note is seen as soon as it is two
    // periods old). Dips of the cumulative-mean-normalised difference are
    // looked up between the lags tLo and searchHi; the difference is computed
    // up to tHi so the octave guard can look at twice the period.
    // Returns { hz, conf }: hz 0 when nothing periodic was found.
    mono::YinResult MonoTracker::yin (int tLo, int tHi, int W, int searchHi) noexcept
    {
        float* const win = win_.data();
        float* const d = d_.data();
        float* const cmnd = cmnd_.data();
        const float* const ring = ring_.data();
        const Tuning& T = tuning;
        const int n = W + tHi;
        // the last n decimated samples, oldest first; the reference is the last W
        int idx = (wi_ - n) & mask_;
        for (int i = 0; i < n; i++) { win[i] = ring[idx]; idx = (idx + 1) & mask_; }

        // difference function (lags from 1: the normalisation needs them all)
        for (int tau = 1; tau <= tHi; tau++)
        {
            double sum = 0;
            const int base = tHi - tau;
            const float* const a = win + tHi;
            const float* const b = win + base;
            for (int j = 0; j < W; j++)
            {
                const double diff = static_cast<double> (a[j]) - static_cast<double> (b[j]);
                sum += diff * diff;
            }
            d[tau] = f32 (sum);
        }
        // cumulative mean normalised difference
        cmnd[0] = 1;
        double run = 0;
        for (int tau = 1; tau <= tHi; tau++)
        {
            run += d[tau];
            cmnd[tau] = run > 0 ? f32 (static_cast<double> (d[tau]) * tau / run) : 1.0f;
        }
        // first dip below the threshold, walked down to its local minimum
        int tau = -1;
        for (int t = tLo; t < searchHi; t++)
        {
            if (cmnd[t] < T.YIN_THRESHOLD)
            {
                while (t + 1 <= tHi && cmnd[t + 1] < cmnd[t]) t++;
                tau = t;
                break;
            }
        }
        if (tau < 0)
        {
            // nothing periodic enough: report the best candidate with its (low) confidence
            double best = 1;
            for (int t = tLo; t <= searchHi; t++)
                if (cmnd[t] < best) { best = cmnd[t]; tau = t; }
            if (tau < 0) return { 0, 0 };
        }
        // Harmonic guard: a strong 2nd or 3rd harmonic (or the attack transient)
        // can dip below the threshold at 1/2, 1/3 or 2/3 of the period. The true
        // period then shows a clearly deeper dip at 2x, 3x or 1.5x the lag.
        int guardTau = tau;
        double guardBest = static_cast<double> (cmnd[tau]) - 0.1;
        static constexpr double multsAll[3] = { 1.5, 2, 3 };
        static constexpr double multsTwo[1] = { 2 };
        const bool allMults = truthy (T.HARMONIC_GUARD);
        const double* const mults = allMults ? multsAll : multsTwo;
        const int nMults = allMults ? 3 : 1;
        for (int mi = 0; mi < nMults; mi++)
        {
            const int c = static_cast<int> (js::round (mults[mi] * tau));
            if (c + 2 > tHi) continue;
            for (int t = c - 2; t <= c + 2; t++)
                if (cmnd[t] < guardBest) { guardBest = cmnd[t]; guardTau = t; }
        }
        tau = guardTau;
        const double conf = 1 - static_cast<double> (cmnd[tau]);
        // parabolic interpolation of the minimum
        double tauF = tau;
        if (tau > 1 && tau < tHi)
        {
            const double a = cmnd[tau - 1], b = cmnd[tau], c = cmnd[tau + 1];
            const double den = a - 2 * b + c;
            if (den > 0)
            {
                const double delta = 0.5 * (a - c) / den;
                if (std::fabs (delta) < 1) tauF = tau + delta;
            }
        }
        return { srD_ / tauF, conf };
    }
}
