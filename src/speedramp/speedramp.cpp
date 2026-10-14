// speedramp.cpp — recovered implementations from the SpeedRampCurve bundle.

#include "speedramp.h"

#include <cmath>

namespace speedramp {

// ---------------------------------------------------------------------------
// easeValue(Easing, double)
//
// __ZN9speedramp9easeValueENS_6EasingEd, 34 instructions, recovered in full.
//
//     movi.2d v1, #0                 ; d1 = 0, and v1 is what the shared return uses
//     fcmp  d0, #0.0 / b.ls <ret>    ; t <= 0 -> 0
//     fmov  d1, #1.0
//     fcmp  d0, d1 / b.ge <ret>      ; t >= 1 -> 1
//
//   Both out-of-range guards jump to the SAME `mov.16b v0, v1` / `ret`, which is why
//   the sequence looks like it returns 0 twice: it returns whatever d1 holds, and d1
//   is 0 before the first guard and 1.0 after the second.
//
//   case 2: d1 = t*t; d2 = t*(-2) + 3; d0 = d1*d2       -> t^2 (3 - 2t)
//   case 3: d1 = t^3; d2 = t*6 - 15; d0 = t*d2 + 10;
//           d0 = t^3 * d0                               -> t^3 (6t^2 - 15t + 10)
//   case 4: fcmp t, 0.5 / fcsel d1, 0, 1, mi            -> 0 below 0.5, 1 at or above
//   default: returns t unchanged
// ---------------------------------------------------------------------------
double easeValue(Easing kind, double t) {
  if (t <= 0.0) return 0.0;
  if (t >= 1.0) return 1.0;

  switch (kind) {
    case Easing::Smoothstep:
      return (t * t) * (t * -2.0 + 3.0);
    case Easing::Smootherstep: {
      const double t3 = t * t * t;
      return t3 * (t * (t * 6.0 - 15.0) + 10.0);
    }
    case Easing::Step:
      return t < 0.5 ? 0.0 : 1.0;
    default:
      return t;
  }
}

// ---------------------------------------------------------------------------
// easeIntegral(Easing, double)
//
// __ZN9speedramp12easeIntegralENS_6EasingEd, 58 instructions, recovered in full.
//
// The antiderivative of easeValue with I(0) = 0. Every curve integrates to 0.5 over
// [0,1], so the t >= 1 branch is one affine expression shared by all of them:
//
//     0x7c0  d0 = (t - 1.0) + 0.5      ; = t - 0.5
//
// Per-curve pieces, all read off the switch:
//
//   linear  (0, 1)  -> 0.5 t^2
//   smoothstep (2)  -> t^3 - 0.5 t^4
//   smootherstep (3) -> t^6 - 3 t^5 + 2.5 t^4
//   step (4)        -> 0 for t < 0.5, t - 0.5 at or above
//
// Differentiating these gives easeValue exactly, which selfcheck.cpp verifies
// numerically rather than taking on trust.
// ---------------------------------------------------------------------------
double easeIntegral(Easing kind, double t) {
  if (t <= 0.0) return 0.0;
  if (t >= 1.0) return (t - 1.0) + 0.5;

  switch (kind) {
    case Easing::Smoothstep:
      return t * t * t - 0.5 * t * t * t * t;
    case Easing::Smootherstep:
      return t * t * t * t * t * t - 3.0 * t * t * t * t * t + 2.5 * t * t * t * t;
    case Easing::Step:
      return t <= 0.5 ? 0.0 : (t - 0.5);
    default:
      return 0.5 * t * t;
  }
}

// ---------------------------------------------------------------------------
// speedAt(const RampParams&, double)
//
// __ZN9speedramp7speedAtERKNS_10RampParamsEd, 58 instructions, recovered in full.
//
// Two early returns share the same tail, which is the shape worth noticing:
//
//     0x85c  d0 = r.before
//     0x870  d0 = d0 / 100.0
//     0x874  b.ls <ret>          ; (a) t <= r.start       -> before
//     ...
//     0x884  b.ls <ret>          ; (b) end - start <= 0   -> before
//
// so a DEGENERATE window (end at or before start) yields the before speed
// everywhere rather than dividing by zero. The easing is then applied inline — the
// same three curves easeValue has, written out again rather than called, so this
// function does not appear in easeValue's callers.
// ---------------------------------------------------------------------------
double speedAt(const RampParams& r, double t) {
  const double before = r.before / 100.0;
  if (t <= r.start) return before;

  const double dur = r.end - r.start;
  if (dur <= 0.0) return before;

  const double after = r.after / 100.0;
  if (t >= r.end) return after;

  const double u = (t - r.start) / dur;
  return before + (after - before) * easeValue(static_cast<Easing>(r.easing), u);
}

// ---------------------------------------------------------------------------
// mapTime(const RampParams&, double)
//
// __ZN9speedramp7mapTimeERKNS_10RampParamsEd, 95 instructions, recovered in full —
// the largest function in the namespace.
//
// Three branches, all sharing an epilogue that adds timeOffset:
//
//   0x9d8  t <= start, or a degenerate window:
//              start + (before/100) * (t - start) + timeOffset
//
//   0xaa0  inside (and also the u >= 1 limit of the eased branch):
//              start + dur * (before/100 * u + (after - before)/100 * I(u))
//              + timeOffset
//          where I is easeIntegral, applied INLINE — this function contains its own
//          copy of the curves, so easeIntegral's symbol has no caller here.
//
//   0x9ec  t >= end:
//              start + dur * (before/100 + (after - before)/200)
//              + (after/100) * (t - end) + timeOffset
//          which is the same expression at u = 1, since I(1) = 0.5 for every curve.
//
// That last equivalence is why the three branches agree at their boundaries, and it
// is asserted rather than assumed.
// ---------------------------------------------------------------------------
double mapTime(const RampParams& r, double t) {
  const double before = r.before / 100.0;
  const double after = r.after / 100.0;
  const double dur = r.end - r.start;

  if (dur <= 0.0 || t <= r.start) {
    return r.start + before * (t - r.start) + r.timeOffset;
  }
  if (t >= r.end) {
    const double delta = after - before;
    const double avg = before + delta * 0.5;
    return (r.start + dur * avg) + after * (t - r.end) + r.timeOffset;
  }

  const double u = (t - r.start) / dur;
  const double delta = after - before;
  const double I = easeIntegral(static_cast<Easing>(r.easing), u);
  return r.start + dur * (before * u + delta * I) + r.timeOffset;
}

// ---------------------------------------------------------------------------
// computeBlend(double, double)
//
// __ZN9speedramp12computeBlendEdd, 16 instructions, recovered in full.
//
//     fcmp  d1, #0.0 / fcsel d1, d1, 1.0, gt   ; speed <= 0 -> 1.0 (normal speed)
//     d0 = time * d1
//     frintm d1, d0                            ; floor
//     d1 = d0 - d1                             ; frac
//     fcsel d1, 0, d1, mi                      ; frac < 0 -> 0
//     fcsel d1, 1.0, d1, gt                    ; frac > 1 -> 1
//     fcvtms x9, d0 / add x10, x9, #1
//     stp x9, x10, [x8] / str d1, [x8, #0x10]  ; {frame0, frame1, frac}
//
// The speed guard is a STRICT `>` against 0.0, so a zero or negative speed is
// replaced by 1.0 rather than freezing the frame. The frac clamps are defensive —
// frac cannot leave [0,1) given floor — but they are in the binary and are kept.
// ---------------------------------------------------------------------------
FrameBlend computeBlend(double time, double speed) {
  const double s = speed > 0.0 ? speed : 1.0;
  const double x = time * s;

  double frac = x - std::floor(x);
  if (frac < 0.0) frac = 0.0;
  if (frac > 1.0) frac = 1.0;

  FrameBlend out;
  out.frame0 = static_cast<long long>(std::floor(x));
  out.frame1 = out.frame0 + 1;
  out.frac = frac;
  return out;
}

}  // namespace speedramp
