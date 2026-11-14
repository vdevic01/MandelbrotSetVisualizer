// Tests of the render request (src/Request.cpp): the JSON request, the named flags, how they combine, and the
// errors, which must name the option that is wrong. Only parsing and validation are tested; nothing is rendered.

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "Request.h"

namespace {

// ------------------------------------------------------------------------------------------------ helpers
const std::string VIEW = R"("view": {"re": [-2, 1], "im": [-1, 1]})";

// A request with the required view and the given extra members, e.g. R"("image": {"width": 10})".
std::string request(const std::string& extra = "") {
    return "{" + VIEW + (extra.empty() ? "" : ", " + extra) + "}";
}

struct Outcome {
    std::optional<MandelbrotConfig> config;
    std::string error;   // what was written to stderr
};

Outcome parseJson(const std::string& json) {
    testing::internal::CaptureStderr();
    Outcome o{parseRequestJson(json), ""};
    o.error = testing::internal::GetCapturedStderr();
    return o;
}

Outcome parseArgs(const std::vector<std::string>& args) {
    std::vector<std::string> all = {"mandelbrot_visualizer"};
    all.insert(all.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& a : all) argv.push_back(a.data());
    testing::internal::CaptureStderr();
    Outcome o{parseCommandLine(static_cast<int>(argv.size()), argv.data()), ""};
    o.error = testing::internal::GetCapturedStderr();
    return o;
}

// Asserts that parsing failed and that the message mentions every given text.
void expectError(const Outcome& o, std::initializer_list<const char*> mentions) {
    EXPECT_FALSE(o.config.has_value()) << "expected an error";
    EXPECT_NE(std::string::npos, o.error.find("Error:")) << o.error;
    for (const char* text : mentions) EXPECT_NE(std::string::npos, o.error.find(text)) << "'" << text << "' missing from: " << o.error;
}

#define EXPECT_PARSED(outcome)                                                  \
    ASSERT_TRUE((outcome).config.has_value()) << (outcome).error;

std::string writeTempFile(const std::string& name, const std::string& content) {
    const std::string path = testing::TempDir() + name;
    std::ofstream(path, std::ios::binary) << content;
    return path;
}

// ------------------------------------------------------------------------------------------------ JSON: defaults and values
TEST(RequestJson, OnlyTheViewIsRequiredAndEverythingElseHasADefault) {
    const Outcome o = parseJson(request());
    EXPECT_PARSED(o);
    const MandelbrotConfig& c = *o.config;
    EXPECT_EQ(Mode::CPU_PARALLEL, c.mode);
    EXPECT_EQ("./mandelbrot_set.png", c.outputFilename);
    EXPECT_EQ(900, c.imageWidth);
    EXPECT_EQ(600, c.imageHeight);
    EXPECT_EQ(10, c.samples);
    EXPECT_EQ(1000, c.maxIter);
    EXPECT_DOUBLE_EQ(DEFAULT_ESCAPE_RADIUS, c.escapeRadius);
    EXPECT_FALSE(c.useHighPrecision);
    EXPECT_EQ(ColorMethod::CYCLIC, c.colorMethod);
    EXPECT_EQ(0, c.paletteId);
    EXPECT_EQ(256, c.paletteLength);
    EXPECT_DOUBLE_EQ(0.0, c.colorSpread);
    EXPECT_DOUBLE_EQ(-2, c.reStart);
    EXPECT_DOUBLE_EQ(1, c.reEnd);
    EXPECT_DOUBLE_EQ(-1, c.imStart);
    EXPECT_DOUBLE_EQ(1, c.imEnd);
}

TEST(RequestJson, EveryOptionIsRead) {
    const Outcome o = parseJson(R"({
        "version": 1,
        "mode": "OPENCL_LOCAL",
        "output": "out/x.png",
        "image": {"width": 1800, "height": 1200, "samples": 4},
        "view": {"re": ["-0.5", "0.25"], "im": [0.125, "1.5"]},
        "precision": "double",
        "iteration": {"maxIter": 2000, "escapeRadius": 16},
        "color": {"method": "cyclic", "palette": "Ocean", "length": 512, "spread": 0.25}
    })");
    EXPECT_PARSED(o);
    const MandelbrotConfig& c = *o.config;
    EXPECT_EQ(Mode::OPENCL_LOCAL, c.mode);
    EXPECT_EQ("out/x.png", c.outputFilename);
    EXPECT_EQ(1800, c.imageWidth);
    EXPECT_EQ(1200, c.imageHeight);
    EXPECT_EQ(4, c.samples);
    EXPECT_EQ(2000, c.maxIter);
    EXPECT_DOUBLE_EQ(16, c.escapeRadius);
    EXPECT_DOUBLE_EQ(-0.5, c.reStart);
    EXPECT_DOUBLE_EQ(0.25, c.reEnd);
    EXPECT_DOUBLE_EQ(0.125, c.imStart);
    EXPECT_DOUBLE_EQ(1.5, c.imEnd);
    EXPECT_EQ(2, c.paletteId);
    EXPECT_EQ(512, c.paletteLength);
    EXPECT_DOUBLE_EQ(0.25, c.colorSpread);
}

