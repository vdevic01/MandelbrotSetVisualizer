#include <complex>
#include <opencv2/opencv.hpp>
#include <utility>
#include <vector>
#include <chrono>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <iomanip>
#include <omp.h>

#include "OpenCLWrapper.h"
#include "FixedPointArithmetics.h"
#include "ColorManager.h"

using namespace std;
using namespace boost::multiprecision;

//double RE_START = -2.0;
//double RE_END = 1.0;
//double IM_START = -1;
//double IM_END = 1;
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

vector<Color> colors = {
        {7, 6, 38},
        {140, 143, 213},
        {7, 6, 38}
};
vector<Color> colors2 = {
        {10, 11, 48},
        {29, 73, 173},
        {34, 175, 245},
        {112, 241, 255},
        {86, 165, 214},
        {6, 6, 33},
        {71, 119, 173},
        {166, 240, 255},
        {47, 235, 235},
        {0, 82, 122}
};
CyclicColorPalette colorManager(IMAGE_SIZE, colors2, PALETTE_LENGTH);
//HistogramColorPalette colorManager(IMAGE_SIZE, MAX_ITER, colors2);
//ExponentialColorPalette colorManager(IMAGE_SIZE, MAX_ITER, colors2);

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

    colorManager.paint(iters, pixels);

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
    cpp_dec_float_100 temp = num < 0 ? -num : num;
    cpp_int whole_int = floor(temp).convert_to<cpp_int>();
    cpp_dec_float_100 fractional_part = temp - cpp_dec_float_100(whole_int);
    res[0] = whole_int.convert_to<unsigned int>();

    cpp_dec_float_100 scale("79228162514264337593543950336");
    cpp_int fractional_int = (fractional_part * scale).convert_to<cpp_int>();

    for (int i = 3; i >= 1; --i) {
        res[i] = static_cast<unsigned int>(fractional_int & 0xFFFFFFFF);
        fractional_int >>= 32;
    }

    if (num < 0) {
        fpa::cmplFixed(res, res);
    }
}

void createMandelbrotSetHP() {
    auto* points = new ComplexHP[IMAGE_HEIGHT * IMAGE_WIDTH];
    auto* iters = new int[IMAGE_HEIGHT * IMAGE_WIDTH];

    auto start = chrono::high_resolution_clock::now();
    cpp_dec_float_100 zeroHP = 0;
    cpp_dec_float_100 scaleImaginary = (IM_END_HP - IM_START_HP) / cpp_dec_float_100(IMAGE_HEIGHT);
    cpp_dec_float_100 scaleReal = (RE_END_HP - RE_START_HP) / cpp_dec_float_100(IMAGE_WIDTH);

    unsigned int realPartFP[4];
    unsigned int imagPartFP[4];

    #pragma omp parallel for private(realPartFP, imagPartFP)
    for (int i = 0; i < IMAGE_HEIGHT; i++) {
        cpp_dec_float_100 imaginaryPart = IM_START_HP + cpp_dec_float_100(i) * scaleImaginary;
        for (int j = 0; j < IMAGE_WIDTH; j++) {
            cpp_dec_float_100 realPart = RE_START_HP + cpp_dec_float_100(j) * scaleReal;
            int idx = j + (i * IMAGE_WIDTH);
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

    colorManager.paint(iters, pixels);

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