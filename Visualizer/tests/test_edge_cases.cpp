// Hand-written edge cases with expectations derived from the mathematics (not from the library):
// range limits, carries through every word, sign handling, truncation direction, conversions and
// error handling. Runs for every supported fraction size.

#include <climits>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "test_support.h"

using namespace fpt;

namespace {

class EdgeTest : public ::testing::TestWithParam<int> {
protected:
    int F() const { return GetParam(); }

    Number half() const { return make(F(), {0, kSignBit}); }
    Number two() const { return make(F(), {2}); }
    Number three() const { return make(F(), {3}); }
    // 0.010101...b: one third truncated to the available fraction words.
    Number oneThird() const { Number n = zero(F()); for (int i = 1; i < words(F()); i++) n[i] = 0x55555555u; return n; }
    // 0.101010...b: two thirds truncated.
    Number twoThirds() const { Number n = zero(F()); for (int i = 1; i < words(F()); i++) n[i] = 0xAAAAAAAAu; return n; }

    // Strictly ascending list of important values, used to check the total order.
    std::vector<Number> ascending() const {
        const int f = F();
        return {minValue(f), add(f, minValue(f), ulp(f)), neg(f, one(f)), neg(f, ulp(f)), zero(f), ulp(f),
                half(), one(f), two(), sub(f, maxValue(f), ulp(f)), maxValue(f)};
    }
};

}  // namespace

// ---------------------------------------------------------------------------------- inc / cmpl
TEST_P(EdgeTest, IncrementCarriesThroughEveryWordPosition) {
    for (int j = 0; j < words(F()); j++) {
        // 0 ... 0 [0] 1 1 ... 1  ->  0 ... 0 [1] 0 0 ... 0 : the carry must ripple to word j and stop.
        Number in = zero(F());
        for (int k = j + 1; k < words(F()); k++) in[k] = kAllOnes;
        Number expected = zero(F());
        expected[j] = 1;
        EXPECT_TRUE(same(expected, inc(F(), in))) << "carry into word " << j;
    }
}

TEST_P(EdgeTest, IncrementWrapsAtTheTopOfTheRange) {
    EXPECT_TRUE(same(minValue(F()), inc(F(), maxValue(F()))));
    EXPECT_TRUE(same(zero(F()), inc(F(), filled(F(), kAllOnes))));   // -1 + 1 == 0
    EXPECT_TRUE(same(ulp(F()), inc(F(), zero(F()))));
}

TEST_P(EdgeTest, ComplementEdgeValues) {
    EXPECT_TRUE(same(zero(F()), neg(F(), zero(F()))));
    EXPECT_TRUE(same(minValue(F()), neg(F(), minValue(F())))) << "-min does not fit and wraps back to min";
    EXPECT_TRUE(same(filled(F(), kAllOnes), neg(F(), ulp(F()))));
    EXPECT_TRUE(same(ulp(F()), neg(F(), filled(F(), kAllOnes))));
    EXPECT_TRUE(same(inc(F(), minValue(F())), neg(F(), maxValue(F())))) << "-max == min + 1";
}

// ---------------------------------------------------------------------------------- add / sub
TEST_P(EdgeTest, AddCarriesAcrossAllWords) {
    Number almostOne = zero(F());                       // 0.111...1b = 1.0 - ulp
    for (int i = 1; i < words(F()); i++) almostOne[i] = kAllOnes;
    EXPECT_TRUE(same(one(F()), add(F(), almostOne, ulp(F()))));
}

TEST_P(EdgeTest, AddWrapsOnOverflow) {
    EXPECT_TRUE(same(minValue(F()), add(F(), maxValue(F()), ulp(F()))));
    EXPECT_TRUE(same(zero(F()), add(F(), minValue(F()), minValue(F()))));
    Number twiceMax = filled(F(), kAllOnes);            // 2 * max mod 2^B == -2 ulp
    twiceMax.back() = kAllOnes - 1;
    EXPECT_TRUE(same(twiceMax, add(F(), maxValue(F()), maxValue(F()))));
}

