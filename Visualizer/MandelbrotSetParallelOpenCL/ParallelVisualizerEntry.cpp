#include <complex>
#include <utility>
#include <vector>
#include <chrono>
#include <iomanip>
#include <omp.h>
#include <iomanip>
#include <iostream>

#include "IterationCalculator.h"
#include "OpenCLIterationCalculator.h"
#include "CUDAIterationCalculator.h"
#include "FixedPointArithmetics.h"
#include "ColorManager.h"
#include "Palettes.h"
#include "fpng.h"

using namespace std;

struct MandelbrotConfig {
    bool useHighPrecision = false;
    float reStart = -0.153004885037500013708;
    float reEnd = -0.152809695287500013708;
    float imStart = 1.039611370300000000002;
    float imEnd = 1.039757762612500000002;
    fpa::uint reStartHP[fpa::FP_SIZE];
    fpa::uint reEndHP[fpa::FP_SIZE];
    fpa::uint imStartHP[fpa::FP_SIZE];
    fpa::uint imEndHP[fpa::FP_SIZE];
    int samples = 10;
    int maxIter = 1000;
    int imageWidth = 900;
    int imageHeight = 600;
    string outputFilename = "./mandelbrot_set.png";
    int paletteLength = 256;
    int paletteId = 0;
};

class ScopedTimer {
public:
    ScopedTimer(const string& name) :
        m_name(name),
        m_start(chrono::high_resolution_clock::now())
    {
    }

    ~ScopedTimer() {
        auto end = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(end - m_start);

        const int NAME_COLUMN_WIDTH = 36;

        cout << left
            << setw(NAME_COLUMN_WIDTH)
            << m_name + ":"
            << duration.count() << " ms\n";

        cout << "===========================================\n";
    }

private:
    string m_name;
    chrono::high_resolution_clock::time_point m_start;
};

void saveColorImageToPng(const vector<Color>& pixels, int width, int height, const string& filename) {
    vector<unsigned char> rgb_data(width * height * 3);

    for (int y = 0; y < height; ++y) {
        int target_y = height - 1 - y;

        for (int x = 0; x < width; ++x) {
            int pixelIndex = y * width + x;
            const Color& pixelValue = pixels[pixelIndex];

            int dataIndex = (target_y * width + x) * 3;

            rgb_data[dataIndex + 0] = pixelValue.red;
            rgb_data[dataIndex + 1] = pixelValue.green;
            rgb_data[dataIndex + 2] = pixelValue.blue;
        }
    }

    if (!fpng::fpng_encode_image_to_file(filename.c_str(), rgb_data.data(), width, height, 3)) {
        throw runtime_error("Failed to write PNG file: " + filename);
    }
}


double fastRandomFromRange(const double& min, const double& max) {
    const double r = rand() / (RAND_MAX + 1.0);
    return min + r * (max - min);
}

vector<Complex> samplePointsFromComplexPlane(const int imageHeight, const int imageWidth, const float imStart, const float imEnd, const float reStart, const float reEnd, const int samples) {
    vector<Complex> points(imageWidth * imageHeight * samples);

    const float scaleImaginary = (imEnd - imStart) / imageHeight;
    const float scaleReal = (reEnd - reStart) / imageWidth;

    #pragma omp parallel for
    for (int i = 0; i < imageHeight; i++) {
        float imaginaryPartBoundary = imStart + i * scaleImaginary;
        float realPartBoundary = reStart;
        for (int j = 0; j < imageWidth; j++) {
            for (int k = 0; k < samples; k++) {
                float realPart = fastRandomFromRange(realPartBoundary, realPartBoundary + scaleReal);
                float imaginaryPart = fastRandomFromRange(imaginaryPartBoundary, imaginaryPartBoundary + scaleImaginary);
                const int idx = (j + i * imageWidth) * samples + k;
                points[idx] = { realPart, imaginaryPart };
            }
            realPartBoundary += scaleReal;
        }
    }
    return points;
}

