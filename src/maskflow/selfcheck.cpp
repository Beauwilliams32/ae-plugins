// Self-check for the recovered maskflow:: namespace.
//
// The two interesting assertions here are both about behaviour that a tidy rewrite would
// "fix": smoothstep extrapolates below its lower edge, and its degenerate case returns a
// negative infinity. Both are what the shipped instructions do.

#include "maskflow.h"

#include <cmath>
#include <cstdio>
#include <limits>

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

  std::printf("maskflow::upsampleBilinear\n");
  {
    // Endpoints land exactly on the source corners: the scale runs src -> dst, so at x = dstW-1
    // the product is srcW-1 by construction. A rewrite that inverts the ratio misses by a pixel.
    const std::vector<float> ramp = {0.0f, 1.0f, 2.0f, 3.0f};   // 4x1
    const std::vector<float> same = maskflow::upsampleBilinear(ramp, 4, 1, 4, 1);
    check(same.size() == 4, "the requested size comes back");
    checkNear(same[0], 0.0f, 1e-6f, "the first sample is copied");
    checkNear(same[3], 3.0f, 1e-6f, "and so is the last");

    // Doubling in x interpolates halfway: dst x=1 -> fx = 3/3 * 1 = 1 -> exactly src 1.
    const std::vector<float> wide = maskflow::upsampleBilinear(ramp, 4, 1, 7, 1);
    check(wide.size() == 7, "a 7-wide output for a 4-wide source");
    checkNear(wide[0], 0.0f, 1e-6f, "x=0 lands on src[0]");
    checkNear(wide[6], 3.0f, 1e-6f, "x=6 lands on src[3]");
    checkNear(wide[2], 1.0f, 1e-6f, "x=2 lands on src[1] (fx = 3/6*2 = 1)");
    checkNear(wide[1], 0.5f, 1e-6f, "x=1 is the halfway sample between src[0] and src[1]");

    // The vertical blend is applied after the horizontal one, in that order, with the same
    // weights — a 2x2 source doubled must reproduce the corners and average the centre.
    const std::vector<float> square = {0.0f, 1.0f, 2.0f, 3.0f};   // 2x2
    const std::vector<float> up = maskflow::upsampleBilinear(square, 2, 2, 3, 3);
    check(up.size() == 9, "3x3 from 2x2");
    checkNear(up[0], 0.0f, 1e-6f, "top-left corner is src[0]");
    checkNear(up[2], 1.0f, 1e-6f, "top-right corner is src[1]");
    checkNear(up[6], 2.0f, 1e-6f, "bottom-left corner is src[2]");
    checkNear(up[8], 3.0f, 1e-6f, "bottom-right corner is src[3]");
    checkNear(up[4], 1.5f, 1e-6f, "the centre is the mean of all four");

    // A 1x1 SOURCE is a broadcast, and it is a branch of its own — not the general loop.
    const std::vector<float> single = {0.75f};
    const std::vector<float> flat = maskflow::upsampleBilinear(single, 1, 1, 3, 2);
    check(flat.size() == 6, "a 1x1 source still sizes the output");
    checkNear(flat[0], 0.75f, 1e-6f, "and the value is broadcast to the first pixel");
    checkNear(flat[5], 0.75f, 1e-6f, "and to the last");

    // A 1-WIDE source goes through the general loop instead: the zero scale collapses x0, wx and
    // x1 to zero, so the horizontal blend is a copy and only the vertical one does anything.
    const std::vector<float> column = {1.0f, 3.0f};   // 1x2
    const std::vector<float> tall = maskflow::upsampleBilinear(column, 1, 2, 3, 3);
    check(tall.size() == 9, "3x3 from a 1x2 source");
    checkNear(tall[0], 1.0f, 1e-6f, "top row is the first sample, repeated");
    checkNear(tall[8], 3.0f, 1e-6f, "bottom row is the second");
    checkNear(tall[4], 2.0f, 1e-6f, "the middle row is the vertical halfway sample");

    // Every guard returns zeros of the requested size rather than an empty vector.
    const std::vector<float> noDst = maskflow::upsampleBilinear(ramp, 4, 1, 0, 4);
    check(noDst.empty(), "a zero destination width has no pixels at all");
    const std::vector<float> shortSrc = maskflow::upsampleBilinear(ramp, 8, 8, 2, 2);
    check(shortSrc.size() == 4, "a source shorter than srcW*srcH still sizes the output");
    checkNear(shortSrc[3], 0.0f, 1e-6f, "and comes out zeros rather than reading past the end");
    const std::vector<float> emptySrc = maskflow::upsampleBilinear({}, 4, 4, 2, 2);
    check(emptySrc.size() == 4 && emptySrc[0] == 0.0f, "an empty source gives zeros");
    const std::vector<float> negDim = maskflow::upsampleBilinear(ramp, -3, 1, 2, 2);
    check(negDim.size() == 4, "a negative source width is a guard, not a huge allocation");
    checkNear(negDim[0], 0.0f, 1e-6f, "and returns zeros");
  }

  std::printf("maskflow::buildMatte\n");
  {
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();

    auto field = [](float span, std::vector<float> values) {
      maskflow::ArrivalField f;
      f.width = static_cast<int>(values.size());
      f.height = 1;
      f.values = std::move(values);
      f.span = span;
      return f;
    };

    // tolerance 50 on a span of 3 is a window of +/-0.01 centred at 1.5, and the matte is the
    // COMPLEMENT of the ramp: out = 1 - smoothstep. Only values inside the window leave 0 or 1.
    {
      const auto m = maskflow::buildMatte(field(3.0f, {0.0f, 1.0f, 1.5f, 2.0f, 3.0f}), 50.0f, 0.01f, false);
      checkNear(m[0], 1.0, 1e-6, "below the window the non-inverted matte is 1");
      checkNear(m[1], 1.0, 1e-6, "and stays 1 up to the lower edge");
      checkNear(m[4], 0.0, 1e-6, "above the window it is 0");
      checkNear(m[3], 0.0, 1e-6, "starting at the upper edge");
      checkNear(m[2], 0.5, 1e-6, "the centre value is a half");
      const auto inverted = maskflow::buildMatte(field(3.0f, {0.0f, 1.0f, 1.5f, 2.0f, 3.0f}), 50.0f, 0.01f, true);
      checkNear(inverted[0], 0.0, 1e-6, "the invert flag flips it");
      checkNear(inverted[2], 0.5, 1e-6, "and leaves the ramp's midpoint alone");
    }

    // tolerance is a PERCENTAGE of the field's span, so the same tolerance on a longer span puts
    // the window further out: 50 % of 100 is 50, and a value of 40 is now below it.
    {
      const auto m = maskflow::buildMatte(field(100.0f, {0.0f, 40.0f, 50.0f, 60.0f, 100.0f}), 50.0f, 0.01f, false);
      checkNear(m[1], 1.0, 1e-6, "a value below the 50 %-of-span window reads 1");
      checkNear(m[2], 0.5, 1e-6, "the window centre is still a half");
      checkNear(m[3], 0.0, 1e-6, "and a value above it reads 0");
    }

    // tolerance 0 and tolerance >= 100 are DEGENERATE: the window collapses and every pixel takes
    // the fill instead of the ramp. At tolerance 0 the fill is 0, so the non-inverted matte is all
    // ones; at 100 (and beyond) it is all zeros.
    {
      const auto all = [](const std::vector<float>& m, float want) {
        for (float v : m) if (std::fabs(v - want) > 1e-6f) return false;
        return !m.empty();
      };
      check(all(maskflow::buildMatte(field(3.0f, {0.0f, 1.5f, 3.0f}), 0.0f, 0.01f, false), 1.0f),
            "tolerance 0 degenerates to all ones");
      check(all(maskflow::buildMatte(field(3.0f, {0.0f, 1.5f, 3.0f}), 100.0f, 0.01f, false), 0.0f),
            "tolerance 100 degenerates to all zeros");
      check(all(maskflow::buildMatte(field(3.0f, {0.0f, 1.5f, 3.0f}), 250.0f, 0.01f, false), 0.0f),
            "and anything above 100 is capped to it");
    }

    // A NEGATIVE tolerance is not clamped to the bottom of the range: it becomes 100.0f and takes
    // the degenerate branch, which is the opposite of what tolerance 0 does.
    {
      const auto neg = maskflow::buildMatte(field(3.0f, {0.0f, 1.5f, 3.0f}), -1.0f, 0.01f, false);
      check(neg[0] == 0.0f && neg[1] == 0.0f && neg[2] == 0.0f,
            "a negative tolerance degenerates to all zeros");
      const auto negInverted = maskflow::buildMatte(field(3.0f, {0.0f, 1.5f, 3.0f}), -1.0f, 0.01f, true);
      checkNear(negInverted[0], 1.0, 1e-6, "and inverting it gives ones");
    }

    // +inf and NaN take the FILL, not the ramp; -inf does not — it is just a very small value and
    // clamps to the bottom of the window like any other.
    {
      const auto m = maskflow::buildMatte(field(3.0f, {inf, nan, -inf, 1.5f, 0.0f}), 50.0f, 0.01f, false);
      checkNear(m[0], 1.0, 1e-6, "+inf takes the fill (1 here), not a NaN from the polynomial");
      checkNear(m[1], 1.0, 1e-6, "and so does NaN");
      checkNear(m[2], 1.0, 1e-6, "-inf is NOT special: below the window, so the ramp gives 1 too");
      checkNear(m[3], 0.5, 1e-6, "while a real value in the window still ramps");
      const auto inv = maskflow::buildMatte(field(3.0f, {inf, 1.5f}), 50.0f, 0.01f, true);
      checkNear(inv[0], 0.0, 1e-6, "the invert flag applies to the fill as well as the ramp");
    }

    // halfWidth is floored at 1e-4, so a degenerate (zero or negative) window still has width.
    {
      // The window is centre +/- 1e-4, so a value 4.5e-5 off centre is INSIDE it and ramps. With
      // an unfloored halfWidth of 0 the window would collapse to a point and this would be 0 or 1.
      const auto thin = maskflow::buildMatte(field(3.0f, {1.49999f, 1.0f}), 50.0f, 0.0f, false);
      check(thin[0] > 0.05f && thin[0] < 0.95f,
            "halfWidth 0 is floored to 1e-4, so the window still RAMPS rather than stepping");
      checkNear(thin[0], 0.5748, 5e-3, "at the half-way point of that floored window");
      checkNear(thin[1], 1.0, 1e-6, "and a value well below it still reads 1");
      const auto negative = maskflow::buildMatte(field(3.0f, {1.49999f}), 50.0f, -1.0f, false);
      checkNear(negative[0], thin[0], 1e-6, "a negative halfWidth is floored to the same window");
    }

    // The `lo == hi` branch is a CANCELLATION guard, not a division guard: with a span large enough
    // relative to the half width, span-half and span+half round to the same float and den is zero.
    // A hand-written version divides by it; this one falls back to a hard step.
    {
      const auto m = maskflow::buildMatte(field(1e30f, {0.0f, 1e30f}), 50.0f, 1e-4f, false);
      checkNear(m[0], 1.0, 1e-6, "a value below the collapsed window steps to 0, so the matte is 1");
      checkNear(m[1], 0.0, 1e-6, "and above it steps to 1, so the matte is 0");
      check(!std::isnan(m[0]) && !std::isnan(m[1]), "no NaN leaks out of the collapsed window");
    }

    // The inverted arm is 1 - (1 - value), which is NOT the identity: for a value small enough that
    // 1 - value rounds to 1, it returns 0. Here t is about 1e-5 and the polynomial about 3e-10.
    {
      const auto m = maskflow::buildMatte(field(0.0f, {-1e-4f + 2e-9f}), 50.0f, 1e-4f, true);
      check(m[0] == 0.0f, "the inverted arm collapses a vanishing ramp value to exactly 0");
    }

    // Shape and guards.
    {
      const auto empty = maskflow::buildMatte(maskflow::ArrivalField{}, 50.0f, 0.01f, false);
      check(empty.empty(), "a zero-sized field produces an empty matte, not a throw");
      maskflow::ArrivalField f = field(3.0f, {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f});
      f.width = 3;
      f.height = 2;
      const auto box = maskflow::buildMatte(f, 50.0f, 0.01f, false);
      check(box.size() == 6, "the matte is width*height long");
      check(!std::isnan(box[5]), "and every element is a real number");
      check(maskflow::ArrivalField{}.span == 0.0f, "a default-constructed field has a zero span");
    }
  }

  if (failures == 0) std::printf("ALL RECOVERED MASKFLOW MATHS VERIFIED (%d checks)\n", passes);
  else std::printf("%d FAILURES out of %d checks\n", failures, passes + failures);
  return failures == 0 ? 0 : 1;
}
