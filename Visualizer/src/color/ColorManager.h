#pragma once

#include <vector>

#include "IterationCalculator.h"

struct Color {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
};

// paint() takes the escape result of every sample and the escape radius it was calculated with. The colour
// is picked from the smooth value of the sample (see SmoothColoring.h); a sample that never escaped is black.
class ColorManager {
public:
    [[nodiscard]] virtual std::vector<Color> paint(const EscapeResult& escape, double escapeRadius) const = 0;
    virtual ~ColorManager() = default;
protected:
    ColorManager(int imageSize, int samples) : imageSize(imageSize), samples(samples) {}
    int imageSize;
    int samples;
};

class CyclicColorPalette : public ColorManager {
public:
    // colorSpread: strength of the logarithmic spread of the colours (0 = linear, see spreadValue).
    CyclicColorPalette(int imageSize, std::vector<Color> colors, int length, int samples, double colorSpread = 0.0);
    [[nodiscard]] std::vector<Color> paint(const EscapeResult& escape, double escapeRadius) const override;
private:
    std::vector<Color> colors;
    double length;
    float colorSpread;
};

class HistogramColorPalette : public ColorManager {
public:
    HistogramColorPalette(int imageSize, int maxIter, std::vector<Color> colors, int samples);
    [[nodiscard]] std::vector<Color> paint(const EscapeResult& escape, double escapeRadius) const override;
private:
    static Color interpolateColor(Color& l, Color& r, double val);
    int maxIter;
    std::vector<Color> colors;
};