TEST(RequestJson, HistogramMethod) {
    const Outcome o = parseJson(request(R"("color": {"method": "histogram", "palette": 3, "phase": 0.25, "cycles": 3})"));
    EXPECT_PARSED(o);
    EXPECT_EQ(ColorMethod::HISTOGRAM, o.config->colorMethod);
    EXPECT_EQ(3, o.config->paletteId);
    EXPECT_DOUBLE_EQ(0.25, o.config->colorPhase);
    EXPECT_DOUBLE_EQ(3, o.config->histogramCycles);
}

TEST(RequestJson, AMethodlessColorObjectIsCyclic) {
    const Outcome o = parseJson(request(R"("color": {"length": 100})"));
    EXPECT_PARSED(o);
    EXPECT_EQ(ColorMethod::CYCLIC, o.config->colorMethod);
    EXPECT_EQ(100, o.config->paletteLength);
}

TEST(RequestJson, TheVersionMayBeGivenButOnlyVersionOneExists) {
    EXPECT_TRUE(parseJson(R"({"version": 1, "view": {"re": [-2, 1], "im": [-1, 1]}})").config.has_value());
    expectError(parseJson(R"({"version": 2, "view": {"re": [-2, 1], "im": [-1, 1]}})"), {"version"});
}

// ------------------------------------------------------------------------------------------------ palettes
TEST(RequestPalette, ByIdAndByNameIgnoringCaseAndSeparators) {
    const std::pair<const char*, int> cases[] = {
        {"0", 0}, {"3", 3}, {"7", 7}, {"\"Navy\"", 0}, {"\"sunset\"", 1}, {"\"OCEAN\"", 2},
        {"\"Fire and Ash\"", 3}, {"\"fire and ash\"", 3}, {"\"FireAndAsh\"", 3}, {"\"fire-and-ash\"", 3},
        {"\"fire_and_ash\"", 3}, {"\"Greyscale\"", 7},
    };
    for (const auto& [value, id] : cases) {
        const Outcome o = parseJson(request(std::string(R"("color": {"palette": )") + value + "}"));
        EXPECT_PARSED(o);
        EXPECT_EQ(id, o.config->paletteId) << value;
    }
}

TEST(RequestPalette, UnknownPalettesAreRejectedWithTheAvailableNames) {
    expectError(parseJson(request(R"("color": {"palette": "Nope"})")), {"color.palette", "Nope", "Navy", "Greyscale"});
    expectError(parseJson(request(R"("color": {"palette": 8})")), {"color.palette", "between 0 and 7"});
    expectError(parseJson(request(R"("color": {"palette": -1})")), {"color.palette"});
    expectError(parseJson(request(R"("color": {"palette": 1.5})")), {"color.palette"});
    expectError(parseJson(request(R"("color": {"palette": true})")), {"color.palette"});
}

// ------------------------------------------------------------------------------------------------ precision and view
TEST(RequestPrecision, DoubleIsTheDefaultAndCanBeNamed) {
    EXPECT_FALSE(parseJson(request()).config->useHighPrecision);
}

