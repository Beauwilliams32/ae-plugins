// colorpulse.cpp — recovered implementations from the ColorPulse bundle.

#include "colorpulse.h"

namespace colorpulse {

// ---------------------------------------------------------------------------
// clampf(float, float, float)
//
// __ZN10colorpulse6clampfEfff: the same five instructions as the other six copies in
// the fleet, with the low bound tested last so it wins on an inverted range.
// ---------------------------------------------------------------------------
float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

// ---------------------------------------------------------------------------
// smoothScalar(float, float, float)
//
// __ZN10colorpulse12smoothScalarEfff, 10 instructions, recovered in full.
//
//     fmov  s3, #1.0 / fcmp s2, s3 / fcsel s4 = min(blend, 1)
//     fcmp  s2, #0.0 / fcsel s2 = (blend < 0) ? 0 : min(blend,1)   ; the clamp
//     fsub  s3, s3, s2                                             ; 1 - t
//     fmul  s1, s1, s3                                             ; previous * (1-t)
//     fmadd s0, s0, s2, s1                                         ; newest * t + that
//
// The clamp is the familiar two-`fcsel` form: `min(blend,1)` then a lower-bound guard,
// so a negative blend becomes 0 rather than passing through.
// ---------------------------------------------------------------------------
float smoothScalar(float newest, float previous, float blend) {
  const float t = clampf(blend, 0.0f, 1.0f);
  return newest * t + previous * (1.0f - t);
}

// ---------------------------------------------------------------------------
// luma709(const RGB&)
//
// __ZN10colorpulse7luma709ERKNS_3RGBE, 15 instructions, recovered in full.
//
//     ldp   s0, s1, [x0]        ; r, g
//     s1 = g * 0.7152f          ; 0x3f371759
//     s0 = r * 0.2126f + s1     ; fmadd, 0x3e59b3d0
//     s1 = [x0, #0x8]           ; b
//     s0 = b * 0.0722f + s0     ; fmadd, 0x3d93dd98
//
// This is the SAME function as lutblend::luma709 — same three constants, same
// `(g*c1 + r*c2) + b*c3` order — written against a different colour struct. The
// duplication is real and is asserted rather than quietly factored, because the two
// live in different namespaces recovered from different bundles.
// ---------------------------------------------------------------------------
float luma709(const RGB& c) {
  return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

// ---------------------------------------------------------------------------
// smoothColor(const RGB&, const RGB&, float)
//
// __ZN10colorpulse11smoothColorERKNS_3RGBES2_f, 17 instructions, recovered in full.
//
// The same lerp as smoothScalar, applied per channel:
//
//     d3 = [x0] (newest.r, newest.g)      d0 = [x1] (previous.r, previous.g)
//     v0 = d0 * (1-t)                     ; fmul.2s — BOTH lanes at once
//     v0 = v3 * t + v0                    ; fmla.2s
//     s3 = [x0+8] (newest.b)              s4 = [x1+8] (previous.b)
//     s1 = (1-t) * previous.b
//     s2 = newest.b * t + s1
//
// Red and green are computed as a two-lane SIMD operation and blue separately; the
// arithmetic is identical either way, which is why the scalar form below is exact
// rather than merely close.
// ---------------------------------------------------------------------------
RGB smoothColor(const RGB& newest, const RGB& previous, float blend) {
  const float t = clampf(blend, 0.0f, 1.0f);
  RGB out;
  out.r = newest.r * t + previous.r * (1.0f - t);
  out.g = newest.g * t + previous.g * (1.0f - t);
  out.b = newest.b * t + previous.b * (1.0f - t);
  return out;
}

// ---------------------------------------------------------------------------
// saturationBoost(const RGB&, float)
//
// __ZN10colorpulse15saturationBoostERKNS_3RGBEf, 41 instructions, recovered in full.
//
//     fcmp s0, #0.0 / b.ls <early>     ; amount <= 0 -> return c UNCHANGED
//     ... luma709 computed inline, same three constants ...
//     s2 = amount + 1.0                ; the scale factor
//     for each channel: s = luma + (ch - luma) * s2
//     then clamp each to [0,1]
//
// Two things worth stating because they are not what a rewrite would do:
//
//   * the guard is `<= 0` and it returns the INPUT components, not clamped ones. A
//     negative amount is therefore a no-op rather than a desaturation, and the
//     function is not symmetric about zero.
//   * the scale is `1 + amount`, so `amount` is a fraction: 1.0 doubles the distance
//     from the luma, not "100%". The parameter is presented as a percentage in the UI
//     and divided before it gets here.
//
// The luma is computed inline rather than by calling luma709, so this function does not
// appear in luma709's callers — the same inlining duplication seen in
// lutblend::applyLUTBlend.
// ---------------------------------------------------------------------------
RGB saturationBoost(const RGB& c, float amount) {
  if (amount <= 0.0f) return c;

  const float l = luma709(c);
  const float scale = amount + 1.0f;

  RGB out;
  out.r = clampf(l + (c.r - l) * scale, 0.0f, 1.0f);
  out.g = clampf(l + (c.g - l) * scale, 0.0f, 1.0f);
  out.b = clampf(l + (c.b - l) * scale, 0.0f, 1.0f);
  return out;
}

}  // namespace colorpulse
