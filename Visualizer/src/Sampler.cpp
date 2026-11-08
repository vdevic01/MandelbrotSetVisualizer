#include "Sampler.h"
#include "FixedPointCore.h"

#include <algorithm>
#include <stdexcept>
#include <string>

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

    fpa::uint imageHeightHP[S] = {static_cast<fpa::uint>(imageHeight)};
    fpa::uint imageWidthHP[S]  = {static_cast<fpa::uint>(imageWidth)};

    fpa::uint temp[S] = {};
    fpa::uint scaleImaginary[S] = {};
    fpa::core::sub<F>(imEnd, imStart, temp);
    fpa::core::div<F>(temp, imageHeightHP, scaleImaginary);

    fpa::uint scaleReal[S] = {};
    fpa::core::sub<F>(reEnd, reStart, temp);
    fpa::core::div<F>(temp, imageWidthHP, scaleReal);

    fpa::uint realPartFP[S];
    fpa::uint imagPartFP[S];

    #pragma omp parallel for default(none) shared(points, imageHeight, imageWidth, imStart, reStart, scaleImaginary, scaleReal, samples) private(realPartFP, imagPartFP)
    for (int i = 0; i < imageHeight; i++) {
        fpa::uint imaginaryPartBoundary[S] = {};
        fpa::uint iHP[S] = {};
        fpa::core::floatingToFixed<F>(static_cast<double>(i), iHP);
        fpa::core::mul<F>(iHP, scaleImaginary, imaginaryPartBoundary);
        fpa::core::add<F>(imStart, imaginaryPartBoundary, imaginaryPartBoundary);

        fpa::uint realPartBoundary[S] = {};
        std::copy(reStart, reStart + S, realPartBoundary);

        for (int j = 0; j < imageWidth; j++) {
            for (int k = 0; k < samples; k++) {
                fpa::core::add<F>(realPartBoundary, scaleReal, realPartFP);
                fpa::core::randomFromRange<F>(realPartBoundary, realPartFP, realPartFP);

                fpa::core::add<F>(imaginaryPartBoundary, scaleImaginary, imagPartFP);
                fpa::core::randomFromRange<F>(imaginaryPartBoundary, imagPartFP, imagPartFP);

                const size_t idx = (static_cast<size_t>(j) + static_cast<size_t>(i) * imageWidth) * samples + k;
                std::copy(realPartFP, realPartFP + S, points.real(idx));
                std::copy(imagPartFP, imagPartFP + S, points.imag(idx));
            }
            fpa::core::add<F>(realPartBoundary, scaleReal, realPartBoundary);
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
