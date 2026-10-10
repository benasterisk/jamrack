// MONO closed test: YIN period search (tracker.js _yin) on pure sines fed
// through the whole front end (DC blocker, low-pass, decimation), full-range
// window and short tracking window (_track's geometry).
//
// Two references, both to 0.01 decimated sample:
//  - the closed-form YIN estimate: the same algorithm (CMND, first dip under
//    YIN_THRESHOLD walked down to its minimum, 3-point parabola) applied to
//    the ideal difference function of a sine, d(t) = 1 - cos(2 pi t / T).
//    Checked at every frequency.
//  - the true period T = srD / f. Checked for periods >= 33 samples (f <= 660 Hz
//    at 22.05 kHz). Below that the CMND factor t / sum(d) adds a cubic term
//    to the dip and the 3-point parabola of the JS is biased by up to ~0.5 / T
//    sample (0.016 at 880 Hz / 24 kHz, 0.02 at 880 Hz / 22.05 kHz): a property
//    of the canonical algorithm, reproduced by the closed form, not a port error.
#include "mono_test_util.h"

using namespace midpluck;

namespace
{
    double closedFormYin (double T, int tLo, int tHi, double threshold)
    {
        std::vector<double> d (static_cast<size_t> (tHi + 1)), cmnd (static_cast<size_t> (tHi + 1));
        cmnd[0] = 1;
        double run = 0;
        for (int t = 1; t <= tHi; t++)
        {
            d[static_cast<size_t> (t)] = 1 - std::cos (2 * monotest::kPi * t / T);
            run += d[static_cast<size_t> (t)];
            cmnd[static_cast<size_t> (t)] = run > 0 ? d[static_cast<size_t> (t)] * t / run : 1;
        }
        int tau = -1;
        for (int t = tLo; t < tHi; t++)
        {
            if (cmnd[static_cast<size_t> (t)] < threshold)
            {
                while (t + 1 <= tHi && cmnd[static_cast<size_t> (t + 1)] < cmnd[static_cast<size_t> (t)]) t++;
                tau = t;
                break;
            }
        }
        if (tau <= 1 || tau >= tHi) return NAN;
        const double a = cmnd[static_cast<size_t> (tau - 1)], b = cmnd[static_cast<size_t> (tau)], c = cmnd[static_cast<size_t> (tau + 1)];
        const double den = a - 2 * b + c;
        return den > 0 ? tau + 0.5 * (a - c) / den : tau;
    }
}

int main()
{
    // E2, A2, D3, G3, B3, E4, A4, E5, A5 and two off-grid pitches (non-integer periods everywhere)
    const double freqs[] = { 82.40689, 110.0, 146.83238, 196.0, 246.94165, 329.62756, 440.0, 659.25511, 880.0, 101.3, 1234.5 };
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        double worst = 0, worstShort = 0, worstModel = 0, minConf = 1;
        for (const double f : freqs)
        {
            MonoTracker tr (sr);
            std::vector<float> sig (static_cast<size_t> (sr * 0.5));
            for (size_t i = 0; i < sig.size(); i++)
                sig[i] = static_cast<float> (0.3 * std::sin (2 * monotest::kPi * f * static_cast<double> (i) / sr));
            auto list = std::make_unique<EventList>();
            for (size_t o = 0; o < sig.size(); o += 128)
            {
                list->clear();
                tr.process (sig.data() + o, static_cast<int> (std::min<size_t> (128, sig.size() - o)), *list);
            }
            const double srD = tr.decimatedRate();
            const double truePeriod = srD / f;

            // full-range search, as for an idle tracker
            const mono::YinResult r = tr.yin (tr.tmin(), tr.tmax(), tr.window());
            const double period = srD / r.hz;
            const double err = std::fabs (period - truePeriod);
            worst = std::max (worst, err);
            minConf = std::min (minConf, r.conf);
            const double model = closedFormYin (truePeriod, tr.tmin(), tr.tmax(), tr.tuning.YIN_THRESHOLD);
            const double errModel = std::fabs (period - model);
            worstModel = std::max (worstModel, errModel);
            MT_CHECK (r.hz > 0 && errModel <= 0.01, "%.0f Hz input, %.3f Hz sine: period %.5f, closed-form YIN %.5f (error %.4f)", sr, f, period, model, errModel);
            if (truePeriod >= 33)
                MT_CHECK (r.hz > 0 && err <= 0.01, "%.0f Hz input, %.3f Hz sine: period %.5f, expected %.5f (error %.4f)", sr, f, period, truePeriod, err);
            if (sr == 48000.0)
                std::printf ("  %8.3f Hz: true period %9.5f, found %9.5f (error %+.5f), closed-form YIN %9.5f (port - model %+.5f), conf %.5f\n",
                             f, truePeriod, period, period - truePeriod, model, period - model, r.conf);
            MT_CHECK (r.conf > 0.99, "%.0f Hz input, %.3f Hz sine: confidence %.4f", sr, f, r.conf);

            // tracking window: 3 periods around the note (geometry of _track)
            const double tau0 = truePeriod;
            const int tLo = std::max (tr.tmin(), static_cast<int> (std::floor (tau0 / 2.2)));
            const int tHi = std::min (tr.tmax(), static_cast<int> (std::ceil (tau0 * 2.2)));
            const int W = static_cast<int> (std::min<double> (tr.window(), std::max (96.0, std::floor (3 * tau0 + 0.5))));
            const mono::YinResult s = tr.yin (tLo, tHi, W);
            const double errShort = std::fabs (srD / s.hz - truePeriod);
            worstShort = std::max (worstShort, errShort);
            const double modelShort = closedFormYin (truePeriod, tLo, tHi, tr.tuning.YIN_THRESHOLD);
            MT_CHECK (s.hz > 0 && std::fabs (srD / s.hz - modelShort) <= 0.01, "%.0f Hz input, %.3f Hz sine, tracking window: period %.5f, closed-form %.5f", sr, f, srD / s.hz, modelShort);
            if (truePeriod >= 33)
                MT_CHECK (s.hz > 0 && errShort <= 0.01, "%.0f Hz input, %.3f Hz sine, tracking window: period error %.4f", sr, f, errShort);

            // hz is reported back in Hz consistently with the input
            MT_CHECK (std::fabs (r.hz - f) / f < 2e-3, "%.0f Hz input: %.3f Hz read as %.4f Hz", sr, f, r.hz);
        }
        std::printf ("%.0f Hz (D = %d, srD = %.1f Hz): worst |port - closed-form YIN| %.5f samples; worst |port - true period| %.5f (full search), %.5f (tracking window), min confidence %.5f\n",
                     sr, MonoTracker (sr).decimation(), MonoTracker (sr).decimatedRate(), worstModel, worst, worstShort, minConf);
    }

    // silence: nothing periodic, hz 0 or a low confidence, never a confident pitch
    {
        MonoTracker tr (48000);
        std::vector<float> zero (48000, 0.0f);
        auto list = std::make_unique<EventList>();
        for (size_t o = 0; o < zero.size(); o += 128) { list->clear(); tr.process (zero.data() + o, 128, *list); }
        const mono::YinResult r = tr.yin (tr.tmin(), tr.tmax(), tr.window());
        MT_CHECK (r.conf < tr.tuning.CONF_TRACK, "digital silence gave confidence %.3f", r.conf);
    }
    return monotest::finish ("test_mono_yin");
}
