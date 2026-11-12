// Property tests. They don't need a table of expected values: they check laws that must hold for any
// inputs (algebraic invariants), bounds that the exact result must satisfy (partial oracles), and
// relations between two runs of the library on related inputs (metamorphic tests).
//
// Every test uses a fixed seed, so a failure is reproducible. Where truncation makes a law approximate,
// the allowed error is derived from the truncation rules (documented next to each test), not tuned.

#include <cmath>
#include <vector>

#include "test_support.h"

using namespace fpt;

namespace {

class InvariantTest : public ::testing::TestWithParam<int> {
protected:
    int F() const { return GetParam(); }
    Rng rng(uint64_t salt) const { return Rng(0xF1A5C0DEull * 31 + static_cast<uint64_t>(GetParam()) * 1000 + salt); }

    Number two() const { return make(F(), {2}); }
    Number half() const { return make(F(), {0, kSignBit}); }
    bool atLeastOne(const Number& n) const { return absValue(F(), n)[0] >= 1; }
};

}  // namespace

// =================================================================== algebraic invariants: + - and negation
TEST_P(InvariantTest, AdditionIsCommutativeAndAssociative) {
    Rng r = rng(1);
    for (int i = 0; i < kIterations; i++) {
        const Number a = r.any(F()), b = r.any(F()), c = r.any(F());
        ASSERT_TRUE(same(add(F(), a, b), add(F(), b, a))) << "commutativity";
        ASSERT_TRUE(same(add(F(), add(F(), a, b), c), add(F(), a, add(F(), b, c)))) << "associativity";
    }
}

TEST_P(InvariantTest, SubtractionIsAdditionOfTheNegation) {
    Rng r = rng(2);
    for (int i = 0; i < kIterations; i++) {
        const Number a = r.any(F()), b = r.any(F());
        ASSERT_TRUE(same(sub(F(), a, b), add(F(), a, neg(F(), b)))) << hex(a) << " - " << hex(b);
    }
}

TEST_P(InvariantTest, AddingAndSubtractingTheSameValueRestoresTheOriginal) {
    Rng r = rng(3);
    for (int i = 0; i < kIterations; i++) {
        const Number a = r.any(F()), b = r.any(F());
        ASSERT_TRUE(same(a, sub(F(), add(F(), a, b), b))) << hex(a) << " + " << hex(b);
        ASSERT_TRUE(same(a, add(F(), sub(F(), a, b), b))) << hex(a) << " - " << hex(b);
    }
}

TEST_P(InvariantTest, NegationIsItsOwnInverseAndCancelsTheValue) {
    Rng r = rng(4);
    for (int i = 0; i < kIterations; i++) {
        const Number a = r.any(F());
        ASSERT_TRUE(same(a, neg(F(), neg(F(), a)))) << hex(a);
        ASSERT_TRUE(same(zero(F()), add(F(), a, neg(F(), a)))) << hex(a);
        ASSERT_TRUE(same(zero(F()), sub(F(), a, a))) << hex(a);
    }
}

TEST_P(InvariantTest, IncrementEqualsAddingOneUlp) {
    Rng r = rng(5);
    for (int i = 0; i < kIterations; i++) {
        const Number a = r.any(F());
        ASSERT_TRUE(same(add(F(), a, ulp(F())), inc(F(), a))) << hex(a);
    }
}

TEST_P(InvariantTest, SubtractionRespectsTranslation) {
    Rng r = rng(6);
    for (int i = 0; i < kIterations; i++) {
        const Number a = r.any(F()), b = r.any(F()), x = r.any(F());
        ASSERT_TRUE(same(sub(F(), a, b), sub(F(), add(F(), a, x), add(F(), b, x)))) << "shifting both by " << hex(x);
    }
}

