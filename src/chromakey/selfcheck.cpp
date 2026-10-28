// Self-check for the recovered chromakey:: namespace.
//
// Every assertion is anchored to something the disassembly establishes, and the ones
// that matter are built so that the plausible alternative implementation would FAIL
// them: the cross gradient kernel (horizontal difference on row y, vertical on
// column x), the ignored luma in chromaDistance, the argument order of hueAngle, the
// > pi wrap in angularDiff, and the element-count row stride.

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
    // 3 wide, 3 tall, stride 3. The kernel is a CROSS: the horizontal difference is
    // taken on the CENTRE row y and the vertical difference on the CENTRE column x.
    //
    // The arm64 listing says so (ChromaKeyAssist listing 0x2b28-0x2bdc): the two
    // horizontal loads read from the row base `field + stride*y*4` and the two
    // vertical loads read column x of rows ym1 and yp1.
    //
    // These grids are chosen so the two readings DISAGREE. The four grids below
    // differ only in WHICH row carries the horizontal step and WHICH column carries
    // the vertical one, so a kernel that took the horizontal difference on row ym1
    // would score the first grid 0 and the second 5, and a kernel that took the
    // vertical difference on a neighbouring column would score the third 0 and the
    // fourth 5.

    // A horizontal step of 10 in the CENTRE row: gx = (10 - 0) / 2 = 5.
    const float stepInCentreRow[9] = {0.0f, 0.0f, 0.0f,
                                       0.0f, 0.0f, 10.0f,
                                       0.0f, 0.0f, 0.0f};
    // The same step, but in the row ABOVE the centre. The shipped kernel does not
    // read that row horizontally at all, so it reads 0.
    const float stepInRowAbove[9] = {0.0f, 0.0f, 10.0f,
                                      0.0f, 0.0f, 0.0f,
                                      0.0f, 0.0f, 0.0f};
    // A vertical step of 10 between row ym1 and row yp1, in the CENTRE column:
    // gy = (15 - 5) / 2 = 5. The step has to straddle the CENTRE row, because the
    // vertical pair is read from rows ym1 and yp1 — a value sitting in row y only
    // would be read by the horizontal pair and not by the vertical one.
    const float stepInCentreColumn[9] = {0.0f, 5.0f, 0.0f,
                                          0.0f, 0.0f, 0.0f,
                                          0.0f, 15.0f, 0.0f};
    // The same vertical step, but in the column to the RIGHT of x: not read.
    const float stepInNextColumn[9] = {0.0f, 0.0f, 5.0f,
                                       0.0f, 0.0f, 0.0f,
                                       0.0f, 0.0f, 15.0f};

    check(chromakey::gradientMagnitude(nullptr, 3, 3, 3, 1, 1) == 0.0f, "a null field returns 0");
    check(chromakey::gradientMagnitude(stepInCentreRow, 0, 3, 3, 1, 1) == 0.0f, "width < 1 returns 0");
    check(chromakey::gradientMagnitude(stepInCentreRow, 3, 0, 3, 1, 1) == 0.0f, "height < 1 returns 0");

    checkNear(chromakey::gradientMagnitude(stepInCentreRow, 3, 3, 3, 1, 1), 5.0f, 1e-6f,
              "the horizontal difference reads the CENTRE ROW y (gx = 5)");
    checkNear(chromakey::gradientMagnitude(stepInRowAbove, 3, 3, 3, 1, 1), 0.0f, 1e-6f,
              "a step in the row ABOVE is not read horizontally (0, not 5)");
    checkNear(chromakey::gradientMagnitude(stepInCentreColumn, 3, 3, 3, 1, 1), 5.0f, 1e-6f,
              "the vertical difference reads the CENTRE COLUMN x (gy = 5)");
    checkNear(chromakey::gradientMagnitude(stepInNextColumn, 3, 3, 3, 1, 1), 0.0f, 1e-6f,
              "a vertical step in the NEXT column is not read (0, not 5)");

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

  // --- uniformityScore: a 0-100 spread percentage.
  std::printf("chromakey::uniformityScore\n");
  {
    std::vector<float> flat(64, 0.42f);
    checkNear(chromakey::uniformityScore(flat.data(), flat.size()), 100.0f, 1e-4f,
              "a uniform set scores 100");
    checkNear(chromakey::uniformityScore(nullptr, 4), 0.0f, 0.0f, "a null pointer scores 0");
    checkNear(chromakey::uniformityScore(flat.data(), 0), 0.0f, 0.0f, "a zero count scores 0");

    // Two samples at 0.25 and 0.75: mean 0.5, sd 0.25, so 1 - 2(0.25) = 0.5 -> 50.
    const float pair[2] = {0.25f, 0.75f};
    checkNear(chromakey::uniformityScore(pair, 2), 50.0f, 1e-4f, "sd of 0.25 scores 50 (1 - 2*sd, times 100)");

    // A set that spans the whole range: sd 0.5 -> 2*sd = 1.0 -> 0, and the clamp keeps it at 0.
    std::vector<float> bipolar(1000);
    for (size_t i = 0; i < bipolar.size(); ++i) bipolar[i] = (i % 2 == 0) ? 0.0f : 1.0f;
    checkNear(chromakey::uniformityScore(bipolar.data(), bipolar.size()), 0.0f, 1e-4f,
              "a 0/1 population scores 0");

    // The score is monotone in spread, and never leaves [0,100], over a sweep.
    bool monotone = true, inRange = true;
    std::vector<float> samples(32);
    float previous = 101.0f;
    for (int step = 0; step < 12; ++step) {
      const float amplitude = static_cast<float>(step) * 0.02f;   // 0 .. 0.22
      for (size_t i = 0; i < samples.size(); ++i) {
        samples[i] = 0.5f + ((i % 2 == 0) ? amplitude : -amplitude);
      }
      const float score = chromakey::uniformityScore(samples.data(), samples.size());
      if (!(score >= 0.0f && score <= 100.0f)) inRange = false;
      if (score > previous + 1e-3f) monotone = false;
      previous = score;
    }
    check(inRange, "every score is inside [0, 100]");
    check(monotone, "and the score is monotone in the spread of the samples");
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

  // ---------------------------------------------------------------------------
  // analyzeFrame<T>: the statistics pass.
  //
  // Read from the shipped instruction stream: the arm64 listing of
  // `analyzeFrame<unsigned char>` (body 0x7d8 .. 0x12f8, 752 instructions) and
  // of `analyzeFrame<unsigned short>` (body 0x19b4 .. 0x25bc, 770). The
  // numbers below were MEASURED against the shipped bundle's own machine code
  // by test/chromakey.test.mjs; this file pins them so that a regression in the
  // recovered source fails here, with the reasoning, rather than only in the
  // differential.
  //
  // The shape the listing establishes:
  //
  //   0x808/0x80c  the returned Stats is zeroed FIRST
  //   0x810..0x828 a width or height below 1 returns it untouched, and neither
  //                output buffer is written
  //   0x840       the threshold, from sensitivityToThreshold
  //   0x8ac..0x900 the KEY colour, NOT divided by the scale
  //   0x988/0x98c/0x998 every PIXEL channel IS divided by the scale
  //   0xc08..0xc18 the achromatic shortcut, an OR
  //   0xd98       the keyed fraction
  //   0xdac       the mean spill, over the pixels that are NOT keyed
  //   0xdc8       the uniformity, over the KEYED distances only
  //   0x1110..0x1238 the 5th and 95th percentiles, over ALL the distances
  //   0x123c      both percentiles, divided by sqrt(2) and multiplied by 100
  // ---------------------------------------------------------------------------
  std::printf("chromakey::analyzeFrame\n");
  {
    const chromakey::Params greenKey{0.0f, 1.0f, 0.0f, 50.0f};

    // ---- the empty frame ----
    // The Stats is zeroed before anything else, and a frame with no pixels is
    // answered with that zeroed Stats. Neither output buffer is touched: the
    // body returns at 0x12cc, before the two copies out at 0x1274/0x1288.
    {
      const chromakey::View<const unsigned char> empty{nullptr, 0, 0, 0};
      float d = 11.0f, s = 13.0f;
      const chromakey::Stats st =
          chromakey::analyzeFrame<unsigned char>(empty, 255.0f, greenKey, &d, &s);
      check(st.uniformity == 0.0f && st.meanSpill == 0.0f && st.p5 == 0.0f &&
                st.p95 == 0.0f && st.keyedFraction == 0.0f,
            "an empty frame answers with the zeroed Stats");
      check(d == 11.0f && s == 13.0f, "an empty frame leaves both output buffers untouched");
    }
    // A frame with a width but no height is empty in the same way (0x810..0x828
    // tests the width AND the height).
    {
      std::vector<unsigned char> px(16, 0);
      const chromakey::View<const unsigned char> noHeight{px.data(), 4, 0, 16};
      float d = 11.0f, s = 13.0f;
      const chromakey::Stats st =
          chromakey::analyzeFrame<unsigned char>(noHeight, 255.0f, greenKey, &d, &s);
      check(st.keyedFraction == 0.0f && st.p95 == 0.0f, "a zero-height frame answers with zeros");
      check(d == 11.0f && s == 13.0f, "and still does not touch either output buffer");
    }

    // ---- the four-pixel frame the oracle was probed on ----
    //
    // key = green (0, 1, 0) — in the units the caller passes, which the shipped
    // code does NOT divide by the scale — and scale = 255, so a pixel of
    // (0, 255, 0) lands on the key exactly.
    //
    // The pixels are, in order: green, dim green, red and blue. With
    // sensitivity 50 the threshold is 0.24, so ONLY the green pixel is keyed:
    // the dim green sits at a distance of 0.265897, which is outside it.
    //
    //   distance  0, 0.265897, 0.932954, 0.897119
    //   score     0, 0.156693, 0,        0
    //   keyedFraction 0.25   meanSpill 0.0522311
    //   p5 2.82026   p95 65.5897   uniformity 100
    std::vector<unsigned char> frame(16, 0);
    const unsigned char colours[4][3] = {{0, 255, 0}, {0, 128, 0}, {255, 0, 0}, {0, 0, 255}};
    for (int i = 0; i < 4; ++i) {
      frame[i * 4 + 0] = 255;               // the alpha component
      frame[i * 4 + 1] = colours[i][0];
      frame[i * 4 + 2] = colours[i][1];
      frame[i * 4 + 3] = colours[i][2];
    }
    const chromakey::View<const unsigned char> src{frame.data(), 4, 1, 16};
    float dist[4], score[4];
    const chromakey::Stats st =
        chromakey::analyzeFrame<unsigned char>(src, 255.0f, greenKey, dist, score);

    checkNear(st.keyedFraction, 0.25f, 0.0f,
              "the keyed fraction is the keyed count over the pixel count");
    checkNear(st.meanSpill, 0.0522311f, 1e-6f,
              "the mean spill is the mean score over the NON-keyed pixels");
    check(std::fabs(st.meanSpill - 0.156693f / 4.0f) > 1e-6f,
          "and NOT over every pixel — the mean over all four would be a quarter less");
    checkNear(st.p5, 2.8202601f, 1e-4f, "the 5th percentile is the measured 2.82026");
    checkNear(st.p95, 65.5897141f, 1e-4f, "the 95th percentile is the measured 65.58971");
    checkNear(st.uniformity, 100.0f, 1e-4f,
              "the uniformity is computed over the KEYED distances only — one sample, so 100");

    // The percentiles are the RAW percentiles of the distances, divided by
    // sqrt(2) and multiplied by 100 (0x123c..0x1270). The raw 5th percentile
    // here is 0.15 of the way from 0 to 0.265897, and the raw 95th is 0.85 of
    // the way from 0.897119 to 0.932954.
    const float rawP5 = 0.0f + (0.265897f - 0.0f) * 0.15f;
    const float rawP95 = 0.897119f + (0.932954f - 0.897119f) * 0.85f;
    checkNear(st.p5, rawP5 / 1.4142135f * 100.0f, 1e-3f,
              "the 5th percentile is the raw percentile over 100/sqrt(2)");
    checkNear(st.p95, rawP95 / 1.4142135f * 100.0f, 1e-3f,
              "and so is the 95th");

    // The output buffers carry the per-pixel chroma distance and the per-pixel
    // score, in row order.
    checkNear(dist[0], 0.0f, 1e-6f, "the distance of the green pixel is 0");
    checkNear(dist[1], 0.265897f, 1e-5f, "the dim green sits at 0.265897");
    checkNear(dist[2], 0.932954f, 1e-5f, "red sits at 0.932954");
    checkNear(dist[3], 0.897119f, 1e-5f, "and blue at 0.897119");
    check(score[0] == 0.0f, "a KEYED pixel scores 0");
    checkNear(score[1], 0.156693f, 1e-5f,
              "the dim green — the one non-keyed pixel with chroma — scores 0.156693");

    // And those distances are exactly the chroma distance of the pixel against
    // the key, computed through the exported helpers.
    {
      const chromakey::YCbCr key = chromakey::rgbToYCbCr({0.0f, 1.0f, 0.0f});
      bool allMatch = true;
      for (int i = 0; i < 4; ++i) {
        const chromakey::RGB rgb{static_cast<float>(frame[i * 4 + 1]) / 255.0f,
                                 static_cast<float>(frame[i * 4 + 2]) / 255.0f,
                                 static_cast<float>(frame[i * 4 + 3]) / 255.0f};
        const float want = chromakey::chromaDistance(chromakey::rgbToYCbCr(rgb), key);
        if (std::fabs(want - dist[i]) > 1e-6f) allMatch = false;
      }
      check(allMatch, "every distance is the chroma distance of the pixel against the key");
    }

    // ---- the key is NOT divided by the scale, and the pixels are ----
    //
    // This is the asymmetry that is easiest to read the wrong way round, so it
    // is pinned with a pair of frames that differ ONLY in the units of the key.
    {
      std::vector<unsigned char> one(4, 0);
      one[0] = 255; one[1] = 0; one[2] = 255; one[3] = 0;
      const chromakey::View<const unsigned char> v{one.data(), 1, 1, 4};
      float d = 0.0f, s = 0.0f;
      // The key in RAW units (0, 255, 0) is not divided by the scale, so it sits
      // far away from the pixel (0, 255, 0) / 255 = (0, 1, 0).
      const chromakey::Stats raw = chromakey::analyzeFrame<unsigned char>(
          v, 255.0f, {0.0f, 255.0f, 0.0f, 50.0f}, &d, &s);
      check(raw.keyedFraction == 0.0f,
            "a key given in RAW units is not divided by the scale, so it keys nothing");
      check(d > 100.0f, "and the distance to it is enormous (135.607)");
      // The same key, normalised, keys the pixel exactly.
      const chromakey::Stats norm =
          chromakey::analyzeFrame<unsigned char>(v, 255.0f, greenKey, &d, &s);
      check(norm.keyedFraction == 1.0f, "the SAME key, normalised, keys the pixel");
      checkNear(d, 0.0f, 1e-6f, "at distance 0");
    }

    // ---- the pixel layout is [A, R, G, B] and the alpha is never read ----
    {
      std::vector<unsigned char> two(8, 0);
      two[0] = 0;   two[1] = 0;   two[2] = 255; two[3] = 0;    // green, alpha 0
      two[4] = 255; two[5] = 0;   two[6] = 255; two[7] = 0;    // green, alpha 255
      const chromakey::View<const unsigned char> v{two.data(), 2, 1, 8};
      float d[2], s[2];
      chromakey::analyzeFrame<unsigned char>(v, 255.0f, greenKey, d, s);
      checkNear(d[0], 0.0f, 1e-6f,
                "the pixel layout is [A, R, G, B]: R is p[1], G is p[2], B is p[3]");
      checkNear(d[1], 0.0f, 1e-6f, "and the ALPHA component is never read");
    }

    // ---- the achromatic shortcut is an OR ----
    //
    // 0xc08..0xc18. The fccmp at 0xc14 forces NZCV to 0x8 (N=1, so "less than")
    // on the path where the pixel's magnitude was already below 1e-6, so the
    // branch is taken for an achromatic pixel whatever the key is; and on the
    // other path it is taken when the KEY's magnitude is below 1e-6. So the
    // shortcut fires when EITHER is achromatic. The exported `spillScore` uses
    // the same OR, and the two shipped functions agree.
    {
      // An achromatic PIXEL under a coloured key.
      std::vector<unsigned char> grey(8, 0);
      grey[0] = 255; grey[1] = 128; grey[2] = 128; grey[3] = 128;
      grey[4] = 255; grey[5] = 128; grey[6] = 128; grey[7] = 128;
      const chromakey::View<const unsigned char> vg{grey.data(), 2, 1, 8};
      float d[2], s[2];
      chromakey::analyzeFrame<unsigned char>(vg, 255.0f, greenKey, d, s);
      check(s[0] == 0.0f && s[1] == 0.0f,
            "an achromatic PIXEL under a coloured key scores 0 (the OR)");

      // A coloured PIXEL under an achromatic KEY. This is the case that
      // separates the OR from an AND: under an AND the shortcut would not
      // fire and the pixel would score on its hue.
      std::vector<unsigned char> coloured(8, 0);
      coloured[0] = 255; coloured[1] = 255; coloured[2] = 0; coloured[3] = 0;
      coloured[4] = 255; coloured[5] = 0; coloured[6] = 0; coloured[7] = 255;
      const chromakey::View<const unsigned char> vc{coloured.data(), 2, 1, 8};
      chromakey::analyzeFrame<unsigned char>(vc, 255.0f, {0.5f, 0.5f, 0.5f, 50.0f}, d, s);
      check(s[0] == 0.0f && s[1] == 0.0f,
            "a coloured PIXEL under an achromatic KEY also scores 0 — so it is an OR, not an AND");
    }

    // ---- the percentiles are clamped to [0, 100] ----
    {
      // With scale 1 the pixel values are not divided down, so the distances
      // are enormous and the scaled percentiles would pass 100.
      std::vector<unsigned char> four(16, 0);
      for (int i = 0; i < 4; ++i) {
        four[i * 4 + 0] = 255; four[i * 4 + 1] = 255;
        four[i * 4 + 2] = 0;    four[i * 4 + 3] = 0;
      }
      const chromakey::View<const unsigned char> v{four.data(), 4, 1, 16};
      float d[4], s[4];
      const chromakey::Stats big =
          chromakey::analyzeFrame<unsigned char>(v, 1.0f, greenKey, d, s);
      check(big.p5 == 100.0f, "the 5th percentile is clamped to 100, not left above it");
      check(big.p95 == 100.0f, "and so is the 95th");
    }

    // ---- the uniformity is over the KEYED distances only ----
    {
      // Two keyed pixels at different distances, and one far-away pixel that is
      // NOT keyed. The uniformity must not see the third one.
      std::vector<unsigned char> three(12, 0);
      three[0] = 255; three[1] = 0;   three[2] = 255; three[3] = 0;    // green, distance 0
      three[4] = 255; three[5] = 0;   three[6] = 128; three[7] = 0;    // dim,   distance 0.265897
      three[8] = 255; three[9] = 255; three[10] = 0;  three[11] = 0;   // red,   distance 0.932954
      const chromakey::View<const unsigned char> v{three.data(), 3, 1, 12};
      float d[3], s[3];
      // Sensitivity 100 gives the maximum threshold, 0.45, so the dim green is
      // keyed too and the red is not.
      const chromakey::Stats keyed = chromakey::analyzeFrame<unsigned char>(
          v, 255.0f, {0.0f, 1.0f, 0.0f, 100.0f}, d, s);
      checkNear(keyed.keyedFraction, 2.0f / 3.0f, 1e-6f, "two of the three pixels are keyed");
      // The two keyed distances are 0 and 0.265897: mean 0.1329485, sd the
      // same, so 1 - 2*sd = 0.734103.
      checkNear(keyed.uniformity, (1.0f - 2.0f * 0.1329485f) * 100.0f, 1e-2f,
                "the uniformity is the spread of the KEYED distances alone");
    }

    // ---- the row stride is in ELEMENTS of T ----
    {
      // A 16-bit frame, 2 wide and 2 tall, with a row stride of 12 ELEMENTS —
      // a padded stride. Row 1 starts at element 12. If the stride were read as
      // BYTES, row 1 would start at byte 12, which is element 6, and the frame
      // would read as something else entirely.
      std::vector<unsigned short> wide(24, 0);
      for (int i = 0; i < 2; ++i) {          // row 0: two red pixels
        wide[i * 4 + 0] = 65535; wide[i * 4 + 1] = 65535;
        wide[i * 4 + 2] = 0;     wide[i * 4 + 3] = 0;
      }
      for (int i = 0; i < 2; ++i) {          // row 1: two green pixels
        wide[12 + i * 4 + 0] = 65535; wide[12 + i * 4 + 1] = 0;
        wide[12 + i * 4 + 2] = 65535; wide[12 + i * 4 + 3] = 0;
      }
      const chromakey::View<const unsigned short> v{wide.data(), 2, 2, 12};
      float d[4], s[4];
      chromakey::analyzeFrame<unsigned short>(v, 65535.0f, greenKey, d, s);
      checkNear(d[0], 0.932954f, 1e-4f, "row 0 is the red row");
      checkNear(d[1], 0.932954f, 1e-4f, "both of row 0");
      checkNear(d[2], 0.0f, 1e-4f,
                "row 1 is the green row — the stride is in ELEMENTS of T, not bytes");
      checkNear(d[3], 0.0f, 1e-4f, "and both of row 1");
    }

    // ---- the score model, written from the disassembly ----
    //
    // The per-pixel score of a pixel that is NOT keyed is
    //   chroma * hueWeight * yWeight
    // with chroma = clamp(2 * pixelChromaMagnitude, 0, 1), hueWeight =
    // clamp(1 - hueDifference / (pi/2), 0, 1) and yWeight =
    // min(|pixelLuma - meanKeyedLuma|, 1) — the mean luma being taken over the
    // KEYED pixels only (0xa10..0xa28, or 0.5 when nothing is keyed).
    {
      const chromakey::YCbCr key = chromakey::rgbToYCbCr({0.0f, 1.0f, 0.0f});
      // Only the green pixel is keyed, so the reference luma is the green
      // pixel's luma, 0.587.
      const float referenceY = key.y;
      const chromakey::YCbCr dim = chromakey::rgbToYCbCr({0.0f, 128.0f / 255.0f, 0.0f});
      const float pMag = std::sqrt(dim.cb * dim.cb + dim.cr * dim.cr);
      const float hue = chromakey::angularDiff(chromakey::hueAngle(dim.cb, dim.cr),
                                                chromakey::hueAngle(key.cb, key.cr));
      const float hueWeight = chromakey::clampf(1.0f - hue / 1.5707964f, 0.0f, 1.0f);
      const float yWeight = std::fmin(std::fabs(dim.y - referenceY), 1.0f);
      const float chroma = chromakey::clampf(2.0f * pMag, 0.0f, 1.0f);
      const float model = chromakey::clampf(chroma * hueWeight * yWeight, 0.0f, 1.0f);
      checkNear(model, 0.156693f, 1e-5f, "the score model reproduces the measured 0.156693");
      checkNear(score[1], model, 1e-6f, "and the recovered function returns it");
      checkNear(referenceY, 0.587f, 1e-6f,
                "the reference luma is the mean luma of the KEYED pixels — here the green one alone");
    }
  }

  if (failures == 0) std::printf("ALL RECOVERED CHROMAKEY MATHS VERIFIED (%d checks)\n", passes);
  else std::printf("%d FAILURES out of %d checks\n", failures, passes + failures);
  return failures == 0 ? 0 : 1;
}