vector<ComplexHP> samplePointsFromComplexPlane(
    const int imageHeight, const int imageWidth,
    const fpa::uint imStart[fpa::FP_SIZE], const fpa::uint imEnd[fpa::FP_SIZE],
    const fpa::uint reStart[fpa::FP_SIZE], const fpa::uint reEnd[fpa::FP_SIZE],
    const int samples) {

    vector<ComplexHP> points(imageWidth * imageHeight * samples);

    fpa::uint imageHeightHP[fpa::FP_SIZE] = {imageHeight, 0, 0, 0};
    fpa::uint imageWidthHP[fpa::FP_SIZE] = {imageWidth, 0, 0, 0};

    fpa::uint temp[fpa::FP_SIZE] = {};
    fpa::subFixed(imEnd, imStart, temp);
    fpa::uint scaleImaginary[fpa::FP_SIZE] = {};
    fpa::divFixed(temp, imageHeightHP, scaleImaginary);

    fpa::subFixed(reEnd, reStart, temp);
    fpa::uint scaleReal[fpa::FP_SIZE] = {};
    fpa::divFixed(temp, imageWidthHP, scaleReal);

    fpa::uint realPartFP[fpa::FP_SIZE];
    fpa::uint imagPartFP[fpa::FP_SIZE];

    #pragma omp parallel for private(realPartFP, imagPartFP)
    for (int i = 0; i < imageHeight; i++) {
        fpa::uint imaginaryPartBoundary[fpa::FP_SIZE] = {};
        fpa::uint iHP[fpa::FP_SIZE] = {};
        fpa::floatingToFixedPoint(static_cast<double>(i), iHP);
        fpa::mulCmplFixed(iHP, scaleImaginary, imaginaryPartBoundary);
        fpa::addFixed(imStart, imaginaryPartBoundary, imaginaryPartBoundary);

        fpa::uint realPartBoundary[fpa::FP_SIZE] = {};
        copy(reStart, reStart + fpa::FP_SIZE, realPartBoundary);

        for (int j = 0; j < imageWidth; j++) {
            for (int k = 0; k < samples; k++) {
                fpa::addFixed(realPartBoundary, scaleReal, realPartFP);
                fpa::randomFromRange(realPartBoundary, realPartFP, realPartFP);

                fpa::addFixed(imaginaryPartBoundary, scaleImaginary, imagPartFP);
                fpa::randomFromRange(imaginaryPartBoundary, imagPartFP, imagPartFP);

                const int idx = (j + i * imageWidth) * samples + k;

                for (int m = 0; m < fpa::FP_SIZE; m++) {
                    points[idx].real[m] = realPartFP[m];
                    points[idx].imag[m] = imagPartFP[m];
                }
            }
            fpa::addFixed(realPartBoundary, scaleReal, realPartBoundary);
        }
    }

    return points;
}

template<typename T_ComplexPointType>
void createMandelbrotSet(const MandelbrotConfig& config, const ColorManager& colorManager, const IterationCalculator& iterCalculator) {
    cout << "===========================================\n";
    ScopedTimer total_timer("Total generation time");

    const int imageSize = config.imageHeight * config.imageWidth;

    vector<T_ComplexPointType> points;
    {
        ScopedTimer timer("Pixel mapping");
        if constexpr (is_same_v<T_ComplexPointType, Complex>) {
            points = samplePointsFromComplexPlane(
                config.imageHeight, config.imageWidth,
                config.imStart, config.imEnd,
                config.reStart, config.reEnd,
                config.samples);
        }
        else {
            points = samplePointsFromComplexPlane(
                config.imageHeight, config.imageWidth,
                config.imStartHP, config.imEndHP,
                config.reStartHP, config.reEndHP,
                config.samples);
        }
    }

    const int totalPoints = imageSize * config.samples;
    vector<int> iters(totalPoints);
    {
        ScopedTimer timer("Calculating escape iterations");
        iterCalculator.calculate(points, iters, config.maxIter);
    }

    vector<Color> pixels;
    {
        ScopedTimer timer("Coloring");
        pixels = colorManager.paint(iters);
    }

    {
        ScopedTimer timer("Image generation");
        saveColorImageToPng(pixels, config.imageWidth, config.imageHeight, config.outputFilename);
    }
}

