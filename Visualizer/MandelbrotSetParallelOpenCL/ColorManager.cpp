#include <ColorManager.h>

#include <stdexcept>

CyclicColorPalette::CyclicColorPalette(int imageSize, vector<Color> colors, int length) : ColorManager(imageSize) {
    this->colors = move(colors);
    this->length = length;
}

Color getColorFromPalette(int val, vector<Color>& colors, double length) {
    double valAdj = (double)(val % (int)length);
    const int N = colors.size();
    const double STEP = length / (N - 1);
    Color* left = nullptr;
    Color* right = nullptr;
    double ratio;
    for (int i = 0; i < N; i++) {
        if (STEP * i > valAdj) {
            left = &colors[i - 1];
            right = &colors[i];
            ratio = (valAdj - STEP * (i - 1)) / STEP;
            break;
        }
    }
    if (left == nullptr || right == nullptr) {
        throw std::runtime_error("Something went wrong - colors are not defined");
    }

    Color output{};
    output.red = (int)(left->red + (right->red - left->red) * ratio);
    output.green = (int)(left->green + (right->green - left->green) * ratio);
    output.blue = (int)(left->blue + (right->blue - left->blue) * ratio);
    return output;
}

void CyclicColorPalette::paint(int* iters, Color pixels[]) {
    #pragma omp parallel for
    for (int i = 0; i < this->imageSize; i++) {
        if (iters[i] == -1) {
            Color black{};
            black.red = 0;
            black.green = 0;
            black.blue = 0;
            pixels[i] = black;
        }
        else {
            pixels[i] = getColorFromPalette(iters[i], this->colors, this->length);
        }
    }
}

HistogramColorPalette::HistogramColorPalette(int imageSize, int maxIter, vector<Color> colors) : ColorManager(imageSize) {
    this->maxIter = maxIter;
    this->colors = colors;
}

Color HistogramColorPalette::interpolateColor(Color& lCol, Color& rCol, double val) {
    unsigned char r, g, b;
    r = static_cast<unsigned char>(lCol.red + ((rCol.red - lCol.red) * val));
    g = static_cast<unsigned char>(lCol.green + ((rCol.green - lCol.green) * val));
    b = static_cast<unsigned char>(lCol.blue + ((rCol.blue - lCol.blue) * val));
    return Color{ r,g,b };
}

void HistogramColorPalette::paint(int* iters, Color pixels[]) {
    vector<int> numItersPerPixel(this->maxIter + 1, 0);
    #pragma omp parallel for
    for (int i = 0; i < this->imageSize; i++) {
        int val = iters[i] == -1 ? this->maxIter : iters[i];
        numItersPerPixel[val]++;
    }
    vector<double> hues(this->imageSize, 0);
    #pragma omp parallel for
    for (int i = 0; i < this->imageSize; i++) {
        double hue = 0;
        for (int j = 0; j < iters[i]; j++) {
            hue += numItersPerPixel[j] * 1.0 / this->imageSize;
        }
        Color c1{ 7,6,38 };
        Color c2{ 140, 143, 213 };
        if (iters[i] == -1) {
            pixels[i] = Color{ 0,0,0 };
        }
        else {
            //pixels[i] = this->interpolateColor(c1, c2, hue);
            int length = 1000;
            pixels[i] = getColorFromPalette(hue * length, this->colors, length);
        }
    }
}


ExponentialColorPalette::ExponentialColorPalette(int imageSize, int maxIter, vector<Color> colors, int length) : ColorManager(imageSize) {
    this->maxIter = maxIter;
    this->colors = colors;
    this->length = length;
}

void ExponentialColorPalette::paint(int* iters, Color pixels[]) {
    const double S = 2.0;      // exponent
    #pragma omp parallel for
    for (int i = 0; i < this->imageSize; i++) {
        if (iters[i] == -1) {
            pixels[i] = Color{ 0,0,0 };
            continue;
        }
        double scaledIter = pow(iters[i] * 1.0 / this->maxIter, S);
        int v = (int)(scaledIter * (this->length - 1));
        pixels[i] = getColorFromPalette(v, this->colors, this->length);
    }
}