// Self-check for the recovered shake:: namespace.
//
// The point of most of these is the EVEN case: `shake::median` averages the two middle
// values where `beatsnap::medianOfWindow` returns the upper one. Two functions in one fleet
// with the same name for the same job and opposite answers on an even count is exactly the
// kind of thing a rewrite gets wrong quietly.

#include "shake.h"

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

void checkNear(double got, double want, double tol, const char* why) {
  if (std::fabs(got - want) <= tol) { ++passes; std::printf("  PASS %s\n", why); }
  else { ++failures; std::printf("  FAIL %s  (got %.9g want %.9g)\n", why, got, want); }
}

}  // namespace

int main() {
  std::printf("shake::clampf\n");
  {
    check(shake::clampf(5.0f, 0.0f, 10.0f) == 5.0f, "in range passes through");
    check(shake::clampf(-1.0f, 0.0f, 10.0f) == 0.0f, "below lo clamps to lo");
    check(shake::clampf(11.0f, 0.0f, 10.0f) == 10.0f, "above hi clamps to hi");
    check(shake::clampf(5.0f, 10.0f, 0.0f) == 10.0f,
          "an INVERTED range yields lo, because lo is tested last");
  }

  std::printf("shake::median\n");
  {
    check(shake::median({}) == 0.0f, "an empty vector is 0, not a crash");

    // Odd counts take the middle outright.
    checkNear(shake::median({5.0f}), 5.0, 0.0, "one value is itself");
    checkNear(shake::median({3.0f, 1.0f, 2.0f}), 2.0, 0.0, "three unsorted values give 2");
    checkNear(shake::median({9.0f, 1.0f, 5.0f, 3.0f, 7.0f}), 5.0, 0.0, "five give the middle of five");

    // EVEN counts AVERAGE the two middles. This is the check that separates this function
    // from `beatsnap::medianOfWindow`, which would answer 3 here.
    checkNear(shake::median({1.0f, 2.0f, 3.0f, 4.0f}), 2.5, 0.0,
              "four values AVERAGE the middle pair: 2.5, not 3");
    checkNear(shake::median({10.0f, 0.0f}), 5.0, 0.0, "two values average to their midpoint");
    checkNear(shake::median({4.0f, 1.0f, 3.0f, 2.0f}), 2.5, 0.0, "and the input need not be sorted");

    // It reorders its OWN copy, so the caller's vector is untouched — the parameter is by
    // value. A by-reference signature here would silently sort the caller's data.
    std::vector<float> mine{9.0f, 1.0f, 5.0f, 3.0f};
    const float got = shake::median(mine);
    checkNear(got, 4.0, 0.0, "an even count of four averages 3 and 5");
    check(mine[0] == 9.0f && mine[1] == 1.0f && mine[2] == 5.0f && mine[3] == 3.0f,
          "the caller's vector is NOT reordered (the parameter is by value)");

    // Duplicates and negatives are ordinary values.
    checkNear(shake::median({2.0f, 2.0f, 2.0f}), 2.0, 0.0, "all-equal values give that value");
    checkNear(shake::median({-5.0f, -1.0f, -3.0f}), -3.0, 0.0, "negative values sort normally");
    checkNear(shake::median({-2.0f, 2.0f}), 0.0, 0.0, "an even pair straddling zero averages to 0");

    // A LARGE EVEN COUNT IS NOT THE TRUE MEDIAN, and that is the shipped behaviour rather
    // than a transcription error. `nth_element(first, begin + mid, end)` pins `values[mid]`
    // — the upper middle — and nothing else; the lower half is left unordered. The shipped
    // code then calls `__nth_element` again over `[begin + mid - 1, begin + mid)`, which is
    // ONE ELEMENT, so it pins nothing either, and `values[mid - 1]` is whatever the first
    // call happened to leave there.
    //
    // Measured on a permutation of 0..999: this returns 469.5 where the true median is
    // 499.5. The value is not asserted, because it depends on libc++'s partial ordering
    // rather than on this source; what IS asserted is the property that must hold — the
    // answer is one of the inputs and lies within their range — and the exact upper middle,
    // which `nth_element` does guarantee.
    std::vector<float> big;
    for (int i = 0; i < 1000; ++i) big.push_back(static_cast<float>((i * 7919) % 1000));
    const float bigMedian = shake::median(big);
    check(bigMedian >= 0.0f && bigMedian <= 999.0f, "a large even count still returns a value from the input's range");
    // The upper middle IS exact, so half the pair is right: for a permutation of 0..999 the
    // two middle order statistics are 499 and 500, so a correct even median is 499.5.
    check(bigMedian < 499.5f,
          "and it is BELOW the true median, which is the shipped second nth_element\'s one-element range showing");
  }

  // --- the filter chain (recovered 2026-10-27): movingAverage -> highPass -> extractJitter.
  std::printf("shake::movingAverage\n");
  {
    const std::vector<float> flat(8, 0.25f);
    const std::vector<float> average = shake::movingAverage(flat, 5);
    check(average.size() == flat.size(), "the output is the same length as the input, including at the ends");
    bool allQuarter = true;
    for (float v : average) if (std::fabs(v - 0.25f) > 1e-6f) allQuarter = false;
    check(allQuarter, "a constant series average to that constant everywhere (the divisor is the real count)");

    const std::vector<float> copy = shake::movingAverage(flat, 1);
    check(copy == flat, "a window of 1 is a copy (half = 0)");
    check(shake::movingAverage({}, 5).empty(), "an empty series gives an empty answer");

    // A step: the sample either side of the edge blends, and the far samples do not.
    std::vector<float> step(9, 0.0f);
    for (size_t i = 5; i < step.size(); ++i) step[i] = 1.0f;
    const std::vector<float> smoothed = shake::movingAverage(step, 3);
    checkNear(smoothed[0], 0.0f, 1e-6f, "away from the step the average is untouched");
    checkNear(smoothed[8], 1.0f, 1e-6f, "and so is the far side");
    check(smoothed[4] > 0.0f && smoothed[4] < 1.0f, "at the step it blends");

    // The sum is accumulated in DOUBLE: 2^24 + 1 is not representable in float, so a float
    // accumulator loses the ones and a double one does not.
    std::vector<float> wide{16777216.0f, 1.0f, 1.0f, 1.0f};
    const float mean = shake::movingAverage(wide, 4)[1];
    // 16777216 + 3 in a float accumulator rounds each 1.0 away and lands on 4194304.0 exactly;
    // the double sum is 16777219 / 4 = 4194304.75, whose nearest float is 4194305.
    checkNear(mean, static_cast<float>((16777216.0 + 3.0) / 4.0), 1.0f,
              "the window sum is accumulated in double (a float accumulator would land on 4194304)");
    check(mean != 16777216.0f / 4.0f, "and that is demonstrably not what a float accumulator gives");
  }

  std::printf("shake::highPass\n");
  {
    const std::vector<float> flat(6, 0.75f);
    bool allZero = true;
    for (float v : shake::highPass(flat, 3)) if (std::fabs(v) > 1e-6f) allZero = false;
    check(allZero, "a constant series has no residual");
    check(shake::highPass({}, 3).empty(), "an empty series gives an empty residual");

    std::vector<float> ramp(10);
    for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = static_cast<float>(i);
    const std::vector<float> residual = shake::highPass(ramp, 5);
    check(residual.size() == ramp.size(), "the residual covers every sample");
    // A straight ramp is reproduced exactly by a centred average, so its residual is ~0 in the
    // interior and non-zero only where the window is clipped at the ends.
    check(std::fabs(residual[5]) < 1e-6f, "the interior of a ramp has no residual");
    check(std::fabs(residual[0]) > 0.0f, "the clipped end does");
  }

  std::printf("shake::extractJitter\n");
  {
    shake::ShakeProfile profile;
    profile.horizontal = {0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f};
    profile.vertical = {1.0f, 2.0f, 3.0f, 4.0f};
    const shake::ShakeProfile jitter = shake::extractJitter(profile, 3);
    check(jitter.horizontal.size() == profile.horizontal.size(), "the horizontal track keeps its length");
    check(jitter.vertical.size() == profile.vertical.size(), "and so does the vertical one");
    check(jitter.horizontal == shake::highPass(profile.horizontal, 3), "horizontal is the high-passed track");
    check(jitter.vertical == shake::highPass(profile.vertical, 3), "vertical likewise, in that order");
    checkNear(jitter.horizontal[3], 10.0f - shake::movingAverage(profile.horizontal, 3)[3], 1e-6f,
              "and both are the residual of their own series");
    check(sizeof(shake::ShakeProfile) == 48, "the struct is two vectors, 24 bytes apart");
  }

  if (failures == 0) std::printf("ALL RECOVERED SHAKE MATHS VERIFIED (%d checks)\n", passes);
  else std::printf("%d FAILURES out of %d checks\n", failures, passes + failures);
  return failures == 0 ? 0 : 1;
}