// =================================================================== algebraic invariants: ordering
TEST_P(InvariantTest, ComparisonIsAStrictTotalOrder) {
    Rng r = rng(7);
    for (int i = 0; i < kIterations; i++) {
        const Number a = r.any(F()), b = r.any(F()), c = r.any(F());
        const bool equal = (a == b);
        ASSERT_FALSE(gt(F(), a, a)) << "irreflexive " << hex(a);
        ASSERT_TRUE(gte(F(), a, a)) << "reflexive " << hex(a);
        // exactly one of a > b, b > a, a == b
        ASSERT_EQ(1, int(gt(F(), a, b)) + int(gt(F(), b, a)) + int(equal)) << hex(a) << " vs " << hex(b);
        ASSERT_EQ(gt(F(), a, b) || equal, gte(F(), a, b)) << "gte == gt || equal";
        if (gt(F(), a, b) && gt(F(), b, c)) {
            ASSERT_TRUE(gt(F(), a, c)) << "transitivity";
        }
    }
}

TEST_P(InvariantTest, ComparisonAgreesWithTheSignOfTheDifference) {
    // For values that are small enough that a - b cannot wrap, a > b exactly when a - b is positive.
    Rng r = rng(8);
    for (int i = 0; i < kIterations; i++) {
        const Number a = r.small(F(), 20), b = r.small(F(), 20);
        const Number d = sub(F(), a, b);
        const bool positive = !isNegative(d) && !isZero(d);
        ASSERT_EQ(positive, gt(F(), a, b)) << hex(a) << " > " << hex(b);
        ASSERT_EQ(!isNegative(d), gte(F(), a, b)) << hex(a) << " >= " << hex(b);
    }
}

TEST_P(InvariantTest, ComparisonRespectsTranslationAndNeighbours) {
    Rng r = rng(9);
    for (int i = 0; i < kIterations; i++) {
        const Number a = r.small(F(), 18), b = r.small(F(), 18), x = r.small(F(), 18);
        ASSERT_EQ(gt(F(), a, b), gt(F(), add(F(), a, x), add(F(), b, x))) << "adding " << hex(x) << " to both";
        ASSERT_TRUE(gt(F(), inc(F(), a), a)) << "a + ulp > a for " << hex(a);
        ASSERT_TRUE(gt(F(), a, sub(F(), a, ulp(F())))) << "a > a - ulp for " << hex(a);
        ASSERT_EQ(gt(F(), a, b), gt(F(), neg(F(), b), neg(F(), a))) << "negation reverses the order";
    }
}

// =================================================================== multiplication
// The exact product is truncated toward zero, so multiplication is not exactly associative or
// distributive. Each approximate law below states the maximum error that truncation can produce.
TEST_P(InvariantTest, MultiplicationIsCommutativeAndSignSymmetric) {
    Rng r = rng(10);
    for (int i = 0; i < kIterations; i++) {
        const Number a = r.small(F(), 14), b = r.small(F(), 14);
        const Number p = mul(F(), a, b);
        ASSERT_TRUE(same(p, mul(F(), b, a))) << "commutativity";
        ASSERT_TRUE(same(neg(F(), p), mul(F(), neg(F(), a), b))) << "(-a)*b == -(a*b)";
        ASSERT_TRUE(same(neg(F(), p), mul(F(), a, neg(F(), b)))) << "a*(-b) == -(a*b)";
        ASSERT_TRUE(same(p, mul(F(), neg(F(), a), neg(F(), b)))) << "(-a)*(-b) == a*b";
    }
}

TEST_P(InvariantTest, SquaringGivesExactlyTheWordsOfMultiplyingByItself) {
    // Not a tolerance: the renderer replaced mul(x, x) by sqr(x), and the image must not change by one bit.
    Rng r = rng(40);
    for (int i = 0; i < 50 * kIterations; i++) {
        const Number a = r.any(F());
        ASSERT_TRUE(same(mul(F(), a, a), sqr(F(), a))) << hex(a);
        if (!same(minValue(F()), a)) {
            ASSERT_TRUE(same(sqr(F(), a), sqr(F(), neg(F(), a)))) << "(-a)^2 == a^2 for " << hex(a);
        }
    }
    // The values the iteration actually squares: magnitude below 4 with a full random fraction.
    for (int i = 0; i < 50 * kIterations; i++) {
        const Number a = r.small(F(), 2);
        ASSERT_TRUE(same(mul(F(), a, a), sqr(F(), a))) << hex(a);
    }
}