TEST(RequestPrecision, FixedPointConvertsTheViewExactly) {
    const Outcome o = parseJson(R"({"precision": {"type": "fixed", "fractionWords": 3},
        "view": {"re": ["0.5", "-0.5"], "im": ["1.5", "-0.25"]}})");
    EXPECT_PARSED(o);
    const MandelbrotConfig& c = *o.config;
    EXPECT_TRUE(c.useHighPrecision);
    EXPECT_EQ(3, c.fractionWords);
    EXPECT_EQ((std::vector<fpa::uint>{0, 0x80000000u, 0, 0}), std::vector<fpa::uint>(c.reStartHP, c.reStartHP + 4));
    EXPECT_EQ((std::vector<fpa::uint>{0xFFFFFFFFu, 0x80000000u, 0, 0}), std::vector<fpa::uint>(c.reEndHP, c.reEndHP + 4));
    EXPECT_EQ((std::vector<fpa::uint>{1, 0x80000000u, 0, 0}), std::vector<fpa::uint>(c.imStartHP, c.imStartHP + 4));
    EXPECT_EQ((std::vector<fpa::uint>{0xFFFFFFFFu, 0xC0000000u, 0, 0}), std::vector<fpa::uint>(c.imEndHP, c.imEndHP + 4));
}

TEST(RequestPrecision, ADeepZoomKeepsEveryDigit) {
    // The view of the "Sequential HP" run configuration, as the exact decimal values of its fixed-point words.
    const Outcome o = parseJson(R"({"precision": {"type": "fixed", "fractionWords": 3}, "view": {
        "re": ["-0.153004885037500013709068298335026161966057141639537312292895787635416127159260213375091552734375",
               "-0.15280969528750001370906829833856147374297196178717932364687914770229326677508652210235595703125"],
        "im": ["1.03961137030000000000232830642617368325708476159226667685786527073332763393409550189971923828125",
               "1.03975776261250000000232830642667764304528269370318208260350889560186260496266186237335205078125"]}})");
    EXPECT_PARSED(o);
    const MandelbrotConfig& c = *o.config;
    EXPECT_EQ((std::vector<fpa::uint>{4294967295u, 3637816318u, 2730300863u, 4039731417u}), std::vector<fpa::uint>(c.reStartHP, c.reStartHP + 4));
    EXPECT_EQ((std::vector<fpa::uint>{4294967295u, 3638654652u, 981237348u, 3835888558u}), std::vector<fpa::uint>(c.reEndHP, c.reEndHP + 4));
    EXPECT_EQ((std::vector<fpa::uint>{1u, 170129539u, 4244482999u, 3226762018u}), std::vector<fpa::uint>(c.imStartHP, c.imStartHP + 4));
    EXPECT_EQ((std::vector<fpa::uint>{1u, 170758290u, 785201715u, 2000138050u}), std::vector<fpa::uint>(c.imEndHP, c.imEndHP + 4));
}

TEST(RequestPrecision, FixedPointDefaultsAndSizes) {
    EXPECT_EQ(fpa::FRACTION_PART, parseJson(R"({"precision": "fixed", "view": {"re": [-2, 1], "im": [-1, 1]}})").config->fractionWords);
    EXPECT_EQ(fpa::FRACTION_PART, parseJson(request(R"("precision": {"type": "fixed"})")).config->fractionWords);
    // Giving only the size means fixed point.
    const Outcome o = parseJson(request(R"("precision": {"fractionWords": 6})"));
    EXPECT_PARSED(o);
    EXPECT_TRUE(o.config->useHighPrecision);
    EXPECT_EQ(6, o.config->fractionWords);
}

TEST(RequestPrecision, Errors) {
    expectError(parseJson(request(R"("precision": {"type": "fixed", "fractionWords": 7})")), {"precision.fractionWords", "between 1 and 6"});
    expectError(parseJson(request(R"("precision": {"type": "fixed", "fractionWords": 0})")), {"precision.fractionWords"});
    expectError(parseJson(request(R"("precision": {"type": "double", "fractionWords": 3})")), {"precision.fractionWords", "fixed"});
    expectError(parseJson(request(R"("precision": "quad")")), {"precision.type", "quad"});
    expectError(parseJson(request(R"("precision": {"type": "quad"})")), {"precision.type"});
    expectError(parseJson(request(R"("precision": 3)")), {"precision"});
    expectError(parseJson(request(R"("precision": {"type": "fixed", "bits": 128})")), {"precision.bits", "unknown"});
}

