// Self-check for the recovered speedramp:: namespace.

#include "speedramp.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace {
int failures = 0, passes = 0;
void check(bool ok, const char* why) {
  if (ok) { ++passes; std::printf("  PASS %s\n", why); }
  else    { ++failures; std::printf("  FAIL %s\n", why); }
}
void near(double got, double want, double tol, const char* why) {
  if (std::fabs(got - want) <= tol) { ++passes; std::printf("  PASS %s\n", why); }
  else { ++failures; std::printf("  FAIL %s  (got %.10g want %.10g)\n", why, got, want); }
}
}  // namespace

int main() {
  using speedramp::Easing;

  std::printf("speedramp::easeValue\n");
  {
    // Saturation, both ends, for every curve.
    for (int e = 0; e <= 5; ++e) {
      const Easing k = static_cast<Easing>(e);
      near(speedramp::easeValue(k, -1.0), 0.0, 0.0, "every curve is 0 at t <= 0");
      near(speedramp::easeValue(k, 0.0), 0.0, 0.0, "and 0 exactly at 0");
      near(speedramp::easeValue(k, 1.0), 1.0, 0.0, "and 1 at t >= 1");
      near(speedramp::easeValue(k, 2.0), 1.0, 0.0, "saturating, not extrapolating");
    }
    near(speedramp::easeValue(Easing::LinearA, 0.25), 0.25, 1e-12, "linear A is identity");
    near(speedramp::easeValue(Easing::LinearB, 0.25), 0.25, 1e-12, "linear B is identical");
    // smoothstep and smootherstep are symmetric about (0.5, 0.5).
    near(speedramp::easeValue(Easing::Smoothstep, 0.5), 0.5, 1e-12, "smoothstep is 0.5 at the midpoint");
    near(speedramp::easeValue(Easing::Smootherstep, 0.5), 0.5, 1e-12, "smootherstep too");
    near(speedramp::easeValue(Easing::Smoothstep, 0.25),
         speedramp::easeValue(Easing::Smoothstep, 0.75) * 0.0 + 0.15625, 1e-12,
         "smoothstep(0.25) = 0.15625");
    // The STEP curve is the one that makes this enum incompatible with typeriser's.
    near(speedramp::easeValue(Easing::Step, 0.499), 0.0, 0.0, "step is 0 below 0.5");
    near(speedramp::easeValue(Easing::Step, 0.5), 1.0, 0.0, "and 1 AT 0.5 (not above)");
    near(speedramp::easeValue(Easing::Step, 0.75), 1.0, 0.0, "and 1 above");
    // Monotone, and inside [0,1], for the continuous curves.
    bool mono = true, inRange = true;
    for (int e = 0; e <= 3; ++e) {
      double prev = -1.0;
      for (int i = 0; i <= 100; ++i) {
        const double v = speedramp::easeValue(static_cast<Easing>(e), i / 100.0);
        if (v < prev - 1e-12) mono = false;
        if (v < -1e-12 || v > 1.0 + 1e-12) inRange = false;
        prev = v;
      }
    }
    check(mono, "the continuous curves are monotone");
    check(inRange, "and stay within [0,1]");
  }

  std::printf("speedramp::easeIntegral IS the antiderivative of easeValue\n");
  {
    // The strongest check available on this pair. If either curve were transcribed
    // wrong the relationship would break, and it is not something a shared typo can
    // satisfy: it is asserted against an independent numerical integral.
    for (int e = 0; e <= 4; ++e) {
      const Easing k = static_cast<Easing>(e);
      const int N = 200000;
      const double h = 1.0 / N;
      double acc = 0.0;
      for (int i = 0; i < N; ++i) {
        const double a = speedramp::easeValue(k, i * h);
        const double b = speedramp::easeValue(k, (i + 1) * h);
        acc += 0.5 * (a + b) * h;      // trapezoid
      }
      // 1e-5, not 1e-6: the trapezoid rule on a curve with a discontinuous second
      // derivative converges slowly, and the residual is the METHOD's error, not the
      // transcription's. Asserting tighter would be asserting against the integrator.
      near(speedramp::easeIntegral(k, 1.0), acc, 1e-5,
           "easeIntegral(1) equals the numerical integral of easeValue over [0,1]");
    }
    // Every curve integrates to 0.5 over the unit interval, which is exactly why the
    // t >= 1 branch can be one affine expression shared by all of them.
    for (int e = 0; e <= 4; ++e) {
      near(speedramp::easeIntegral(static_cast<Easing>(e), 1.0), 0.5, 1e-12,
           "every curve integrates to exactly 0.5 over [0,1]");
    }
    // Past the ramp the integral is linear at the finishing rate.
    near(speedramp::easeIntegral(Easing::Smoothstep, 2.0), 1.5, 1e-12,
         "past t=1 the integral is t - 0.5");
    near(speedramp::easeIntegral(Easing::Smoothstep, -1.0), 0.0, 0.0, "and 0 below 0");
    near(speedramp::easeIntegral(Easing::Step, 0.25), 0.0, 1e-12, "step integrates to 0 below 0.5");
    near(speedramp::easeIntegral(Easing::Step, 0.75), 0.25, 1e-12, "and to t-0.5 above");
  }

  std::printf("speedramp::speedAt\n");
  {
    speedramp::RampParams r;
    r.start = 2.0; r.end = 4.0; r.before = 200.0; r.after = 50.0; r.easing = 0;

    near(speedramp::speedAt(r, 0.0), 2.0, 1e-12, "before the ramp, the before speed");
    near(speedramp::speedAt(r, 2.0), 2.0, 1e-12, "at the start, still the before speed");
    near(speedramp::speedAt(r, 4.0), 0.5, 1e-12, "at the end, the after speed");
    near(speedramp::speedAt(r, 9.0), 0.5, 1e-12, "past the end, the after speed");
    near(speedramp::speedAt(r, 3.0), 1.25, 1e-12, "halfway on a LINEAR ramp, halfway in speed");

    // Percentages really are percentages.
    speedramp::RampParams one;
    one.start = 0.0; one.end = 1.0; one.before = 100.0; one.after = 100.0;
    near(speedramp::speedAt(one, 0.5), 1.0, 1e-12, "100% is normal speed");

    // A degenerate window returns the before speed everywhere rather than
    // dividing by zero — this is an early return in the binary, not luck.
    speedramp::RampParams degenerate;
    degenerate.start = 5.0; degenerate.end = 5.0; degenerate.before = 300.0; degenerate.after = 10.0;
    near(speedramp::speedAt(degenerate, 1.0), 3.0, 1e-12, "a zero-length ramp yields before, early");
    near(speedramp::speedAt(degenerate, 9.0), 3.0, 1e-12, "and everywhere else too");
    speedramp::RampParams inverted;
    inverted.start = 5.0; inverted.end = 1.0; inverted.before = 300.0; inverted.after = 10.0;
    near(speedramp::speedAt(inverted, 9.0), 3.0, 1e-12, "an INVERTED window also yields before");
  }

  std::printf("speedramp::mapTime\n");
  {
    speedramp::RampParams r;
    r.start = 1.0; r.end = 3.0; r.before = 100.0; r.after = 100.0; r.easing = 0;

    // At normal speed throughout, the map is the identity: source time equals output
    // time. Checked on the DIFFERENCE rather than on absolute values, which is the
    // only form that holds — see the integral note below.
    near(speedramp::mapTime(r, 5.0) - speedramp::mapTime(r, 0.0), 5.0, 1e-12,
         "at 100% -> 100% the map advances one second per second");
    near(speedramp::mapTime(r, 0.0), 0.0, 1e-12, "and at 100% the map passes through zero");
    r.timeOffset = 2.0;
    near(speedramp::mapTime(r, 0.0), 2.0, 1e-12, "timeOffset shifts the whole map");
    near(speedramp::mapTime(r, 5.0) - speedramp::mapTime(r, 0.0), 5.0, 1e-12,
         "and does not change the rate");
    r.timeOffset = 0.0;

    // The three branches must agree AT their boundaries, which they do because every
    // curve integrates to 0.5 at u = 1. That equivalence is the thing to assert.
    speedramp::RampParams g;
    g.start = 1.0; g.end = 3.0; g.before = 50.0; g.after = 200.0; g.easing = 2;
    const double atEnd = speedramp::mapTime(g, g.end);
    const double justBefore = speedramp::mapTime(g, g.end - 1e-9);
    near(atEnd, justBefore, 1e-6, "the inside and past-the-end branches agree at the boundary");
    const double atStart = speedramp::mapTime(g, g.start);
    const double justInside = speedramp::mapTime(g, g.start + 1e-9);
    near(atStart, justInside, 1e-6, "and so do the before and inside branches");

    // Total distance travelled equals the integral of speedAt, which ties mapTime to
    // speedAt independently of how either was transcribed.
    const int N = 100000;
    const double lo = 0.0, hi = 4.0, h = (hi - lo) / N;
    double dist = 0.0;
    for (int i = 0; i < N; ++i) {
      const double a = speedramp::speedAt(g, lo + i * h);
      const double b = speedramp::speedAt(g, lo + (i + 1) * h);
      dist += 0.5 * (a + b) * h;
    }
    // mapTime(t) is mapTime(lo) PLUS the distance travelled, not the distance alone:
    // before the ramp the map is already offset from zero by
    // `before * (lo - start)`. Comparing the difference is the relationship that
    // actually holds, and getting this wrong is what made this assertion fail while
    // the implementation was right.
    near(speedramp::mapTime(g, hi) - speedramp::mapTime(g, lo), dist, 1e-4,
         "mapTime advances by exactly the integral of speedAt, numerically confirmed");

    // Monotone in t for a positive ramp.
    bool mono = true;
    double prev = -1e18;
    for (int i = 0; i <= 400; ++i) {
      const double v = speedramp::mapTime(g, -1.0 + i * 0.02);
      if (v < prev - 1e-9) mono = false;
      prev = v;
    }
    check(mono, "a positive ramp maps time monotonically");
  }

  std::printf("speedramp::computeBlend\n");
  {
    auto b = speedramp::computeBlend(10.4, 1.0);
    check(b.frame0 == 10 && b.frame1 == 11, "frames are floor and floor+1");
    near(b.frac, 0.4, 1e-9, "frac is the fractional part");

    // A speed of zero or less is replaced by 1.0 — a STRICT guard, so the frame
    // does not freeze.
    auto z = speedramp::computeBlend(10.4, 0.0);
    check(z.frame0 == 10, "speed 0 falls back to normal speed, not a frozen frame");
    near(z.frac, 0.4, 1e-9, "and keeps the fraction");
    auto neg = speedramp::computeBlend(10.4, -2.0);
    check(neg.frame0 == 10, "a negative speed does the same");

    // Speed scales the time.
    auto fast = speedramp::computeBlend(10.0, 2.0);
    check(fast.frame0 == 20, "200% samples twice as far along");

    // frac is always in [0,1).
    bool ok = true;
    for (int i = -50; i <= 50; ++i) {
      for (double s : {0.1, 1.0, 3.7}) {
        const auto f = speedramp::computeBlend(i * 0.37, s);
        if (f.frac < 0.0 || f.frac >= 1.0) ok = false;
        if (f.frame1 != f.frame0 + 1) ok = false;
      }
    }
    check(ok, "frac stays in [0,1) and frame1 is always frame0+1");
    // Negative time floors toward negative infinity, matching frintm.
    auto nt = speedramp::computeBlend(-0.25, 1.0);
    check(nt.frame0 == -1 && nt.frame1 == 0, "negative time floors toward -inf (frintm)");
    near(nt.frac, 0.75, 1e-9, "with the correct fraction");
  }

  std::printf("\n%s\n", failures == 0
                ? "ALL RECOVERED SPEEDRAMP MATHS VERIFIED"
                : "TRANSCRIPTION ERRORS PRESENT");
  return failures == 0 ? 0 : 1;
}