TEST_P(InvariantTest, MultiplyingByTwoIsDoubling) {
    Rng r = rng(11);
    for (int i = 0; i < kIterations; i++) {
        const Number a = r.small(F(), 20);
        ASSERT_TRUE(same(add(F(), a, a), mul(F(), a, two()))) << hex(a);
    }
}

TEST_P(InvariantTest, MultiplyingByOneHalfHalvesTheMagnitudeAndTruncates) {
    Rng r = rng(12);
    for (int i = 0; i < kIterations; i++) {
        const Number a = r.small(F(), 20);
        const Number halved = absValue(F(), mul(F(), a, half()));
        const Number rest = sub(F(), absValue(F(), a), add(F(), halved, halved));   // |a| - 2 * |a/2| is 0 or 1 ulp
        ASSERT_TRUE(isZero(rest) || same(ulp(F()), rest)) << hex(a);
        ASSERT_EQ(isNegative(a) && !isZero(halved), isNegative(mul(F(), a, half()))) << "sign preserved";
    }
}

TEST_P(InvariantTest, MultiplicationIsMonotonicForNonNegativeFactors) {
    Rng r = rng(13);
    for (int i = 0; i < kIterations; i++) {
        Number a = absValue(F(), r.small(F(), 12)), b = absValue(F(), r.small(F(), 12));
        const Number c = absValue(F(), r.small(F(), 12));
        if (gt(F(), a, b)) std::swap(a, b);                       // now a <= b
        ASSERT_TRUE(gte(F(), mul(F(), b, c), mul(F(), a, c))) << "a<=b => a*c <= b*c";
    }
}

TEST_P(InvariantTest, MultiplicationDistributesOverAdditionWithinTwoUlps) {
    // T(a(b+c)) - (T(ab) + T(ac)): each T() is off by less than 1 ulp, so the difference is at most 2 ulps.
    Rng r = rng(14);
    for (int i = 0; i < kIterations; i++) {
        const Number a = r.small(F(), 10), b = r.small(F(), 10), c = r.small(F(), 10);
        const Number left = mul(F(), a, add(F(), b, c));
        const Number right = add(F(), mul(F(), a, b), mul(F(), a, c));
        ASSERT_TRUE(withinUlps(F(), left, right, 2)) << "a=" << hex(a) << " b=" << hex(b) << " c=" << hex(c);
    }
}

TEST_P(InvariantTest, MultiplicationIsAssociativeWithinFourUlpsForFactorsBelowOne) {
    // With |a|,|b|,|c| < 1 the truncation error of the inner product shrinks when multiplied again,
    // so both groupings are within 2 ulps of the true product and thus within 4 ulps of each other.
    Rng r = rng(15);
    for (int i = 0; i < kIterations; i++) {
        const Number a = r.small(F(), 0), b = r.small(F(), 0), c = r.small(F(), 0);
        const Number left = mul(F(), mul(F(), a, b), c);
        const Number right = mul(F(), a, mul(F(), b, c));
        ASSERT_TRUE(withinUlps(F(), left, right, 4)) << "a=" << hex(a) << " b=" << hex(b) << " c=" << hex(c);
    }
}

TEST_P(InvariantTest, DoublingAFactorDoublesTheProductWithinOneUlp) {
    // T(2x) - 2 T(x) is 0 or 1 ulp for any x.
    Rng r = rng(16);
    for (int i = 0; i < kIterations; i++) {
        const Number a = r.small(F(), 10), b = r.small(F(), 10);
        const Number viaFactor = mul(F(), add(F(), a, a), b);
        const Number doubledProduct = add(F(), mul(F(), a, b), mul(F(), a, b));
        ASSERT_TRUE(withinUlps(F(), viaFactor, doubledProduct, 1)) << "a=" << hex(a) << " b=" << hex(b);
    }
}