TEST(RequestView, IsRequiredWithBothAxes) {
    expectError(parseJson("{}"), {"view", "required"});
    expectError(parseJson(R"({"view": {"re": [-2, 1]}})"), {"view.im", "required"});
    expectError(parseJson(R"({"view": {"im": [-2, 1]}})"), {"view.re", "required"});
    expectError(parseJson(R"({"view": [-2, 1, -1, 1]})"), {"view"});
}

TEST(RequestView, EachAxisIsTwoNumbersOrDecimalStrings) {
    expectError(parseJson(R"({"view": {"re": [-2], "im": [-1, 1]}})"), {"view.re", "two values"});
    expectError(parseJson(R"({"view": {"re": [-2, 1, 3], "im": [-1, 1]}})"), {"view.re", "two values"});
    expectError(parseJson(R"({"view": {"re": "-2:1", "im": [-1, 1]}})"), {"view.re"});
    expectError(parseJson(R"({"view": {"re": [true, 1], "im": [-1, 1]}})"), {"view.re[0]"});
    expectError(parseJson(R"({"view": {"re": [-2, "abc"], "im": [-1, 1]}})"), {"view.re[1]", "abc"});
    expectError(parseJson(R"({"view": {"re": [-2, 1], "im": [-1, null]}})"), {"view.im[1]"});
    expectError(parseJson(R"({"view": {"re": [-2, 1], "im": [-1, 1], "zoom": 2}})"), {"view.zoom", "unknown"});
}

TEST(RequestView, ACoordinateThatDoesNotFitFixedPointIsRejected) {
    expectError(parseJson(R"({"precision": "fixed", "view": {"re": ["3000000000", 1], "im": [-1, 1]}})"), {"view.re[0]", "fit"});
    expectError(parseJson(R"({"precision": "fixed", "view": {"re": ["x", 1], "im": [-1, 1]}})"), {"view.re[0]"});
}

TEST(RequestView, DoubleCoordinatesMayBeLongDecimalText) {
    const Outcome o = parseJson(R"({"view": {"re": ["-0.153004885037500013708", "-0.152809695287500013708"], "im": [1, 2]}})");
    EXPECT_PARSED(o);
    EXPECT_DOUBLE_EQ(-0.153004885037500013708, o.config->reStart);
    EXPECT_FALSE(parseJson(R"({"view": {"re": ["1e400", 1], "im": [1, 2]}})").config.has_value());   // not finite
}

// ------------------------------------------------------------------------------------------------ errors in values
TEST(RequestErrors, UnknownOptionsAreRejectedAtEveryLevel) {
    expectError(parseJson(request(R"("colour": {})")), {"colour", "unknown option"});
    expectError(parseJson(request(R"("image": {"wdith": 10})")), {"image.wdith", "unknown option"});
    expectError(parseJson(request(R"("iteration": {"max": 10})")), {"iteration.max", "unknown option"});
    expectError(parseJson(request(R"("color": {"shade": 1})")), {"color.shade", "unknown option"});
}

TEST(RequestErrors, SectionsMustBeObjects) {
    expectError(parseJson(request(R"("image": 5)")), {"image", "object"});
    expectError(parseJson(request(R"("color": "navy")")), {"color", "object"});
    expectError(parseJson(request(R"("iteration": [])")), {"iteration", "object"});
}

