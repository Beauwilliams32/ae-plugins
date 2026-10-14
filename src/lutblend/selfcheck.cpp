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

  std::printf("lutblend::makeIdentity\n");
  {
    // 3D identity, size 2: 8 entries, R fastest, each stored (r, g, b).
    lutblend::CubeLUT l;
    lutblend::makeIdentity(&l, 2, true);

    check(l.is3D, "3D flag is set");
    check(l.size == 2, "size is stored");
    check(l.minR == 0.f && l.minG == 0.f && l.minB == 0.f, "min is 0 on every axis");
    check(l.maxR == 1.f && l.maxG == 1.f && l.maxB == 1.f, "max is 1 on every axis");
    check(l.data.size() == 24, "3 * size^3 == 24 floats for size 2");
    check(l.valid(), "the result passes CubeLUT::valid()");

    // The whole point of the ordering: entry (b,g,r) must be (r,g,b) normalised.
    const float want2[24] = {
      0.f, 0.f, 0.f,   1.f, 0.f, 0.f,   // b=0 g=0: r=0, r=1
      0.f, 1.f, 0.f,   1.f, 1.f, 0.f,   // b=0 g=1
      0.f, 0.f, 1.f,   1.f, 0.f, 1.f,   // b=1 g=0
      0.f, 1.f, 1.f,   1.f, 1.f, 1.f,   // b=1 g=1
    };
    bool same = l.data.size() == 24;
    for (size_t i = 0; same && i < 24; ++i) same = (l.data[i] == want2[i]);
    check(same, "entries are in .cube order: R fastest, triples stored (r,g,b)");

    // A identity LUT must round-trip through sampleLUT.
    float o[3];
    lutblend::sampleLUT(l, 0.25f, 0.5f, 0.75f, o);
    checkNear(o[0], 0.25f, 1e-5f, "the 3D identity round-trips R");
    checkNear(o[1], 0.50f, 1e-5f, "the 3D identity round-trips G");
    checkNear(o[2], 0.75f, 1e-5f, "the 3D identity round-trips B");

    // Bigger, so interpolation actually has to happen between RGB lattice points.
    lutblend::CubeLUT l5;
    lutblend::makeIdentity(&l5, 5, true);
    check(l5.data.size() == 375, "3 * 5^3 == 375 floats for size 5");
    lutblend::sampleLUT(l5, 0.3f, 0.6f, 0.9f, o);
    checkNear(o[0], 0.3f, 1e-5f, "a size-5 identity round-trips R through interpolation");
    checkNear(o[1], 0.6f, 1e-5f, "a size-5 identity round-trips G through interpolation");
    checkNear(o[2], 0.9f, 1e-5f, "a size-5 identity round-trips B through interpolation");

    // 1D identity: size entries of (v, v, v).
    lutblend::CubeLUT l1;
    lutblend::makeIdentity(&l1, 4, false);
    check(!l1.is3D, "1D flag is cleared");
    check(l1.data.size() == 12, "3 * size == 12 floats for a size-4 1D LUT");
    check(l1.data[0] == 0.f && l1.data[1] == 0.f && l1.data[2] == 0.f, "1D entry 0 is black");
    checkNear(l1.data[3], 1.f / 3.f, 1e-6f, "1D entry 1");
    checkNear(l1.data[4], 1.f / 3.f, 1e-6f, "1D entry 1 repeats across channels");
    checkNear(l1.data[5], 1.f / 3.f, 1e-6f, "1D entry 1 repeats across channels again");
    check(l1.data[3] == l1.data[4] && l1.data[4] == l1.data[5],
          "1D entries are identical across channels (grey ramp)");
    checkNear(l1.data[6], 2.f / 3.f, 1e-6f, "1D entry 2");
    check(l1.data[9] == 1.f && l1.data[10] == 1.f && l1.data[11] == 1.f,
          "the last 1D entry is white");

    // Reuse: a second call must not reallocate, and must not leave stale data.
    lutblend::CubeLUT reuse;
    lutblend::makeIdentity(&reuse, 3, true);
    const size_t cap1 = reuse.data.capacity();
    const size_t n1 = reuse.data.size();
    lutblend::makeIdentity(&reuse, 3, true);
    check(reuse.data.size() == n1, "a repeated call produces the same size, not an append");
    check(reuse.data.capacity() == cap1, "a repeated call reuses the allocation (reserve, not resize)");

    // Shrinking then growing must not leave stale entries behind.
    lutblend::makeIdentity(&reuse, 2, true);
    check(reuse.data.size() == 24, "shrinking resizes correctly");
    lutblend::makeIdentity(&reuse, 3, true);
    check(reuse.data.size() == 81, "growing back resizes correctly, with no stale tail");

    // size <= 0 writes no data (after the reserve has run).
    lutblend::CubeLUT lz;
    lutblend::makeIdentity(&lz, 0, true);
    check(lz.data.empty(), "size 0 writes no entries");
    check(!lz.valid(), "and is not a valid LUT");
  }


  std::printf("lutblend::applyLUTBlend\n");
  {
    // Build two constant LUTs so the expected blend is exact.
    auto constant3D = [](float r, float g, float b) {
      lutblend::CubeLUT l;
      lutblend::makeIdentity(&l, 2, true);
      for (size_t i = 0; i + 2 < l.data.size(); i += 3) {
        l.data[i] = r; l.data[i + 1] = g; l.data[i + 2] = b;
      }
      return l;
    };

    const lutblend::CubeLUT black = constant3D(0.f, 0.f, 0.f);
    const lutblend::CubeLUT white = constant3D(1.f, 1.f, 1.f);
    const lutblend::CubeLUT half  = constant3D(0.5f, 0.5f, 0.5f);
    const float rgb[3] = {0.2f, 0.4f, 0.6f};
    float o[3];

    // blend 0 -> entirely the first LUT; blend 1 -> entirely the second.
    lutblend::applyLUTBlend(black, white, 0.f, 1.f, false, rgb, o);
    checkNear(o[0], 0.f, 1e-6f, "blend 0 takes LUT A");
    checkNear(o[1], 0.f, 1e-6f, "blend 0 takes LUT A on every channel");
    checkNear(o[2], 0.f, 1e-6f, "blend 0 takes LUT A on every channel again");

    lutblend::applyLUTBlend(black, white, 1.f, 1.f, false, rgb, o);
    checkNear(o[0], 1.f, 1e-6f, "blend 1 takes LUT B");
    checkNear(o[2], 1.f, 1e-6f, "blend 1 takes LUT B on every channel");

    lutblend::applyLUTBlend(black, white, 0.25f, 1.f, false, rgb, o);
    checkNear(o[0], 0.25f, 1e-6f, "blend 0.25 lands a quarter of the way to B");

    // intensity 0 -> the original pixel, whatever the LUTs say.
    lutblend::applyLUTBlend(black, white, 1.f, 0.f, false, rgb, o);
    checkNear(o[0], 0.2f, 1e-6f, "intensity 0 returns the original R");
    checkNear(o[1], 0.4f, 1e-6f, "intensity 0 returns the original G");
    checkNear(o[2], 0.6f, 1e-6f, "intensity 0 returns the original B");

    // intensity 0.5 -> halfway between the original and the LUT result.
    lutblend::applyLUTBlend(half, half, 0.f, 0.5f, false, rgb, o);
    checkNear(o[0], 0.35f, 1e-6f, "intensity 0.5 blends the original halfway");
    checkNear(o[2], 0.55f, 1e-6f, "intensity 0.5 blends the original halfway on B");

    // Both amounts are CLAMPED, not merely expected to be in range.
    lutblend::applyLUTBlend(black, white, 5.f, 1.f, false, rgb, o);
    checkNear(o[0], 1.f, 1e-6f, "blend > 1 clamps to 1");
    lutblend::applyLUTBlend(black, white, -5.f, 1.f, false, rgb, o);
    checkNear(o[0], 0.f, 1e-6f, "blend < 0 clamps to 0");
    lutblend::applyLUTBlend(black, white, 1.f, 2.f, false, rgb, o);
    checkNear(o[0], 1.f, 1e-6f, "intensity > 1 clamps to 1");
    lutblend::applyLUTBlend(white, white, 0.f, -1.f, false, rgb, o);
    checkNear(o[0], 0.2f, 1e-6f, "intensity < 0 clamps to 0");

    // Identity LUTs leave the pixel alone when both amounts are 1.
    lutblend::CubeLUT id;
    lutblend::makeIdentity(&id, 5, true);
    lutblend::applyLUTBlend(id, id, 0.5f, 1.f, false, rgb, o);
    checkNear(o[0], 0.2f, 1e-5f, "identity LUTs pass R through");
    checkNear(o[1], 0.4f, 1e-5f, "identity LUTs pass G through");
    checkNear(o[2], 0.6f, 1e-5f, "identity LUTs pass B through");

    // With identity LUTs the luma already matches, so preserveLuma is a no-op.
    lutblend::applyLUTBlend(id, id, 0.5f, 1.f, true, rgb, o);
    checkNear(o[0], 0.2f, 1e-5f, "preserveLuma on an identity grade changes nothing");

    // A black result must not divide by zero: the 1e-5 guard skips the rescale.
    lutblend::applyLUTBlend(black, black, 0.f, 1.f, true, rgb, o);
    checkNear(o[0], 0.f, 1e-6f, "preserveLuma against a black LUT stays black (no 0/0)");
    check(o[0] == o[0] && o[1] == o[1] && o[2] == o[2], "and produces no NaN");

    // Luminance preservation actually rescales: blend a white LUT toward a
    // dim grey one and check the result's luma equals the input's.
    const lutblend::CubeLUT grey = constant3D(0.25f, 0.25f, 0.25f);
    lutblend::applyLUTBlend(white, grey, 0.f, 1.f, true, rgb, o);
    const float wantLuma = lutblend::luma709(rgb[0], rgb[1], rgb[2]);
    const float gotLuma  = lutblend::luma709(o[0], o[1], o[2]);
    checkNear(gotLuma, wantLuma, 1e-5f, "preserveLuma restores the input's Rec.709 luma");

    // ...and without it, the luma is NOT restored. Same inputs, flag off.
    lutblend::applyLUTBlend(white, grey, 0.f, 1.f, false, rgb, o);
    check(std::fabs(lutblend::luma709(o[0], o[1], o[2]) - wantLuma) > 0.1f,
          "without preserveLuma the luma is genuinely different (the flag does something)");

    // The luma is computed the way luma709 computes it, so a colour that only
    // differs in which channel carries the energy must scale identically.
    lutblend::applyLUTBlend(white, constant3D(0.5f, 0.25f, 0.125f), 0.f, 1.f, true, rgb, o);
    const float r2 = lutblend::luma709(o[0], o[1], o[2]);
    checkNear(r2, wantLuma, 1e-5f, "preserveLuma holds for a coloured LUT too");
  }


  std::printf("\n%s\n", failures == 0 ? "ALL RECOVERED LUTBLEND MATHS VERIFIED"
                                      : "TRANSCRIPTION ERRORS PRESENT");
  return failures == 0 ? 0 : 1;
}
