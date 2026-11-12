#include "CpuParallelIterationCalculator.h"
#include "FixedPointArithmetics.h"
#include "FixedPointCore.h"

#include <stdexcept>
#include <string>

// Points a thread takes from the OpenMP scheduler at a time. One point per grab (the default) makes the
// scheduler lock a visible cost on cheap points; 256 was the best of 64..1024 in the double-precision benchmark.
constexpr int OMP_CHUNK = 256;


void CpuParallelIterationCalculator::calculate(const std::vector<Complex>& points, EscapeResult& result, const unsigned int maxIter, const double escapeRadius) const {
    const int N = static_cast<int>(points.size());
    const double escapeSq = escapeRadius * escapeRadius;
    std::vector<int>& iters = result.iters;
    std::vector<float>& magSq = result.magSq;

    #pragma omp parallel for schedule(dynamic, OMP_CHUNK) default(none) shared(points, iters, magSq, maxIter, N, escapeSq)
    for (int idx = 0; idx < N; idx++) {
        const double x0 = points[idx].real;
        const double y0 = points[idx].imag;

        double x2 = 0, y2 = 0, x = 0, y = 0;

        iters[idx] = -1;
        magSq[idx] = 0;
        for (unsigned int i = 0; i < maxIter; i++) {
            y  = (x + x) * y + y0;
            x  = x2 - y2 + x0;
            x2 = x * x;
            y2 = y * y;
            if (x2 + y2 > escapeSq) {
                iters[idx] = static_cast<int>(i);
                magSq[idx] = static_cast<float>(x2 + y2);
                break;
            }
        }
    }
}

namespace {

// The high-precision loop for one fraction size F (a compile-time constant): the arithmetic is called
// without any per-operation dispatch and the compiler can inline it into the loop.
template<int F>
void calculateHp(const HpPoints& points, EscapeResult& result, const unsigned int maxIter, const double escapeRadius) {
    using namespace fpa;
    constexpr int S = WHOLE_PART + F;   // words per number
    const int N = static_cast<int>(points.size());
    std::vector<int>& iters = result.iters;
    std::vector<float>& magSq = result.magSq;

    uint thresholdWhole, thresholdFraction;
    escapeThresholdWords(escapeRadius, thresholdWhole, thresholdFraction);

    #pragma omp parallel for schedule(dynamic, OMP_CHUNK) default(none) shared(points, iters, magSq, maxIter, N, thresholdWhole, thresholdFraction)
    for (int idx = 0; idx < N; idx++) {
        uint x0[S], y0[S];
        const uint* re = points.real(idx);
        const uint* im = points.imag(idx);
        for (int k = 0; k < S; k++) {
            x0[k] = re[k];
            y0[k] = im[k];
        }

        uint x[S]  = {}, y[S]  = {};
        uint x2[S] = {}, y2[S] = {};
        uint temp[S];
        const uint threshold[S] = {thresholdWhole, thresholdFraction};   // R^2; the other fraction words are 0

        iters[idx] = -1;
        magSq[idx] = 0;
        for (unsigned int i = 0; i < maxIter; i++) {
            // y = (x + x) * y + y0
            core::add<F>(x, x, temp);
            core::mul<F>(temp, y, temp);
            core::add<F>(temp, y0, y);

            // x = x2 - y2 + x0
            core::sub<F>(x2, y2, temp);
            core::add<F>(temp, x0, x);

            // x2 = x * x
            core::sqr<F>(x, x2);

            // y2 = y * y
            core::sqr<F>(y, y2);

            // escape check: x2 + y2 > R^2
            core::add<F>(x2, y2, temp);
            if (core::gt<F>(temp, threshold)) {
                iters[idx] = static_cast<int>(i);
                // |z|^2 as a float: the whole word and the first fraction word carry more than enough bits
                magSq[idx] = static_cast<float>(temp[0]) + static_cast<float>(temp[1]) * (1.0f / 4294967296.0f);
                break;
            }
        }
    }
}

}  // namespace

void CpuParallelIterationCalculator::calculate(const HpPoints& points, EscapeResult& result, const unsigned int maxIter, const double escapeRadius) const {
    // Pick the compile-time format once per render.
    switch (points.fractionWords) {
        case 1: calculateHp<1>(points, result, maxIter, escapeRadius); break;
        case 2: calculateHp<2>(points, result, maxIter, escapeRadius); break;
        case 3: calculateHp<3>(points, result, maxIter, escapeRadius); break;
        case 4: calculateHp<4>(points, result, maxIter, escapeRadius); break;
        case 5: calculateHp<5>(points, result, maxIter, escapeRadius); break;
        case 6: calculateHp<6>(points, result, maxIter, escapeRadius); break;
        default:
            throw std::invalid_argument("Unsupported fraction size: " + std::to_string(points.fractionWords) + " words");
    }
}
