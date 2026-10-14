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


  std::printf("lutblend::sampleLUT\n");
  {
    // An IDENTITY LUT is the strongest available check: trilinear interpolation
    // of a linear ramp must reproduce the input exactly, so any index, stride or
    // weight error shows up as a mismatch rather than hiding in a tolerance.
    auto identity3d = [](int n) {
      lutblend::CubeLUT l;
      l.is3D = true; l.size = n;
      l.minR = l.minG = l.minB = 0.0f;
      l.maxR = l.maxG = l.maxB = 1.0f;
      l.data.resize(static_cast<size_t>(n) * n * n * 3);
      const float d = static_cast<float>(n - 1);
      // .cube ordering, with RED VARYING FASTEST. The recovered index expression
      // is k*size^2 + j*size + i where i is the R index, so R has stride 1. An
      // earlier version of this helper looped R outermost, which produced a LUT
      // whose red and blue were transposed — and the identity round-trip caught
      // it immediately, which is exactly why an identity LUT is the right test.
      size_t w = 0;
      for (int b = 0; b < n; ++b)
        for (int g = 0; g < n; ++g)
          for (int r = 0; r < n; ++r) {
            l.data[w++] = static_cast<float>(r) / d;
            l.data[w++] = static_cast<float>(g) / d;
            l.data[w++] = static_cast<float>(b) / d;
          }
      return l;
    };

    {
      const lutblend::CubeLUT l = identity3d(17);
      check(l.valid(), "the identity LUT passes valid()");
      float o[3];
      // Lattice points must be reproduced exactly.
      lutblend::sampleLUT(l, 0.0f, 0.0f, 0.0f, o);
      checkNear(o[0], 0.0f, 1e-6f, "3D identity: black maps to black");
      lutblend::sampleLUT(l, 1.0f, 1.0f, 1.0f, o);
      checkNear(o[0], 1.0f, 1e-5f, "3D identity: white maps to white");
      lutblend::sampleLUT(l, 0.5f, 0.25f, 0.75f, o);
      checkNear(o[0], 0.5f, 1e-5f, "3D identity: a mid R value round-trips");
      checkNear(o[1], 0.25f, 1e-5f, "3D identity: a mid G value round-trips");
      checkNear(o[2], 0.75f, 1e-5f, "3D identity: a mid B value round-trips");

      // Interior values between lattice points, which is what trilinear buys.
      lutblend::sampleLUT(l, 0.301f, 0.602f, 0.903f, o);
      checkNear(o[0], 0.301f, 1e-4f, "3D identity: off-lattice R interpolates exactly");
      checkNear(o[1], 0.602f, 1e-4f, "3D identity: off-lattice G interpolates exactly");
      checkNear(o[2], 0.903f, 1e-4f, "3D identity: off-lattice B interpolates exactly");

      // Out-of-range inputs clamp rather than extrapolate.
      lutblend::sampleLUT(l, -2.0f, 5.0f, 0.5f, o);
      checkNear(o[0], 0.0f, 1e-5f, "3D identity: R below the domain clamps to 0");
      checkNear(o[1], 1.0f, 1e-5f, "3D identity: G above the domain clamps to 1");
    }

    // A non-unit declared domain is honoured: input 0.5 with domain [0,2] is the
    // midpoint of the ramp, i.e. normalised 0.25.
    {
      lutblend::CubeLUT l = identity3d(17);
      l.maxR = l.maxG = l.maxB = 2.0f;
      check(l.valid(), "identity LUT with a widened domain is still valid");
      float o[3];
      lutblend::sampleLUT(l, 0.5f, 1.0f, 1.5f, o);
      checkNear(o[0], 0.25f, 1e-4f, "domain [0,2]: input 0.5 normalises to 0.25");
      checkNear(o[1], 0.5f, 1e-4f, "domain [0,2]: input 1.0 normalises to 0.5");
      checkNear(o[2], 0.75f, 1e-4f, "domain [0,2]: input 1.5 normalises to 0.75");
    }

    // 1D: three independent curves, each fed by its OWN coordinate. Build a LUT
    // whose three channels are deliberately different ramps, so mixing up which
    // coordinate indexes which channel cannot pass.
    {
      lutblend::CubeLUT l;
      l.is3D = false; l.size = 3;
      l.minR = l.minG = l.minB = 0.0f;
      l.maxR = l.maxG = l.maxB = 1.0f;
      l.data = {
        0.0f, 0.0f, 0.0f,     // entry 0: all channels 0
        0.5f, 0.1f, 0.9f,     // entry 1
        1.0f, 1.0f, 1.0f,     // entry 2: all channels 1
      };
      check(l.valid(), "the 1D test LUT is valid");
      float o[3];
      // Input (1.0, 0.0, 0.5) -> R indexes entry 2 ch0 = 1.0,
      //                         G indexes entry 0 ch1 = 0.0,
      //                         B indexes entry 1 ch2 = 0.9
      lutblend::sampleLUT(l, 1.0f, 0.0f, 0.5f, o);
      checkNear(o[0], 1.0f, 1e-6f, "1D: R is indexed by the R coordinate");
      checkNear(o[1], 0.0f, 1e-6f, "1D: G is indexed by the G coordinate");
      checkNear(o[2], 0.9f, 1e-6f, "1D: B is indexed by the B coordinate");
      // A mid input lands between entries and interpolates.
      lutblend::sampleLUT(l, 0.25f, 0.25f, 0.25f, o);
      checkNear(o[0], 0.25f, 1e-5f, "1D: halfway between entry 0 and 1 interpolates");
      checkNear(o[2], 0.45f, 1e-5f, "1D: and uses that channel's own values");
    }
  }

  std::printf("\n%s\n", failures == 0 ? "ALL RECOVERED LUTBLEND MATHS VERIFIED"
                                      : "TRANSCRIPTION ERRORS PRESENT");
  return failures == 0 ? 0 : 1;
}
