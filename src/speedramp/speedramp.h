// speedramp.h — recovered from the compiled SpeedRampCurve bundle.
//
// SpeedRampCurve is a TIME-REMAPPING effect: it ramps playback speed from one value
// to another across a window, easing the transition, and reports the source time a
// given output time should sample. Five small scalar functions, 261 instructions in
// total, recovered in full.
//
// NAMESPACE-COMPLETE: every `speedramp::` symbol in the bundle is transcribed, so
// tools/codegen-diff.mjs reports zero unrecovered symbols for this directory.
// test/codegen.test.mjs asserts that marker and that measurement agree.
//
//
// Recovered from the demangled symbol table and disassembly of SpeedRampCurve.plugin.
// The original C++ was destroyed on 2026-10-13 with no remote and no backup.

#pragma once

namespace speedramp {

// ---------------------------------------------------------------------------
// [RECOVERED] Easing — the selector values are read off easeValue's switch.
//
// These are NOT the same curves as typeriser::Easing despite the shared name: this
// namespace has its own enum, and the values mean different things. In particular
// value 4 is a STEP — a deliberately discontinuous curve — where typeriser's 4 is
// an overshoot. Assuming the two were shared would be a silent error.
//
// The names are descriptive, not recovered: the source had no symbols for them. The
// POPUP's choice string in the binary is the only place the author's names survive,
// and it has not been read.
// ---------------------------------------------------------------------------
enum class Easing : int {
  LinearA = 0,      // returns the input unchanged
  LinearB = 1,      // identical to LinearA in the binary
  Smoothstep = 2,   // t^2 * (3 - 2t)
  Smootherstep = 3, // t^3 * (6t^2 - 15t + 10)
  Step = 4,         // 0 below 0.5, 1 at or above it
  // Any other value returns the input unchanged.
};

// ---------------------------------------------------------------------------
// [RECOVERED LAYOUT] RampParams — pinned by the field accesses in speedAt and
// mapTime. 48 bytes:
//
//     ldr  d2, [x0]        ; +0x00  start      (seconds)
//     ldr  d5, [x0, #0x8]  ; +0x08  end        (seconds)
//     ldr  d1, [x0, #0x10] ; +0x10  before     (percent)
//     ldr  d6, [x0, #0x18] ; +0x18  after      (percent)
//     ldr  w8, [x0, #0x20] ; +0x20  easing     (int)
//     ldr  d1, [x0, #0x28] ; +0x28  timeOffset (seconds)
//
// The `before` and `after` fields are PERCENTAGES: both functions divide them by
// 100.0 before use, which is how the "%" in the recovered parameter names is
// honoured internally.
// ---------------------------------------------------------------------------
struct RampParams {
  double start = 0.0;       // [recovered] +0x00, seconds
  double end = 0.0;         // [recovered] +0x08, seconds
  double before = 100.0;    // [recovered] +0x10, percent
  double after = 100.0;     // [recovered] +0x18, percent
  int easing = 0;           // [recovered] +0x20, an Easing value
  double timeOffset = 0.0;  // [recovered] +0x28, seconds
};

static_assert(sizeof(RampParams) == 48, "RampParams must stay the 0x30 the code strides by");

// ---------------------------------------------------------------------------
// [exact] The easing curve, on a 0..1 input.
//
// Outside 0..1 it SATURATES rather than extrapolating: t <= 0 gives 0 and t >= 1
// gives 1, both via the same `mov.16b v0, v1` return the compiler shared.
// ---------------------------------------------------------------------------
double easeValue(Easing kind, double t);

// ---------------------------------------------------------------------------
// [exact] The ANTIDERIVATIVE of easeValue with I(0) = 0.
//
// Every curve integrates to exactly 0.5 over [0,1], which is why the t >= 1 branch
// is the single affine `t - 0.5` rather than a per-curve expression: past the end of
// the ramp the integral is linear at the speed the ramp finished at.
//
// This relationship is the strongest check available on the pair, and a test asserts
// it numerically — see selfcheck.cpp.
// ---------------------------------------------------------------------------
double easeIntegral(Easing kind, double t);

// ---------------------------------------------------------------------------
// [exact] Playback speed at time t, as a FRACTION (1.0 == normal speed).
//
//     t <= start or end <= start  ->  before / 100
//     t >= end                    ->  after  / 100
//     otherwise                   ->  lerp(before, after, easeValue(u)) / 100
//   with u = (t - start) / (end - start).
//
// Note the degenerate-window case: a ramp whose end is at or before its start
// returns the BEFORE speed everywhere, rather than dividing by zero.
// ---------------------------------------------------------------------------
double speedAt(const RampParams& r, double t);

// ---------------------------------------------------------------------------
// [exact] The source time an output time t should sample.
//
// Before the ramp this is a straight line at the before speed. Inside it, the
// distance travelled is the integral of the speed curve, which is exactly what
// easeIntegral provides:
//
//     result = start + (end - start) * (before/100 * u + (after - before)/100 * I(u))
//
// and past the ramp the same expression degenerates to a line at the after speed.
// timeOffset is added last, in every branch.
// ---------------------------------------------------------------------------
double mapTime(const RampParams& r, double t);

// ---------------------------------------------------------------------------
// [RECOVERED LAYOUT] The frame blend a remap asks for. Returned BY VALUE through
// the hidden first argument (24 bytes, so it does not fit in registers):
//
//     stp x9, x10, [x8]     ; +0x00 frame0, +0x08 frame1
//     str d1, [x8, #0x10]   ; +0x10 frac
//
// frame0 is floor(x) and frame1 is floor(x)+1, so a caller renders both and mixes
// by frac — linear interpolation between the two source frames.
// ---------------------------------------------------------------------------
struct FrameBlend {
  long long frame0 = 0;  // [recovered] +0x00, floor(x)
  long long frame1 = 0;  // [recovered] +0x08, floor(x) + 1
  double frac = 0.0;     // [recovered] +0x10, clamped to 0..1
};

static_assert(sizeof(FrameBlend) == 24, "FrameBlend is returned through x8 in 24 bytes");

// ---------------------------------------------------------------------------
// [exact] Turn a source time and a speed into the two frames to mix.
//
//     x    = time * (speed > 0 ? speed : 1.0)
//     frac = clamp(x - floor(x), 0, 1)
//
// The guard on speed is a STRICT `> 0` against 1.0, so a speed of zero or less is
// replaced by normal speed rather than producing a frozen frame.
// ---------------------------------------------------------------------------
FrameBlend computeBlend(double time, double speed);

}  // namespace speedramp
