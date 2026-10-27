// Self-check for the recovered maskflow:: namespace.
//
// The two interesting assertions here are both about behaviour that a tidy rewrite would
// "fix": smoothstep extrapolates below its lower edge, and its degenerate case returns a
// negative infinity. Both are what the shipped instructions do.

#include "maskflow.h"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;
int passes = 0;

void check(bool ok, const char* why) {
  if (ok) { ++passes; std::printf("  PASS %s\n", why); }
  else    { ++failures; std::printf("  FAIL %s\n", why); }
}

void checkNear(double got, double want, double tol, const char* why) {
  if (std::fabs(got - want) <= tol) { ++passes; std::printf("  PASS %s\n", why); }
  else { ++failures; std::printf("  FAIL %s  (got %.9g want %.9g)\n", why, got, want); }
}

void checkEqI(int got, int want, const char* why) {
  if (got == want) { ++passes; std::printf("  PASS %s\n", why); }
  else { ++failures; std::printf("  FAIL %s  (got %d want %d)\n", why, got, want); }
}

}  // namespace

int main() {
  std::printf("maskflow::clampf\n");
  {
    check(maskflow::clampf(5.0f, 0.0f, 10.0f) == 5.0f, "in range passes through");
    check(maskflow::clampf(11.0f, 0.0f, 10.0f) == 10.0f, "above hi clamps to hi");
    check(maskflow::clampf(5.0f, 10.0f, 0.0f) == 10.0f,
          "an INVERTED range yields lo, because lo is tested last");
  }

  std::printf("maskflow::edgeCostScaleFromTolerance\n");
  {
    checkNear(maskflow::edgeCostScaleFromTolerance(0.0f), 40.0f, 1e-6, "tolerance 0 is the maximum scale, 40");
    checkNear(maskflow::edgeCostScaleFromTolerance(50.0f), 20.0f, 1e-6, "tolerance 50 is halfway: 20");
    checkNear(maskflow::edgeCostScaleFromTolerance(100.0f), 0.0f, 1e-6, "tolerance 100 is 0");
    checkNear(maskflow::edgeCostScaleFromTolerance(25.0f), 30.0f, 1e-6, "tolerance 25 is 30");
    // The saturation, and the direction: tolerance rises, the scale FALLS.
    checkNear(maskflow::edgeCostScaleFromTolerance(500.0f), 0.0f, 1e-6, "above 100 saturates at 0");
    check(maskflow::edgeCostScaleFromTolerance(80.0f) < maskflow::edgeCostScaleFromTolerance(20.0f),
          "a higher tolerance is a LOWER scale");
    // A negative tolerance short-circuits to the maximum rather than extrapolating past it.
    checkNear(maskflow::edgeCostScaleFromTolerance(-1.0f), 40.0f, 0.0f,
              "a NEGATIVE tolerance takes 40.0 exactly, not an extrapolation");
    checkNear(maskflow::edgeCostScaleFromTolerance(-1000.0f), 40.0f, 0.0f, "however negative");
  }

  std::printf("maskflow::smoothstep\n");
  {
    checkNear(maskflow::smoothstep(0.0f, 1.0f, 0.0f), 0.0, 1e-6, "the lower edge is 0");
    checkNear(maskflow::smoothstep(0.0f, 1.0f, 1.0f), 1.0, 1e-6, "the upper edge is 1");
    checkNear(maskflow::smoothstep(0.0f, 1.0f, 0.5f), 0.5, 1e-6, "and the middle is 0.5: t*t*(3-2t)");
    checkNear(maskflow::smoothstep(2.0f, 6.0f, 4.0f), 0.5, 1e-6, "the edges need not be 0 and 1");
    // Above the upper edge it flattens.
    checkNear(maskflow::smoothstep(0.0f, 1.0f, 2.0f), 1.0, 1e-6, "past the upper edge it is 1");
    checkNear(maskflow::smoothstep(0.0f, 1.0f, 1000.0f), 1.0, 1e-6, "and stays there");

    // BELOW THE LOWER EDGE IT EXTRAPOLATES. There is no floor on t, so at x = -1 the
    // polynomial is (-1)^2 * (3 - 2(-1)) = 5. A `std::clamp(t, 0, 1)` would return 0,
    // and that is a different function.
    checkNear(maskflow::smoothstep(0.0f, 1.0f, -1.0f), 5.0, 1e-5,
              "below the lower edge it EXTRAPOLATES: x = -1 gives 5, not 0");
    check(maskflow::smoothstep(0.0f, 1.0f, -1.0f) > 1.0f, "which can exceed 1 in magnitude");
    check(maskflow::smoothstep(0.0f, 1.0f, -0.5f) > 0.0f, "and stays positive further out");

    // The degenerate case: equal edges divide by zero, and the shipped code returns the
    // raw ratio rather than a polynomial. For x below the edge that is a NEGATIVE
    // INFINITY, which propagates into whatever the caller does with it.
    const float below = maskflow::smoothstep(5.0f, 5.0f, 4.0f);
    check(std::isinf(below) && below < 0.0f,
          "equal edges with x below returns negative infinity — the division's result, not a clamp");
    checkNear(maskflow::smoothstep(5.0f, 5.0f, 6.0f), 1.0, 0.0,
              "and with x above it returns exactly 1");
  }

  std::printf("maskflow::computeDownsampleDims\n");
  {
    const maskflow::DownsampleDims none = maskflow::computeDownsampleDims(100, 50, 200);
    checkEqI(none.width, 100, "a size already inside the limit keeps its width");
    checkEqI(none.height, 50, "and its height");
    checkNear(none.scale, 1.0, 0.0, "with a scale of exactly 1");

    // The boundary is STRICTLY greater: the longest side equal to maxDim needs no scaling.
    const maskflow::DownsampleDims exact = maskflow::computeDownsampleDims(100, 50, 100);
    checkEqI(exact.width, 100, "a longest side exactly at the limit is left alone");
    checkNear(exact.scale, 1.0, 0.0, "still with a scale of 1");

    const maskflow::DownsampleDims half = maskflow::computeDownsampleDims(1000, 500, 100);
    checkEqI(half.width, 100, "a 1000-wide image limited to 100 becomes 100");
    checkEqI(half.height, 50, "and its height scales with it: 500 to 50");
    checkNear(half.scale, 0.1, 1e-7, "the returned scale is maxDim / longest");

    // A non-integer result rounds — and is then floored at 1, which is the part a
    // rewrite forgets.
    const maskflow::DownsampleDims odd = maskflow::computeDownsampleDims(1000, 501, 100);
    checkEqI(odd.height, 50, "501 at scale 0.1 is 50.1, which rounds to 50");
    const maskflow::DownsampleDims thin = maskflow::computeDownsampleDims(10, 1, 1);
    checkEqI(thin.width, 1, "a 10-wide image limited to 1 becomes 1");
    checkEqI(thin.height, 1, "and its 1-pixel height is FLOORED at 1 rather than reaching 0");

    // The invalid path: non-positive dimensions are repaired to at least 1, with a scale
    // of 1 because nothing was actually scaled.
    const maskflow::DownsampleDims zeroW = maskflow::computeDownsampleDims(0, 5, 100);
    checkEqI(zeroW.width, 1, "a width of 0 is repaired to 1");
    checkEqI(zeroW.height, 5, "and the height is left alone");
    checkNear(zeroW.scale, 1.0, 0.0, "with a scale of 1");
    const maskflow::DownsampleDims negative = maskflow::computeDownsampleDims(-3, -4, 10);
    checkEqI(negative.width, 1, "negative dimensions are repaired too");
    checkEqI(negative.height, 1, "both of them");

    // The struct's shape, which the return convention establishes: two ints in x0 and a
    // float in x1.
    static_assert(sizeof(maskflow::DownsampleDims) == 12, "two ints and a float");
    check(true, "the 12-byte shape holds (checked at compile time)");
  }

  // --- downsampleBoxFilter (recovered 2026-10-27): an integer box average with a total mapping.
  std::printf("maskflow::downsampleBoxFilter\n");
  {
    auto makeImage = [](int w, int h, float value) {
      maskflow::Image img;
      img.width = w;
      img.height = h;
      img.pixels.assign(static_cast<size_t>(w) * static_cast<size_t>(h), maskflow::RGB{value, value * 2.0f, value * 3.0f});
      return img;
    };

    const maskflow::Image flat = makeImage(8, 8, 0.25f);
    const maskflow::Image half = maskflow::downsampleBoxFilter(flat, 4, 4);
    check(half.width == 4 && half.height == 4, "the output is the requested size");
    check(half.pixels.size() == 16, "with one pixel per cell");
    checkNear(half.pixels[0].r, 0.25f, 1e-6f, "a constant image averages to itself");
    checkNear(half.pixels[7].b, 0.75f, 1e-6f, "on every channel (b = 3r here)");

    // A 2x2 block average, checked by hand on a ramp: source row 0..1, col 0..1 = 0,1,2,3 -> 1.5
    maskflow::Image ramp;
    ramp.width = 4;
    ramp.height = 4;
    ramp.pixels.resize(16);
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 4; ++x) {
        const float v = static_cast<float>(y * 4 + x);
        ramp.pixels[static_cast<size_t>(y) * 4 + static_cast<size_t>(x)] = maskflow::RGB{v, v, v};
      }
    const maskflow::Image quarters = maskflow::downsampleBoxFilter(ramp, 2, 2);
    checkNear(quarters.pixels[0].r, 2.5f, 1e-6f, "the top-left block averages its four samples");
    checkNear(quarters.pixels[3].r, 12.5f, 1e-6f, "and so does the bottom-right");

    // Upsampling is a NEAREST-neighbour copy through the same code path: a rect can only be one
    // pixel wide, never empty, which is what the max(i0 + 1, i1) exists for.
    const maskflow::Image wider = maskflow::downsampleBoxFilter(ramp, 8, 8);
    check(wider.width == 8 && wider.height == 8, "a larger output is produced");
    checkNear(wider.pixels[0].r, 0.0f, 1e-6f, "the first sample is copied, not averaged away");
    checkNear(wider.pixels[8 * 7 + 7].r, 15.0f, 1e-6f, "and so is the last");

    const maskflow::Image degW = maskflow::downsampleBoxFilter(ramp, 0, 3);
    check(degW.width == 1 && degW.height == 3, "a zero dimension is floored at 1");
    const maskflow::Image emptySrc = maskflow::downsampleBoxFilter(maskflow::Image{}, 2, 2);
    check(emptySrc.width == 2 && emptySrc.height == 2, "an empty source still sizes the output");
    checkNear(emptySrc.pixels[0].r, 0.0f, 1e-6f, "and comes out black rather than NaN");
  }

  if (failures == 0) std::printf("ALL RECOVERED MASKFLOW MATHS VERIFIED (%d checks)\n", passes);
  else std::printf("%d FAILURES out of %d checks\n", failures, passes + failures);
  return failures == 0 ? 0 : 1;
}
