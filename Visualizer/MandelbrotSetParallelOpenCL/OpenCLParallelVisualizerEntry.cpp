#include <complex>
#include <opencv2/opencv.hpp>
#include <utility>
#include <vector>
#include <chrono>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <iomanip>

#include "OpenCLWrapper.h"
#include "FixedPointArithmetics.h"

using namespace std;
using namespace boost::multiprecision;

//const double RE_START = -2.0;
//const double RE_END = 1.0;
//const double IM_START = -1;
//const double IM_END = 1;
double RE_START = -0.153004885037500013708;
double RE_END = -0.152809695287500013708;
double IM_START = 1.039611370300000000002;
double IM_END = 1.039757762612500000002;
cpp_dec_float_100 RE_START_HP = RE_START;
cpp_dec_float_100 RE_END_HP = RE_END;
cpp_dec_float_100 IM_START_HP = IM_START;
cpp_dec_float_100 IM_END_HP = IM_END;
bool USE_HIGH_PRECISSION = false;

int MAX_ITER = 700;

//int IMAGE_WIDTH = 6144;
//int IMAGE_HEIGHT = 4096;
int IMAGE_WIDTH = 900;
int IMAGE_HEIGHT = 600;
int IMAGE_SIZE = IMAGE_WIDTH * IMAGE_HEIGHT;

int PALETTE_LENGTH = 256;

string OUTPUT_FILENAME = "./mandelbrot_set.png";

struct Color {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
};

class ColorPalette {
public:
    vector<Color> colors;
    double length;

    ColorPalette(vector<Color> colors, int length) {
        this->colors = move(colors);
        this->length = length;
    }

    Color fit(int val) {
        auto valAdj = (double)(val % (int)this->length);
        const int N = this->colors.size();
        const double STEP = this->length / (N - 1);
        Color* left = nullptr;
        Color* right = nullptr;
        double ratio;
        for (int i = 0; i < N; i++) {
            if (STEP * i > valAdj) {
                left = &this->colors[i - 1];
                right = &this->colors[i];
                ratio = (valAdj - STEP * (i - 1)) / STEP;
                break;
            }
        }
        if (left == nullptr || right == nullptr) {
            exit(2);
        }
        Color output{};
        output.red = (int)(left->red + (right->red - left->red) * ratio);
        output.green = (int)(left->green + (right->green - left->green) * ratio);
        output.blue = (int)(left->blue + (right->blue - left->blue) * ratio);
        return output;
    }
};

void createColorImage(Color* pixels) {
    cv::Mat image(IMAGE_HEIGHT, IMAGE_WIDTH, CV_8UC3);
    uchar* imageData = image.data;

    for (int y = 0; y < IMAGE_HEIGHT; ++y) {
        uchar* rowPtr = imageData + (IMAGE_HEIGHT - y - 1) * image.step;

        for (int x = 0; x < IMAGE_WIDTH; ++x) {
            int pixelIndex = y * IMAGE_WIDTH + x;
            Color pixel_value = pixels[pixelIndex];

            uchar* pixelPtr = rowPtr + x * 3;

            pixelPtr[0] = pixel_value.blue;
            pixelPtr[1] = pixel_value.green;
            pixelPtr[2] = pixel_value.red;
        }
    }

    cv::imwrite(OUTPUT_FILENAME, image);
}

