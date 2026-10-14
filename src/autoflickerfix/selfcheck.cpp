// selfcheck.cpp — behavioural gate for the recovered aff:: namespace.
//
// The codegen differ proves the SHAPE of the recovered code against the bundle. This
// proves its BEHAVIOUR, and the two catch different mistakes: a transcription can
// reproduce a function's instruction stream from the wrong reading only by coincidence,
// and it can behave correctly while compiling to something else entirely.
//
// Every assertion here is written from the DISASSEMBLY, not from the transcription. That
// distinction matters more than it sounds: `meanOf` returning NaN on an empty vector
// instead of 0 passed a self-check written from the same misreading, and only the
// instruction-count comparison exposed it. Where an expectation comes from a specific
// instruction or call site, the comment says which.

#include "autoflickerfix.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void pass(const char* what) {
  ++checks;
  std::printf("  PASS  %s\n", what);
}

void fail(const char* what, const char* detail) {
  ++checks;
  ++failures;
  std::printf("  FAIL  %s -- %s\n", what, detail);
}

void expectClose(const char* what, float got, float want, float tol = 1e-5f) {
  if (std::fabs(got - want) <= tol) {
    pass(what);
  } else {
    char buf[160];
    std::snprintf(buf, sizeof buf, "got %.9g want %.9g", static_cast<double>(got),
                  static_cast<double>(want));
    fail(what, buf);
  }
}

void expectTrue(const char* what, bool cond) {
  if (cond) pass(what); else fail(what, "condition was false");
}

void expectEqualI(const char* what, long got, long want) {
  if (got == want) {
    pass(what);
  } else {
    char buf[128];
    std::snprintf(buf, sizeof buf, "got %ld want %ld", got, want);
    fail(what, buf);
  }
}

}  // namespace

