// test/poly-engine.test.mjs, test 2: "resampler: scipy kernel (unit DC gain x up,
// symmetric), 48 kHz -> 24 kHz passes a 1 kHz sine untouched".
#include "poly_test_util.h"
#include "midpluck/poly/resample.h"
#include "midpluck/poly/engine.h"

#include <cmath>
#include <vector>

int main()
{
    using namespace polytest;
    using namespace midpluck::poly;

    const std::vector<double> h = firwinKaiser (41, 0.5, 5.0);
    double sum = 0;
    for (double v : h) sum += v;
    PT_CHECK (std::fabs (sum - 1) < 1e-12, "unit DC gain: %.17g", sum);
    for (int i = 0; i < 20; i++)
        PT_CHECK (std::fabs (h[static_cast<size_t> (i)] - h[static_cast<size_t> (40 - i)]) < 1e-15, "symmetric at %d", i);

    Resampler rs (48000, 24000);
    PT_CHECK (rs.up == 1, "up %lld", rs.up);
    PT_CHECK (rs.down == 2, "down %lld", rs.down);
    PT_CHECK (rs.halfLen == 20, "halfLen %lld", rs.halfLen);
    const int n = 48000;
    std::vector<double> y;
    y.reserve (n / 2);
    for (int i = 0; i < n; i++)
        rs.pushSample (std::sin (2 * kPi * 1000 * i / 48000), [&] (double v) { y.push_back (v); });
    PT_CHECK (static_cast<int> (y.size()) == n / 2, "%zu output samples", y.size());
    // steady state, delay = halfLen input samples = 10 output samples; the
    // 41-tap Kaiser-5 kernel has ~0.1 % passband ripple (scipy's too)
    double err = 0;
    for (size_t i = 100; i + 100 < y.size(); i++)
    {
        const double expect = std::sin (2 * kPi * 1000 * (static_cast<double> (i) - 10) / 24000);
        err = std::fmax (err, std::fabs (y[i] - expect));
    }
    PT_CHECK (err < 5e-3, "max error %.6g", err);

    Resampler rs2 (44100, 24000);
    PT_CHECK (rs2.up == 80, "up %lld", rs2.up);
    PT_CHECK (rs2.down == 147, "down %lld", rs2.down);
    PT_CHECK (std::fabs (rs2.delaySeconds() * 1000 - 0.4167) < 0.01, "same 0.42 ms delay at 44.1 kHz: %.4f ms", rs2.delaySeconds() * 1000);
    PT_CHECK (rs2.perPhase == 37, "37 taps per output sample at 44.1 kHz, got %d", rs2.perPhase);

    // the static delay helper agrees with an instance, at the plugin's usual rates
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        Resampler r (sr, 24000);
        PT_CHECK (PolyEngineState::delaySeconds (sr) == r.delaySeconds(), "delaySeconds(%g)", sr);
    }
    return finish ("test_poly_resample");
}
