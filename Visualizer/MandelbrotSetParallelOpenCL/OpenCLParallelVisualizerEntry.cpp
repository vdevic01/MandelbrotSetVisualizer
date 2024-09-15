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
cpp_dec_float_50 RE_START_HP = RE_START;
cpp_dec_float_50 RE_END_HP = RE_END;
cpp_dec_float_50 IM_START_HP = IM_START;
cpp_dec_float_50 IM_END_HP = IM_END;
bool USE_HIGH_PRECISSION = false;

int MAX_ITER = 400;

//int IMAGE_WIDTH = 6144;
//int IMAGE_HEIGHT = 4096;
int IMAGE_WIDTH = 900;
int IMAGE_HEIGHT = 600;
int IMAGE_SIZE = IMAGE_WIDTH * IMAGE_HEIGHT;

int PALETTE_LENGTH = 256;

string OUTPUT_FILENAME = "./mandelbrot_set.png";
vector<vector<Color>> palettes = {
    {   // Navy
        {10, 11, 48},
        {29, 73, 173},
        {34, 175, 245},
        {112, 241, 255},
        {86, 165, 214},
        {6, 6, 33},
        {71, 119, 173},
        {166, 240, 255},
        {47, 235, 235},
        {0, 82, 122},
        {10, 11, 48}
    },
    {   // Sunset
        {255, 94, 77},   // Soft red
        {255, 165, 0},   // Orange
        {255, 223, 0},   // Yellow
        {255, 136, 77},  // Coral
        {255, 78, 80},   // Bright red
        {255, 132, 89},  // Salmon
        {255, 241, 208}, // Peach
        {255, 183, 197}, // Light pink
        {255, 94, 77},   // Soft red
    },
    {   // Ocean
        {0, 34, 102},   // Deep blue
        {0, 51, 102},   // Navy
        {25, 100, 126}, // Blue-green
        {54, 151, 197}, // Sky blue
        {122, 197, 205},// Pale cyan
        {198, 224, 221},// Light teal
        {0, 34, 102},   // Deep blue
    },
    {   // Fire and Ashe
        {255, 69, 0},    // Red-orange
        {255, 140, 0},   // Dark orange
        {255, 215, 0},   // Gold
        {169, 169, 169}, // Dark gray
        {105, 105, 105}, // Dim gray
        {47, 79, 79},    // Dark slate gray
        {0, 0, 0},       // Black
        {255, 69, 0},    // Red-orange
    },
    {   // Twilight
        {25, 25, 112},   // Midnight blue
        {72, 61, 139},   // Dark slate blue
        {123, 104, 238}, // Medium slate blue
        {238, 130, 238}, // Violet
        {147, 112, 219}, // Medium purple
        {199, 21, 133},  // Medium violet red
        {255, 182, 193}, // Light pink
        {25, 25, 112},   // Midnight blue
    },
    {   // Garden
        {0, 128, 0},      // Green
        {46, 139, 87},    // Sea green
        {60, 179, 113},   // Medium sea green
        {173, 255, 47},   // Green-yellow
        {240, 255, 240},  // Honeydew
        {165, 42, 42},    // Brown
        {0, 128, 0},      // Green
    },
    {   // Pomegranate
        {165, 42, 42},    // Brown
        {188, 143, 143},  // Rosy brown
        {205, 92, 92},    // Indian red
        {139, 0, 0},      // Dark red
        {128, 0, 0},      // Maroon
        {244, 164, 96},   // Sandy brown
        {165, 42, 42},    // Brown
    },
    {   // Greyscale
        {255, 255, 255},
        {0, 0, 0},
        {255, 255, 255}
    }
};


ColorManager* colorManager = new CyclicColorPalette(IMAGE_SIZE, palettes[0], PALETTE_LENGTH);
//CyclicColorPalette colorManager(IMAGE_SIZE, colors2, PALETTE_LENGTH);
//HistogramColorPalette colorManager(IMAGE_SIZE, MAX_ITER, colors2);
//ExponentialColorPalette colorManager(IMAGE_SIZE, MAX_ITER, colors2, PALETTE_LENGTH);