int main() {
  using namespace aff;

  // -----------------------------------------------------------------------
  // clampf
  //
  // The disassembly applies the HIGH bound first and then tests the LOW bound against
  // the original value, so on an inverted range `lo` wins. If this had been written as
  // min(max(v, lo), hi) the two inverted-range cases below would come out as hi.
  // -----------------------------------------------------------------------
  expectClose("clampf below range", clampf(-1.0f, 0.0f, 10.0f), 0.0f);
  expectClose("clampf above range", clampf(50.0f, 0.0f, 10.0f), 10.0f);
  expectClose("clampf inside", clampf(5.0f, 0.0f, 10.0f), 5.0f);
  expectClose("clampf inverted: low bound wins", clampf(5.0f, 10.0f, 0.0f), 10.0f);
  expectClose("clampf inverted: below the low bound", clampf(2.0f, 10.0f, 0.0f), 10.0f);

  // -----------------------------------------------------------------------
  // dbToGainClamp
  //
  // RECOVERED AS A STRUCT RETURN. The body leaves `gain` in s1 and `1/gain` in s0 and
  // returns, and the EffectMain call site stores both with a single `stp s0, s1`. So the
  // low bound is the reciprocal and the high bound is the gain itself: a SYMMETRIC
  // decibel window. Asserting only `lo` would have passed against a float-returning
  // misreading, which is exactly the version that was written first.
  // -----------------------------------------------------------------------
  {
    const GainClamp c6 = dbToGainClamp(6.0f);
    expectClose("dbToGainClamp(6).hi is +6 dB as a gain", c6.hi, 1.9952623f, 1e-5f);
    expectClose("dbToGainClamp(6).lo is -6 dB as a gain", c6.lo, 0.50118723f, 1e-5f);
    expectTrue("dbToGainClamp(6) is a symmetric window",
               std::fabs(c6.lo * c6.hi - 1.0f) < 1e-6f);

    const GainClamp cm6 = dbToGainClamp(-6.0f);
    expectTrue("the sign of the decibel value is discarded (fabs)",
               cm6.lo == c6.lo && cm6.hi == c6.hi);

    const GainClamp c0 = dbToGainClamp(0.0f);
    expectClose("0 dB is unity on both bounds", c0.lo, 1.0f);
    expectClose("0 dB is unity on both bounds (hi)", c0.hi, 1.0f);

    const GainClamp cn = dbToGainClamp(std::numeric_limits<float>::quiet_NaN());
    // The guard is `gain > 0`, and every comparison against NaN is false, so the false
    // arm is taken: lo becomes 1.0. `hi` is NOT guarded and stays NaN in the binary.
    expectClose("NaN decibels: the guarded bound falls back to 1.0", cn.lo, 1.0f);
    expectTrue("NaN decibels: the unguarded bound is NaN, as in the binary",
               std::isnan(cn.hi));
  }

  // -----------------------------------------------------------------------
  // computeGain
  //
  // ARGUMENT ORDER IS `current` THEN `target`. The body divides s1 by s0, and the four
  // call sites in EffectMain load v0 from the per-frame measurement and v1 from
  // temporalTarget. The asymmetric test below is the one that catches a swap: with the
  // arguments reversed, a 4x-too-bright frame yields the reciprocal gain instead.
  // -----------------------------------------------------------------------
  {
    const GainClamp wide{0.0f, 1e9f};
    expectClose("computeGain moves current toward a brighter target",
                computeGain(0.25f, 1.0f, 1.0f, wide), 4.0f);
    expectClose("computeGain moves current toward a darker target",
                computeGain(1.0f, 0.25f, 1.0f, wide), 0.25f);
    expectClose("amount 0 leaves the value alone",
                computeGain(0.25f, 1.0f, 0.0f, wide), 1.0f);
    expectClose("amount is a lerp, not a switch",
                computeGain(0.25f, 1.0f, 0.5f, wide), 2.5f);
    expectClose("a negative amount clamps to 0, not to a negative lerp",
                computeGain(0.25f, 1.0f, -1.0f, wide), 1.0f);
    expectClose("an amount above 1 clamps to 1",
                computeGain(0.25f, 1.0f, 5.0f, wide), 4.0f);

    // The guards are on the INPUTS, at 1e-6, and the comparison is <=. Testing the ratio
    // afterwards would not keep a black frame from producing inf.
    expectClose("a black current frame is not corrected", computeGain(0.0f, 1.0f, 1.0f, wide), 1.0f);
    expectClose("a black target frame is not corrected", computeGain(1.0f, 0.0f, 1.0f, wide), 1.0f);
    expectClose("exactly 1e-6 trips the guard", computeGain(1e-6f, 1.0f, 1.0f, wide), 1.0f);
    expectClose("just above 1e-6 does not",
                computeGain(2e-6f, 1.0f, 1.0f, wide), 1.0f + (1.0f / 2e-6f - 1.0f));

    // The clamp is applied to the RESULT, and through the same clampf. A gain above the
    // window's high bound is cut to `hi`; a gain below its low bound is raised to `lo`.
    const GainClamp tight{0.9f, 1.1f};
    expectClose("a gain above hi is cut to hi", computeGain(0.25f, 1.0f, 1.0f, tight), 1.1f);
    expectClose("a gain below lo is raised to lo", computeGain(1.0f, 0.25f, 1.0f, tight), 0.9f);
    expectClose("a gain inside the window is untouched",
                computeGain(1.0f, 1.05f, 1.0f, tight), 1.05f);
  }

  // -----------------------------------------------------------------------
  // centerRegion
  //
  // frac 0.5 of a 1920x1080 frame is the centred 960x540 rectangle. frac 1 is the whole
  // frame; frac 0 is a single pixel at the origin. The member order is pinned by the
  // `orr x0, minX, minY<<32` return.
  // -----------------------------------------------------------------------
  {
    const RegionBounds half = centerRegion(1920, 1080, 0.5f);
    expectEqualI("centerRegion(1920,1080,0.5).minX", half.minX, 480);
    expectEqualI("centerRegion(1920,1080,0.5).minY", half.minY, 270);
    expectEqualI("centerRegion(1920,1080,0.5).maxX", half.maxX, 1440);
    expectEqualI("centerRegion(1920,1080,0.5).maxY", half.maxY, 810);

    const RegionBounds all = centerRegion(1920, 1080, 1.0f);
    expectEqualI("frac 1 starts at the origin (x)", all.minX, 0);
    expectEqualI("frac 1 starts at the origin (y)", all.minY, 0);
    expectEqualI("frac 1 ends at the width", all.maxX, 1920);
    expectEqualI("frac 1 ends at the height", all.maxY, 1080);

    // frac 0 does NOT give a pixel at the origin — it gives a ZERO-AREA rectangle at the
    // CENTRE, because the offsets are `max((width - rw) / 2, 0)` and rw is 0. This was
    // written wrong first, from the assumption that shrinking to nothing would pin the
    // rectangle to a corner.
    const RegionBounds none = centerRegion(1920, 1080, 0.0f);
    expectEqualI("frac 0 is centred on x", none.minX, 960);
    expectEqualI("frac 0 is centred on y", none.minY, 540);
    expectEqualI("frac 0 has zero width", none.maxX - none.minX, 0);
    expectEqualI("frac 0 has zero height", none.maxY - none.minY, 0);

    // A hair above zero is the smallest non-empty rectangle, and it is still centred:
    // 0.001 * 1920 = 1.92, rounded to 2, and dx = 1918 so halfW = 959.
    const RegionBounds sliver = centerRegion(1920, 1080, 0.001f);
    expectEqualI("a sliver is centred, not cornered (minX)", sliver.minX, 959);
    expectEqualI("a sliver is two pixels wide", sliver.maxX - sliver.minX, 2);

    // The clamp on frac is the same clampf, so 2.0 is the whole frame and -1.0 clamps
    // to the same degenerate centre as 0.
    const RegionBounds over = centerRegion(100, 100, 2.0f);
    expectEqualI("frac above 1 clamps to the whole frame", over.maxX, 100);
    const RegionBounds under = centerRegion(100, 100, -1.0f);
    expectEqualI("frac below 0 clamps to the degenerate centre",
                 under.maxX - under.minX, 0);

    // lroundf, ties away from zero: 0.5 * 3 = 1.5 rounds to 2, and dx = 1 makes the
    // offsets 0, so this is the case that a truncating cast would get wrong (it would
    // give rw = 1).
    const RegionBounds odd = centerRegion(3, 3, 0.5f);
    expectEqualI("odd dimension rounds 1.5 up to 2", odd.maxX - odd.minX, 2);

    // The rectangle must stay inside the frame for a spread of fractions.
    bool inside = true;
    for (int w = 1; w <= 97; w += 7) {
      for (int h = 1; h <= 61; h += 5) {
        for (int f = 0; f <= 10; ++f) {
          const RegionBounds r = centerRegion(w, h, static_cast<float>(f) / 10.0f);
          if (r.minX < 0 || r.minY < 0 || r.maxX > w || r.maxY > h
              || r.maxX < r.minX || r.maxY < r.minY) {
            inside = false;
          }
        }
      }
    }
    expectTrue("every fraction of every small frame yields a contained rectangle", inside);
  }

  // -----------------------------------------------------------------------
  // meanOf
  //
  // THE EMPTY CASE RETURNS 0.0f. This is the assertion that would have failed on the
  // first transcription: with the guard left implicit, `0.0 / 0.0` gave NaN. The bundle
  // has its own `ret` on that path, which is how the omission was found.
  // -----------------------------------------------------------------------
  {
    expectClose("meanOf of an empty vector is 0, not NaN", meanOf({}), 0.0f);
    expectTrue("meanOf of an empty vector is not NaN", !std::isnan(meanOf({})));
    expectClose("meanOf of one element", meanOf({7.5f}), 7.5f);
    expectClose("meanOf of a simple set", meanOf({1.0f, 2.0f, 3.0f, 4.0f}), 2.5f);

    // The accumulator is double. A sum large enough to lose low bits in float is the
    // case that distinguishes the two; here 2^24 + 1 is not representable in float at
    // all, so the expected value is computed in double.
    std::vector<float> big(1024, 16777216.0f);   // 2^24
    big.push_back(1.0f);
    double exact = 0.0;
    for (const float v : big) exact += static_cast<double>(v);
    expectClose("a large sum keeps its low bits (double accumulator)",
                meanOf(big), static_cast<float>(exact / static_cast<double>(big.size())), 0.5f);
  }

  // -----------------------------------------------------------------------
  // medianOf
  //
  // Takes the vector BY VALUE, so the caller's buffer survives unsorted. The even case is
  // `(a + b) * 0.5f` — a multiply, matching `fmul s0, s0, s8` with the 0.5f in s8.
  // -----------------------------------------------------------------------
  {
    std::vector<float> src{5.0f, 1.0f, 3.0f};
    expectClose("medianOf odd length", medianOf(src), 3.0f);
    expectTrue("medianOf does not sort the caller's vector", src[0] == 5.0f && src[2] == 3.0f);

    expectClose("medianOf even length is the mean of the middle two",
                medianOf({1.0f, 2.0f, 3.0f, 4.0f}), 2.5f);
    expectClose("medianOf of an empty vector is 0, not NaN", medianOf({}), 0.0f);
    expectTrue("medianOf of an empty vector is not NaN", !std::isnan(medianOf({})));
    expectClose("medianOf of one element", medianOf({-4.0f}), -4.0f);

    // 0.5f is a MULTIPLY in the binary. For the middle pair (1, 2) the product is exact,
    // so this asserts the value, not the rounding mode; the multiply itself is pinned by
    // the codegen comparison rather than here.
    expectClose("medianOf even length, negative values", medianOf({-3.0f, -1.0f}), -2.0f);
  }

  // -----------------------------------------------------------------------
  // trimmedMean
  //
  // trim is clamped to [0, 0.49]. The lower clamp means a negative trim is a plain mean;
  // the upper clamp is why n - 2k can never reach zero.
  // -----------------------------------------------------------------------
  {
    expectClose("trimmedMean of an empty range is 0", trimmedMean(nullptr, 0, 0.1f), 0.0f);
    const float one[1] = {3.5f};
    expectClose("trimmedMean of one element returns it", trimmedMean(one, 1, 0.1f), 3.5f);

    // 1..10: the plain mean is 5.5. Trimming 20% from each end drops the 1 and the 10
    // from a set of ten, leaving 2..9, whose mean is 5.5 as well. 30% drops three from
    // each end (2,3,4 and 8,9,10), leaving {5,6} -> 5.5. Use an asymmetric set instead so
    // the trim is visible.
    std::vector<float> skew{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 1000.0f};
    expectClose("trimmedMean with no trim includes the outlier",
                trimmedMean(skew.data(), skew.size(), 0.0f), 104.5f);
    expectClose("trimmedMean at 10% drops the outlier and the 1",
                trimmedMean(skew.data(), skew.size(), 0.1f), 5.5f);
    expectClose("a negative trim clamps to 0",
                trimmedMean(skew.data(), skew.size(), -1.0f), 104.5f);
    expectClose("a trim above 0.49 clamps to 0.49",
                trimmedMean(skew.data(), skew.size(), 0.9f),
                trimmedMean(skew.data(), skew.size(), 0.49f));

    // 0.49 on ten elements is cnt = 4, and k = min(4, 4) = 4, so n - 2k = 2: the median
    // pair. That is the most aggressive trim the function can reach, and it never empties.
    expectClose("the most aggressive trim leaves the middle pair",
                trimmedMean(skew.data(), skew.size(), 0.49f), 5.5f);

    // The vector overload is a tail call into the pointer form, so the two must agree.
    expectTrue("the vector overload agrees with the pointer form",
               trimmedMean(skew, 0.1f) == trimmedMean(skew.data(), skew.size(), 0.1f));

    // The caller's buffer must survive: the pointer form copies before sorting.
    std::vector<float> unsorted{3.0f, 1.0f, 2.0f};
    (void)trimmedMean(unsorted.data(), unsorted.size(), 0.0f);
    expectTrue("trimmedMean does not sort the caller's buffer",
               unsorted[0] == 3.0f && unsorted[2] == 2.0f);
  }

  // -----------------------------------------------------------------------
  // medianFilter
  //
  // The window is [max(i-w, 0), min(i+w, n-1)] INCLUSIVE, clamped rather than wrapped.
  // -----------------------------------------------------------------------
  {
    expectTrue("medianFilter of an empty vector is empty", medianFilter({}, 3).empty());

    // w = 0 makes every window a single sample, so the filter is the identity. That is
    // the value a negative width is also mapped to.
    const std::vector<float> v{5.0f, 1.0f, 9.0f, 3.0f};
    const std::vector<float> id = medianFilter(v, 0);
    expectTrue("w = 0 is the identity", id == v);
    expectTrue("a negative w is also the identity", medianFilter(v, -4) == v);
    expectTrue("w = 1 with a negative width maps to the same", medianFilter(v, -1) == v);

    // A window is `2w + 1` samples wide, so ANY spike narrower than w + 1 samples is
    // removed. The pair of cases below is the boundary: a two-sample spike survives a
    // 3-wide window and dies in a 5-wide one. The first version of this check asserted
    // that a single-sample spike survived w = 1, which is simply false — the middle of
    // {1, 100, 1} sorted is 1.
    const std::vector<float> spike2{1.0f, 1.0f, 100.0f, 100.0f, 1.0f, 1.0f};
    expectClose("w = 1 keeps a two-sample spike", medianFilter(spike2, 1)[2], 100.0f);
    expectClose("w = 2 removes a two-sample spike", medianFilter(spike2, 2)[2], 1.0f);

    // An isolated impulse is removed by any w >= 1: the window is 3 wide and the impulse
    // is 1 of 3, so the median is a neighbour.
    const std::vector<float> impulse{1.0f, 1.0f, 1.0f, 50.0f, 1.0f, 1.0f, 1.0f};
    expectClose("w = 1 removes an isolated impulse",
                medianFilter(impulse, 1)[3], 1.0f);

    // ...but not by w = 0, which is the identity.
    expectClose("w = 0 does not remove an impulse", medianFilter(impulse, 0)[3], 50.0f);

    // A median filter is not a mean: on a step it leaves the step where it is, which is
    // the property that makes it usable as the smoothing stage of a target.
    const std::vector<float> step{0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    const std::vector<float> fs = medianFilter(step, 1);
    expectClose("a step stays a step (before)", fs[1], 0.0f);
    expectClose("a step stays a step (after)", fs[3], 1.0f);

    // Edge windows are smaller, not wrapped: the first sample of a 6-long ramp with w = 2
    // sees only {0, 1, 2}, whose median is 1.
    const std::vector<float> ramp{0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    const std::vector<float> fr = medianFilter(ramp, 2);
    expectClose("the first window is clamped, not wrapped", fr[0], 1.0f);
    expectClose("the last window is clamped, not wrapped", fr[5], 4.0f);
    expectClose("the interior is unchanged on a monotone ramp", fr[3], 3.0f);
    expectTrue("w wider than the vector still works", medianFilter(ramp, 100).size() == 6);
    expectTrue("the output is the same length as the input",
               medianFilter(ramp, 2).size() == ramp.size());
  }

  // -----------------------------------------------------------------------
  // temporalTarget
  //
  // `w <= 0` short-circuits to the raw mean; `w > 0` means the signal first. The empty
  // case returns 0.0f from its own early return, ahead of the stack frame.
  // -----------------------------------------------------------------------
  {
    expectClose("temporalTarget of an empty vector is 0", temporalTarget({}, 3), 0.0f);
    expectTrue("temporalTarget of an empty vector is not NaN",
               !std::isnan(temporalTarget({}, 3)));

    const std::vector<float> v{1.0f, 1.0f, 100.0f, 1.0f, 1.0f};
    const float raw = meanOf(v);
    expectClose("w = 0 uses the raw mean", temporalTarget(v, 0), raw);
    expectClose("a negative w uses the raw mean", temporalTarget(v, -3), raw);

    // The spike is 20% of a five-sample signal, so the raw mean is dragged far above the
    // median-filtered one. This is the whole point of the effect's smoothing stage.
    const float smoothed = temporalTarget(v, 1);
    expectTrue("w > 0 pulls the target down toward the de-spiked level", smoothed < raw);
    expectClose("w > 0 is exactly the mean of the median filter",
                smoothed, meanOf(medianFilter(v, 1)));

    // The composition is what the two stages buy: a single-frame flash does not move the
    // target, while a level change that persists does.
    const std::vector<float> steady{0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    expectClose("a steady signal has that level as its target", temporalTarget(steady, 1), 0.5f);
    std::vector<float> flashed = steady;
    flashed[2] = 1000.0f;
    expectClose("a one-frame flash does not move the target", temporalTarget(flashed, 1), 0.5f);
    std::vector<float> raised(steady);
    for (float& x : raised) x = 1.0f;
    expectClose("a sustained change does move the target", temporalTarget(raised, 1), 1.0f);
  }

  std::printf("\n%d checks, %d failures\n", checks, failures);
  if (failures == 0) {
    std::printf("ALL RECOVERED AFF MATHS VERIFIED\n");
  }
  return failures == 0 ? 0 : 1;
}