TEST(RequestErrors, ValuesAreChecked) {
    expectError(parseJson(request(R"("mode": "GPU")")), {"mode", "GPU"});
    expectError(parseJson(request(R"("mode": 3)")), {"mode"});
    expectError(parseJson(request(R"("output": 5)")), {"output", "string"});
    expectError(parseJson(request(R"("image": {"width": "big"})")), {"image.width", "integer"});
    expectError(parseJson(request(R"("image": {"width": 1.5})")), {"image.width", "integer"});
    expectError(parseJson(request(R"("image": {"width": 0})")), {"image.width", "between"});
    expectError(parseJson(request(R"("image": {"height": -5})")), {"image.height", "between"});
    expectError(parseJson(request(R"("image": {"width": 40000})")), {"image.width", "between"});
    expectError(parseJson(request(R"("image": {"samples": 0})")), {"image.samples", "between"});
    expectError(parseJson(request(R"("iteration": {"maxIter": 0})")), {"iteration.maxIter", "between"});
    expectError(parseJson(request(R"("iteration": {"escapeRadius": 1})")), {"iteration.escapeRadius", "between 2 and 128"});
    expectError(parseJson(request(R"("iteration": {"escapeRadius": 200})")), {"iteration.escapeRadius"});
    expectError(parseJson(request(R"("iteration": {"escapeRadius": "2"})")), {"iteration.escapeRadius", "number"});
    expectError(parseJson(request(R"("color": {"length": 0})")), {"color.length"});
    expectError(parseJson(request(R"("color": {"spread": 11})")), {"color.spread", "between 0 and 10"});
    expectError(parseJson(request(R"("color": {"spread": -0.5})")), {"color.spread"});
}

TEST(RequestErrors, TheBoundsOfRangesAreAccepted) {
    EXPECT_TRUE(parseJson(request(R"("iteration": {"escapeRadius": 2})")).config.has_value());
    EXPECT_TRUE(parseJson(request(R"("iteration": {"escapeRadius": 128})")).config.has_value());
    EXPECT_TRUE(parseJson(request(R"("color": {"spread": 0})")).config.has_value());
    EXPECT_TRUE(parseJson(request(R"("color": {"spread": 10})")).config.has_value());
    EXPECT_TRUE(parseJson(request(R"("color": {"method": "histogram", "phase": 0, "cycles": 1000})")).config.has_value());
    EXPECT_TRUE(parseJson(request(R"("color": {"method": "histogram", "phase": 1})")).config.has_value());
    EXPECT_TRUE(parseJson(request(R"("image": {"width": 1, "height": 1, "samples": 1})")).config.has_value());
}

TEST(RequestErrors, TheTextMustBeAJsonObject) {
    expectError(parseJson(""), {"not valid JSON"});
    expectError(parseJson("{"), {"not valid JSON"});
    expectError(parseJson("{'view': 1}"), {"not valid JSON"});
    expectError(parseJson(R"({"view": {"re": [-2, 1], "im": [-1, 1]}, // note
        })"), {"not valid JSON"});   // JSON has no comments
    expectError(parseJson("[]"), {"object"});
    expectError(parseJson("3"), {"object"});
}

// ------------------------------------------------------------------------------------------------ colour methods
TEST(RequestColor, SettingsOfTheOtherMethodAreRejected) {
    expectError(parseJson(request(R"("color": {"method": "histogram", "length": 100})")), {"color.length", "cyclic", "histogram"});
    expectError(parseJson(request(R"("color": {"method": "histogram", "spread": 0.1})")), {"color.spread", "cyclic"});
    EXPECT_TRUE(parseJson(request(R"("color": {"method": "cyclic", "phase": 0.1})")).config.has_value());
    expectError(parseJson(request(R"("color": {"cycles": 2})")), {"color.cycles", "unknown option"});
}

TEST(RequestColor, TheMethodAndItsSettingsAreChecked) {
    expectError(parseJson(request(R"("color": {"method": "rainbow"})")), {"color.method", "rainbow"});
    expectError(parseJson(request(R"("color": {"method": 1})")), {"color.method"});
    expectError(parseJson(request(R"("color": {"method": "histogram", "phase": 1.5})")), {"color.phase", "between 0 and 1"});
    expectError(parseJson(request(R"("color": {"method": "histogram", "phase": -0.1})")), {"color.phase"});
    expectError(parseJson(request(R"("color": {"method": "histogram", "cycles": 0})")), {"color.cycles"});
    expectError(parseJson(request(R"("color": {"method": "histogram", "cycles": 1001})")), {"color.cycles"});
}

// ------------------------------------------------------------------------------------------------ flags
TEST(RequestFlags, TheViewAloneIsEnough) {
    const Outcome o = parseArgs({"--re=-2:1", "--im=-1:1"});
    EXPECT_PARSED(o);
    EXPECT_DOUBLE_EQ(-2, o.config->reStart);
    EXPECT_DOUBLE_EQ(1, o.config->reEnd);
    EXPECT_EQ(900, o.config->imageWidth);
}