void createColorImage(Color* pixels) {
    cv::Mat image(IMAGE_HEIGHT, IMAGE_WIDTH, CV_8UC3);
    uchar* imageData = image.data;

    for (int y = 0; y < IMAGE_HEIGHT; ++y) {
        uchar* rowPtr = imageData + (IMAGE_HEIGHT - y - 1) * image.step;

        for (int x = 0; x < IMAGE_WIDTH; ++x) {
            int pixelIndex = y * IMAGE_WIDTH + x;
            Color pixelValue = pixels[pixelIndex];

            uchar* pixelPtr = rowPtr + x * 3;

            pixelPtr[0] = pixelValue.blue;
            pixelPtr[1] = pixelValue.green;
            pixelPtr[2] = pixelValue.red;
        }
    }

    cv::imwrite(OUTPUT_FILENAME, image);
}

double mapVal(double value, double inMin, double inMax, double outMin, double outMax) {
    return (value - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}


void createMandelbrotSet() {
    auto* points = new Complex[IMAGE_HEIGHT * IMAGE_WIDTH];
    auto* iters = new int[IMAGE_HEIGHT * IMAGE_WIDTH];

    auto start = chrono::high_resolution_clock::now();
    auto startX = chrono::high_resolution_clock::now();
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

    colorManager->paint(iters, pixels);

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
    auto endX = chrono::high_resolution_clock::now();
    cout << "Total time: " << chrono::duration_cast<chrono::milliseconds>(endX - startX).count() << " ms" << endl;

}

void convertToFixedPoint(const cpp_dec_float_50& num, unsigned int res[4]) {
    cpp_dec_float_50 temp = num < 0 ? -num : num;
    cpp_int whole_int = floor(temp).convert_to<cpp_int>();
    cpp_dec_float_50 fractional_part = temp - cpp_dec_float_50(whole_int);
    res[0] = whole_int.convert_to<unsigned int>();

    cpp_dec_float_50 scale("79228162514264337593543950336");
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
    cpp_dec_float_50 zeroHP = 0;
    cpp_dec_float_50 scaleImaginary = (IM_END_HP - IM_START_HP) / cpp_dec_float_50(IMAGE_HEIGHT);
    cpp_dec_float_50 scaleReal = (RE_END_HP - RE_START_HP) / cpp_dec_float_50(IMAGE_WIDTH);

    unsigned int realPartFP[4];
    unsigned int imagPartFP[4];

    #pragma omp parallel for private(realPartFP, imagPartFP)
    for (int i = 0; i < IMAGE_HEIGHT; i++) {
        cpp_dec_float_50 imaginaryPart = IM_START_HP + cpp_dec_float_50(i) * scaleImaginary;
        for (int j = 0; j < IMAGE_WIDTH; j++) {
            cpp_dec_float_50 realPart = RE_START_HP + cpp_dec_float_50(j) * scaleReal;
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

    colorManager->paint(iters, pixels);

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
// HIGHG_PRECISION
// RE_START, RE_END, IM_START, IM_END,
// OUTPUT_FILENAME
// MAX_ITER
// PALETTE_LENGTH
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
                RE_START_HP = cpp_dec_float_50(argv[2]);
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
                RE_END_HP = cpp_dec_float_50(argv[3]);
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
                IM_START_HP = cpp_dec_float_50(argv[4]);
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
                IM_END_HP = cpp_dec_float_50(argv[5]);
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
        try {
            MAX_ITER = stod(argv[7]);
        }
        catch (const invalid_argument& e) {
            return 1;
        }
        try {
            PALETTE_LENGTH = stod(argv[8]);
            int paletteId = stod(argv[9]);
            if (paletteId < 0 || paletteId >= palettes.size()) {
                return 1;
            }
            //colorManager = new ExponentialColorPalette(IMAGE_SIZE, MAX_ITER, colors2, PALETTE_LENGTH);
            colorManager = new CyclicColorPalette(IMAGE_SIZE, palettes[paletteId], PALETTE_LENGTH);
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