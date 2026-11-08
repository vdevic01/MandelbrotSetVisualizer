#include "CpuParallelIterationCalculator.h"
#include "FixedPointArithmetics.h"
#include "FixedPointCore.h"

#include <stdexcept>
#include <string>


void CpuParallelIterationCalculator::calculate(const std::vector<Complex>& points, std::vector<int>& iters, const unsigned int maxIter) const {
    const int N = static_cast<int>(points.size());

    #pragma omp parallel for schedule(dynamic) default(none) shared(points, iters, maxIter, N)
    for (int idx = 0; idx < N; idx++) {
        const double x0 = points[idx].real;
        const double y0 = points[idx].imag;

        double x2 = 0, y2 = 0, x = 0, y = 0;

        iters[idx] = -1;
        for (unsigned int i = 0; i < maxIter; i++) {
            y  = (x + x) * y + y0;
            x  = x2 - y2 + x0;
            x2 = x * x;
            y2 = y * y;
            if (x2 + y2 > 4) {
                iters[idx] = static_cast<int>(i);
                break;
            }
        }
    }
}

namespace {

// The high-precision loop for one fraction size F (a compile-time constant): the arithmetic is called
// without any per-operation dispatch and the compiler can inline it into the loop.
template<int F>
void calculateHp(const HpPoints& points, std::vector<int>& iters, const unsigned int maxIter) {
    using namespace fpa;
    constexpr int S = WHOLE_PART + F;   // words per number
    const int N = static_cast<int>(points.size());

    #pragma omp parallel for schedule(dynamic) default(none) shared(points, iters, maxIter, N)
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
        const uint fourFixed[S] = {4};   // 4.0: the whole word is 4, the fraction words are 0

        iters[idx] = -1;
        for (unsigned int i = 0; i < maxIter; i++) {
            // y = (x + x) * y + y0
            core::add<F>(x, x, temp);
            core::mul<F>(temp, y, temp);
            core::add<F>(temp, y0, y);

            // x = x2 - y2 + x0
            core::sub<F>(x2, y2, temp);
            core::add<F>(temp, x0, x);

            // x2 = x * x
            core::mul<F>(x, x, x2);

            // y2 = y * y
            core::mul<F>(y, y, y2);

            // escape check: x2 + y2 > 4
            core::add<F>(x2, y2, temp);
            if (core::gt<F>(temp, fourFixed)) {
                iters[idx] = static_cast<int>(i);
                break;
            }
        }
    }
}

}  // namespace

void CpuParallelIterationCalculator::calculate(const HpPoints& points, std::vector<int>& iters, const unsigned int maxIter) const {
    // Pick the compile-time format once per render.
    switch (points.fractionWords) {
        case 1: calculateHp<1>(points, iters, maxIter); break;
        case 2: calculateHp<2>(points, iters, maxIter); break;
        case 3: calculateHp<3>(points, iters, maxIter); break;
        case 4: calculateHp<4>(points, iters, maxIter); break;
        case 5: calculateHp<5>(points, iters, maxIter); break;
        case 6: calculateHp<6>(points, iters, maxIter); break;
        default:
            throw std::invalid_argument("Unsupported fraction size: " + std::to_string(points.fractionWords) + " words");
    }
}