TEST(RequestFlags, ValuesMayFollowAsSeparateArgumentsOrAfterAnEqualsSign) {
    const Outcome a = parseArgs({"--re", "-2:1", "--im", "-1:1", "--max-iter", "50"});
    const Outcome b = parseArgs({"--re=-2:1", "--im=-1:1", "--max-iter=50"});
    EXPECT_PARSED(a);
    EXPECT_PARSED(b);
    EXPECT_EQ(50, a.config->maxIter);
    EXPECT_EQ(50, b.config->maxIter);
}

TEST(RequestFlags, EveryFlagSetsItsOption) {
    const Outcome o = parseArgs({"--mode", "CUDA_LOCAL", "--output", "a b.png", "--size", "1800x1200", "--samples", "3",
                                 "--re=-0.5:0.25", "--im=0.125:1.5", "--max-iter", "77", "--escape-radius", "8",
                                 "--palette", "Sunset", "--palette-length", "99", "--color-spread", "0.5"});
    EXPECT_PARSED(o);
    const MandelbrotConfig& c = *o.config;
    EXPECT_EQ(Mode::CUDA_LOCAL, c.mode);
    EXPECT_EQ("a b.png", c.outputFilename);
    EXPECT_EQ(1800, c.imageWidth);
    EXPECT_EQ(1200, c.imageHeight);
    EXPECT_EQ(3, c.samples);
    EXPECT_DOUBLE_EQ(-0.5, c.reStart);
    EXPECT_DOUBLE_EQ(1.5, c.imEnd);
    EXPECT_EQ(77, c.maxIter);
    EXPECT_DOUBLE_EQ(8, c.escapeRadius);
    EXPECT_EQ(1, c.paletteId);
    EXPECT_EQ(99, c.paletteLength);
    EXPECT_DOUBLE_EQ(0.5, c.colorSpread);
}

TEST(RequestFlags, HistogramFlags) {
    const Outcome o = parseArgs({"--re=-2:1", "--im=-1:1", "--color", "histogram", "--histogram-phase", "0.5",
                                 "--histogram-cycles", "4", "--palette", "2"});
    EXPECT_PARSED(o);
    EXPECT_EQ(ColorMethod::HISTOGRAM, o.config->colorMethod);
    EXPECT_DOUBLE_EQ(0.5, o.config->colorPhase);
    EXPECT_DOUBLE_EQ(4, o.config->histogramCycles);
    EXPECT_EQ(2, o.config->paletteId);
}

TEST(RequestFlags, AFractionSizeAloneMeansFixedPoint) {
    const Outcome o = parseArgs({"--re=0.5:-0.5", "--im=1:-1", "--fraction-words", "4"});
    EXPECT_PARSED(o);
    EXPECT_TRUE(o.config->useHighPrecision);
    EXPECT_EQ(4, o.config->fractionWords);
    EXPECT_EQ(0x80000000u, o.config->reStartHP[1]);
}

TEST(RequestFlags, ExplicitFixedPointUsesTheDefaultSize) {
    const Outcome o = parseArgs({"--re=0.5:-0.5", "--im=1:-1", "--precision", "fixed"});
    EXPECT_PARSED(o);
    EXPECT_TRUE(o.config->useHighPrecision);
    EXPECT_EQ(fpa::FRACTION_PART, o.config->fractionWords);
}

TEST(RequestFlags, ANegativeStartCanBeGivenAsASeparateArgument) {
    const Outcome o = parseArgs({"--re", "-2.5:-1", "--im", "-1:1"});
    EXPECT_PARSED(o);
    EXPECT_DOUBLE_EQ(-2.5, o.config->reStart);
    EXPECT_DOUBLE_EQ(-1, o.config->reEnd);
}

TEST(RequestFlags, PalettesByIdOrName) {
    EXPECT_EQ(2, parseArgs({"--re=-2:1", "--im=-1:1", "--palette", "2"}).config->paletteId);
    EXPECT_EQ(3, parseArgs({"--re=-2:1", "--im=-1:1", "--palette", "Fire and Ash"}).config->paletteId);
}

