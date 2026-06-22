#pragma once

#include <vector>

struct Color {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
};

class ColorManager {
public:
    [[nodiscard]] virtual std::vector<Color> paint(const std::vector<int>& iters) const = 0;
    virtual ~ColorManager() = default;
protected:
    ColorManager(int imageSize, int samples) : imageSize(imageSize), samples(samples) {}
    int imageSize;
    int samples;
};

class CyclicColorPalette : public ColorManager {
public:
    CyclicColorPalette(int imageSize, std::vector<Color> colors, int length, int samples);
    [[nodiscard]] std::vector<Color> paint(const std::vector<int>& iters) const override;
private:
    std::vector<Color> colors;
    double length;
};

class HistogramColorPalette : public ColorManager {
public:
    HistogramColorPalette(int imageSize, int maxIter, std::vector<Color> colors, int samples);
    [[nodiscard]] std::vector<Color> paint(const std::vector<int>& iters) const override;
private:
    static Color interpolateColor(Color& l, Color& r, double val);
    int maxIter;
    std::vector<Color> colors;
};
