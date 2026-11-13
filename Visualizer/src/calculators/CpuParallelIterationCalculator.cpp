#include "CpuParallelIterationCalculator.h"
#include "FixedPointArithmetics.h"
#include "FixedPointCore.h"

#include <stdexcept>
#include <string>

// Points a thread takes from the OpenMP scheduler at a time. One point per grab (the default) makes the
// scheduler lock a visible cost on cheap points; 256 was the best of 64..1024 in the double-precision benchmark.
constexpr int OMP_CHUNK = 256;


#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
#define HAVE_AVX2_PATH 1
#include <immintrin.h>
#endif

namespace {

// One point of the double-precision loop. The AVX2 version below must give exactly the same numbers.
inline void iteratePoint(const Complex& p, int& itersOut, float& magSqOut, const unsigned int maxIter, const double escapeSq) {
    const double x0 = p.real;
    const double y0 = p.imag;

    double x2 = 0, y2 = 0, x = 0, y = 0;

    itersOut = -1;
    magSqOut = 0;
    for (unsigned int i = 0; i < maxIter; i++) {
        y  = (x + x) * y + y0;
        x  = x2 - y2 + x0;
        x2 = x * x;
        y2 = y * y;
        if (x2 + y2 > escapeSq) {
            itersOut = static_cast<int>(i);
            magSqOut = static_cast<float>(x2 + y2);
            break;
        }
    }
}

void calculateScalar(const std::vector<Complex>& points, EscapeResult& result, const unsigned int maxIter, const double escapeSq) {
    const int N = static_cast<int>(points.size());
    std::vector<int>& iters = result.iters;
    std::vector<float>& magSq = result.magSq;

    #pragma omp parallel for schedule(dynamic, OMP_CHUNK) default(none) shared(points, iters, magSq, maxIter, N, escapeSq, OMP_CHUNK)
    for (int idx = 0; idx < N; idx++)
        iteratePoint(points[idx], iters[idx], magSq[idx], maxIter, escapeSq);
}

#ifdef HAVE_AVX2_PATH

// Four points per step in one AVX2 register. All four run until every one of them has escaped (or maxIter
// is reached); a point that escapes records its iteration and |z|^2 once and is ignored from then on (its
// values may overflow to infinity afterwards, which is harmless). No fused multiply-add is used, so every
// lane computes bit for bit what iteratePoint computes.
__attribute__((target("avx2")))
void calculateAvx2(const std::vector<Complex>& points, EscapeResult& result, const unsigned int maxIter, const double escapeSq) {
    const int N = static_cast<int>(points.size());
    const int groups = N / 4;
    std::vector<int>& iters = result.iters;
    std::vector<float>& magSq = result.magSq;

    #pragma omp parallel for schedule(dynamic, OMP_CHUNK / 4) default(none) shared(points, iters, magSq, maxIter, groups, escapeSq, OMP_CHUNK)
    for (int g = 0; g < groups; g++) {
        const double* src = &points[static_cast<size_t>(g) * 4].real;   // r0 i0 r1 i1 | r2 i2 r3 i3
        const __m256d a = _mm256_loadu_pd(src);
        const __m256d b = _mm256_loadu_pd(src + 4);
        const __m256d x0 = _mm256_permute4x64_pd(_mm256_unpacklo_pd(a, b), 0xD8);   // r0 r1 r2 r3
        const __m256d y0 = _mm256_permute4x64_pd(_mm256_unpackhi_pd(a, b), 0xD8);   // i0 i1 i2 i3

        const __m256d limit = _mm256_set1_pd(escapeSq);
        const __m256d one = _mm256_set1_pd(1.0);
        __m256d x = _mm256_setzero_pd(), y = _mm256_setzero_pd();
        __m256d x2 = _mm256_setzero_pd(), y2 = _mm256_setzero_pd();
        __m256d active = _mm256_castsi256_pd(_mm256_set1_epi64x(-1));   // lanes that have not escaped yet
        __m256d count = _mm256_setzero_pd();                            // the iteration number i, as a double
        __m256d foundIter = _mm256_set1_pd(-1.0);
        __m256d foundMag = _mm256_setzero_pd();

        for (unsigned int i = 0; i < maxIter; i++) {
            y  = _mm256_add_pd(_mm256_mul_pd(_mm256_add_pd(x, x), y), y0);
            x  = _mm256_add_pd(_mm256_sub_pd(x2, y2), x0);
            x2 = _mm256_mul_pd(x, x);
            y2 = _mm256_mul_pd(y, y);
            const __m256d s = _mm256_add_pd(x2, y2);
            const __m256d escaped = _mm256_and_pd(_mm256_cmp_pd(s, limit, _CMP_GT_OQ), active);
            foundIter = _mm256_blendv_pd(foundIter, count, escaped);
            foundMag  = _mm256_blendv_pd(foundMag, s, escaped);
            active = _mm256_andnot_pd(escaped, active);
            if (_mm256_movemask_pd(active) == 0) break;
            count = _mm256_add_pd(count, one);
        }

        alignas(32) double itersD[4], magD[4];
        _mm256_store_pd(itersD, foundIter);
        _mm256_store_pd(magD, foundMag);
        for (int k = 0; k < 4; k++) {
            iters[g * 4 + k] = static_cast<int>(itersD[k]);
            magSq[g * 4 + k] = static_cast<float>(magD[k]);
        }
    }

    for (int idx = groups * 4; idx < N; idx++)   // the last 0..3 points
        iteratePoint(points[idx], iters[idx], magSq[idx], maxIter, escapeSq);
}

#endif

}  // namespace

void CpuParallelIterationCalculator::calculate(const std::vector<Complex>& points, EscapeResult& result, const unsigned int maxIter, const double escapeRadius) const {
    const double escapeSq = escapeRadius * escapeRadius;
#ifdef HAVE_AVX2_PATH
    static const bool hasAvx2 = __builtin_cpu_supports("avx2");
    if (hasAvx2) {
        calculateAvx2(points, result, maxIter, escapeSq);
        return;
    }
#endif
    calculateScalar(points, result, maxIter, escapeSq);
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

    #pragma omp parallel for schedule(dynamic, OMP_CHUNK) default(none) shared(points, iters, magSq, maxIter, N, thresholdWhole, thresholdFraction, OMP_CHUNK)
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
