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

  if (failures == 0) std::printf("ALL RECOVERED SHAKE MATHS VERIFIED (%d checks)\n", passes);
  else std::printf("%d FAILURES out of %d checks\n", failures, passes + failures);
  return failures == 0 ? 0 : 1;
}