// =================================================================== division
TEST_P(InvariantTest, DivisionIsSignSymmetric) {
    Rng r = rng(20);
    for (int i = 0; i < kIterations; i++) {
        const Number a = r.small(F(), 12), b = r.nonZero(F(), 12);
        const Number q = div(F(), a, b);
        ASSERT_TRUE(same(neg(F(), q), div(F(), neg(F(), a), b))) << "(-a)/b == -(a/b)";
        ASSERT_TRUE(same(neg(F(), q), div(F(), a, neg(F(), b)))) << "a/(-b) == -(a/b)";
        ASSERT_TRUE(same(q, div(F(), neg(F(), a), neg(F(), b)))) << "(-a)/(-b) == a/b";
    }
}

TEST_P(InvariantTest, DivisionIsUnchangedByScalingBothOperands) {
    // (2a)/(2b) is the same rational number as a/b, so the truncated quotient must be identical.
    Rng r = rng(21);
    for (int i = 0; i < kIterations; i++) {
        const Number a = r.small(F(), 12), b = r.nonZero(F(), 12);
        ASSERT_TRUE(same(div(F(), a, b), div(F(), add(F(), a, a), add(F(), b, b)))) << hex(a) << " / " << hex(b);
    }
}

TEST_P(InvariantTest, QuotientTimesDivisorBracketsTheDividend) {
    // For q = trunc(a/b) with a, b > 0:  q*b <= a < (q + ulp)*b.  The library multiplication truncates
    // (error < 1 ulp), so the computed remainder a - T(q*b) must satisfy 0 <= remainder < b + ulp.
    Rng r = rng(22);
    for (int i = 0; i < kIterations; i++) {
        const Number a = absValue(F(), r.small(F(), 12));
        Number b = absValue(F(), r.nonZero(F(), 12));
        const Number q = div(F(), a, b);
        const Number remainder = sub(F(), a, mul(F(), q, b));
        ASSERT_FALSE(isNegative(remainder)) << "q*b exceeded a: a=" << hex(a) << " b=" << hex(b) << " q=" << hex(q);
        ASSERT_TRUE(gt(F(), add(F(), b, ulp(F())), remainder))
            << "quotient too small: a=" << hex(a) << " b=" << hex(b) << " q=" << hex(q);
    }
}

TEST_P(InvariantTest, MultiplyThenDivideRecoversTheFactorWithinTwoUlps) {
    // For |b| >= 1: p = T(a*b) is within 1 ulp of a*b, so p/b is within 1 ulp of a, and truncating it
    // adds less than another ulp.
    Rng r = rng(23);
    int checked = 0;
    for (int i = 0; i < kIterations * 4 && checked < kIterations; i++) {
        const Number a = r.small(F(), 12), b = r.nonZero(F(), 12);
        if (!atLeastOne(b)) continue;
        checked++;
        const Number recovered = div(F(), mul(F(), a, b), b);
        ASSERT_TRUE(withinUlps(F(), a, recovered, 2)) << "a=" << hex(a) << " b=" << hex(b) << " got " << hex(recovered);
    }
    ASSERT_GT(checked, 0);
}

TEST_P(InvariantTest, DividingByOneHalfDoubles) {
    Rng r = rng(24);
    for (int i = 0; i < kIterations; i++) {
        const Number a = r.small(F(), 20);
        ASSERT_TRUE(same(add(F(), a, a), div(F(), a, half()))) << hex(a);
    }
}

// =================================================================== conversions
TEST_P(InvariantTest, DoubleToFixedIsSignSymmetricAndMonotonic) {
    std::uniform_real_distribution<double> dist(-1000.0, 1000.0);
    std::mt19937_64 g(777 + GetParam());
    for (int i = 0; i < kIterations; i++) {
        const double x = dist(g), y = dist(g);
        ASSERT_TRUE(same(neg(F(), fromDouble(F(), x)), fromDouble(F(), -x))) << "x=" << x;
        const double lo = std::fmin(x, y), hi = std::fmax(x, y);
        ASSERT_FALSE(gt(F(), fromDouble(F(), lo), fromDouble(F(), hi))) << lo << " <= " << hi;
    }
}