TEST_P(EdgeTest, SubWrapsOnOverflow) {
    EXPECT_TRUE(same(maxValue(F()), sub(F(), minValue(F()), ulp(F()))));
    EXPECT_TRUE(same(minValue(F()), sub(F(), zero(F()), minValue(F()))));
    EXPECT_TRUE(same(neg(F(), ulp(F())), sub(F(), zero(F()), ulp(F()))));
}

TEST_P(EdgeTest, SubtractingAValueFromItselfGivesZero) {
    for (const Number& x : ascending()) EXPECT_TRUE(same(zero(F()), sub(F(), x, x))) << hex(x);
}

TEST_P(EdgeTest, ZeroIsTheAdditiveIdentity) {
    for (const Number& x : ascending()) {
        EXPECT_TRUE(same(x, add(F(), x, zero(F())))) << hex(x);
        EXPECT_TRUE(same(x, add(F(), zero(F()), x))) << hex(x);
        EXPECT_TRUE(same(x, sub(F(), x, zero(F())))) << hex(x);
    }
}

// ---------------------------------------------------------------------------------- comparisons
TEST_P(EdgeTest, ComparisonsFormAStrictTotalOrderOnTheKeyValues) {
    const auto v = ascending();
    for (size_t i = 0; i < v.size(); i++) {
        for (size_t j = 0; j < v.size(); j++) {
            if (i == j) {
                EXPECT_FALSE(gt(F(), v[i], v[j])) << hex(v[i]);
                EXPECT_TRUE(gte(F(), v[i], v[j])) << hex(v[i]);
            } else if (i > j) {
                EXPECT_TRUE(gt(F(), v[i], v[j])) << hex(v[i]) << " > " << hex(v[j]);
                EXPECT_TRUE(gte(F(), v[i], v[j])) << hex(v[i]) << " >= " << hex(v[j]);
            } else {
                EXPECT_FALSE(gt(F(), v[i], v[j])) << hex(v[i]) << " > " << hex(v[j]);
                EXPECT_FALSE(gte(F(), v[i], v[j])) << hex(v[i]) << " >= " << hex(v[j]);
            }
        }
    }
}

TEST_P(EdgeTest, ValuesDifferingInOnlyTheLastWordAreOrdered) {
    Number a = make(F(), {0, 7});
    a.back() = 100;
    Number b = a;
    b.back() = 101;
    EXPECT_TRUE(gt(F(), b, a));
    EXPECT_FALSE(gt(F(), a, b));
    EXPECT_FALSE(gte(F(), a, b));
}

TEST_P(EdgeTest, ValuesDifferingInOnlyTheFirstWordAreOrdered) {
    const Number a = make(F(), {1});
    const Number b = make(F(), {2});
    EXPECT_TRUE(gt(F(), b, a));
    EXPECT_FALSE(gt(F(), a, b));
    const Number negA = neg(F(), a), negB = neg(F(), b);   // -1 > -2
    EXPECT_TRUE(gt(F(), negA, negB));
    EXPECT_FALSE(gt(F(), negB, negA));
}

// ---------------------------------------------------------------------------------- multiplication
TEST_P(EdgeTest, MultiplyExactCases) {
    const int f = F();
    EXPECT_TRUE(same(make(f, {0, 0x40000000u}), mul(f, half(), half()))) << "0.5 * 0.5 == 0.25";
    EXPECT_TRUE(same(one(f), mul(f, two(), half()))) << "2 * 0.5 == 1";
    EXPECT_TRUE(same(one(f), mul(f, neg(f, one(f)), neg(f, one(f))))) << "(-1) * (-1) == 1";
    EXPECT_TRUE(same(make(f, {6}), mul(f, two(), three())));
    EXPECT_TRUE(same(neg(f, make(f, {6})), mul(f, neg(f, two()), three())));
}

