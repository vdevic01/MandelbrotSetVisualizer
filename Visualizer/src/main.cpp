#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <type_traits>

#include "Mode.h"
#include "IterationCalculator.h"
#include "CpuParallelIterationCalculator.h"
#ifdef ENABLE_OPENCL
#include "OpenCLIterationCalculator.h"
#endif
#include "CUDARemoteIterationCalculator.h"
#ifdef ENABLE_CUDA
#include "CUDAIterationCalculator.h"
#endif
#include "FixedPointArithmetics.h"
#include "ColorManager.h"
#include "Palettes.h"
#include "Sampler.h"
#include "ScopedTimer.h"
#include "ImageWriter.h"
#include "EscapeFile.h"
#include "Request.h"

using namespace std;

static void saveEscapeIfRequested(const MandelbrotConfig& config, const EscapeResult& escape) {
    if (config.saveEscapeFile.empty()) return;
    ScopedTimer timer("Saving escape results");
    saveEscapeFile(config.saveEscapeFile, {config.imageWidth, config.imageHeight, config.samples, config.escapeRadius, escape});
}

static void colorAndSave(const MandelbrotConfig& config, const ColorManager& colorManager, const EscapeResult& escape) {
    vector<Color> pixels;
    {
        ScopedTimer timer("Coloring");
        pixels = colorManager.paint(escape, config.escapeRadius);
    }
    {
        ScopedTimer timer("Image generation");
        saveColorImageToPng(pixels, config.imageWidth, config.imageHeight, config.outputFilename);
    }
}

// T_Points is vector<Complex> (double precision) or HpPoints (fixed point).
template<typename T_Points>
void createMandelbrotSet(const MandelbrotConfig& config, const ColorManager& colorManager, const IterationCalculator& iterCalculator) {
    cout << "=====================================================\n";
    ScopedTimer total_timer("Total generation time");

    const int imageSize = config.imageHeight * config.imageWidth;

    T_Points points;
    {
        ScopedTimer timer("Pixel mapping");
        if constexpr (is_same_v<T_Points, vector<Complex>>) {
            points = samplePointsFromComplexPlane(
                config.imageHeight, config.imageWidth,
                config.imStart, config.imEnd,
                config.reStart, config.reEnd,
                config.samples);
        } else {
            points = samplePointsFromComplexPlane(
                config.imageHeight, config.imageWidth,
                config.imStartHP, config.imEndHP,
                config.reStartHP, config.reEndHP,
                config.samples, config.fractionWords);
        }
    }

    const int totalPoints = imageSize * config.samples;
    EscapeResult escape(totalPoints);
    {
        ScopedTimer timer("Calculating escape iterations");
        iterCalculator.calculate(points, escape, config.maxIter, config.escapeRadius);
    }
    saveEscapeIfRequested(config, escape);

    colorAndSave(config, colorManager, escape);
}

#ifdef ENABLE_OPENCL
static bool isOpenCLAvailable() {
    cl_uint numPlatforms = 0;
    return clGetPlatformIDs(0, nullptr, &numPlatforms) == CL_SUCCESS && numPlatforms > 0;
}
#endif

#ifdef ENABLE_CUDA
static bool isCUDAAvailable() {
    int deviceCount = 0;
    return cudaGetDeviceCount(&deviceCount) == cudaSuccess && deviceCount > 0;
}
#endif

static void listAvailableModes() {
    cout << "CPU_PARALLEL\n";
#ifdef ENABLE_OPENCL
    if (isOpenCLAvailable()) cout << "OPENCL_LOCAL\n";
#endif
#ifdef ENABLE_CUDA
    if (isCUDAAvailable()) cout << "CUDA_LOCAL\n";
#endif
    cout << "CUDA_REMOTE\n";
}

unique_ptr<IterationCalculator> makeCalculator(Mode mode) {
    switch (mode) {
        case Mode::CPU_PARALLEL:
            return make_unique<CpuParallelIterationCalculator>();
#ifdef ENABLE_OPENCL
        case Mode::OPENCL_LOCAL:
            return make_unique<OpenCLIterationCalculator>();
#endif
#ifdef ENABLE_CUDA
        case Mode::CUDA_LOCAL:
            return make_unique<CUDAIterationCalculator>();
#endif
        default:
            cerr << "Error: Mode '" << modeToString(mode) << "' is not supported in this build.\n";
            cerr << "Rebuild with -DENABLE_OPENCL=ON or -DENABLE_CUDA=ON to enable local GPU modes.\n";
            return nullptr;
    }
}

