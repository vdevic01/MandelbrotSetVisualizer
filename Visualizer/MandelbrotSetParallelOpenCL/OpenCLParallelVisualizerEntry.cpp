#include <complex>
#include <opencv2/opencv.hpp>
#include <utility>
#include <vector>
#include <chrono>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <iomanip>
#include <omp.h>
#include <iomanip>

#include "OpenCLWrapper.h"
#include "FixedPointArithmetics.h"
#include "ColorManager.h"

using namespace std;
using namespace boost::multiprecision;

struct MandelbrotConfig {
    bool useHighPrecision = false;
    double reStart = -0.153004885037500013708;
    double reEnd = -0.152809695287500013708;
    double imStart = 1.039611370300000000002;
    double imEnd = 1.039757762612500000002;
    cpp_dec_float_50 reStartHP = reStart;
    cpp_dec_float_50 reEndHP = reEnd;
    cpp_dec_float_50 imStartHP = imStart;
    cpp_dec_float_50 imEndHP = imEnd;
    int maxIter = 400;
    int imageWidth = 900;
    int imageHeight = 600;
    string outputFilename = "./mandelbrot_set.png";
    int paletteLength = 256;
    int paletteId = 0;
};

const std::vector<std::vector<Color>> palettes = {
    // Navy
    {{10, 11, 48}, {29, 73, 173}, {34, 175, 245}, {112, 241, 255}, {86, 165, 214}, {6, 6, 33}, {71, 119, 173}, {166, 240, 255}, {47, 235, 235}, {0, 82, 122}, {10, 11, 48}},
    // Sunset
    {{255, 94, 77}, {255, 165, 0}, {255, 223, 0}, {255, 136, 77}, {255, 78, 80}, {255, 132, 89}, {255, 241, 208}, {255, 183, 197}, {255, 94, 77}},
    // Ocean
    {{0, 34, 102}, {0, 51, 102}, {25, 100, 126}, {54, 151, 197}, {122, 197, 205}, {198, 224, 221}, {0, 34, 102}},
    // Fire and Ash
    {{255, 69, 0}, {255, 140, 0}, {255, 215, 0}, {169, 169, 169}, {105, 105, 105}, {47, 79, 79}, {0, 0, 0}, {255, 69, 0}},
    // Twilight
    {{25, 25, 112}, {72, 61, 139}, {123, 104, 238}, {238, 130, 238}, {147, 112, 219}, {199, 21, 133}, {255, 182, 193}, {25, 25, 112}},
    // Garden
    {{0, 128, 0}, {46, 139, 87}, {60, 179, 113}, {173, 255, 47}, {240, 255, 240}, {165, 42, 42}, {0, 128, 0}},
    // Pomegranate
    {{165, 42, 42}, {188, 143, 143}, {205, 92, 92}, {139, 0, 0}, {128, 0, 0}, {244, 164, 96}, {165, 42, 42}},
    // Greyscale
    {{255, 255, 255}, {0, 0, 0}, {255, 255, 255}}
};

class ScopedTimer {
public:
    ScopedTimer(const string& name) :
        m_name(name),
        m_start(std::chrono::high_resolution_clock::now())
    {
    }

    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - m_start);

        const int NAME_COLUMN_WIDTH = 35;

        std::cout << std::left
            << std::setw(NAME_COLUMN_WIDTH)
            << m_name + ":"
            << duration.count() << " ms\n";

        // Optional: Remove the separator line if you only want timing output.
         std::cout << "==========================================\n";
    }

private:
    string m_name;
    chrono::high_resolution_clock::time_point m_start;
};

cv::Mat createColorImage(vector<Color>& pixels, const int width, const int height) {
    cv::Mat image(height, width, CV_8UC3);
    uchar* imageData = image.data;

    for (int y = 0; y < height; ++y) {
        uchar* rowPtr = imageData + (height - y - 1) * image.step;

        for (int x = 0; x < width; ++x) {
            int pixelIndex = y * width + x;
            Color pixelValue = pixels[pixelIndex];

            uchar* pixelPtr = rowPtr + x * 3;

            pixelPtr[0] = pixelValue.blue;
            pixelPtr[1] = pixelValue.green;
            pixelPtr[2] = pixelValue.red;
        }
    }
    return image;
}


