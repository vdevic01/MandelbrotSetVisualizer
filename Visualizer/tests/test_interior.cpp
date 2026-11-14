// Tests of the interior check (src/calculators/InteriorCheck.h). The check may only say "inside" for points
// that really never escape, otherwise skipping their iteration would change the image.

#include <gtest/gtest.h>

#include "InteriorCheck.h"

namespace {

// Plain escape-time loop: true if the point has not escaped after maxIter iterations.
bool staysBounded(double x0, double y0, int maxIter) {
    double x = 0, y = 0;
    for (int i = 0; i < maxIter; i++) {
        const double nx = x * x - y * y + x0;
        y = 2 * x * y + y0;
        x = nx;
        if (x * x + y * y > 4.0) return false;
    }
    return true;
}

TEST(InteriorCheck, KnownPoints) {
    EXPECT_TRUE(isInteriorPoint(0.0, 0.0));      // the origin, in the main cardioid
    EXPECT_TRUE(isInteriorPoint(-0.5, 0.3));     // main cardioid
    EXPECT_TRUE(isInteriorPoint(-1.0, 0.0));     // centre of the period-2 bulb
    EXPECT_TRUE(isInteriorPoint(-1.1, 0.1));     // period-2 bulb
    EXPECT_FALSE(isInteriorPoint(0.3, 0.0));     // right of the cusp
    EXPECT_FALSE(isInteriorPoint(-1.3, 0.0));    // left of the bulb
    EXPECT_FALSE(isInteriorPoint(0.0, 1.0));     // above the cardioid
    EXPECT_FALSE(isInteriorPoint(-2.0, 0.0));    // on the real axis tip, in the set but not covered
    EXPECT_FALSE(isInteriorPoint(1.0, 1.0));     // far outside
}

TEST(InteriorCheck, NeverSaysInsideForAPointThatEscapes) {
    int inside = 0;
    for (int ix = 0; ix <= 600; ix++) {
        for (int iy = 0; iy <= 300; iy++) {
            const double x = -2.2 + 3.0 * ix / 600.0;
            const double y = -1.0 + 2.0 * iy / 300.0;
            if (isInteriorPoint(x, y)) {
                inside++;
                ASSERT_TRUE(staysBounded(x, y, 5000)) << "escapes, but reported inside: " << x << ", " << y;
            }
        }
    }
    EXPECT_GT(inside, 1000);   // the check does catch a large part of the interior
}

TEST(InteriorCheck, PointsNearTheBoundaryAreLeftToTheLoop) {
    // The cardioid boundary crosses the real axis at x = 0.25 and the bulb boundary at x = -1.25 and -0.75.
    EXPECT_FALSE(isInteriorPoint(0.25, 0.0));
    EXPECT_FALSE(isInteriorPoint(-0.75, 0.0));
    EXPECT_FALSE(isInteriorPoint(-1.25, 0.0));
}

}  // namespace
