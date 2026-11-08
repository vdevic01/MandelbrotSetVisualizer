// Golden-vector tests: every case in tests/vectors/fpa_f<F>.txt is checked against the library, for
// every supported fraction size F. The vectors come from an independent Python reference.
//
// Besides the plain "does it compute the right value" test, each operation is also run with
//  - an output buffer pre-filled with garbage      (the result must not depend on previous contents),
//  - the output buffer aliasing an input operand   (callers may compute in place).

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

#include "test_support.h"
#include "vector_loader.h"

using namespace fpt;

namespace {

class VectorTest : public ::testing::TestWithParam<int> {
protected:
    int F() const { return GetParam(); }
    const VectorFile& file() const { return loadVectors(GetParam()); }
};

// Runs `check` on every case of `op`. `check` returns an empty string on success or a description of
// the mismatch. At most 5 mismatches are printed individually; the total is always reported.
void forEachCase(const VectorFile& file, const char* op, const std::function<std::string(const VectorCase&)>& check) {
    const auto cases = casesFor(file, op);
    ASSERT_FALSE(cases.empty()) << "no '" << op << "' cases in " << file.path;
    int failures = 0;
    for (const VectorCase* c : cases) {
        const std::string error = check(*c);
        if (error.empty()) continue;
        if (failures < 5)
            ADD_FAILURE() << file.path << ":" << c->line << " [" << op << "/" << c->tag << "] " << error;
        failures++;
    }
    EXPECT_EQ(0, failures) << failures << " of " << cases.size() << " '" << op << "' cases failed";
}

std::string mismatch(const Number& expected, const Number& actual) {
    if (expected == actual) return "";
    return "expected " + hex(expected) + " actual " + hex(actual);
}

enum class Mode { Clean, DirtyOutput, OutputIsFirstOperand, OutputIsSecondOperand };

// Garbage that differs from case to case, including the patterns most likely to hide bugs.
uint garbageFor(int line) {
    static const uint patterns[] = {kAllOnes, 0x5A5A5A5Au, 0x00000001u, 0x80000000u, 0xA5A5A5A5u};
    return patterns[line % 5];
}

std::string checkBinary(int f, const VectorCase& c, BinaryFn op, Mode mode) {
    const Number a = parseNumber(c.args[0], f);
    const Number b = parseNumber(c.args[1], f);
    const Number expected = parseNumber(c.args[2], f);
    Number out = zero(f);
    switch (mode) {
        case Mode::Clean:
            op(a.data(), b.data(), out.data(), f);
            break;
        case Mode::DirtyOutput:
            out = filled(f, garbageFor(c.line));
            op(a.data(), b.data(), out.data(), f);
            break;
        case Mode::OutputIsFirstOperand:
            out = a;
            op(out.data(), b.data(), out.data(), f);
            break;
        case Mode::OutputIsSecondOperand:
            out = b;
            op(a.data(), out.data(), out.data(), f);
            break;
    }
    return mismatch(expected, out);
}

std::string checkUnary(int f, const VectorCase& c, UnaryFn op, Mode mode) {
    const Number a = parseNumber(c.args[0], f);
    const Number expected = parseNumber(c.args[1], f);
    Number out = zero(f);
    if (mode == Mode::OutputIsFirstOperand) {
        out = a;
        op(out.data(), out.data(), f);
    } else {
        if (mode == Mode::DirtyOutput) out = filled(f, garbageFor(c.line));
        op(a.data(), out.data(), f);
    }
    return mismatch(expected, out);
}

}  // namespace

#define BINARY_VECTOR_TESTS(Name, opName, fn)                                                                         \
    TEST_P(VectorTest, Name) {                                                                                        \
        forEachCase(file(), opName, [&](const VectorCase& c) { return checkBinary(F(), c, fn, Mode::Clean); });       \
    }                                                                                                                 \
    TEST_P(VectorTest, Name##IgnoresPreviousOutputContents) {                                                         \
        forEachCase(file(), opName, [&](const VectorCase& c) { return checkBinary(F(), c, fn, Mode::DirtyOutput); }); \
    }                                                                                                                 \
    TEST_P(VectorTest, Name##WithOutputAliasingFirstOperand) {                                                        \
        forEachCase(file(), opName,                                                                                   \
                    [&](const VectorCase& c) { return checkBinary(F(), c, fn, Mode::OutputIsFirstOperand); });        \
    }                                                                                                                 \
    TEST_P(VectorTest, Name##WithOutputAliasingSecondOperand) {                                                       \
        forEachCase(file(), opName,                                                                                   \
                    [&](const VectorCase& c) { return checkBinary(F(), c, fn, Mode::OutputIsSecondOperand); });       \
    }

