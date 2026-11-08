// randomFromRange(min, max, result): a pseudo-random value in [min, max) built from rand().
// There is no exact expected value, so these tests check the contract: range, degenerate ranges,
// reproducibility with srand, spread, and independence from the previous contents of the output.

#include <cstdlib>

#include "test_support.h"

using namespace fpt;

namespace {

class RandomRangeTest : public ::testing::TestWithParam<int> {
protected:
    int F() const { return GetParam(); }

    Number sample(const Number& lo, const Number& hi, uint outputFill = 0) const {
        Number out = filled(F(), outputFill);
        fpa::randomFromRange(lo.data(), hi.data(), out.data(), F());
        return out;
    }

    ::testing::AssertionResult inRange(const Number& v, const Number& lo, const Number& hi) const {
        if (gte(F(), v, lo) && gt(F(), hi, v)) return ::testing::AssertionSuccess();
        return ::testing::AssertionFailure() << hex(v) << " is outside [" << hex(lo) << ", " << hex(hi) << ")";
    }
};

}  // namespace

TEST_P(RandomRangeTest, ResultsStayInsideTheRequestedRange) {
    Rng r(555 + GetParam());
    std::srand(2024);
    for (int i = 0; i < kIterations; i++) {
        Number lo = r.small(F(), 12), hi = r.small(F(), 12);
        if (gt(F(), lo, hi)) std::swap(lo, hi);
        if (lo == hi) continue;
        for (int k = 0; k < 5; k++) ASSERT_TRUE(inRange(sample(lo, hi), lo, hi));
    }
}

TEST_P(RandomRangeTest, WorksForNegativeAndMixedSignRanges) {
    std::srand(7);
    const struct { double lo, hi; } ranges[] = {{-1.0, 1.0}, {-2.0, -1.0}, {-0.5, 0.0}, {0.0, 0.25}, {-1000.0, 1000.0}};
    for (const auto& range : ranges) {
        const Number lo = fromDouble(F(), range.lo), hi = fromDouble(F(), range.hi);
        for (int i = 0; i < 200; i++) ASSERT_TRUE(inRange(sample(lo, hi), lo, hi)) << range.lo << ".." << range.hi;
    }
}

TEST_P(RandomRangeTest, EmptyRangeReturnsItsBound) {
    std::srand(11);
    Rng r(66 + GetParam());
    for (int i = 0; i < 50; i++) {
        const Number v = r.small(F(), 12);
        EXPECT_TRUE(same(v, sample(v, v))) << hex(v);
    }
}

TEST_P(RandomRangeTest, SameSeedGivesTheSameSequence) {
    const Number lo = fromDouble(F(), -3.0), hi = fromDouble(F(), 7.0);
    std::srand(99);
    std::vector<Number> first;
    for (int i = 0; i < 50; i++) first.push_back(sample(lo, hi));
    std::srand(99);
    for (int i = 0; i < 50; i++) ASSERT_TRUE(same(first[static_cast<size_t>(i)], sample(lo, hi))) << "sample " << i;
}

TEST_P(RandomRangeTest, ProducesVariedValuesThatCoverTheWholeRange) {
    std::srand(31337);
    const Number lo = fromDouble(F(), 2.0), hi = fromDouble(F(), 4.0), mid = fromDouble(F(), 3.0);
    int below = 0, above = 0;
    std::vector<Number> seen;
    for (int i = 0; i < 400; i++) {
        const Number v = sample(lo, hi);
        (gt(F(), mid, v) ? below : above)++;
        if (std::find(seen.begin(), seen.end(), v) == seen.end()) seen.push_back(v);
    }
    EXPECT_GT(below, 100) << "values should land in the lower half about half of the time";
    EXPECT_GT(above, 100) << "values should land in the upper half about half of the time";
    EXPECT_GT(seen.size(), 100u) << "too many repeated values";
}

TEST_P(RandomRangeTest, ResultDoesNotDependOnPreviousOutputContents) {
    const Number lo = fromDouble(F(), -4.0), hi = fromDouble(F(), 4.0);
    for (uint garbage : {kAllOnes, 0x5A5A5A5Au, 0x80000000u}) {
        std::srand(5);
        const Number clean = sample(lo, hi, 0);
        std::srand(5);
        EXPECT_TRUE(same(clean, sample(lo, hi, garbage))) << "garbage " << std::hex << garbage;
    }
}

TEST_P(RandomRangeTest, OutputMayAliasTheBounds) {
    const Number lo = fromDouble(F(), 1.0), hi = fromDouble(F(), 2.0);
    for (int variant = 0; variant < 2; variant++) {
        std::srand(8);
        const Number expected = sample(lo, hi);
        std::srand(8);
        Number a = lo, b = hi;
        fpa::randomFromRange(a.data(), b.data(), variant == 0 ? a.data() : b.data(), F());
        EXPECT_TRUE(same(expected, variant == 0 ? a : b)) << "output aliasing " << (variant == 0 ? "min" : "max");
    }
}

FPA_INSTANTIATE_FOR_ALL_FORMATS(RandomRangeTest);
