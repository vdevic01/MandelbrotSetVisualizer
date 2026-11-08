// Minimal reproductions of defects found while reviewing FixedPointArithmetics. Each test states the
// correct behaviour, so it FAILS while the defect exists and passes once it is fixed. The tests are
// intentionally written first (before the fixes) to prove that the fixes are what makes them pass.
//
// The generic vector tests in test_vectors.cpp also trip over these defects (the "...IgnoresPrevious
// OutputContents" and "...WithOutputAliasing..." variants); the tests here are the short versions that
// are easy to read and debug.

#include "test_support.h"

using namespace fpt;

namespace {

class KnownBugsTest : public ::testing::TestWithParam<int> {
protected:
    int F() const { return GetParam(); }
    Number half() const { return make(F(), {0, kSignBit}); }
};

}  // namespace

// Defect 1: divFixed adds the quotient bits into the output buffer instead of overwriting it, so the
// result is only right when the caller happened to zero the buffer first. Every other operation
// overwrites its output.
TEST_P(KnownBugsTest, DivisionOverwritesTheOutputInsteadOfAccumulatingIntoIt) {
    const Number dividend = one(F()), divisor = make(F(), {2});
    Number out = filled(F(), kAllOnes);                   // pre-existing garbage
    fpa::divFixed(dividend.data(), divisor.data(), out.data(), F());
    EXPECT_TRUE(same(half(), out)) << "1 / 2 into a dirty buffer";
}

TEST_P(KnownBugsTest, RepeatedDivisionIntoTheSameBufferGivesTheSameResult) {
    const Number dividend = one(F()), divisor = make(F(), {2});
    Number out = zero(F());
    fpa::divFixed(dividend.data(), divisor.data(), out.data(), F());
    const Number first = out;
    fpa::divFixed(dividend.data(), divisor.data(), out.data(), F());   // same call, buffer reused
    EXPECT_TRUE(same(first, out)) << "second call must not add onto the first result";
}

// Defect 2: subFixed(a, b, c) first writes -b into c and only then reads a. When c is the same buffer
// as a, the computation therefore becomes (-b) + (-b) instead of a - b.
TEST_P(KnownBugsTest, SubtractionWorksWhenTheOutputIsTheFirstOperand) {
    Number a = make(F(), {5});
    const Number b = make(F(), {2});
    fpa::subFixed(a.data(), b.data(), a.data(), F());
    EXPECT_TRUE(same(make(F(), {3}), a)) << "5 - 2 computed in place into the first operand";
}

// Defect 3: the same in-place pattern for division. (This is a consequence of defect 1 - the output
// is both an input and an accumulator - plus the fact that the inputs are read after the output is touched.)
TEST_P(KnownBugsTest, DivisionWorksWhenTheOutputIsTheDividend) {
    Number a = make(F(), {5});
    const Number b = make(F(), {2});
    fpa::divFixed(a.data(), b.data(), a.data(), F());
    EXPECT_TRUE(same(make(F(), {2, kSignBit}), a)) << "5 / 2 == 2.5 computed in place into the dividend";
}

TEST_P(KnownBugsTest, DivisionWorksWhenTheOutputIsTheDivisor) {
    const Number a = make(F(), {5});
    Number b = make(F(), {2});
    fpa::divFixed(a.data(), b.data(), b.data(), F());
    EXPECT_TRUE(same(make(F(), {2, kSignBit}), b)) << "5 / 2 == 2.5 computed in place into the divisor";
}

// Defect 4: when the quotient does not fit the number format (e.g. 1.0 divided by the smallest step),
// divFixed silently returns a wrapped value and writes out of bounds in an internal buffer (power()
// is called with a bit index past the end of an array).
// Agreed behaviour: divFixed throws std::overflow_error when the (truncated) quotient is outside
// [min, max]. The out-of-bounds write is also reported by AddressSanitizer (-DFPA_SANITIZE=ON).
TEST_P(KnownBugsTest, DivisionWhoseQuotientDoesNotFitThrowsOverflowError) {
    const int f = F();
    const Number twoToThe30 = make(f, {0x40000000u});                 // 2^30
    const struct { Number a, b; const char* what; } cases[] = {
        {one(f), ulp(f), "1.0 / smallest step"},
        {maxValue(f), ulp(f), "max / smallest step"},
        {minValue(f), ulp(f), "min / smallest step"},
        {twoToThe30, half(), "2^30 / 0.5 == 2^31, one above max"},
        {neg(f, twoToThe30), neg(f, half()), "(-2^30) / (-0.5) == 2^31, one above max"},
        {minValue(f), neg(f, one(f)), "min / -1 == 2^31, one above max"},
    };
    for (const auto& c : cases) {
        Number out = zero(f);
        EXPECT_THROW(fpa::divFixed(c.a.data(), c.b.data(), out.data(), f), std::overflow_error) << c.what;
    }
}

FPA_INSTANTIATE_FOR_ALL_FORMATS(KnownBugsTest);