double mapVal(double value, double inMin, double inMax, double outMin, double outMax) {
    return (value - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}
cpp_dec_float_100 mapVal(cpp_dec_float_100 value, cpp_dec_float_100 inMin, cpp_dec_float_100 inMax, cpp_dec_float_100 outMin, cpp_dec_float_100 outMax) {
    return (value - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

int complexPointIter(complex<double>& c) {
    complex<double> z(0, 0);
    for (int i = 0; i < MAX_ITER; i++) {
        z = z * z + c;
        if (abs(z) > 2) {
            return i;
        }
    }
    return -1;
}

void createMandelbrotSet() {
    auto* points = new Complex[IMAGE_HEIGHT * IMAGE_WIDTH];
    auto* iters = new int[IMAGE_HEIGHT * IMAGE_WIDTH];

    vector<Color> colors;

    Color c1{};
    c1.red = 7;
    c1.green = 6;
    c1.blue = 38;
    Color c2{};
    c2.red = 140;
    c2.green = 143;
    c2.blue = 213;

    colors.push_back(c1);
    colors.push_back(c2);
    colors.push_back(c1);
    ColorPalette palette(colors, PALETTE_LENGTH);

    auto start = chrono::high_resolution_clock::now();
    for (int i = 0; i < IMAGE_HEIGHT; i++) {
        double imaginaryPart = mapVal(i, 0, IMAGE_HEIGHT, IM_START, IM_END);
        for (int j = 0; j < IMAGE_WIDTH; j++) {
            double realPart = mapVal(j, 0, IMAGE_WIDTH, RE_START, RE_END);
            int idx = j + (i * IMAGE_WIDTH);
            points[idx] = { realPart, imaginaryPart };
        }
    }
    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);
    cout << "Pixel mapping: " << duration.count() << " ms\n\n";
    start = chrono::high_resolution_clock::now();

    calculateIters(points, iters, IMAGE_SIZE, MAX_ITER);

    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::milliseconds>(end - start);
    cout << "\nCalculating escape iteration: " << duration.count() << " ms" << endl;
    start = chrono::high_resolution_clock::now();

    delete[] points;

    auto* pixels = new Color[IMAGE_SIZE];

    for (int i = 0; i < IMAGE_SIZE; i++) {
        if (iters[i] == -1) {
            Color black{};
            black.red = 0;
            black.green = 0;
            black.blue = 0;
            pixels[i] = black;
        }
        else {
            pixels[i] = palette.fit(iters[i]);
        }
    }

    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::milliseconds>(end - start);
    cout << "Coloring: " << duration.count() << " ms" << endl;
    start = chrono::high_resolution_clock::now();

    delete[] iters;

    createColorImage(pixels);

    delete[] pixels;

    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::milliseconds>(end - start);
    cout << "Image building: " << duration.count() << " ms" << endl;

}

void convertToFixedPoint(const cpp_dec_float_100& num, unsigned int res[4]) {
    bool isNegative = false;
    cpp_dec_float_100 temp = cpp_dec_float_100(num);
    if (num < 0) {
        isNegative = true;
        temp = -temp;
    }
    cpp_dec_float_100 whole_part = floor(temp);
    cpp_dec_float_100 fractional_part = temp - whole_part;

    cpp_int whole_int = whole_part.convert_to<cpp_int>();
    unsigned int whole_part_int = whole_int.convert_to<unsigned int>();

    res[0] = whole_part_int;

    cpp_dec_float_100 scale = cpp_dec_float_100("79228162514264337593543950336");
    cpp_int fractional_int = (fractional_part * scale).convert_to<cpp_int>();

    for (int i = 3; i >= 1; --i) {
        res[i] = static_cast<unsigned int>(fractional_int & 0xFFFFFFFF);
        fractional_int >>= 32;
    }
    if (isNegative) {
        fpa::cmplFixed(res, res);
    }
}

void createMandelbrotSetHP() {
    auto* points = new ComplexHP[IMAGE_HEIGHT * IMAGE_WIDTH];
    auto* iters = new int[IMAGE_HEIGHT * IMAGE_WIDTH];

    vector<Color> colors;

    Color c1{};
    c1.red = 7;
    c1.green = 6;
    c1.blue = 38;
    Color c2{};
    c2.red = 140;
    c2.green = 143;
    c2.blue = 213;

    colors.push_back(c1);
    colors.push_back(c2);
    colors.push_back(c1);
    ColorPalette palette(colors, PALETTE_LENGTH);

    auto start = chrono::high_resolution_clock::now();
    for (int i = 0; i < IMAGE_HEIGHT; i++) {
        cpp_dec_float_100 imaginaryPart = mapVal(cpp_dec_float_100(i), cpp_dec_float_100(0), cpp_dec_float_100(IMAGE_HEIGHT), IM_START_HP, IM_END_HP);
        for (int j = 0; j < IMAGE_WIDTH; j++) {
            cpp_dec_float_100 realPart = mapVal(cpp_dec_float_100(j), cpp_dec_float_100(0), cpp_dec_float_100(IMAGE_WIDTH), RE_START_HP, RE_END_HP);
            int idx = j + (i * IMAGE_WIDTH);
            unsigned int* realPartFP = new unsigned int[4];
            unsigned int* imagPartFP = new unsigned int[4];
            convertToFixedPoint(realPart, realPartFP);
            convertToFixedPoint(imaginaryPart, imagPartFP);
            for (int k = 0; k < 4; k++) {
                points[idx].real[k] = realPartFP[k];
                points[idx].imag[k] = imagPartFP[k];
            }
        }
    }
    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);
    cout << "Pixel mapping: " << duration.count() << " ms\n\n";
    start = chrono::high_resolution_clock::now();

    calculateItersHighPrecision(points, iters, IMAGE_SIZE, MAX_ITER);

    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::milliseconds>(end - start);
    cout << "\nCalculating escape iteration: " << duration.count() << " ms" << endl;
    start = chrono::high_resolution_clock::now();

    delete[] points;

    auto* pixels = new Color[IMAGE_SIZE];

    for (int i = 0; i < IMAGE_SIZE; i++) {
        if (iters[i] == -1) {
            Color black{};
            black.red = 0;
            black.green = 0;
            black.blue = 0;
            pixels[i] = black;
        }
        else {
            pixels[i] = palette.fit(iters[i]);
        }
    }

    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::milliseconds>(end - start);
    cout << "Coloring: " << duration.count() << " ms" << endl;
    start = chrono::high_resolution_clock::now();

    delete[] iters;

    createColorImage(pixels);

    delete[] pixels;

    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::milliseconds>(end - start);
    cout << "Image building: " << duration.count() << " ms" << endl;

}

// Command line arguments:
// RE_START, RE_END, IM_START, IM_END, IMG_WIDTH, IMG_HEIGHT, MAX_ITER, OUTPUT_FILENAME
int main(int argc, char* argv[]) {
    if (argc > 1) {
        try {
            USE_HIGH_PRECISSION = stod(argv[1]);
        }
        catch (const invalid_argument& e) {
            return 1;
        }
        try {
            if (USE_HIGH_PRECISSION) {
                RE_START_HP = cpp_dec_float_100(argv[2]);
            }
            else {
                RE_START = stod(argv[2]);
            }
        }
        catch (const invalid_argument& e) {
            return 1;
        }
        try {
            if (USE_HIGH_PRECISSION) {
                RE_END_HP = cpp_dec_float_100(argv[3]);
            }
            else {
                RE_END = stod(argv[3]);
            }
        }
        catch (const invalid_argument& e) {
            return 1;
        }
        try {           
            if (USE_HIGH_PRECISSION) {
                IM_START_HP = cpp_dec_float_100(argv[4]);
            }
            else {
                IM_START = stod(argv[4]);
            }
        }
        catch (const invalid_argument& e) {
            return 1;
        }
        try {
            if (USE_HIGH_PRECISSION) {
                IM_END_HP = cpp_dec_float_100(argv[5]);
            }
            else {
                IM_END = stod(argv[5]);
            }
        }
        catch (const invalid_argument& e) {
            return 1;
        }
        try {
            OUTPUT_FILENAME = argv[6];
        }
        catch (const invalid_argument& e) {
            return 1;
        }
    }
    if (USE_HIGH_PRECISSION) {
        createMandelbrotSetHP();
    }
    else {
        createMandelbrotSet();
    }
}