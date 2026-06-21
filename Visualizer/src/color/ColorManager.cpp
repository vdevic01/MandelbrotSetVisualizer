#include "ColorManager.h"

#include <cmath>
#include <stdexcept>

CyclicColorPalette::CyclicColorPalette(int imageSize, std::vector<Color> colors, int length, int samples)
    : ColorManager(imageSize, samples), colors(std::move(colors)), length(length) {}

static Color getColorFromPalette(int val, const std::vector<Color>& colors, double length) {
    double valAdj = static_cast<double>(val % static_cast<int>(length));
    const int N = static_cast<int>(colors.size());
    const double STEP = length / (N - 1);
    const Color* left = nullptr;
    const Color* right = nullptr;
    double ratio = 0;
    for (int i = 0; i < N; i++) {
        if (STEP * i > valAdj) {
            left  = &colors[i - 1];
            right = &colors[i];
            ratio = (valAdj - STEP * (i - 1)) / STEP;
            break;
        }
    }
    if (!left || !right)
        throw std::runtime_error("Something went wrong - colors are not defined");

    return Color{
        static_cast<unsigned char>(left->red   + (right->red   - left->red)   * ratio),
        static_cast<unsigned char>(left->green + (right->green - left->green) * ratio),
        static_cast<unsigned char>(left->blue  + (right->blue  - left->blue)  * ratio)
    };
}

std::vector<Color> CyclicColorPalette::paint(const std::vector<int>& iters) const {
    std::vector<Color> pixels(this->imageSize);
    #pragma omp parallel for default(none) shared(iters, pixels)
    for (int i = 0; i < this->imageSize; i++) {
        int idx = i * this->samples;
        double averageColor[3] = { 0, 0, 0 };
        for (int k = 0; k < this->samples; k++) {
            if (iters[idx + k] != -1) {
                Color c = getColorFromPalette(iters[idx + k], this->colors, this->length);
                averageColor[0] += c.red;
                averageColor[1] += c.green;
                averageColor[2] += c.blue;
            }
        }
        averageColor[0] /= this->samples;
        averageColor[1] /= this->samples;
        averageColor[2] /= this->samples;

        pixels[i] = {
            static_cast<unsigned char>(averageColor[0]),
            static_cast<unsigned char>(averageColor[1]),
            static_cast<unsigned char>(averageColor[2]) };
    }
    return pixels;
}

HistogramColorPalette::HistogramColorPalette(int imageSize, int maxIter, std::vector<Color> colors, int samples)
    : ColorManager(imageSize, samples), maxIter(maxIter), colors(std::move(colors)) {}

Color HistogramColorPalette::interpolateColor(Color& lCol, Color& rCol, double val) {
    return Color{
        static_cast<unsigned char>(lCol.red   + (rCol.red   - lCol.red)   * val),
        static_cast<unsigned char>(lCol.green + (rCol.green - lCol.green) * val),
        static_cast<unsigned char>(lCol.blue  + (rCol.blue  - lCol.blue)  * val)
    };
}

std::vector<Color> HistogramColorPalette::paint(const std::vector<int>& iters) const {
    std::vector<Color> pixels(this->imageSize);
    std::vector<int> numItersPerPixel(this->maxIter + 1, 0);
    #pragma omp parallel for default(none) shared(iters, numItersPerPixel)
    for (int i = 0; i < this->imageSize; i++) {
        int val = iters[i] == -1 ? this->maxIter : iters[i];
        numItersPerPixel[val]++;
    }
    std::vector<double> hues(this->imageSize, 0);
    #pragma omp parallel for default(none) shared(iters, pixels, numItersPerPixel, hues)
    for (int i = 0; i < this->imageSize; i++) {
        double hue = 0;
        for (int j = 0; j < iters[i]; j++) {
            hue += numItersPerPixel[j] * 1.0 / this->imageSize;
        }
        if (iters[i] == -1) {
            pixels[i] = Color{ 0, 0, 0 };
        } else {
            constexpr int length = 1000;
            pixels[i] = getColorFromPalette(static_cast<int>(hue * length), this->colors, length);
        }
    }
    return pixels;
}
