// selfcheck.cpp — compile-and-run verification of the recovered lutblend
// primitives, in the same style as the spectra harness: assert mathematical
// properties and cross-check the two independently recovered clampf copies.

#include "lutblend.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {
int failures = 0;

void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "  PASS" : "  FAIL", what);
  if (!ok) ++failures;
}

void checkNear(float got, float want, float tol, const char* what) {
  const bool ok = std::fabs(got - want) <= tol;
  std::printf("%s %s  (got %.9g want %.9g)\n", ok ? "  PASS" : "  FAIL", what, got, want);
  if (!ok) ++failures;
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  std::printf("lutblend::clampf\n");
  checkNear(lutblend::clampf(5.0f, 0.0f, 10.0f), 5.0f, 0.0f, "mid value passes through");
  checkNear(lutblend::clampf(-3.0f, 0.0f, 10.0f), 0.0f, 0.0f, "below lo clamps to lo");
  checkNear(lutblend::clampf(99.0f, 0.0f, 10.0f), 10.0f, 0.0f, "above hi clamps to hi");
  checkNear(lutblend::clampf(-1.0f, 2.0f, 1.0f), 2.0f, 0.0f, "inverted range: lo wins (fcsel order)");

  std::printf("lutblend::lerpf\n");
  checkNear(lutblend::lerpf(0.0f, 10.0f, 0.0f), 0.0f, 1e-6f, "t=0 gives a");
  checkNear(lutblend::lerpf(0.0f, 10.0f, 1.0f), 10.0f, 1e-6f, "t=1 gives b");
  checkNear(lutblend::lerpf(0.0f, 10.0f, 0.5f), 5.0f, 1e-6f, "t=0.5 is the midpoint");
  checkNear(lutblend::lerpf(100.0f, 200.0f, 0.25f), 125.0f, 1e-4f, "works on a non-zero base");
  // The third argument is a mix factor, not a bound: no clamping is applied.
  check(lutblend::lerpf(0.0f, 10.0f, 2.0f) == 20.0f, "extrapolates when t > 1 (not clamped)");

  std::printf("lutblend::luma709\n");
  checkNear(lutblend::luma709(1.0f, 1.0f, 1.0f), 1.0f, 1e-6f, "white is 1.0 (coefficients sum to 1)");
  checkNear(lutblend::luma709(0.0f, 0.0f, 0.0f), 0.0f, 0.0f, "black is 0.0");
  checkNear(lutblend::luma709(1.0f, 0.0f, 0.0f), 0.2126f, 1e-6f, "pure red is the R coefficient");
  checkNear(lutblend::luma709(0.0f, 1.0f, 0.0f), 0.7152f, 1e-6f, "pure green is the G coefficient");
  checkNear(lutblend::luma709(0.0f, 0.0f, 1.0f), 0.0722f, 1e-6f, "pure blue is the B coefficient");
  // Green must dominate: that is BT.709 rather than a flat or Rec.601 weighting.
  check(lutblend::luma709(0.0f, 1.0f, 0.0f) > lutblend::luma709(1.0f, 0.0f, 0.0f),
        "green weighs more than red (BT.709, not a flat average)");

  std::printf("lutblend::CubeLUT::valid\n");
  {
    auto mk = [](bool is3D, int size, float span) {
      lutblend::CubeLUT l;
      l.is3D = is3D;
      l.size = size;
      l.minR = l.minG = l.minB = 0.0f;
      l.maxR = l.maxG = l.maxB = span;
      const size_t n = static_cast<size_t>(size) *
                       (is3D ? static_cast<size_t>(size) * size : 1) * 3;
      l.data.assign(n, 0.0f);
      return l;
    };

    check(mk(true, 33, 1.0f).valid(), "a well-formed 33^3 3D LUT is valid");
    check(mk(false, 256, 1.0f).valid(), "a well-formed 256-entry 1D LUT is valid");

    // size >= 2
    check(!mk(true, 1, 1.0f).valid(), "size < 2 is rejected");
    check(!mk(true, 0, 1.0f).valid(), "size 0 is rejected");

    // Exact data length, in both directions.
    {
      lutblend::CubeLUT short3d = mk(true, 33, 1.0f);
      short3d.data.pop_back();
      check(!short3d.valid(), "a 3D LUT one float short is rejected");
      lutblend::CubeLUT long1d = mk(false, 256, 1.0f);
      long1d.data.push_back(0.0f);
      check(!long1d.valid(), "a 1D LUT one float long is rejected (exact length, no slack)");
    }

    // A 3D LUT needs size^3 triples; a 1D-shaped table must not pass as 3D.
    {
      lutblend::CubeLUT wrong = mk(false, 33, 1.0f);   // 33*3 floats
      wrong.is3D = true;                                // now expects 33^3*3
      check(!wrong.valid(), "a 1D-sized table is not accepted as 3D");

      lutblend::CubeLUT wrong2 = mk(true, 33, 1.0f);    // 33^3*3 floats
      wrong2.is3D = false;                              // now expects 33*3
      check(!wrong2.valid(), "a 3D-sized table is not accepted as 1D");
    }

    // Strictly increasing domain per channel.
    {
      lutblend::CubeLUT flat = mk(true, 33, 1.0f);
      flat.maxR = flat.minR;
      check(!flat.valid(), "maxR == minR is rejected (must strictly exceed)");
      lutblend::CubeLUT inverted = mk(true, 33, 1.0f);
      inverted.maxG = -1.0f;   // minG is 0, so max < min
      check(!inverted.valid(), "maxG < minG is rejected");
      // A shifted but valid domain is fine.
      lutblend::CubeLUT shifted = mk(true, 33, 1.0f);
      shifted.minR = shifted.minG = shifted.minB = -0.5f;
      shifted.maxR = shifted.maxG = shifted.maxB = 2.0f;
      check(shifted.valid(), "a non-unit domain ([-0.5, 2.0]) is accepted");
    }

    // The exact float counts, asserted so the multiplier chain cannot regress.
    check(mk(true, 2, 1.0f).data.size() == 2 * 2 * 2 * 3, "2^3 3D LUT holds 24 floats");
    check(mk(false, 2, 1.0f).data.size() == 2 * 3, "2-entry 1D LUT holds 6 floats");
  }

  std::printf("\n%s\n", failures == 0 ? "ALL RECOVERED LUTBLEND MATHS VERIFIED"
                                      : "TRANSCRIPTION ERRORS PRESENT");
  return failures == 0 ? 0 : 1;
}
