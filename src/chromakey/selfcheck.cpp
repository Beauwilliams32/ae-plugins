// Self-check for the recovered chromakey:: namespace.
//
// Every assertion is anchored to something the disassembly establishes, and the ones
// that matter are built so that the plausible alternative implementation would FAIL
// them: the top-left gradient kernel, the ignored luma in chromaDistance, the
// argument order of hueAngle, the > pi wrap in angularDiff, and the element-count
// row stride.

#include "chromakey.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;
int passes = 0;

void check(bool ok, const char* why) {
  if (ok) { ++passes; std::printf("  PASS %s\n", why); }
  else    { ++failures; std::printf("  FAIL %s\n", why); }
}

void checkNear(float got, float want, float tol, const char* why) {
  if (std::fabs(got - want) <= tol) { ++passes; std::printf("  PASS %s\n", why); }
  else { ++failures; std::printf("  FAIL %s  (got %.9g want %.9g)\n", why, got, want); }
}

}  // namespace

int main() {
  std::printf("chromakey::clampf\n");
  {
    check(chromakey::clampf(5.0f, 0.0f, 10.0f) == 5.0f, "in range passes through");
    check(chromakey::clampf(-1.0f, 0.0f, 10.0f) == 0.0f, "below lo clamps to lo");
    check(chromakey::clampf(11.0f, 0.0f, 10.0f) == 10.0f, "above hi clamps to hi");
    check(chromakey::clampf(5.0f, 10.0f, 0.0f) == 10.0f,
          "an INVERTED range yields lo, because lo is tested last");
  }

  std::printf("chromakey::rgbToYCbCr\n");
  {
    const chromakey::YCbCr black = chromakey::rgbToYCbCr({0.0f, 0.0f, 0.0f});
    check(black.y == 0.0f && black.cb == 0.0f && black.cr == 0.0f, "black is (0, 0, 0)");

    const chromakey::YCbCr white = chromakey::rgbToYCbCr({1.0f, 1.0f, 1.0f});
    checkNear(white.y, 1.0f, 1e-6f, "white has luma 1: the three Rec.601 coefficients sum to 1");
    checkNear(white.cb, 0.0f, 1e-6f, "and no chroma");
    checkNear(white.cr, 0.0f, 1e-6f, "on either axis");

    // Rec.601 by hand: pure red is Y = 0.299, Cb = (0 - 0.299) * 0.564334,
    // Cr = (1 - 0.299) * 0.713267.
    const chromakey::YCbCr red = chromakey::rgbToYCbCr({1.0f, 0.0f, 0.0f});
    checkNear(red.y, 0.299f, 1e-6f, "pure red has luma 0.299");
    checkNear(red.cb, -0.299f * 0.564334f, 1e-6f, "and the Rec.601 Cb");
    checkNear(red.cr, 0.701f * 0.713267f, 1e-6f, "and the Rec.601 Cr");

    // The chroma scales are the six-decimal LITERALS, not 1/1.772 and 1/1.402: those
    // divisions are 0x3f107833 and 0x3f3698a7 against the shipped 0x3f107831 and
    // 0x3f3698ab. The expression below is bit-identical to what the function computes,
    // so this pins the constant rather than approximating it.
    const float b = 1.0f;
    const float y = chromakey::rgbToYCbCr({0.0f, 0.0f, b}).y;
    const chromakey::YCbCr blue = chromakey::rgbToYCbCr({0.0f, 0.0f, b});
    check(blue.cb == (b - y) * 0.564334f, "Cb uses the literal 0.564334 bit-exactly");
    check(blue.cb != (b - y) * (1.0f / 1.772f),
          "and NOT the division 1/1.772, which is three ulps away");
    check(blue.cr == (0.0f - y) * 0.713267f, "Cr uses the literal 0.713267 bit-exactly");
  }

  std::printf("chromakey::sensitivityToThreshold\n");
  {
    checkNear(chromakey::sensitivityToThreshold(0.0f), 0.03f, 1e-7f, "sensitivity 0 gives 0.03");
    checkNear(chromakey::sensitivityToThreshold(100.0f), 0.45f, 1e-6f, "sensitivity 100 gives 0.45");
    checkNear(chromakey::sensitivityToThreshold(50.0f), 0.24f, 1e-6f, "and 50 lands halfway: 0.24");
    checkNear(chromakey::sensitivityToThreshold(500.0f), 0.45f, 1e-6f,
              "above 100 saturates — the clamp is one-sided");
    checkNear(chromakey::sensitivityToThreshold(-1.0f), 0.03f, 0.0f,
              "a NEGATIVE sensitivity takes 0.03 exactly, not the ramp");
    checkNear(chromakey::sensitivityToThreshold(-1000.0f), 0.03f, 0.0f,
              "however negative it gets");
  }

  std::printf("chromakey::chromaDistance\n");
  {
    const chromakey::YCbCr a{0.5f, 0.1f, 0.2f};
    checkNear(chromakey::chromaDistance(a, a), 0.0f, 0.0f, "a colour is at distance 0 from itself");

    // 3-4-5, in chroma only, whatever the luma does.
    const chromakey::YCbCr p{0.0f, 0.0f, 0.0f};
    const chromakey::YCbCr q{999.0f, 3.0f, 4.0f};
    checkNear(chromakey::chromaDistance(p, q), 5.0f, 1e-5f, "dCb 3 and dCr 4 give 5");
    // THE POINT: Y differs by 999 here and contributes nothing. A Euclidean distance
    // over all three components would return about 999.
    check(chromakey::chromaDistance(p, q) < 6.0f,
          "and luma is IGNORED: a 999 difference in Y does not enter the distance");
    checkNear(chromakey::chromaDistance({5.0f, 0.0f, 0.0f}, {5.0f, 3.0f, 4.0f}), 5.0f, 1e-5f,
              "the same holds when only Y differs, which is distance 0");
  }

  std::printf("chromakey::hueAngle\n");
  {
    // The argument order is the content of this function: the FIRST parameter is the
    // atan2 denominator. Reading it the other way round swaps these two answers.
    checkNear(chromakey::hueAngle(1.0f, 0.0f), 0.0f, 1e-7f, "hueAngle(1, 0) is atan2(0, 1) = 0");
    checkNear(chromakey::hueAngle(0.0f, 1.0f), 1.5707963f, 1e-6f,
              "hueAngle(0, 1) is atan2(1, 0) = pi/2 — so the SECOND parameter is the numerator");
    checkNear(chromakey::hueAngle(-1.0f, 0.0f), 3.1415927f, 1e-6f, "hueAngle(-1, 0) is pi");
    checkNear(chromakey::hueAngle(0.0f, -1.0f), -1.5707963f, 1e-6f, "hueAngle(0, -1) is -pi/2");
    check(chromakey::hueAngle(0.0f, 0.0f) == 0.0f, "the origin is special-cased to 0");
  }

  std::printf("chromakey::angularDiff\n");
  {
    checkNear(chromakey::angularDiff(0.3f, 0.3f), 0.0f, 0.0f, "an angle is at distance 0 from itself");
    checkNear(chromakey::angularDiff(0.0f, 1.0f), 1.0f, 1e-6f, "a small difference passes through");
    checkNear(chromakey::angularDiff(-0.5f, 0.5f), 1.0f, 1e-6f, "the difference is taken as a MAGNITUDE");
    checkNear(chromakey::angularDiff(0.0f, 3.1415927f), 3.1415927f, 1e-5f,
              "exactly pi is NOT wrapped (the test is strictly greater)");
    // The wrap: 3.5 rad apart the short way round is 2*pi - 3.5 = 2.783.
    checkNear(chromakey::angularDiff(0.0f, 3.5f), 2.7831855f, 1e-5f,
              "past pi it wraps: 3.5 becomes 2*pi - 3.5, not 3.5");
    checkNear(chromakey::angularDiff(0.1f, 6.1831855f), 0.2f, 1e-4f,
              "two angles 0.2 apart the short way round, 2*pi apart the long way");
    checkNear(chromakey::angularDiff(0.0f, 6.2831855f), 0.0f, 1e-5f, "a full turn is distance 0 (fmod)");
  }

  std::printf("chromakey::percentile\n");
  {
    // This variant does NOT sort: it reads the caller's array in place, so the caller
    // has to have sorted it. That is what the `Unsorted` sibling is for.
    const float sorted3[3] = {1.0f, 5.0f, 9.0f};
    check(chromakey::percentile(nullptr, 8, 50.0f) == 0.0f, "a null pointer returns 0");
    check(chromakey::percentile(sorted3, 0, 50.0f) == 0.0f, "an empty array returns 0");
    check(chromakey::percentile(sorted3, 1, 50.0f) == 1.0f, "one sample is that sample");

    checkNear(chromakey::percentile(sorted3, 3, 0.0f), 1.0f, 0.0f, "p=0 is the minimum");
    checkNear(chromakey::percentile(sorted3, 3, 100.0f), 9.0f, 0.0f, "p=100 is the maximum");
    checkNear(chromakey::percentile(sorted3, 3, 50.0f), 5.0f, 0.0f, "p=50 is the middle sample");
    // Interpolated: index = 0.25 * 2 = 0.5, between 1 and 5.
    checkNear(chromakey::percentile(sorted3, 3, 25.0f), 3.0f, 1e-6f,
              "p=25 interpolates halfway between the first two samples");
    checkNear(chromakey::percentile(sorted3, 3, 150.0f), 9.0f, 0.0f, "p above 100 clamps to the maximum");

    const float pair[2] = {0.0f, 10.0f};
    checkNear(chromakey::percentile(pair, 2, 50.0f), 5.0f, 1e-6f, "two samples and p=50 gives their midpoint");

    // A negative p is carried into the index arithmetic unclamped; the saturating
    // float-to-unsigned conversions are what keep it in range, and it returns the
    // minimum. The sibling clamps to 0 instead and returns the same thing.
    checkNear(chromakey::percentile(sorted3, 3, -50.0f), 1.0f, 0.0f,
              "a NEGATIVE p returns the minimum rather than reading out of bounds");
    checkNear(chromakey::percentile(sorted3, 3, -1e6f), 1.0f, 0.0f, "however negative");
  }

  std::printf("chromakey::percentileUnsorted\n");
  {
    check(chromakey::percentileUnsorted({}, 50.0f) == 0.0f, "an empty vector returns 0");
    check(chromakey::percentileUnsorted({7.0f}, 50.0f) == 7.0f, "one sample is that sample");

    // THE POINT: the input does not have to be sorted, because the function sorts its
    // own copy. The same numbers as the sorted case above, shuffled.
    checkNear(chromakey::percentileUnsorted({9.0f, 1.0f, 5.0f}, 50.0f), 5.0f, 0.0f,
              "an UNSORTED input gives the same answer as a sorted one");
    checkNear(chromakey::percentileUnsorted({9.0f, 1.0f, 5.0f}, 0.0f), 1.0f, 0.0f, "p=0 is still the minimum");
    checkNear(chromakey::percentileUnsorted({9.0f, 1.0f, 5.0f}, 25.0f), 3.0f, 1e-6f,
              "and interpolation works on the sorted copy");

    // The vector is taken BY VALUE, so the caller's own vector cannot be reordered by
    // the sort. A by-reference signature would make this a silent mutation.
    std::vector<float> mine{9.0f, 1.0f, 5.0f};
    (void)chromakey::percentileUnsorted(mine, 50.0f);
    check(mine[0] == 9.0f && mine[1] == 1.0f && mine[2] == 5.0f,
          "the caller's vector is NOT reordered (the parameter is by value)");

    checkNear(chromakey::percentileUnsorted({9.0f, 1.0f, 5.0f}, -50.0f), 1.0f, 0.0f,
              "a negative p returns the minimum here too, by the other route");
  }

  std::printf("chromakey::gradientMagnitude\n");
  {
    // 3 wide, 3 tall, stride 3. Only the top row has a step in it.
    const float grid[9] = {0.0f, 0.0f, 10.0f,
                           0.0f, 0.0f, 0.0f,
                           0.0f, 0.0f, 0.0f};
    check(chromakey::gradientMagnitude(nullptr, 3, 3, 3, 1, 1) == 0.0f, "a null field returns 0");
    check(chromakey::gradientMagnitude(grid, 0, 3, 3, 1, 1) == 0.0f, "width < 1 returns 0");
    check(chromakey::gradientMagnitude(grid, 3, 0, 3, 1, 1) == 0.0f, "height < 1 returns 0");

    // THE KERNEL IS ANCHORED TOP-LEFT: the horizontal difference is taken on the row
    // ABOVE (row 0 here), which reads 10 - 0 = 10, so gx = 5. A centred kernel would
    // take the difference on row 1, read 0 - 0, and return 0.
    checkNear(chromakey::gradientMagnitude(grid, 3, 3, 3, 1, 1), 5.0f, 1e-6f,
              "the horizontal difference reads the row ABOVE (gx = 5, not 0)");

    // A uniform field has no gradient anywhere.
    const float flat[9] = {4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f};
    check(chromakey::gradientMagnitude(flat, 3, 3, 3, 1, 1) == 0.0f, "a constant field has no gradient");

    // A horizontal ramp of 1 per column: gx = (xp1 - xm1) / 2 = 1, gy = 0.
    const float ramp[9] = {0.0f, 1.0f, 2.0f, 0.0f, 1.0f, 2.0f, 0.0f, 1.0f, 2.0f};
    checkNear(chromakey::gradientMagnitude(ramp, 3, 3, 3, 1, 1), 1.0f, 1e-6f,
              "a 1-per-column ramp has gradient 1");
    // A vertical ramp: gy = (yp1 - ym1) / 2 = 1.
    const float vramp[9] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 2.0f, 2.0f, 2.0f};
    checkNear(chromakey::gradientMagnitude(vramp, 3, 3, 3, 1, 1), 1.0f, 1e-6f,
              "a 1-per-row ramp has gradient 1");

    // Both, for the square root: gx = 1 and gy = 1 give sqrt(2).
    const float diag[9] = {0.0f, 1.0f, 2.0f, 1.0f, 2.0f, 3.0f, 2.0f, 3.0f, 4.0f};
    checkNear(chromakey::gradientMagnitude(diag, 3, 3, 3, 1, 1), 1.4142135f, 1e-6f,
              "a diagonal ramp gives sqrt(2)");

    // The denominators are floored at 1, which is what a 1-wide field needs: xp1 and
    // xm1 both clamp to 0, so the difference is 0 and an unfloored divide would be
    // 0/0. It must be 0, not NaN.
    const float column[3] = {1.0f, 2.0f, 3.0f};
    const float oneWide = chromakey::gradientMagnitude(column, 1, 3, 1, 0, 1);
    check(oneWide == oneWide, "a 1-wide field does not produce NaN");
    checkNear(oneWide, 1.0f, 1e-6f, "and its vertical gradient still comes out at 1");

    // THE STRIDE IS IN ELEMENTS. With a stride of 6 over a 3-wide grid, row 2 starts
    // at element 12; reading it as bytes would land somewhere else entirely and return
    // a different number.
    float padded[18];
    for (int i = 0; i < 18; ++i) padded[i] = 0.0f;
    padded[6] = 0.0f; padded[7] = 0.0f; padded[8] = 0.0f;    // row 1
    padded[12] = 4.0f; padded[13] = 4.0f; padded[14] = 4.0f; // row 2
    checkNear(chromakey::gradientMagnitude(padded, 3, 3, 6, 1, 1), 2.0f, 1e-6f,
              "a row stride of 6 ELEMENTS is honoured (gy = 4/2)");
  }

  // --- spillScore: an independent model, because the rebuilt code compiles to 80 of the bundle's 82
  // instructions (clang elides the lower clamp on a value it can prove non-negative). So the outputs
  // are pinned against a model written from the disassembly rather than against the compiler.
  std::printf("chromakey::spillScore\n");
  {
    auto model = [](chromakey::YCbCr p, chromakey::YCbCr key, float referenceY) {
      const float pMag = std::sqrt(p.cb * p.cb + p.cr * p.cr);
      if (pMag < 1e-6f) return 0.0f;
      const float kMag = std::sqrt(key.cb * key.cb + key.cr * key.cr);
      if (kMag < 1e-6f) return 0.0f;
      const float d = chromakey::angularDiff(chromakey::hueAngle(p.cb, p.cr), chromakey::hueAngle(key.cb, key.cr));
      const float hueWeight = chromakey::clampf(1.0f - d / 1.5707964f, 0.0f, 1.0f);
      const float yWeight = std::fmin(std::fabs(p.y - referenceY), 1.0f);
      const float chroma = chromakey::clampf(2.0f * pMag, 0.0f, 1.0f);
      return chromakey::clampf(chroma * hueWeight * yWeight, 0.0f, 1.0f);
    };

    const chromakey::YCbCr grey{0.5f, 0.0f, 0.0f};
    const chromakey::YCbCr key{0.6f, 0.0f, 0.5f};   // a strongly blue-ish key, 90 degrees from blue-green
    check(chromakey::spillScore(grey, key, 0.5f) == 0.0f, "an achromatic PIXEL scores 0");
    check(chromakey::spillScore(key, grey, 0.5f) == 0.0f, "an achromatic KEY scores 0");
    checkNear(chromakey::spillScore(key, key, key.y), 0.0f, 1e-6f,
              "a pixel at the reference luma scores 0 (the luma term, not the hue term, is what zeroes it)");

    // The hue term on its own: same chroma, four rotations, with the luma term held at its maximum.
    const float chromaMag = 0.5f;
    const chromakey::YCbCr at0{0.0f, chromaMag, 0.0f};                 // hue = 0
    const chromakey::YCbCr at90{0.0f, 0.0f, chromaMag};                // hue = +pi/2
    checkNear(chromakey::spillScore(at0, at0, -1.0f), 1.0f, 1e-6f, "identical hue + max chroma + max luma delta = 1");
    checkNear(chromakey::spillScore(at90, at0, -1.0f), 0.0f, 1e-6f, "90 degrees of hue difference scores 0");
    const chromakey::YCbCr at45{0.0f, chromaMag * 0.7071068f, chromaMag * 0.7071068f};
    const float at45Score = chromakey::spillScore(at45, at0, -1.0f);
    check(at45Score > 0.4f && at45Score < 0.6f, "45 degrees is about half weight (linear in angle)");

    // The luma term: |pixel.y - referenceY| saturates at 1, and equal luma zeroes the score.
    checkNear(chromakey::spillScore(at0, at0, 0.0f), 0.0f, 1e-6f, "pixel luma equal to the reference zeroes the score");
    const float nearRef = chromakey::spillScore(at0, at0, -0.25f);
    const float farRef = chromakey::spillScore(at0, at0, -1.0f);
    check(nearRef > 0.0f && nearRef < farRef, "a luma closer to the reference scores lower");
    checkNear(farRef, 1.0f, 1e-6f, "and saturates at 1 once the delta passes 1.0");

    // Grid parity against the independent model, over ranges that cross every clamp.
    int mismatches = 0;
    for (float y = 0.0f; y <= 1.0f; y += 0.125f) {
      for (float cb = -0.5f; cb <= 0.5f; cb += 0.125f) {
        for (float cr = -0.5f; cr <= 0.5f; cr += 0.125f) {
          const chromakey::YCbCr pixel{y, cb, cr};
          const float got = chromakey::spillScore(pixel, key, 0.35f);
          const float want = model(pixel, key, 0.35f);
          if (std::fabs(got - want) > 1e-6f) ++mismatches;
          if (!(got >= 0.0f && got <= 1.0f)) ++mismatches;
        }
      }
    }
    std::printf("%s 0 mismatches against the disassembly model over the grid\n", mismatches == 0 ? "  PASS" : "  FAIL");
    if (mismatches != 0) ++failures; else ++passes;
  }

  if (failures == 0) std::printf("ALL RECOVERED CHROMAKEY MATHS VERIFIED (%d checks)\n", passes);
  else std::printf("%d FAILURES out of %d checks\n", failures, passes + failures);
  return failures == 0 ? 0 : 1;
}
