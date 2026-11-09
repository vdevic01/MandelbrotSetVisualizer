#include "SmoothColoring.h"

#include <cmath>

std::vector<float> computeSmoothValues(const EscapeResult& escape, const double escapeRadius) {
    const int N = static_cast<int>(escape.size());
    const std::vector<int>& iters = escape.iters;
    const std::vector<float>& magSq = escape.magSq;
    // ln|z| / ln R == ln(|z|^2) / ln(R^2), so the squared magnitude can be used as it is.
    const double logThreshold = std::log(escapeRadius * escapeRadius);

    std::vector<float> smooth(N);
    #pragma omp parallel for default(none) shared(iters, magSq, smooth, N, logThreshold)
    for (int i = 0; i < N; i++) {
        if (iters[i] < 0) {
            smooth[i] = SMOOTH_NOT_ESCAPED;
            continue;
        }
        const double ratio = std::log(static_cast<double>(magSq[i])) / logThreshold;
        // ratio is >= 1 for an escaped point; guard against rounding and against bad input.
        const double correction = ratio > 1.0 ? std::log2(ratio) : 0.0;
        const double value = iters[i] + 1.0 - correction;
        // A point far outside the set can overshoot R^2 in a single step; keep the value non-negative.
        smooth[i] = static_cast<float>(value > 0.0 ? value : 0.0);
    }
    return smooth;
}
