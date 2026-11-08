// Metamorphic tests across fraction sizes: the same numbers computed in a coarse format F1 and in a finer
// format F2 > F1 must agree. This guards the "fraction size may grow" requirement - a size-dependent
// indexing or buffer bug shows up as a disagreement between two sizes even where no table of expected
// values exists.
//
// A value in F1 is embedded into F2 by appending zero words (the value does not change). Then:
//   * add, sub, negate and comparisons are exact: embedding commutes with them;
//   * multiplication and division truncate toward zero, so the coarse result is the finer result truncated
//     again: the magnitude of the F1 result equals the leading words of the magnitude of the F2 result;
//   * conversions behave the same way.

#include <algorithm>
#include <cmath>
#include <random>

#include "test_support.h"

using namespace fpt;

namespace {

// Appends zero fraction words: the same value in a finer format.
Number embed(const Number& n, int toFractionWords) {
    Number out = n;
    out.resize(words(toFractionWords), 0);
    return out;
}

// Keeps the leading words: the same value truncated (toward minus infinity for the raw bits) to a coarser format.
Number leading(const Number& n, int toFractionWords) {
    return Number(n.begin(), n.begin() + words(toFractionWords));
}

class CrossFormatTest : public ::testing::TestWithParam<int> {
protected:
    int Fine() const { return GetParam(); }
    // Every format strictly coarser than the one under test.
    std::vector<int> coarserFormats() const {
        std::vector<int> v;
        for (int f = fpa::MIN_FRACTION_PART; f < Fine(); f++) v.push_back(f);
        return v;
    }
    Rng rng(uint64_t salt) const { return Rng(0xC0FFEEull * 17 + static_cast<uint64_t>(GetParam()) * 100 + salt); }
};

}  // namespace

TEST_P(CrossFormatTest, AdditionSubtractionAndNegationCommuteWithEmbedding) {
    const int fine = Fine();
    for (int coarse : coarserFormats()) {
        Rng r = rng(static_cast<uint64_t>(coarse));
        for (int i = 0; i < kIterations; i++) {
            const Number a = r.any(coarse), b = r.any(coarse);
            ASSERT_TRUE(same(embed(add(coarse, a, b), fine), add(fine, embed(a, fine), embed(b, fine))))
                << "add, F" << coarse << " -> F" << fine;
            ASSERT_TRUE(same(embed(sub(coarse, a, b), fine), sub(fine, embed(a, fine), embed(b, fine))))
                << "sub, F" << coarse << " -> F" << fine;
            ASSERT_TRUE(same(embed(neg(coarse, a), fine), neg(fine, embed(a, fine))))
                << "negate, F" << coarse << " -> F" << fine;
        }
    }
}

TEST_P(CrossFormatTest, ComparisonsAreIndependentOfTheFormat) {
    const int fine = Fine();
    for (int coarse : coarserFormats()) {
        Rng r = rng(100 + static_cast<uint64_t>(coarse));
        for (int i = 0; i < kIterations; i++) {
            const Number a = r.any(coarse), b = r.any(coarse);
            ASSERT_EQ(gt(coarse, a, b), gt(fine, embed(a, fine), embed(b, fine))) << "F" << coarse << " vs F" << fine;
            ASSERT_EQ(gte(coarse, a, b), gte(fine, embed(a, fine), embed(b, fine))) << "F" << coarse << " vs F" << fine;
        }
    }
}

TEST_P(CrossFormatTest, MultiplicationAgreesUpToTruncation) {
    const int fine = Fine();
    for (int coarse : coarserFormats()) {
        Rng r = rng(200 + static_cast<uint64_t>(coarse));
        for (int i = 0; i < kIterations; i++) {
            const Number a = r.small(coarse, 14), b = r.small(coarse, 14);
            const Number coarseResult = mul(coarse, a, b);
            const Number fineResult = mul(fine, embed(a, fine), embed(b, fine));
            ASSERT_TRUE(same(absValue(coarse, coarseResult), leading(absValue(fine, fineResult), coarse)))
                << "a=" << hex(a) << " b=" << hex(b) << " F" << coarse << " vs F" << fine;
        }
    }
}

TEST_P(CrossFormatTest, DivisionAgreesUpToTruncation) {
    const int fine = Fine();
    for (int coarse : coarserFormats()) {
        Rng r = rng(300 + static_cast<uint64_t>(coarse));
        for (int i = 0; i < kIterations; i++) {
            const Number a = r.small(coarse, 14), b = r.nonZero(coarse, 14);
            const Number coarseResult = div(coarse, a, b);
            const Number fineResult = div(fine, embed(a, fine), embed(b, fine));
            ASSERT_TRUE(same(absValue(coarse, coarseResult), leading(absValue(fine, fineResult), coarse)))
                << "a=" << hex(a) << " b=" << hex(b) << " F" << coarse << " vs F" << fine;
        }
    }
}

TEST_P(CrossFormatTest, ConversionsAgreeUpToTruncation) {
    const int fine = Fine();
    std::uniform_real_distribution<double> dist(-5000.0, 5000.0);
    for (int coarse : coarserFormats()) {
        std::mt19937_64 g(1234 + static_cast<uint64_t>(coarse) * 10 + static_cast<uint64_t>(fine));
        Rng r = rng(400 + static_cast<uint64_t>(coarse));
        for (int i = 0; i < kIterations; i++) {
            const double x = dist(g) * std::pow(2.0, -static_cast<int>(g() % 50));
            ASSERT_TRUE(same(absValue(coarse, fromDouble(coarse, x)), leading(absValue(fine, fromDouble(fine, x)), coarse)))
                << "x=" << x << " F" << coarse << " vs F" << fine;

            const Number a = r.any(coarse);
            ASSERT_EQ(toDouble(coarse, a), toDouble(fine, embed(a, fine))) << "fixed->double, F" << coarse << " vs F" << fine;
        }
    }
}

FPA_INSTANTIATE_FOR_ALL_FORMATS(CrossFormatTest);