TEST_P(EdgeTest, MultiplyIdentitiesAndZero) {
    const int f = F();
    for (const Number& x : ascending()) {
        EXPECT_TRUE(same(x, mul(f, x, one(f)))) << "x * 1 for " << hex(x);
        EXPECT_TRUE(same(x, mul(f, one(f), x))) << "1 * x for " << hex(x);
        EXPECT_TRUE(same(zero(f), mul(f, x, zero(f)))) << "x * 0 for " << hex(x);
        EXPECT_TRUE(same(zero(f), mul(f, zero(f), x))) << "0 * x for " << hex(x);
        if (!same(minValue(f), x)) {
            EXPECT_TRUE(same(neg(f, x), mul(f, x, neg(f, one(f))))) << "x * -1 for " << hex(x);
        }
    }
}

TEST_P(EdgeTest, MultiplyTruncatesTowardZero) {
    const int f = F();
    EXPECT_TRUE(same(zero(f), mul(f, ulp(f), ulp(f))));
    EXPECT_TRUE(same(zero(f), mul(f, half(), ulp(f)))) << "0.5 ulp rounds down, not up";
    // The result of (-ulp) * (+ulp) is -2^-(64F), which truncates to 0, not to -ulp (that would be floor).
    EXPECT_TRUE(same(zero(f), mul(f, neg(f, ulp(f)), ulp(f))));
    EXPECT_TRUE(same(zero(f), mul(f, neg(f, half()), ulp(f))));
    EXPECT_TRUE(same(ulp(f), mul(f, ulp(f), one(f))));
    EXPECT_TRUE(same(neg(f, ulp(f)), mul(f, neg(f, ulp(f)), one(f))));
}

// ---------------------------------------------------------------------------------- division
TEST_P(EdgeTest, DivideExactCases) {
    const int f = F();
    EXPECT_TRUE(same(half(), div(f, one(f), two())));
    EXPECT_TRUE(same(one(f), div(f, one(f), one(f))));
    EXPECT_TRUE(same(neg(f, half()), div(f, neg(f, one(f)), two())));
    EXPECT_TRUE(same(neg(f, half()), div(f, one(f), neg(f, two()))));
    EXPECT_TRUE(same(half(), div(f, neg(f, one(f)), neg(f, two()))));
    EXPECT_TRUE(same(make(f, {3}), div(f, make(f, {6}), two())));
}

TEST_P(EdgeTest, DivideIdentitiesAndZero) {
    const int f = F();
    for (const Number& x : ascending()) {
        EXPECT_TRUE(same(x, div(f, x, one(f)))) << "x / 1 for " << hex(x);
        if (!isZero(x)) {
            EXPECT_TRUE(same(one(f), div(f, x, x))) << "x / x for " << hex(x);
            EXPECT_TRUE(same(zero(f), div(f, zero(f), x))) << "0 / x for " << hex(x);
        }
    }
}

TEST_P(EdgeTest, DivideTruncatesTowardZero) {
    const int f = F();
    EXPECT_TRUE(same(oneThird(), div(f, one(f), three())));
    EXPECT_TRUE(same(twoThirds(), div(f, two(), three())));
    EXPECT_TRUE(same(neg(f, oneThird()), div(f, neg(f, one(f)), three()))) << "-1/3 truncates toward zero";
    EXPECT_TRUE(same(neg(f, oneThird()), div(f, one(f), neg(f, three()))));
    EXPECT_TRUE(same(zero(f), div(f, ulp(f), two()))) << "half an ulp truncates to 0";
    EXPECT_TRUE(same(zero(f), div(f, neg(f, ulp(f)), two())));
    EXPECT_TRUE(same(ulp(f), div(f, ulp(f), one(f))));
}