TEST(RequestFlags, Errors) {
    expectError(parseArgs({}), {"view", "required"});
    expectError(parseArgs({"--re=-2:1"}), {"view.im", "required"});
    expectError(parseArgs({"--re=-2:1", "--im=-1:1", "--bogus", "1"}), {"unknown option", "--bogus"});
    expectError(parseArgs({"--re=-2:1", "--im=-1:1", "--mode"}), {"--mode", "needs a value"});
    expectError(parseArgs({"--re=-2:1", "--im=-1:1", "--max-iter", "abc"}), {"--max-iter", "integer", "abc"});
    expectError(parseArgs({"--re=-2:1", "--im=-1:1", "--max-iter", "1.5"}), {"--max-iter", "integer"});
    expectError(parseArgs({"--re=-2:1", "--im=-1:1", "--escape-radius", "x"}), {"--escape-radius", "number"});
    expectError(parseArgs({"--re=-2:1", "--im=-1:1", "--escape-radius", "1"}), {"iteration.escapeRadius", "between"});
    expectError(parseArgs({"--re=-2:1", "--im=-1:1", "--size", "1800"}), {"--size", "WIDTHxHEIGHT"});
    expectError(parseArgs({"--re=-2:1", "--im=-1:1", "--size", "axb"}), {"--size"});
    expectError(parseArgs({"--re=-2:1", "--im=-1:1", "--size", "0x10"}), {"image.width"});
    expectError(parseArgs({"--re=1", "--im=-1:1"}), {"--re", "START:END"});
    expectError(parseArgs({"--re=:1", "--im=-1:1"}), {"--re", "START:END"});
    expectError(parseArgs({"--re=1:", "--im=-1:1"}), {"--re", "START:END"});
    expectError(parseArgs({"--re=-2:1", "--im=-1:1", "--palette", "Nope"}), {"color.palette", "Nope"});
    expectError(parseArgs({"--re=-2:1", "--im=-1:1", "--palette", "9"}), {"color.palette"});
}

TEST(RequestFlags, TheOldPositionalFormatIsRejectedWithAHint) {
    expectError(parseArgs({"CPU_PARALLEL", "out.png", "700", "512", "0", "4", "0", "-2", "1", "-1", "1"}),
                {"unexpected argument 'CPU_PARALLEL'", "named"});
    expectError(parseArgs({"--re=-2:1", "--im=-1:1", "-3"}), {"unexpected argument '-3'"});
}

TEST(RequestFlags, SettingsOfTheOtherMethodAreRejected) {
    expectError(parseArgs({"--re=-2:1", "--im=-1:1", "--color", "histogram", "--color-spread", "1"}), {"color.spread", "cyclic"});
    expectError(parseArgs({"--re=-2:1", "--im=-1:1", "--color", "histogram", "--palette-length", "10"}), {"color.length", "cyclic"});
    const Outcome phase = parseArgs({"--re=-2:1", "--im=-1:1", "--color-phase", "0.5"});   // both methods have a phase
    EXPECT_PARSED(phase);
    EXPECT_DOUBLE_EQ(0.5, phase.config->colorPhase);
    expectError(parseArgs({"--re=-2:1", "--im=-1:1", "--precision", "double", "--fraction-words", "4"}), {"precision.fractionWords", "fixed"});
}

TEST(RequestFlags, TheOrderOfTheFlagsDoesNotMatter) {
    const Outcome a = parseArgs({"--fraction-words", "4", "--re=0.5:-0.5", "--im=1:-1", "--palette", "1"});
    const Outcome b = parseArgs({"--palette", "1", "--im=1:-1", "--re=0.5:-0.5", "--fraction-words", "4"});
    EXPECT_PARSED(a);
    EXPECT_PARSED(b);
    EXPECT_EQ(a.config->fractionWords, b.config->fractionWords);
    EXPECT_EQ(a.config->paletteId, b.config->paletteId);
    EXPECT_EQ(a.config->reStartHP[1], b.config->reStartHP[1]);
}

