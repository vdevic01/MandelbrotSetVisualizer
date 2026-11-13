#include "ColorManager.h"
#include "SmoothColoring.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

CyclicColorPalette::CyclicColorPalette(int imageSize, std::vector<Color> colors, int length, int samples, double colorSpread, double colorPhase)
    : ColorManager(imageSize, samples), colors(std::move(colors)), length(length),
      colorSpread(static_cast<float>(colorSpread)), colorPhase(static_cast<float>(colorPhase)) {}

// val is a position along the palette cycle. It does not have to be a whole number: the colour is
// interpolated between the two nearest palette entries.
static Color getColorFromPalette(double val, const std::vector<Color>& colors, double length) {
    double valAdj = std::fmod(val, length);
    if (valAdj < 0) valAdj = 0;
    const auto N = static_cast<int>(colors.size());
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

std::vector<Color> CyclicColorPalette::paint(const EscapeResult& escape, const double escapeRadius) const {
    std::vector<Color> pixels(this->imageSize);
    const std::vector<int>& iters = escape.iters;
    const std::vector<float>& magSq = escape.magSq;
    const float offset = smoothOffset(escapeRadius);
    const float spread = this->colorSpread;
    // Values below a tenth of the palette cycle are not stacked yet; leave them as they are.
    const float knee = static_cast<float>(this->length) * 0.1f;
    // The phase moves the start of the colouring along the palette; the lookup wraps around at the palette length.
    const float shift = this->colorPhase * static_cast<float>(this->length);
    #pragma omp parallel for default(none) shared(iters, magSq, pixels, offset, spread, knee, shift)
    for (int i = 0; i < this->imageSize; i++) {
        const int idx = i * this->samples;
        double r = 0, g = 0, b = 0;
        for (int k = 0; k < this->samples; k++) {
            if (iters[idx + k] >= 0) {   // negative: the sample never escaped and stays black
                const float value = spreadValue(smoothValue(iters[idx + k], magSq[idx + k], offset), spread, knee);
                const Color c = getColorFromPalette(value + shift, this->colors, this->length);
                r += c.red;
                g += c.green;
                b += c.blue;
            }
        }
        pixels[i] = {
            static_cast<unsigned char>(r / this->samples),
            static_cast<unsigned char>(g / this->samples),
            static_cast<unsigned char>(b / this->samples) };
    }
    return pixels;
}

HistogramColorPalette::HistogramColorPalette(int imageSize, std::vector<Color> colors, int length, int samples, double phase, double cycles)
    : ColorManager(imageSize, samples), colors(std::move(colors)), length(length), phase(phase), cycles(cycles) {}

// Histogram equalisation of the smooth values. Every escaped sample gets the share of all escaped samples
// that have a smaller smooth value (0..1); that share, and not the value itself, picks the colour. A range of
// values that holds many samples, such as the crowded area near the set, is stretched over a large part of the
// palette, and a range with few samples is squeezed. The palette is walked once from start to end; the phase
// shifts where that walk starts (the palette wraps around), which changes which colour goes where without
// shortening the palette. With several cycles the palette repeats that many times over the range.
std::vector<Color> HistogramColorPalette::paint(const EscapeResult& escape, const double escapeRadius) const {
    const std::vector<int>& iters = escape.iters;
    const std::vector<float>& magSq = escape.magSq;
    const float offset = smoothOffset(escapeRadius);
    const int total = this->imageSize * this->samples;

    std::vector<float> values(total);
    #pragma omp parallel for default(none) shared(iters, magSq, values, offset, total)
    for (int i = 0; i < total; i++) {
        values[i] = smoothValue(iters[i], magSq[i], offset);
    }

    float maxValue = 0.0f;
    for (const float v : values) maxValue = std::max(maxValue, v);

    // cdf[b] is the share of the escaped samples that are below the start of bin b (cdf[0] = 0, cdf[BINS] = 1).
    constexpr int BINS = 8192;
    const float binScale = maxValue > 0.0f ? static_cast<float>(BINS) / maxValue : 0.0f;
    std::vector<double> cdf(BINS + 1, 0.0);
    double escaped = 0;
    for (const float v : values) {
        if (v < 0.0f) continue;   // never escaped
        cdf[std::min(static_cast<int>(v * binScale), BINS - 1) + 1] += 1.0;
        escaped += 1.0;
    }
    for (int b = 0; b < BINS; b++) cdf[b + 1] += cdf[b];
    if (escaped > 0) for (double& c : cdf) c /= escaped;

    // Just below the palette length: a position equal to it would wrap around to the first colour.
    const double span = this->length * 0.9999;
    std::vector<Color> pixels(this->imageSize);
    #pragma omp parallel for default(none) shared(values, pixels, cdf, binScale, span)
    for (int i = 0; i < this->imageSize; i++) {
        const int idx = i * this->samples;
        double r = 0, g = 0, b = 0;
        for (int k = 0; k < this->samples; k++) {
            const float v = values[idx + k];
            if (v < 0.0f) continue;   // never escaped: black
            const float x = v * binScale;
            const int bin = std::min(static_cast<int>(x), BINS - 1);
            // Linear between the edges of the bin, so the colour changes continuously with the value.
            const double share = cdf[bin] + (cdf[bin + 1] - cdf[bin]) * std::min(static_cast<double>(x - bin), 1.0);
            const Color c = getColorFromPalette(std::fmod(share * this->cycles + this->phase, 1.0) * span, this->colors, this->length);
            r += c.red;
            g += c.green;
            b += c.blue;
        }
        pixels[i] = {
            static_cast<unsigned char>(r / this->samples),
            static_cast<unsigned char>(g / this->samples),
            static_cast<unsigned char>(b / this->samples) };
    }
    return pixels;
}