// ---------------------------------------------------------------------------------- conversions
TEST_P(EdgeTest, DoubleToFixedExactValues) {
    const int f = F();
    EXPECT_TRUE(same(zero(f), fromDouble(f, 0.0)));
    EXPECT_TRUE(same(zero(f), fromDouble(f, -0.0)));
    EXPECT_TRUE(same(one(f), fromDouble(f, 1.0)));
    EXPECT_TRUE(same(neg(f, one(f)), fromDouble(f, -1.0)));
    EXPECT_TRUE(same(half(), fromDouble(f, 0.5)));
    EXPECT_TRUE(same(make(f, {1, kSignBit}), fromDouble(f, 1.5)));
    EXPECT_TRUE(same(neg(f, make(f, {1, kSignBit})), fromDouble(f, -1.5)));
    EXPECT_TRUE(same(make(f, {0, 1}), fromDouble(f, std::ldexp(1.0, -fpa::WORD_BITS)))) << "2^-32 sets the lowest bit of the first fraction word";
    EXPECT_TRUE(same(ulp(f), fromDouble(f, std::ldexp(1.0, -fpa::WORD_BITS * F())))) << "smallest representable step";
    EXPECT_TRUE(same(make(f, {0x7FFFFFFFu}), fromDouble(f, 2147483647.0)));
    EXPECT_TRUE(same(minValue(f), fromDouble(f, -2147483648.0)));
}

TEST_P(EdgeTest, DoubleToFixedTruncatesTowardZero) {
    const int f = F();
    const double belowUlp = std::ldexp(1.0, -fpa::WORD_BITS * F() - 1);   // half an ulp
    EXPECT_TRUE(same(zero(f), fromDouble(f, belowUlp)));
    EXPECT_TRUE(same(zero(f), fromDouble(f, -belowUlp))) << "negative values truncate toward zero too, not to -ulp";
    EXPECT_TRUE(same(zero(f), fromDouble(f, 5e-324))) << "smallest subnormal";
}

TEST_P(EdgeTest, FixedToDoubleExactValues) {
    const int f = F();
    EXPECT_EQ(0.0, toDouble(f, zero(f)));
    EXPECT_EQ(1.0, toDouble(f, one(f)));
    EXPECT_EQ(-1.0, toDouble(f, neg(f, one(f))));
    EXPECT_EQ(0.5, toDouble(f, half()));
    EXPECT_EQ(1.5, toDouble(f, make(f, {1, kSignBit})));
    EXPECT_EQ(std::ldexp(1.0, -fpa::WORD_BITS * F()), toDouble(f, ulp(f)));
    EXPECT_EQ(-std::ldexp(1.0, -fpa::WORD_BITS * F()), toDouble(f, neg(f, ulp(f))));
    EXPECT_EQ(-2147483648.0, toDouble(f, minValue(f)));
}

TEST_P(EdgeTest, ConversionRoundTripIsLosslessForRepresentableDoubles) {
    const int f = F();
    const double values[] = {0.0, 1.0, -1.0, 0.5, -0.5, 0.375, 1234.5, -98765.4375, 2147483647.0, -2147483648.0,
                             std::ldexp(1.0, -20), std::ldexp(3.0, -33), 1.0 + std::ldexp(1.0, -52)};
    for (double v : values) {
        const double scaled = std::ldexp(v, fpa::WORD_BITS * F());
        if (scaled != std::floor(scaled)) continue;           // needs more fraction bits than this format has
        EXPECT_EQ(v, toDouble(f, fromDouble(f, v))) << v;
    }
}

FPA_INSTANTIATE_FOR_ALL_FORMATS(EdgeTest);

// ---------------------------------------------------------------------------------- error handling
TEST(ErrorHandling, DivisionByZeroThrowsForEveryFormat) {
    for (int f : allFormats()) {
        const Number zeroDivisor = zero(f);
        for (const Number& dividend : {zero(f), one(f), neg(f, one(f)), minValue(f), maxValue(f)}) {
            Number out = zero(f);
            EXPECT_THROW(fpa::divFixed(dividend.data(), zeroDivisor.data(), out.data(), f), std::invalid_argument)
                << "F=" << f << " dividend " << hex(dividend);
        }
    }
}

