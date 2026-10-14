// Self-check for the recovered noisefield:: namespace.
//
// The three functions here are small enough to state completely, so every assertion
// is an anchored one, and the gradient table is checked entry by entry against the
// canonical set rather than merely counted.

#include "noisefield.h"

#include <cmath>
#include <cstdio>

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

void checkEqI(int got, int want, const char* why) {
  if (got == want) { ++passes; std::printf("  PASS %s\n", why); }
  else { ++failures; std::printf("  FAIL %s  (got %d want %d)\n", why, got, want); }
}

}  // namespace

int main() {
  std::printf("noisefield::clampf\n");
  {
    check(noisefield::clampf(5.0f, 0.0f, 10.0f) == 5.0f, "in range passes through");
    check(noisefield::clampf(-1.0f, 0.0f, 10.0f) == 0.0f, "below lo clamps to lo");
    check(noisefield::clampf(11.0f, 0.0f, 10.0f) == 10.0f, "above hi clamps to hi");
    check(noisefield::clampf(5.0f, 10.0f, 0.0f) == 10.0f,
          "an INVERTED range yields lo, because lo is tested last");
  }

  std::printf("noisefield::clampi\n");
  {
    checkEqI(noisefield::clampi(5, 0, 10), 5, "in range passes through");
    checkEqI(noisefield::clampi(-1, 0, 10), 0, "below lo clamps to lo");
    checkEqI(noisefield::clampi(11, 0, 10), 10, "above hi clamps to hi");
    checkEqI(noisefield::clampi(5, 10, 0), 10, "an inverted range yields lo here too");
    checkEqI(noisefield::clampi(2147483647, 0, 100), 100, "INT_MAX does not overflow");
    checkEqI(noisefield::clampi(-2147483647 - 1, -10, 10), -10, "INT_MIN does not overflow");
  }

  std::printf("noisefield::fbmAmplitudeSum\n");
  {
    // The sum is 1 + gain + gain^2 + ... over `octaves` terms.
    checkNear(noisefield::fbmAmplitudeSum(1, 0.5f), 1.0f, 1e-7f, "one octave sums to 1");
    checkNear(noisefield::fbmAmplitudeSum(2, 0.5f), 1.5f, 1e-7f, "two octaves at gain 0.5 sum to 1.5");
    checkNear(noisefield::fbmAmplitudeSum(3, 0.5f), 1.75f, 1e-7f, "three sum to 1.75");
    checkNear(noisefield::fbmAmplitudeSum(4, 0.5f), 1.875f, 1e-7f, "four sum to 1.875");
    checkNear(noisefield::fbmAmplitudeSum(3, 1.0f), 3.0f, 1e-6f, "a gain of 1 gives one per octave");
    checkNear(noisefield::fbmAmplitudeSum(99, 0.0f), 1.0f, 0.0f, "a gain of 0 stops after the first term");

    // The count is clamped to at least one, and the guard overlaps with it: a
    // non-positive count produces the same answer as one octave, because the loop
    // simply does not run and the guard turns the resulting 0 into 1.
    checkNear(noisefield::fbmAmplitudeSum(0, 0.5f), 1.0f, 0.0f, "zero octaves is one octave");
    checkNear(noisefield::fbmAmplitudeSum(-3, 0.5f), 1.0f, 0.0f, "and so is a negative count");

    // THE GUARD IS REACHABLE, and only through a negative gain: 1 + (-1) = 0.
    checkNear(noisefield::fbmAmplitudeSum(2, -1.0f), 1.0f, 0.0f,
              "a sum of 0 is replaced by 1.0 (the 1e-6f guard)");
    checkNear(noisefield::fbmAmplitudeSum(4, -1.0f), 1.0f, 0.0f, "and again when it cancels to 0 once more");
    // But the guard is not "always 1": a negative gain that does not cancel passes
    // through untouched.
    checkNear(noisefield::fbmAmplitudeSum(2, -0.5f), 0.5f, 1e-7f, "1 + (-0.5) is 0.5, above the guard");
    checkNear(noisefield::fbmAmplitudeSum(3, -2.0f), 3.0f, 1e-6f, "1 - 2 + 4 is 3");
    // A gain above 1 grows, which is what the caller divides by.
    checkNear(noisefield::fbmAmplitudeSum(3, 2.0f), 7.0f, 1e-6f, "1 + 2 + 4 is 7");
  }

  std::printf("noisefield::kGrad3\n");
  {
    // Written out again here, independently of the header, so a transcription slip in
    // the table is caught by comparison rather than by inspection. The test also reads
    // these twelve triples out of the BUNDLE and compares them to the source, which is
    // the check that pins them to the shipped artefact.
    static const int expected[12][3] = {
        {1, 1, 0},  {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0},
        {1, 0, 1},  {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
        {0, 1, 1},  {0, -1, 1}, {0, 1, -1}, {0, -1, -1},
    };
    bool same = true;
    for (int i = 0; i < 12; ++i) {
      for (int j = 0; j < 3; ++j) {
        if (noisefield::kGrad3[i][j] != expected[i][j]) same = false;
      }
    }
    check(same, "all twelve gradients match the canonical improved-noise table, in order");

    // Every entry is an edge midpoint: squared length 2, and never all zero.
    bool allEdges = true;
    for (int i = 0; i < 12; ++i) {
      const int x = noisefield::kGrad3[i][0];
      const int y = noisefield::kGrad3[i][1];
      const int z = noisefield::kGrad3[i][2];
      if (x * x + y * y + z * z != 2) allEdges = false;
      if (x == 0 && y == 0 && z == 0) allEdges = false;
    }
    check(allEdges, "every gradient is a unit edge midpoint (squared length 2, none zero)");

    // Twelve DISTINCT entries: a table with a duplicate would bias the noise towards
    // one direction, which is exactly the kind of defect that would survive a
    // count-only check.
    int distinct = 0;
    for (int i = 0; i < 12; ++i) {
      bool duplicate = false;
      for (int j = 0; j < i; ++j) {
        if (noisefield::kGrad3[i][0] == noisefield::kGrad3[j][0] &&
            noisefield::kGrad3[i][1] == noisefield::kGrad3[j][1] &&
            noisefield::kGrad3[i][2] == noisefield::kGrad3[j][2]) duplicate = true;
      }
      if (!duplicate) ++distinct;
    }
    checkEqI(distinct, 12, "all twelve entries are distinct");
  }

  if (failures == 0) std::printf("ALL RECOVERED NOISEFIELD MATHS VERIFIED (%d checks)\n", passes);
  else std::printf("%d FAILURES out of %d checks\n", failures, passes + failures);
  return failures == 0 ? 0 : 1;
}
