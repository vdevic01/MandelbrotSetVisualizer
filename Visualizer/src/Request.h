#pragma once

#include <optional>
#include <string>

#include "FixedPointArithmetics.h"
#include "IterationCalculator.h"
#include "Mode.h"

enum class ColorMethod { CYCLIC, HISTOGRAM };

// A fully validated render request, in the form the renderer works with. It is built from a JSON request
// and/or named command-line flags (see parseCommandLine); both go through the same validation.
struct MandelbrotConfig {
    Mode mode = Mode::CPU_PARALLEL;
    std::string outputFilename = "./mandelbrot_set.png";

    // Escape file: saveEscapeFile (optional) is where a render stores its escape results. recolourFrom (optional)
    // paints an earlier render's escape file again with the colour settings below, without iterating; the image
    // size, samples and escape radius then come from that file and the view and iteration options are not allowed.
    std::string saveEscapeFile;
    std::string recolourFrom;

    // image
    int imageWidth  = 900;
    int imageHeight = 600;
    int samples     = 10;

    // view: double precision uses the doubles, fixed point the words (most significant first, 1 whole word
    // followed by fractionWords fraction words)
    bool useHighPrecision = false;
    int fractionWords = fpa::FRACTION_PART;
    double reStart = 0, reEnd = 0, imStart = 0, imEnd = 0;
    fpa::uint reStartHP[fpa::MAX_FP_SIZE] = {};
    fpa::uint reEndHP[fpa::MAX_FP_SIZE]   = {};
    fpa::uint imStartHP[fpa::MAX_FP_SIZE] = {};
    fpa::uint imEndHP[fpa::MAX_FP_SIZE]   = {};

    // iteration
    int maxIter = 1000;
    double escapeRadius = DEFAULT_ESCAPE_RADIUS;   // iteration stops once |z| is above this

    // color
    ColorMethod colorMethod = ColorMethod::CYCLIC;
    int paletteId = 0;
    int paletteLength = 256;       // cyclic: iterations per palette cycle
    double colorSpread = 0.0;      // cyclic: logarithmic spread of the colours, 0 = linear
    double colorPhase = 0.0;       // where in the palette the colouring starts, 0..1 (both methods)
    double histogramCycles = 1.0;  // histogram: how many times the palette repeats
};

constexpr double MAX_COLOR_SPREAD = 10.0;
constexpr double MAX_HISTOGRAM_CYCLES = 1000.0;

// Reads the request from the command line: optionally a JSON request (--request FILE, or - for stdin) and
// named flags, which override what the request says. Prints the problem to stderr and returns nullopt when
// the arguments are wrong. Does not handle --help and --list-modes (the caller does).
std::optional<MandelbrotConfig> parseCommandLine(int argc, char* argv[]);

// The same as parseCommandLine, for a request already available as JSON text (used by the tests).
std::optional<MandelbrotConfig> parseRequestJson(const std::string& jsonText);

// One line per setting, for the log.
void printConfig(const MandelbrotConfig& config);

// The help text.
std::string usageText(const char* program);
