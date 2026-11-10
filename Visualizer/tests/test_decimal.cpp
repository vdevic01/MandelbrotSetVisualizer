// Tests of fpa::decimalToFixedPoint: exact conversion of decimal text to fixed point, truncating toward zero.
// Expectations are derived from the mathematics (binary fractions, powers of two, truncation rules), not
// from the library. Runs for every supported fraction size.

#include <stdexcept>
#include <string>
#include <vector>

#include "test_support.h"

using namespace fpt;

namespace {

Number dec(int f, const std::string& text) {
    Number n = filled(f, 0xDEADBEEFu);   // a dirty buffer: every word must be overwritten
    fpa::decimalToFixedPoint(text, n.data(), f);
    return n;
}

// The exact decimal expansion of 2^-k ("0.5", "0.25", "0.125", ...), computed by halving digit by digit.
std::string powerOfTwoFraction(int k) {
    std::string digits = "5";
    for (int i = 1; i < k; i++) {
        int carry = 0;
        for (char& c : digits) {
            const int cur = carry * 10 + (c - '0');
            c = static_cast<char>('0' + cur / 2);
            carry = cur % 2;
        }
        if (carry) digits.push_back('5');
    }
    return "0." + digits;
}

class DecimalTest : public ::testing::TestWithParam<int> {
protected:
    int F() const { return GetParam(); }
    Number ref(const std::string& text) const { return dec(F(), text); }
};

TEST_P(DecimalTest, WholeNumbers) {
    EXPECT_TRUE(same(zero(F()), ref("0")));
    EXPECT_TRUE(same(one(F()), ref("1")));
    EXPECT_TRUE(same(make(F(), {2}), ref("2")));
    EXPECT_TRUE(same(make(F(), {1234567}), ref("1234567")));
    EXPECT_TRUE(same(make(F(), {kSignBit - 1}), ref("2147483647")));
    EXPECT_TRUE(same(make(F(), {kAllOnes}), ref("-1")));
    EXPECT_TRUE(same(make(F(), {kSignBit + 1}), ref("-2147483647")));
}

TEST_P(DecimalTest, BinaryFractionsAreExact) {
    EXPECT_TRUE(same(make(F(), {0, 0x80000000u}), ref("0.5")));
    EXPECT_TRUE(same(make(F(), {0, 0x40000000u}), ref("0.25")));
    EXPECT_TRUE(same(make(F(), {0, 0xC0000000u}), ref("0.75")));
    EXPECT_TRUE(same(make(F(), {1, 0x80000000u}), ref("1.5")));
    EXPECT_TRUE(same(make(F(), {0, 0x20000000u}), ref("0.125")));
}

TEST_P(DecimalTest, NegativeIsTwosComplement) {
    EXPECT_TRUE(same(make(F(), {kAllOnes, 0x80000000u}), ref("-0.5")));
    EXPECT_TRUE(same(make(F(), {kAllOnes - 1, 0x80000000u}), ref("-1.5")));
    EXPECT_TRUE(same(neg(F(), ref("0.25")), ref("-0.25")));
}

TEST_P(DecimalTest, NonBinaryFractionIsTruncatedNotRounded) {
    // 0.1 = 0x0.19999999 99999999 99999999 ... (the digits repeat), cut after the last fraction word.
    Number expected = zero(F());
    expected[1] = 0x19999999u;
    for (int i = 2; i < words(F()); i++) expected[i] = 0x99999999u;
    EXPECT_TRUE(same(expected, ref("0.1")));
}

TEST_P(DecimalTest, NegativeValueIsTheNegatedTruncatedMagnitude) {
    // Truncation is toward zero for both signs, so -x is exactly the two's complement of +x.
    for (const char* text : {"0.1", "0.3", "0.7", "1.1", "12.345678901234567890123456789", "0.000001", "2147483647.9"}) {
        EXPECT_TRUE(same(neg(F(), ref(text)), ref(std::string("-") + text))) << text;
    }
}

TEST_P(DecimalTest, SmallerThanOneUnitTruncatesToZeroForBothSigns) {
    const std::string half = powerOfTwoFraction(32 * F() + 1);   // half of the smallest step
    EXPECT_TRUE(same(zero(F()), ref(half)));
    EXPECT_TRUE(same(zero(F()), ref("-" + half)));
}

TEST_P(DecimalTest, SmallestStepIsExact) {
    const std::string step = powerOfTwoFraction(32 * F());
    EXPECT_TRUE(same(ulp(F()), ref(step)));
    EXPECT_TRUE(same(neg(F(), ulp(F())), ref("-" + step)));
    // A little more than one step is still one step; the extra digits are dropped.
    EXPECT_TRUE(same(ulp(F()), ref(step + "9")));
}

TEST_P(DecimalTest, LargestValueAndJustPastIt) {
    // 60 nines is closer to 1 than the largest fraction of any format is (2^-192 is about 1.6e-58).
    const std::string nines(60, '9');
    EXPECT_TRUE(same(maxValue(F()), ref("2147483647." + nines)));
    EXPECT_THROW(ref("2147483648"), std::out_of_range);
    EXPECT_THROW(ref("-2147483649"), std::out_of_range);
    EXPECT_THROW(ref("99999999999999999999"), std::out_of_range);
}

TEST_P(DecimalTest, EquivalentSpellingsGiveTheSameNumber) {
    const Number twelveAndAHalf = ref("12.5");
    for (const char* text : {"+12.5", "012.5", "12.50000", "0012.500", "125e-1", "1.25E+1", "1.25e1", "0.125e2", "12.5e0"})
        EXPECT_TRUE(same(twelveAndAHalf, ref(text))) << text;
    EXPECT_TRUE(same(ref("0.5"), ref(".5")));
    EXPECT_TRUE(same(ref("5"), ref("5.")));
    EXPECT_TRUE(same(ref("1500"), ref("1.5E+3")));
    EXPECT_TRUE(same(ref("-22.5"), ref("-2.25E+1")));
    EXPECT_TRUE(same(ref("0.00000015"), ref("1.5e-7")));
    EXPECT_TRUE(same(ref("0.00000015"), ref("15e-8")));
}

TEST_P(DecimalTest, ZeroInEveryForm) {
    for (const char* text : {"0", "-0", "+0", "0.0", "-0.000", "0e5", "0e-5", ".0", "00"})
        EXPECT_TRUE(same(zero(F()), ref(text))) << text;
}

TEST_P(DecimalTest, AgreesWithTheDoubleConversionForExactDoubles) {
    // These doubles are exact binary fractions, so both conversions must give the same words.
    const std::pair<const char*, double> cases[] = {
        {"0.375", 0.375}, {"1234.5", 1234.5}, {"-3.140625", -3.140625},
        {"0.0009765625", 0.0009765625}, {"-1024", -1024.0}, {"100.03125", 100.03125},
    };
    for (const auto& [text, value] : cases) {
        EXPECT_TRUE(same(fromDouble(F(), value), ref(text))) << text;
    }
}

TEST_P(DecimalTest, PrefixOfALongerFormat) {
    // Truncating a positive value to fewer fraction words gives the leading words of the longer format.
    const Number longest = dec(fpa::MAX_FRACTION_PART, "3.14159265358979323846264338327950288419716939937510");
    const Number n = ref("3.14159265358979323846264338327950288419716939937510");
    for (size_t i = 0; i < n.size(); i++) EXPECT_EQ(longest[i], n[i]) << "word " << i;
}

TEST_P(DecimalTest, TextThatIsNotANumberIsRejected) {
    for (const char* text : {"", " ", "abc", "1.2.3", "--1", "+-1", "-", "+", ".", "1e", "1e+", "1e-", "1e5x", "1x", "e5",
                             " 1", "1 ", "0x10", "1,5", "1_000", "1e99999", "1e+-5", "inf", "nan"}) {
        EXPECT_THROW(ref(text), std::invalid_argument) << "'" << text << "'";
    }
}

TEST_P(DecimalTest, ExponentsOutOfAnyUsefulRangeAreRejected) {
    EXPECT_THROW(ref("1e9999"), std::out_of_range);
    EXPECT_THROW(ref("1e-9999"), std::out_of_range);
}

TEST_P(DecimalTest, VeryLongTextIsConvertedWithEveryDigit) {
    // A 1 followed by a long run of zeros and then a final 1: the final 1 is far below this format's step, the
    // leading 1 is not.
    std::string text = "1." + std::string(500, '0') + "1";
    EXPECT_TRUE(same(one(F()), ref(text)));
    // And a long number whose last digits do matter: the fraction 0.5 followed by lots of zeros.
    EXPECT_TRUE(same(make(F(), {0, 0x80000000u}), ref("0.5" + std::string(400, '0'))));
}

FPA_INSTANTIATE_FOR_ALL_FORMATS(DecimalTest);

TEST(DecimalFormat, UnsupportedFractionSizesAreRejected) {
    fpa::uint out[fpa::MAX_FP_SIZE + 2] = {};
    EXPECT_THROW(fpa::decimalToFixedPoint("1", out, 0), std::invalid_argument);
    EXPECT_THROW(fpa::decimalToFixedPoint("1", out, fpa::MAX_FRACTION_PART + 1), std::invalid_argument);
    EXPECT_THROW(fpa::decimalToFixedPoint("1", out, -1), std::invalid_argument);
}

TEST(DecimalFormat, DefaultFractionSizeIsUsedWhenOmitted) {
    fpa::uint out[fpa::FP_SIZE] = {};
    fpa::decimalToFixedPoint("0.5", out);
    EXPECT_EQ(0u, out[0]);
    EXPECT_EQ(0x80000000u, out[1]);
}

// A deep-zoom view the app really renders. Its four coordinates are exact decimal values of 3-fraction-word
// numbers, so they must convert back to exactly those words.
TEST(DecimalRegression, DeepZoomCoordinatesRoundTripToTheirWords) {
    struct Case { const char* text; Number words; };
    const Case cases[] = {
        {"-0.153004885037500013709068298335026161966057141639537312292895787635416127159260213375091552734375",
         {4294967295u, 3637816318u, 2730300863u, 4039731417u}},
        {"-0.15280969528750001370906829833856147374297196178717932364687914770229326677508652210235595703125",
         {4294967295u, 3638654652u, 981237348u, 3835888558u}},
        {"1.03961137030000000000232830642617368325708476159226667685786527073332763393409550189971923828125",
         {1u, 170129539u, 4244482999u, 3226762018u}},
        {"1.03975776261250000000232830642667764304528269370318208260350889560186260496266186237335205078125",
         {1u, 170758290u, 785201715u, 2000138050u}},
    };
    for (const Case& c : cases) EXPECT_TRUE(same(c.words, dec(3, c.text))) << c.text;
}

// Shorter decimals are truncated at the last word, not rounded: the first three words match the values above,
// the last one is smaller because the digits beyond the 21st are missing.
TEST(DecimalRegression, TwentyOneDigitCoordinatesAreTruncated) {
    EXPECT_TRUE(same(Number({4294967295u, 3637816318u, 2730300863u, 4124370732u}), dec(3, "-0.153004885037500013708")));
    EXPECT_TRUE(same(Number({1u, 170129539u, 4244482999u, 3200750903u}), dec(3, "1.039611370300000000002")));
}

}  // namespace