TEST(ErrorHandling, UnsupportedFractionSizesThrowInEveryFunction) {
    const int badSizes[] = {fpa::MIN_FRACTION_PART - 1, 0, -1, fpa::MAX_FRACTION_PART + 1, 100, INT_MAX, INT_MIN};
    // Buffers large enough for any supported format so that a wrongly accepted call cannot overrun them.
    uint a[fpa::MAX_FP_SIZE + 4] = {}, b[fpa::MAX_FP_SIZE + 4] = {}, c[fpa::MAX_FP_SIZE + 4] = {};
    b[fpa::MAX_FP_SIZE - 1] = 1;
    for (int bad : badSizes) {
        EXPECT_THROW(fpa::addFixed(a, b, c, bad), std::invalid_argument) << bad;
        EXPECT_THROW(fpa::subFixed(a, b, c, bad), std::invalid_argument) << bad;
        EXPECT_THROW(fpa::incFixed(a, c, bad), std::invalid_argument) << bad;
        EXPECT_THROW(fpa::cmplFixed(a, c, bad), std::invalid_argument) << bad;
        EXPECT_THROW(fpa::mulCmplFixed(a, b, c, bad), std::invalid_argument) << bad;
        EXPECT_THROW(fpa::divFixed(a, b, c, bad), std::invalid_argument) << bad;
        EXPECT_THROW(fpa::gtFixed(a, b, bad), std::invalid_argument) << bad;
        EXPECT_THROW(fpa::gteFixed(a, b, bad), std::invalid_argument) << bad;
        EXPECT_THROW(fpa::floatingToFixedPoint(1.0, c, bad), std::invalid_argument) << bad;
        EXPECT_THROW(fpa::fixedToFloatingPoint(a, bad), std::invalid_argument) << bad;
        EXPECT_THROW(fpa::randomFromRange(a, b, c, bad), std::invalid_argument) << bad;
    }
}

TEST(ErrorHandling, RejectedCallsLeaveTheOutputUntouched) {
    uint a[fpa::MAX_FP_SIZE] = {}, b[fpa::MAX_FP_SIZE] = {};
    uint c[fpa::MAX_FP_SIZE];
    for (auto& w : c) w = 0xDEADBEEF;
    EXPECT_THROW(fpa::addFixed(a, b, c, fpa::MAX_FRACTION_PART + 1), std::invalid_argument);
    for (uint w : c) EXPECT_EQ(0xDEADBEEF, w);
}

// ---------------------------------------------------------------------------------- format metadata
TEST(FormatMetadata, SizesAreConsistent) {
    EXPECT_EQ(fpa::WHOLE_PART + 3, fpa::fpSize(3));
    for (int f = fpa::MIN_FRACTION_PART; f <= fpa::MAX_FRACTION_PART; f++) {
        EXPECT_TRUE(fpa::isSupportedFractionPart(f)) << f;
        EXPECT_EQ(fpa::WHOLE_PART + f, fpa::fpSize(f));
        EXPECT_LE(fpa::fpSize(f), fpa::MAX_FP_SIZE);
    }
    EXPECT_FALSE(fpa::isSupportedFractionPart(fpa::MIN_FRACTION_PART - 1));
    EXPECT_FALSE(fpa::isSupportedFractionPart(fpa::MAX_FRACTION_PART + 1));
    EXPECT_TRUE(fpa::isSupportedFractionPart(fpa::FRACTION_PART)) << "the default format must be a supported one";
    EXPECT_EQ(fpa::WORD_BITS, static_cast<int>(sizeof(fpa::uint) * 8));
}

TEST(FormatMetadata, OmittingTheFractionSizeUsesTheDefaultFormat) {
    const int f = fpa::FRACTION_PART;
    const Number a = make(f, {5, 0x12345678u, 0x9ABCDEF0u, 0x0FEDCBA9u});
    const Number b = make(f, {2, 0x87654321u, 0x0BADCAFEu, 0x13572468u});
    Number viaDefault = zero(f);
    fpa::addFixed(a.data(), b.data(), viaDefault.data());
    EXPECT_TRUE(same(add(f, a, b), viaDefault));
    fpa::mulCmplFixed(a.data(), b.data(), viaDefault.data());
    EXPECT_TRUE(same(mul(f, a, b), viaDefault));
    EXPECT_EQ(gt(f, a, b), fpa::gtFixed(a.data(), b.data()));
    EXPECT_EQ(toDouble(f, a), fpa::fixedToFloatingPoint(a.data()));
}