// ------------------------------------------------------------------------------------------------ request file + flags
TEST(RequestFile, IsReadAndFlagsOverrideIt) {
    const std::string path = writeTempFile("request_basic.json", request(R"("iteration": {"maxIter": 100}, "image": {"samples": 2})"));
    const Outcome plain = parseArgs({"--request", path});
    EXPECT_PARSED(plain);
    EXPECT_EQ(100, plain.config->maxIter);
    EXPECT_EQ(2, plain.config->samples);

    // The flag wins whether it comes before or after the file.
    const Outcome after = parseArgs({"--request", path, "--max-iter", "5"});
    const Outcome before = parseArgs({"--max-iter", "5", "--request", path});
    EXPECT_PARSED(after);
    EXPECT_PARSED(before);
    EXPECT_EQ(5, after.config->maxIter);
    EXPECT_EQ(5, before.config->maxIter);
    EXPECT_EQ(2, after.config->samples);   // what the flags do not mention stays as in the file
    std::remove(path.c_str());
}

TEST(RequestFile, AFlagCanSupplyTheViewOfAFileWithoutOne) {
    const std::string path = writeTempFile("request_noview.json", R"({"image": {"width": 50, "height": 40}})");
    const Outcome o = parseArgs({"--request", path, "--re=-2:1", "--im=-1:1"});
    EXPECT_PARSED(o);
    EXPECT_EQ(50, o.config->imageWidth);
    EXPECT_DOUBLE_EQ(-2, o.config->reStart);
    std::remove(path.c_str());
}

TEST(RequestFile, FlagsAreValidatedTogetherWithTheFile) {
    // The file asks for the cyclic method; a histogram flag then makes its length option invalid.
    const std::string path = writeTempFile("request_cyclic.json", request(R"("color": {"length": 100})"));
    expectError(parseArgs({"--request", path, "--color", "histogram"}), {"color.length", "cyclic"});
    std::remove(path.c_str());
}

TEST(RequestFile, ProblemsWithTheFileAreReported) {
    expectError(parseArgs({"--request", testing::TempDir() + "does_not_exist.json"}), {"cannot open", "does_not_exist.json"});
    const std::string bad = writeTempFile("request_bad.json", "{ not json");
    expectError(parseArgs({"--request", bad}), {"not valid JSON", "request_bad.json"});
    std::remove(bad.c_str());
    const std::string typo = writeTempFile("request_typo.json", request(R"("iteraton": {})"));
    expectError(parseArgs({"--request", typo}), {"iteraton", "unknown option"});
    std::remove(typo.c_str());
    expectError(parseArgs({"--request"}), {"--request", "needs a value"});
}

// ------------------------------------------------------------------------------------------------ escape file / recolour
TEST(RequestEscapeFile, SaveEscapeIsAnOptionOfARender) {
    const Outcome o = parseArgs({"--re=-2:1", "--im=-1:1", "--save-escape", "a.esc"});
    EXPECT_PARSED(o);
    EXPECT_EQ("a.esc", o.config->saveEscapeFile);
    EXPECT_TRUE(o.config->recolourFrom.empty());
}

TEST(RequestEscapeFile, RecolouringNeedsNoViewAndTakesTheColourOptions) {
    const Outcome o = parseArgs({"--recolour-from", "a.esc", "--palette", "3", "--palette-length", "80", "--output", "b.png"});
    EXPECT_PARSED(o);
    EXPECT_EQ("a.esc", o.config->recolourFrom);
    EXPECT_EQ(3, o.config->paletteId);
    EXPECT_EQ(80, o.config->paletteLength);
    EXPECT_EQ("b.png", o.config->outputFilename);
}

TEST(RequestEscapeFile, RecolouringRejectsWhatTheEscapeFileAlreadyDecides) {
    expectError(parseArgs({"--recolour-from", "a.esc", "--re=-2:1"}), {"view", "recolour"});
    expectError(parseArgs({"--recolour-from", "a.esc", "--size", "10x10"}), {"image", "recolour"});
    expectError(parseArgs({"--recolour-from", "a.esc", "--max-iter", "10"}), {"iteration", "recolour"});
    expectError(parseArgs({"--recolour-from", "a.esc", "--save-escape", "b.esc"}), {"saveEscape", "recolour"});
}

}  // namespace
