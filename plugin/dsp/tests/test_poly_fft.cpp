// test/poly-engine.test.mjs, test 1: "FFT magnitude matches a direct DFT".
#include "poly_test_util.h"
#include "midpluck/poly/fft.h"

#include <cmath>
#include <stdexcept>
#include <vector>

int main()
{
    using namespace polytest;
    const int n = 2048;
    std::vector<double> x (1024);
    for (size_t i = 0; i < x.size(); i++)
        x[i] = std::sin (2 * kPi * 100.5 * i / 1024) + 0.3 * std::cos (2 * kPi * 7 * i / 1024);
    std::vector<double> mag (n / 2 + 1);
    midpluck::poly::RealFFT fft (n);
    fft.magnitude (x.data(), static_cast<int> (x.size()), mag.data());
    for (int k : { 0, 7 * 2, 201, 500, 1024 })
    {
        double re = 0, im = 0;
        for (size_t i = 0; i < x.size(); i++)
        {
            re += x[i] * std::cos (2 * kPi * k * i / n);
            im -= x[i] * std::sin (2 * kPi * k * i / n);
        }
        const double ref = std::hypot (re, im);
        PT_CHECK (std::fabs (mag[static_cast<size_t> (k)] - ref) < 1e-9, "bin %d: %.17g vs %.17g", k, mag[static_cast<size_t> (k)], ref);
    }
    // not a power of two -> refused
    bool threw = false;
    try { midpluck::poly::RealFFT bad (1000); } catch (const std::exception&) { threw = true; }
    PT_CHECK (threw, "RealFFT(1000) must throw");
    return finish ("test_poly_fft");
}
