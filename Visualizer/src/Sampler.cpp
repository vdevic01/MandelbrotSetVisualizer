#include "Sampler.h"

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

std::vector<ComplexHP> samplePointsFromComplexPlane(
    int imageHeight, int imageWidth,
    const fpa::uint imStart[fpa::FP_SIZE], const fpa::uint imEnd[fpa::FP_SIZE],
    const fpa::uint reStart[fpa::FP_SIZE], const fpa::uint reEnd[fpa::FP_SIZE],
    int samples)
{
    std::vector<ComplexHP> points(imageWidth * imageHeight * samples);

    fpa::uint imageHeightHP[fpa::FP_SIZE] = {static_cast<fpa::uint>(imageHeight), 0, 0, 0};
    fpa::uint imageWidthHP[fpa::FP_SIZE]  = {static_cast<fpa::uint>(imageWidth),  0, 0, 0};

    fpa::uint temp[fpa::FP_SIZE] = {};
    fpa::uint scaleImaginary[fpa::FP_SIZE] = {};
    fpa::subFixed(imEnd, imStart, temp);
    fpa::divFixed(temp, imageHeightHP, scaleImaginary);

    fpa::uint scaleReal[fpa::FP_SIZE] = {};
    fpa::subFixed(reEnd, reStart, temp);
    fpa::divFixed(temp, imageWidthHP, scaleReal);

    fpa::uint realPartFP[fpa::FP_SIZE];
    fpa::uint imagPartFP[fpa::FP_SIZE];

    #pragma omp parallel for default(none) shared(points, imageHeight, imageWidth, imStart, reStart, scaleImaginary, scaleReal, samples) private(realPartFP, imagPartFP)
    for (int i = 0; i < imageHeight; i++) {
        fpa::uint imaginaryPartBoundary[fpa::FP_SIZE] = {};
        fpa::uint iHP[fpa::FP_SIZE] = {};
        fpa::floatingToFixedPoint(static_cast<double>(i), iHP);
        fpa::mulCmplFixed(iHP, scaleImaginary, imaginaryPartBoundary);
        fpa::addFixed(imStart, imaginaryPartBoundary, imaginaryPartBoundary);

        fpa::uint realPartBoundary[fpa::FP_SIZE] = {};
        std::copy(reStart, reStart + fpa::FP_SIZE, realPartBoundary);

        for (int j = 0; j < imageWidth; j++) {
            for (int k = 0; k < samples; k++) {
                fpa::addFixed(realPartBoundary, scaleReal, realPartFP);
                fpa::randomFromRange(realPartBoundary, realPartFP, realPartFP);

                fpa::addFixed(imaginaryPartBoundary, scaleImaginary, imagPartFP);
                fpa::randomFromRange(imaginaryPartBoundary, imagPartFP, imagPartFP);

                const int idx = (j + i * imageWidth) * samples + k;
                std::copy(realPartFP, realPartFP + fpa::FP_SIZE, points[idx].real);
                std::copy(imagPartFP, imagPartFP + fpa::FP_SIZE, points[idx].imag);
            }
            fpa::addFixed(realPartBoundary, scaleReal, realPartBoundary);
        }
    }
    return points;
}