double mapVal(double value, double inMin, double inMax, double outMin, double outMax) {
    return (value - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

vector<Complex> samplePointsFromComplexPlane(const int imageHeight, const int imageWidth, const double imStart, const double imEnd, const double reStart, const double reEnd) {
    vector<Complex> points(imageWidth * imageHeight);
    for (int i = 0; i < imageHeight; i++) {
        double imaginaryPart = mapVal(i, 0, imageHeight, imStart, imEnd);
        for (int j = 0; j < imageWidth; j++) {
            double realPart = mapVal(j, 0, imageWidth, reEnd, reStart);
            int idx = j + (i * imageWidth);
            points[idx] = { realPart, imaginaryPart };
        }
    }
    return points;
}
vector<ComplexHP> samplePointsFromComplexPlane(const int imageHeight, const int imageWidth, const cpp_dec_float_50 imStart, const cpp_dec_float_50 imEnd, const cpp_dec_float_50 reStart, const cpp_dec_float_50 reEnd) {
    vector<ComplexHP> points(imageWidth * imageHeight);
    cpp_dec_float_50 zeroHP = 0;
    cpp_dec_float_50 scaleImaginary = (imEnd - imStart) / cpp_dec_float_50(imageHeight);
    cpp_dec_float_50 scaleReal = (reEnd - reStart) / cpp_dec_float_50(imageWidth);

    unsigned int realPartFP[4];
    unsigned int imagPartFP[4];

    #pragma omp parallel for private(realPartFP, imagPartFP)
    for (int i = 0; i < imageHeight; i++) {
        cpp_dec_float_50 imaginaryPart = imStart + cpp_dec_float_50(i) * scaleImaginary;
        for (int j = 0; j < imageWidth; j++) {
            cpp_dec_float_50 realPart = reStart + cpp_dec_float_50(j) * scaleReal;
            int idx = j + (i * imageWidth);
            fpa::convertToFixedPoint(realPart, realPartFP);
            fpa::convertToFixedPoint(imaginaryPart, imagPartFP);
            for (int k = 0; k < 4; k++) {
                points[idx].real[k] = realPartFP[k];
                points[idx].imag[k] = imagPartFP[k];
            }
        }
    }
}

void createMandelbrotSet(const MandelbrotConfig& config, const ColorManager& colorManager) {
    const int imageSize = config.imageHeight * config.imageWidth;

    vector<Complex> points;
    {
        ScopedTimer timer("Pixel mapping");
        points = samplePointsFromComplexPlane(config.imageHeight, config.imageWidth, config.imStart, config.imEnd, config.reStart, config.reEnd);

    }

    vector<int> iters(imageSize);
    {
        ScopedTimer timer("\nCalculating escape iterations");
        calculateIters(points, iters, imageSize, config.maxIter);
    }

    vector<Color> pixels;
    {
        ScopedTimer timer("Coloring");
        pixels = colorManager.paint(iters);
    }

    {
        ScopedTimer timer("Image generation");
        cv::Mat image = createColorImage(pixels, config.imageWidth, config.imageHeight);
        cv::imwrite(config.outputFilename, image);
    }
}

void createMandelbrotSetHP(const MandelbrotConfig& config, const ColorManager& colorManager) {
    const int imageSize = config.imageHeight * config.imageWidth;

    vector<ComplexHP> points;
    {
        ScopedTimer timer("Pixel mapping");
        points = samplePointsFromComplexPlane(config.imageHeight, config.imageWidth, config.imStartHP, config.imEndHP, config.reStartHP, config.reEndHP);
    }

    vector<int> iters(imageSize);
    {
        ScopedTimer timer("\nCalculating escape iteration");       
        calculateItersHighPrecision(points, iters, imageSize, config.maxIter);
    }

    vector<Color> pixels;
    {
        ScopedTimer timer("Coloring");
        pixels = colorManager.paint(iters);
    }

    {
        ScopedTimer timer("Image generation");
        cv::Mat image = createColorImage(pixels, config.imageWidth, config.imageHeight);
        cv::imwrite(config.outputFilename, image);
    }
}

optional<MandelbrotConfig> parseCommandLine(int argc, char* argv[]) {
    MandelbrotConfig config;
    if (argc > 1) {
        try {
            config.useHighPrecision = (std::stod(argv[1]) != 0.0);
            if (config.useHighPrecision) {
                config.reStartHP = cpp_dec_float_50(argv[2]);
                config.reEndHP = cpp_dec_float_50(argv[3]);
                config.imStartHP = cpp_dec_float_50(argv[4]);
                config.imEndHP = cpp_dec_float_50(argv[5]);
            }
            else {
                config.reStart = stod(argv[2]);
                config.reEnd = stod(argv[3]);
                config.imStart = stod(argv[4]);
                config.imEnd = stod(argv[5]);
            }
            config.outputFilename = argv[6];
            config.maxIter = stod(argv[7]);
            config.paletteLength = stod(argv[8]);
            int paletteId = stod(argv[9]);
            if (paletteId < 0 || paletteId >= palettes.size()) {
                cerr << "Error: Invalid palette ID. Must be between 0 and " << palettes.size() - 1 << ".\n";
                return nullopt;
            }
            config.paletteId = paletteId;
        }
        catch (const exception& e) {
            cerr << "Error parsing arguments: " << e.what() << "\n";
            cerr << "Usage: " << argv[0] << " <USE_HIGH_PRECISION(0/1)> <RE_START> <RE_END> <IM_START> <IM_END> <OUTPUT_FILENAME> <MAX_ITER> <PALETTE_LENGTH> <PALETTE_ID>\n";
            return nullopt;
        }
    }
    return config;
}


// Command line arguments:
// HIGHG_PRECISION
// RE_START, RE_END, IM_START, IM_END,
// OUTPUT_FILENAME
// MAX_ITER
// PALETTE_LENGTH
int main(int argc, char* argv[]) {
    auto configOpt = parseCommandLine(argc, argv);
    if (!configOpt) {
        return 1;
    }
    const MandelbrotConfig config = configOpt.value();
    const CyclicColorPalette colorManager(config.imageHeight * config.imageWidth, palettes[0], config.paletteLength);

    cout << "==========================================\n";
    {
        ScopedTimer timer("Total time");
        if (config.useHighPrecision) {
            createMandelbrotSetHP(config, colorManager);
        }
        else {
            createMandelbrotSet(config, colorManager);
        }
    }
}