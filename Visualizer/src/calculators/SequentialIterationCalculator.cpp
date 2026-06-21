#include "SequentialIterationCalculator.h"
#include "FixedPointArithmetics.h"

#include <omp.h>

int SequentialIterationCalculator::calculate(const std::vector<Complex>& points, std::vector<int>& iters, const unsigned int maxIter) const {
    const int N = static_cast<int>(points.size());

    #pragma omp parallel for schedule(dynamic)
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
                iters[idx] = i;
                break;
            }
        }
    }
    return 0;
}

int SequentialIterationCalculator::calculate(const std::vector<ComplexHP>& points, std::vector<int>& iters, const unsigned int maxIter) const {
    using namespace fpa;
    const int N = static_cast<int>(points.size());

    #pragma omp parallel for schedule(dynamic)
    for (int idx = 0; idx < N; idx++) {
        uint x0[FP_SIZE], y0[FP_SIZE];
        for (int k = 0; k < FP_SIZE; k++) {
            x0[k] = points[idx].real[k];
            y0[k] = points[idx].imag[k];
        }

        uint x[FP_SIZE]  = {}, y[FP_SIZE]  = {};
        uint x2[FP_SIZE] = {}, y2[FP_SIZE] = {};
        uint temp[FP_SIZE];
        const uint fourFixed[FP_SIZE] = {4, 0, 0, 0};

        iters[idx] = -1;
        for (unsigned int i = 0; i < maxIter; i++) {
            // y = (x + x) * y + y0
            addFixed(x, x, temp);
            mulCmplFixed(temp, y, temp);
            addFixed(temp, y0, y);

            // x = x2 - y2 + x0
            subFixed(x2, y2, temp);
            addFixed(temp, x0, x);

            // x2 = x * x
            mulCmplFixed(x, x, x2);

            // y2 = y * y
            mulCmplFixed(y, y, y2);

            // escape check: x2 + y2 > 4
            addFixed(x2, y2, temp);
            if (gtFixed(temp, fourFixed)) {
                iters[idx] = i;
                break;
            }
        }
    }
    return 0;
}