optional<MandelbrotConfig> parseCommandLine(int argc, char* argv[]) {
    MandelbrotConfig config;
    if (argc > 1) {
        try {
            config.outputFilename = argv[1];
            config.maxIter = stoi(argv[2]);
            config.paletteLength = stoi(argv[3]);

            cout << "Output file:     " << config.outputFilename << "\n";
            cout << "Max iterations:  " << config.maxIter << "\n";
            cout << "Palette length:  " << config.paletteLength << "\n";

            int paletteId = stoi(argv[4]);
            if (paletteId < 0 || paletteId >= palettes.size()) {
                cerr << "Error: Invalid palette ID. Must be between 0 and " << palettes.size() - 1 << ".\n";
                return nullopt;
            }
            config.paletteId = paletteId;
            cout << "Palette ID:      " << paletteId << "\n";

            int samples = stoi(argv[5]);
            if (samples < 1) {
                cerr << "Error: Invalid samples number. Must be greater than 0.\n";
                return nullopt;
            }
            config.samples = samples;
            cout << "Samples:         " << samples << "\n";

            config.useHighPrecision = (stoi(argv[6]) != 0);
            cout << "Using high precision: " << boolalpha << config.useHighPrecision << "\n";

            if (config.useHighPrecision) {
                config.reStartHP[0] = stoul(argv[7]);
                config.reStartHP[1] = stoul(argv[8]);
                config.reStartHP[2] = stoul(argv[9]);
                config.reStartHP[3] = stoul(argv[10]);

                config.reEndHP[0] = stoul(argv[11]);
                config.reEndHP[1] = stoul(argv[12]);
                config.reEndHP[2] = stoul(argv[13]);
                config.reEndHP[3] = stoul(argv[14]);

                config.imStartHP[0] = stoul(argv[15]);
                config.imStartHP[1] = stoul(argv[16]);
                config.imStartHP[2] = stoul(argv[17]);
                config.imStartHP[3] = stoul(argv[18]);

                config.imEndHP[0] = stoul(argv[19]);
                config.imEndHP[1] = stoul(argv[20]);
                config.imEndHP[2] = stoul(argv[21]);
                config.imEndHP[3] = stoul(argv[22]);

                cout << "reStartHP: {" << config.reStartHP[0] << ", "<< config.reStartHP[1] << ", " << config.reStartHP[2] << ", " << config.reStartHP[3] << "}\n";
                cout << "reEndHP:   {" << config.reEndHP[0] << ", " << config.reEndHP[1] << ", " << config.reEndHP[2] << ", " << config.reEndHP[3] << "}\n";
                cout << "imStartHP: {" << config.imStartHP[0] << ", " << config.imStartHP[1] << ", " << config.imStartHP[2] << ", " << config.imStartHP[3] << "}\n";
                cout << "imEndHP:   {" << config.imEndHP[0] << ", " << config.imEndHP[1] << ", " << config.imEndHP[2] << ", " << config.imEndHP[3] << "}\n";

            }
            else {
                config.reStart = stof(argv[7]);
                config.reEnd = stof(argv[8]);
                config.imStart = stof(argv[9]);
                config.imEnd = stof(argv[10]);

                cout << "reStart: " << config.reStart << "\n";
                cout << "reEnd:   " << config.reEnd << "\n";
                cout << "imStart: " << config.imStart << "\n";
                cout << "imEnd:   " << config.imEnd << "\n";
            }
        }
        catch (const exception& e) {
            cerr << "Error parsing arguments: " << e.what() << "\n";
            cerr << "Usage: " << argv[0] << " <USE_HIGH_PRECISION(0/1)> <RE_START> <RE_END> <IM_START> <IM_END> <OUTPUT_FILENAME> <MAX_ITER> <PALETTE_LENGTH> <PALETTE_ID> <SAMPLES>\n";
            return nullopt;
        }
    }
    return config;
}

/**
 * @brief Main entry point of the Mandelbrot set visualizer.
 *
 * This program generates a Mandelbrot set image based on user-provided parameters
 * from the command line, and saves the output to a PNG file. It supports both
 * standard double-precision and high-precision floating-point calculations.
 *
 * Command-line arguments are parsed in the following order:
 *
 * @param argc The number of command-line arguments.
 * @param argv An array of C-style strings representing the command-line arguments.
 *
 * Expected arguments (order is strict):
 * [1] <OUTPUT_FILENAME>    (string): Path and name for the output PNG image file (e.g., "output.png").
 * [2] <MAX_ITER>           (int): Maximum number of iterations for the Mandelbrot calculation.
 * [3] <PALETTE_LENGTH>     (int): The desired length of the color palette to be used.
 * [4] <PALETTE_ID>         (int): An index (0-based) to select a predefined color palette.
 * [5] <SAMPLES>            (int): Number of samples used for each image pixel.
 * [6] <USE_HIGH_PRECISION> (int): 0 for standard double-precision, 1 for high-precision
 * (boost::multiprecision::cpp_dec_float_50).
 * [7] <RE_START>         (double or high-precision fixed point number): Real component start of the complex plane.
 * [8] <RE_END>           (double or high-precision fixed point number): Real component end of the complex plane.
 * [9] <IM_START>         (double or high-precision fixed point number): Imaginary component start of the complex plane.
 * [10] <IM_END>          (double or high-precision fixed point number): Imaginary component end of the complex plane.
 * NOTE: The type of RE_START/END and IM_START/END depends on <USE_HIGH_PRECISION>.
 *
 * Example Usage:
 * ./mandelbrot 0 -2.0 1.0 -1.0 1.0 mandelbrot_double.png 400 256 0
 * ./mandelbrot 1 -0.153004885037500013708 -0.152809695287500013708 1.039611370300000000002 1.039757762612500000002 mandelbrot_hp.png 1000 512 1
 *
 * @return int Returns 0 on successful execution, 1 if argument parsing fails or an invalid palette ID is provided.
 */
int main(int argc, char* argv[]) {
    auto configOpt = parseCommandLine(argc, argv);
    if (!configOpt) {
        return 1;
    }
    const MandelbrotConfig config = configOpt.value();
    const CyclicColorPalette colorManager(config.imageHeight * config.imageWidth, palettes[config.paletteId], config.paletteLength, config.samples);
    #if 1      
    const CUDAIterationCalculator iterCalculator;
    #else       
    const OpenCLIterationCalculator iterCalculator;
    #endif

    if (config.useHighPrecision) {
        createMandelbrotSet<ComplexHP>(config, colorManager, iterCalculator);
    }
    else {
        createMandelbrotSet<Complex>(config, colorManager, iterCalculator);
    }
}