// maskflow.cpp — transcribed from the compiled MaskFlow bundle.

#include "maskflow.h"

#include <algorithm>
#include <cmath>

namespace maskflow {

// The shared copy, machine-code-identical to the fleet's other four carriers.
//
float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

// __ZN8maskflow26edgeCostScaleFromToleranceEf, 12 instrs
//
//   fmov s1, #100.0 / fcmp s0, s1 / fcsel s2, s1, s0, gt   ; min(tolerance, 100)
//   fsub s2, s1, s2                                        ; 100 - that
//   fdiv s1, s2, s1                                        ; / 100
//   fmov s2, #40.0 / fmul s1, s1, s2                       ; * 40
//   fcmp s0, #0.0 / fcsel s0, s2, s1, mi                   ; a NEGATIVE tolerance is 40.0
float edgeCostScaleFromTolerance(float tolerance) {
  const float clamped = tolerance > 100.0f ? 100.0f : tolerance;
  const float scale = (100.0f - clamped) / 100.0f * 40.0f;
  return tolerance < 0.0f ? 40.0f : scale;
}

// __ZN8maskflow10smoothstepEfff, 19 instrs
//
//   fsub s3, s2, s0 / fsub s4, s1, s0 / fdiv s3, s3, s4    ; t = (x - edge0) / (edge1 - edge0)
//   fmov s4, #1.0 / fcmp s3, s4 / fcsel s5, s4, s3, gt     ; cap at 1
//   fcmp s3, #0.0 / movi.2d v3, #0 / fcsel s5, s3, s5, mi  ; NOT floored: below zero the
//                                                          ; select returns t unchanged
//   fmul s6, s5, s5 / fmov s7, #3.0 / fmov s16, #-2.0
//   fmadd s5, s5, s16, s7 / fmul s5, s6, s5                ; t*t*(3 - 2t)
//   fcmp s2, s0 / fcsel s2, s3, s4, mi                     ; the degenerate-edge value
//   fcmp s0, s1 / fcsel s0, s5, s2, ne                     ; edge0 == edge1 ? that : the polynomial
float smoothstep(float edge0, float edge1, float x) {
  const float t = (x - edge0) / (edge1 - edge0);
  // Written as the two selects are: `t < 0 ? t : (t > 1 ? 1 : t)`. The lower branch
  // returns `t` itself, which is a clamp that clamps nothing — almost certainly a typo
  // for `0` in the original — and it is why this EXTRAPOLATES below edge0 instead of
  // flattening. Correcting it to a real clamp would change the effect's output.
  const float clamped = t < 0.0f ? t : (t > 1.0f ? 1.0f : t);
  const float polynomial = clamped * clamped * (3.0f - 2.0f * clamped);
  // ONE EXPRESSION, not an if and two returns. The shipped code computes both candidates
  // and selects at the end (`fcsel s0, s5, s2, ne`); writing it as a branch emitted an
  // early branch instead, and the arm order moves the count by two: 20 instructions with
  // the polynomial first, 18 with it second, against the bundle's 19. The behaviour is
  // the same either way and the count is recorded rather than chased.
  return edge0 == edge1 ? (x < edge0 ? t : 1.0f) : polynomial;
}

// __ZN8maskflow21computeDownsampleDimsEiii, 32 instrs
//
//   cmp w0, #0x1 / b.lt <invalid> / cmp w1, #0x0 / b.le <invalid>
//       ; width < 1 or height < 1 takes the invalid path
//   cmp w0, w1 / csel w8, w0, w1, hi        ; longest = max(width, height)
//   fmov s0, #1.0                           ; the default scale
//   cmp w8, w2 / b.le <dims>                ; no downsampling needed
//   scvtf s0, w2 / ucvtf s1, w8 / fdiv s0, s0, s1   ; scale = maxDim / longest
//   ucvtf s1, w0 / fmul s1, s0, s1 / fcvtas x8, s1  ; ROUND, ties away from zero
//   cmp w8, #0x1 / csinc w8, w8, wzr, gt             ; floored at 1
//   ... the same for height ...
//   fmov w1, s0                             ; the float rides in w1, i.e. the low half of x1
//   <invalid>: w8 = max(width,1) / w9 = max(height,1) / w1 = 0x3f800000 (1.0f)
//   <both>:    mov w8, w8 / orr x0, x8, x9, lsl #32 / ret
//              ; x0 = width | (height << 32), x1 = the scale
DownsampleDims computeDownsampleDims(int width, int height, int maxDim) {
  if (width < 1 || height < 1) {
    return {std::max(width, 1), std::max(height, 1), 1.0f};
  }
  const int longest = width > height ? width : height;
  float scale = 1.0f;
  if (longest > maxDim) scale = static_cast<float>(maxDim) / static_cast<float>(longest);
  return {
      std::max(static_cast<int>(std::lround(scale * static_cast<float>(width))), 1),
      std::max(static_cast<int>(std::lround(scale * static_cast<float>(height))), 1),
      scale,
  };
}

}  // namespace maskflow