static unique_ptr<CUDARemoteIterationCalculator> makeRemoteCalculator() {
    const char* endpoint = getenv("REMOTE_ENDPOINT");
    const char* apiKey   = getenv("REMOTE_API_KEY");
    if (!endpoint || !apiKey || endpoint[0] == '\0' || apiKey[0] == '\0') {
        cerr << "Error: CUDA_REMOTE requires REMOTE_ENDPOINT and REMOTE_API_KEY environment variables.\n";
        return nullptr;
    }
    return make_unique<CUDARemoteIterationCalculator>(endpoint, apiKey);
}

static void createMandelbrotSetRemote(
    const MandelbrotConfig& config, const ColorManager& colorManager,
    const CUDARemoteIterationCalculator& calc)
{
    cout << "=====================================================\n";
    ScopedTimer total_timer("Total generation time");

    EscapeResult escape;
    {
        ScopedTimer timer("Remote execution (sampling + iterations)");
        if (config.useHighPrecision) {
            escape = calc.calculateHP(
                config.reStartHP, config.reEndHP,
                config.imStartHP, config.imEndHP,
                config.fractionWords,
                config.imageWidth, config.imageHeight,
                config.samples, static_cast<unsigned int>(config.maxIter), config.escapeRadius);
        } else {
            escape = calc.calculate(
                config.reStart, config.reEnd,
                config.imStart, config.imEnd,
                config.imageWidth, config.imageHeight,
                config.samples, static_cast<unsigned int>(config.maxIter), config.escapeRadius);
        }
    }

    saveEscapeIfRequested(config, escape);
    colorAndSave(config, colorManager, escape);
}

int main(int argc, char* argv[]) {
    if (argc == 2 && string(argv[1]) == "--list-modes") {
        listAvailableModes();
        return 0;
    }
    if (argc == 2 && (string(argv[1]) == "--help" || string(argv[1]) == "-h")) {
        cout << usageText("mandelbrot_visualizer");
        return 0;
    }

    auto configOpt = parseCommandLine(argc, argv);
    if (!configOpt) return 1;

    MandelbrotConfig config = configOpt.value();

    // Recolouring: the image size, samples and escape radius are those of the render that wrote the file.
    EscapeData saved;
    if (!config.recolourFrom.empty()) {
        try {
            saved = loadEscapeFile(config.recolourFrom);
        } catch (const exception& e) {
            cerr << "Error: " << e.what() << "\n";
            return 1;
        }
        config.imageWidth = saved.width;
        config.imageHeight = saved.height;
        config.samples = saved.samples;
        config.escapeRadius = saved.escapeRadius;
    }
    printConfig(config);
    const int imageSize = config.imageHeight * config.imageWidth;
    unique_ptr<ColorManager> colorManagerPtr;
    if (config.colorMethod == ColorMethod::HISTOGRAM)
        // The palette length is only the resolution of the palette here; the colours do not depend on it.
        colorManagerPtr = make_unique<HistogramColorPalette>(
            imageSize, palettes[config.paletteId], 1000, config.samples, config.colorPhase, config.histogramCycles);
    else
        colorManagerPtr = make_unique<CyclicColorPalette>(
            imageSize, palettes[config.paletteId], config.paletteLength, config.samples, config.colorSpread, config.colorPhase);
    const ColorManager& colorManager = *colorManagerPtr;

    try {
        if (!config.recolourFrom.empty()) {
            cout << "=====================================================\n";
            ScopedTimer total_timer("Total recolour time");
            colorAndSave(config, colorManager, saved.escape);
        } else if (config.mode == Mode::CUDA_REMOTE) {
            const auto remoteCalc = makeRemoteCalculator();
            if (!remoteCalc) return 1;
            createMandelbrotSetRemote(config, colorManager, *remoteCalc);
        } else {
            const auto iterCalculator = makeCalculator(config.mode);
            if (!iterCalculator) return 1;
            if (config.useHighPrecision)
                createMandelbrotSet<HpPoints>(config, colorManager, *iterCalculator);
            else
                createMandelbrotSet<vector<Complex>>(config, colorManager, *iterCalculator);
        }
    } catch (const exception& e) {
        // A clean message and exit code instead of std::terminate, so the GUI can show what went wrong.
        cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