TEST_P(InvariantTest, FixedToDoubleIsSignSymmetric) {
    Rng r = rng(31);
    for (int i = 0; i < kIterations; i++) {
        const Number a = r.any(F());
        if (same(minValue(F()), a)) continue;                     // -min is not representable
        ASSERT_EQ(-toDouble(F(), a), toDouble(F(), neg(F(), a))) << hex(a);
    }
}

TEST_P(InvariantTest, RoundTripThroughDoubleStaysWithinOneUlpPlusDoubleRounding) {
    // fixed(x) is x truncated to the format, back to double it differs from x by < 1 format ulp, plus the
    // rounding of the double itself (relative 2^-52, with some slack for the bit-by-bit summation).
    std::uniform_real_distribution<double> dist(-1000.0, 1000.0);
    std::mt19937_64 g(4242 + GetParam());
    for (int i = 0; i < kIterations; i++) {
        const double x = dist(g) * std::pow(2.0, -static_cast<int>(g() % 40));
        const double back = toDouble(F(), fromDouble(F(), x));
        const double tolerance = std::ldexp(1.0, -fpa::WORD_BITS * F()) + std::fabs(x) * 4e-16;
        ASSERT_LE(std::fabs(back - x), tolerance) << "x=" << x << " back=" << back;
    }
}

TEST_P(InvariantTest, DoublesWithFewBitsSurviveTheRoundTripExactly) {
    // k * 2^-e with k < 2^40 and e <= 32F is exactly representable both as a double and in the format.
    std::mt19937_64 g(99 + GetParam());
    const int maxExponent = std::min(40, fpa::WORD_BITS * F());
    for (int i = 0; i < kIterations; i++) {
        const double k = static_cast<double>(g() % (1ull << 40)) * ((g() & 1) ? 1.0 : -1.0);
        const int e = static_cast<int>(g() % (maxExponent + 1));
        const double x = std::ldexp(k, -e);
        if (std::fabs(x) >= 2147483648.0) continue;
        ASSERT_EQ(x, toDouble(F(), fromDouble(F(), x))) << "k=" << k << " e=" << e;
    }
}

// =================================================================== end-to-end partial oracle
TEST_P(InvariantTest, MandelbrotIterationTracksADoublePrecisionReference) {
    // z -> z^2 + c exercises mul, add and sub together. For |c| < 0.2 the orbit stays below 0.3 and the
    // map is contracting, so rounding errors do not grow: after 30 steps the fixed-point orbit must
    // match a double-precision orbit to within a generous multiple of the format's ulp (plus the double's
    // own rounding error).
    std::uniform_real_distribution<double> dist(-0.14, 0.14);
    std::mt19937_64 g(31337 + GetParam());
    const double tolerance = 200.0 * std::ldexp(1.0, -fpa::WORD_BITS * F()) + 1e-12;
    for (int trial = 0; trial < 50; trial++) {
        const double cr = dist(g), ci = dist(g);
        const Number cReal = fromDouble(F(), cr), cImag = fromDouble(F(), ci);
        Number x = zero(F()), y = zero(F());
        double dx = 0, dy = 0;
        for (int step = 0; step < 30; step++) {
            const Number x2 = mul(F(), x, x), y2 = mul(F(), y, y);
            const Number xy = mul(F(), x, y);
            x = add(F(), sub(F(), x2, y2), cReal);
            y = add(F(), add(F(), xy, xy), cImag);
            const double ndx = dx * dx - dy * dy + cr;
            dy = 2 * dx * dy + ci;
            dx = ndx;
            ASSERT_NEAR(dx, toDouble(F(), x), tolerance) << "trial " << trial << " step " << step << " (real part)";
            ASSERT_NEAR(dy, toDouble(F(), y), tolerance) << "trial " << trial << " step " << step << " (imaginary part)";
        }
    }
}

FPA_INSTANTIATE_FOR_ALL_FORMATS(InvariantTest);
