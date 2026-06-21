#include "ColorManager.h"

#include <stdexcept>

#define PI 3.14159265358979323846

CyclicColorPalette::CyclicColorPalette(int imageSize, vector<Color> colors, int length, int samples) : ColorManager(imageSize, samples) {
    this->colors = move(colors);
    this->length = length;
}

Color getColorFromPalette(int val, const vector<Color>& colors, double length) {
    double valAdj = (double)(val % (int)length);
    const int N = colors.size();
    const double STEP = length / (N - 1);
    const Color* left = nullptr;
    const Color* right = nullptr;
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

vector<Color> CyclicColorPalette::paint(vector<int>& iters) const {
    vector<Color> pixels(this->imageSize);
    #pragma omp parallel for
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

HistogramColorPalette::HistogramColorPalette(int imageSize, int maxIter, vector<Color> colors, int samples) : ColorManager(imageSize, samples) {
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

vector<Color> HistogramColorPalette::paint(vector<int>& iters) const {
    vector<Color> pixels(this->imageSize);
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
    return pixels;
}


ExponentialColorPalette::ExponentialColorPalette(int imageSize, int maxIter, vector<Color> colors, int length, int samples) : ColorManager(imageSize, samples) {
    this->maxIter = maxIter;
    this->colors = colors;
    this->length = length;
}

void LAB2RGB(int L, int a, int b, unsigned char& R, unsigned char& G, unsigned char& B)
{
    float X, Y, Z, fX, fY, fZ;
    int RR, GG, BB;

    fY = pow((L + 16.0) / 116.0, 3.0);
    if (fY < 0.008856)
        fY = L / 903.3;
    Y = fY;

    if (fY > 0.008856)
        fY = powf(fY, 1.0 / 3.0);
    else
        fY = 7.787 * fY + 16.0 / 116.0;

    fX = a / 500.0 + fY;
    if (fX > 0.206893)
        X = powf(fX, 3.0);
    else
        X = (fX - 16.0 / 116.0) / 7.787;

    fZ = fY - b / 200.0;
    if (fZ > 0.206893)
        Z = powf(fZ, 3.0);
    else
        Z = (fZ - 16.0 / 116.0) / 7.787;

    X *= (0.950456 * 255);
    Y *= 255;
    Z *= (1.088754 * 255);

    RR = (int)(3.240479 * X - 1.537150 * Y - 0.498535 * Z + 0.5);
    GG = (int)(-0.969256 * X + 1.875992 * Y + 0.041556 * Z + 0.5);
    BB = (int)(0.055648 * X - 0.204043 * Y + 1.057311 * Z + 0.5);

    R = (unsigned char)(RR < 0 ? 0 : RR > 255 ? 255 : RR);
    G = (unsigned char)(GG < 0 ? 0 : GG > 255 ? 255 : GG);
    B = (unsigned char)(BB < 0 ? 0 : BB > 255 ? 255 : BB);

    //printf("Lab=(%f,%f,%f) ==> RGB(%f,%f,%f)\n",L,a,b,*R,*G,*B);
}

vector<Color> ExponentialColorPalette::paint(vector<int>& iters) const {
    vector<Color> pixels(this->imageSize);
    return pixels;
    const double S = 2.0;      // exponent
    //#pragma omp parallel for
    //for (int i = 0; i < this->imageSize; i++) {
    //    if (iters[i] == -1) {
    //        pixels[i] = Color{ 0,0,0 };
    //        continue;
    //    }
    //    double s = iters[i]*1.0 / this->maxIter;
    //    double v = 1.0 - pow(cos(PI * s), 2.0);
    //    
    //    double L_LCH = 75 - (75 * v);
    //    double C_LCH = 103 - (75 * v);
    //    double H_LCH = (int)pow(360 * s, 1.5) % 360;

    //    int L_LAB = (int)L_LCH;
    //    int A_LAB = (int)(cos(H_LCH * 0.01745329251) * C_LCH);
    //    int B_LAB = (int)(sin(H_LCH * 0.01745329251) * C_LCH);
    //    unsigned char R, G, B;

    //    LAB2RGB(L_LAB, A_LAB, B_LAB, R, G, B);

    //    pixels[i] = Color{ R, G, B };
    //}
}