#define UNARY_VECTOR_TESTS(Name, opName, fn)                                                                          \
    TEST_P(VectorTest, Name) {                                                                                        \
        forEachCase(file(), opName, [&](const VectorCase& c) { return checkUnary(F(), c, fn, Mode::Clean); });        \
    }                                                                                                                 \
    TEST_P(VectorTest, Name##IgnoresPreviousOutputContents) {                                                         \
        forEachCase(file(), opName, [&](const VectorCase& c) { return checkUnary(F(), c, fn, Mode::DirtyOutput); });  \
    }                                                                                                                 \
    TEST_P(VectorTest, Name##InPlace) {                                                                               \
        forEachCase(file(), opName,                                                                                   \
                    [&](const VectorCase& c) { return checkUnary(F(), c, fn, Mode::OutputIsFirstOperand); });         \
    }

BINARY_VECTOR_TESTS(Add, "add", fpa::addFixed)
BINARY_VECTOR_TESTS(Sub, "sub", fpa::subFixed)
BINARY_VECTOR_TESTS(Mul, "mul", fpa::mulCmplFixed)
BINARY_VECTOR_TESTS(Div, "div", fpa::divFixed)
UNARY_VECTOR_TESTS(Inc, "inc", fpa::incFixed)
UNARY_VECTOR_TESTS(Complement, "cmpl", fpa::cmplFixed)

TEST_P(VectorTest, GreaterThan) {
    forEachCase(file(), "gt", [&](const VectorCase& c) -> std::string {
        const Number a = parseNumber(c.args[0], F()), b = parseNumber(c.args[1], F());
        const bool expected = c.args[2] == "1";
        return gt(F(), a, b) == expected ? "" : "gt(" + hex(a) + ", " + hex(b) + ") expected " + c.args[2];
    });
}

TEST_P(VectorTest, GreaterThanOrEqual) {
    forEachCase(file(), "gte", [&](const VectorCase& c) -> std::string {
        const Number a = parseNumber(c.args[0], F()), b = parseNumber(c.args[1], F());
        const bool expected = c.args[2] == "1";
        return gte(F(), a, b) == expected ? "" : "gte(" + hex(a) + ", " + hex(b) + ") expected " + c.args[2];
    });
}

TEST_P(VectorTest, DoubleToFixed) {
    forEachCase(file(), "f2x", [&](const VectorCase& c) {
        const double d = parseDouble(c.args[0]);
        return mismatch(parseNumber(c.args[1], F()), fromDouble(F(), d));
    });
}

TEST_P(VectorTest, DoubleToFixedIgnoresPreviousOutputContents) {
    forEachCase(file(), "f2x", [&](const VectorCase& c) {
        Number out = filled(F(), garbageFor(c.line));
        fpa::floatingToFixedPoint(parseDouble(c.args[0]), out.data(), F());
        return mismatch(parseNumber(c.args[1], F()), out);
    });
}

TEST_P(VectorTest, FixedToDouble) {
    forEachCase(file(), "x2f", [&](const VectorCase& c) -> std::string {
        const Number a = parseNumber(c.args[0], F());
        const double expected = parseDouble(c.args[1]);
        const double actual = toDouble(F(), a);
        char text[160];
        std::snprintf(text, sizeof text, "x2f(%s) expected %.17g actual %.17g", hex(a).c_str(), expected, actual);
        if (c.tag == "exact") return actual == expected ? "" : text;
        // `approx`: the library sums bit by bit, so allow a few ULP of accumulated rounding error.
        const double tolerance = std::fabs(expected) * 1e-15 + 1e-300;
        return std::fabs(actual - expected) <= tolerance ? "" : text;
    });
}

FPA_INSTANTIATE_FOR_ALL_FORMATS(VectorTest);
