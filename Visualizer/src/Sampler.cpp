#include "Sampler.h"
#include "FixedPointCore.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include <algorithm>
#include <cstdlib>

static double fastRandomFromRange(double min, double max) {
    return min + (rand() / (RAND_MAX + 1.0)) * (max - min);
}

std::vector<Complex> samplePointsFromComplexPlane(
    int imageHeight, int imageWidth,
    double imStart, double imEnd,
    double reStart, double reEnd,
    int samples)
{
    std::vector<Complex> points(imageWidth * imageHeight * samples);

    const double scaleImaginary = (imEnd - imStart) / imageHeight;
    const double scaleReal      = (reEnd - reStart) / imageWidth;

    #pragma omp parallel for default(none) shared(points, imageHeight, imageWidth, imStart, scaleImaginary, reStart, scaleReal, samples)
    for (int i = 0; i < imageHeight; i++) {
        double imaginaryPartBoundary = imStart + i * scaleImaginary;
        double realPartBoundary = reStart;
        for (int j = 0; j < imageWidth; j++) {
            for (int k = 0; k < samples; k++) {
                double realPart = fastRandomFromRange(realPartBoundary, realPartBoundary + scaleReal);
                double imagPart = fastRandomFromRange(imaginaryPartBoundary, imaginaryPartBoundary + scaleImaginary);
                points[(j + i * imageWidth) * samples + k] = { realPart, imagPart };
            }
            realPartBoundary += scaleReal;
        }
    }
    return points;
}

namespace {

// High-precision sampling for one fraction size F (a compile-time constant, so the arithmetic is inlined).
template<int F>
HpPoints samplePointsHp(
    int imageHeight, int imageWidth,
    const fpa::uint* imStart, const fpa::uint* imEnd,
    const fpa::uint* reStart, const fpa::uint* reEnd,
    int samples)
{
    constexpr int S = fpa::WHOLE_PART + F;   // words per number

    HpPoints points;
    points.fractionWords = F;
    points.words.resize(static_cast<size_t>(imageWidth) * imageHeight * samples * 2 * S);

    // Pixel boundaries. Column j covers [cols[j], cols[j+1]) and row i covers [rows[i], rows[i+1]).
    // Each boundary is computed from the start as start + (index * range) / count, so its error is at most
    // a couple of resolution steps however many pixels there are. (Adding a truncated per-pixel step instead
    // would let the error grow with the pixel index, which distorts the image when a pixel is only a few
    // resolution steps wide.)
    auto boundaries = [](int count, const fpa::uint* start, const fpa::uint* end) {
        std::vector<fpa::uint> result(static_cast<size_t>(count + 1) * S);
        fpa::uint range[S] = {};
        fpa::core::sub<F>(end, start, range);
        fpa::uint countHP[S] = {static_cast<fpa::uint>(count)};
        for (int k = 0; k < count; k++) {
            fpa::uint indexHP[S] = {static_cast<fpa::uint>(k)};
            fpa::uint product[S] = {};
            fpa::uint offset[S] = {};
            fpa::core::mul<F>(indexHP, range, product);
            fpa::core::div<F>(product, countHP, offset);
            fpa::core::add<F>(start, offset, &result[static_cast<size_t>(k) * S]);
        }
        std::copy(end, end + S, &result[static_cast<size_t>(count) * S]);   // the last boundary is the end itself
        return result;
    };
    const std::vector<fpa::uint> cols = boundaries(imageWidth, reStart, reEnd);
    const std::vector<fpa::uint> rows = boundaries(imageHeight, imStart, imEnd);

    #pragma omp parallel for default(none) shared(points, cols, rows, imageHeight, imageWidth, samples)
    for (int i = 0; i < imageHeight; i++) {
        fpa::uint realPart[S];
        fpa::uint imagPart[S];
        const fpa::uint* rowLow = &rows[static_cast<size_t>(i) * S];
        const fpa::uint* rowHigh = rowLow + S;

        for (int j = 0; j < imageWidth; j++) {
            const fpa::uint* colLow = &cols[static_cast<size_t>(j) * S];
            const fpa::uint* colHigh = colLow + S;
            for (int k = 0; k < samples; k++) {
                fpa::core::randomFromRange<F>(colLow, colHigh, realPart);
                fpa::core::randomFromRange<F>(rowLow, rowHigh, imagPart);

                const size_t idx = (static_cast<size_t>(j) + static_cast<size_t>(i) * imageWidth) * samples + k;
                std::copy(realPart, realPart + S, points.real(idx));
                std::copy(imagPart, imagPart + S, points.imag(idx));
            }
        }
    }
    return points;
}

}  // namespace

HpPoints samplePointsFromComplexPlane(
    int imageHeight, int imageWidth,
    const fpa::uint* imStart, const fpa::uint* imEnd,
    const fpa::uint* reStart, const fpa::uint* reEnd,
    int samples, int fractionWords)
{
    switch (fractionWords) {
        case 1: return samplePointsHp<1>(imageHeight, imageWidth, imStart, imEnd, reStart, reEnd, samples);
        case 2: return samplePointsHp<2>(imageHeight, imageWidth, imStart, imEnd, reStart, reEnd, samples);
        case 3: return samplePointsHp<3>(imageHeight, imageWidth, imStart, imEnd, reStart, reEnd, samples);
        case 4: return samplePointsHp<4>(imageHeight, imageWidth, imStart, imEnd, reStart, reEnd, samples);
        case 5: return samplePointsHp<5>(imageHeight, imageWidth, imStart, imEnd, reStart, reEnd, samples);
        case 6: return samplePointsHp<6>(imageHeight, imageWidth, imStart, imEnd, reStart, reEnd, samples);
        default:
            throw std::invalid_argument("Unsupported fraction size: " + std::to_string(fractionWords) + " words");
    }
}
