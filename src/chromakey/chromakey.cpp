// chromakey.cpp — transcribed from the compiled ChromaKeyAssist bundle.
//
// Each function carries the instructions that establish its behaviour, because
// that is the whole recovery: where a claim about the original cannot be traced to
// an instruction, it does not belong here. The scalar and statistics layer is
// transcribed; `analyzeFrame<T>`, `renderView<T>` and the host integration are not,
// and are absent rather than stubbed.

#include "chromakey.h"

#include <cmath>

namespace chromakey {

// __ZN9chromakey6clampfEfff, 5 instrs
//
//   fcmp s0, s2 / fcsel s2, s2, s0, gt   ; s2 = (v > hi) ? hi : v
//   fcmp s0, s1 / fcsel s0, s1, s2, mi   ; s0 = (v < lo) ? lo : s2
//
// The low bound is tested LAST, so it wins on an inverted range. Shared with the
// other fourteen copies of this function in the fleet.
float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

// __ZN9chromakey10rgbToYCbCrERKNS_3RGBE, 25 instrs
//
//   ldp s2, s0, [x0]        ; s2 = r, s0 = g
//   ldr s1, [x0, #0x8]      ; s1 = b
//   s3 = 0x3f1645a2 = 0.587      ; fmul  s0, s0, s3          -> y  = g * 0.587
//   s3 = 0x3e991687 = 0.299      ; fmadd s0, s2, s3, s0      -> y += r * 0.299
//   s3 = 0x3de978d5 = 0.114      ; fmadd s0, s1, s3, s0      -> y += b * 0.114
//   fsub s1, s1, s0              ; b - y
//   s3 = 0x3f107831 = 0.564334   ; fmul  s1, s1, s3          -> cb = (b - y) * 0.564334
//   fsub s2, s2, s0              ; r - y
//   s3 = 0x3f3698ab = 0.713267   ; fmul  s2, s2, s3          -> cr = (r - y) * 0.713267
//
// Rec.601 luma. The chroma scales are the literals 0.564334 and 0.713267, which are
// 1/1.772 and 1/1.402 rounded — NOT the divisions: `1.0f / 1.772f` is 0x3f107833,
// three ulps from the shipped 0x3f107831, and the differ would say so.
YCbCr rgbToYCbCr(const RGB& rgb) {
  // All three channels are read up front: the shipped code's first two instructions
  // are `ldp s2, s0, [x0]` and `ldr s1, [x0, #0x8]`, before any constant is built.
  const float r = rgb.r;
  const float g = rgb.g;
  const float b = rgb.b;

  YCbCr out;
  out.y = g * 0.587f;
  out.y += r * 0.299f;
  out.y += b * 0.114f;
  out.cb = (b - out.y) * 0.564334f;
  out.cr = (r - out.y) * 0.713267f;
  return out;
}

// __ZN9chromakey22sensitivityToThresholdEf, 15 instrs
//
//   fmov s1, #100.0 / fcmp s0, s1 / fcsel s2, s1, s0, gt   ; s2 = min(sensitivity, 100)
//   fdiv s1, s2, s1                                        ; fraction = that / 100
//   s2 = 0x3cf5c28f = 0.03, s3 = 0x3ed70a3d = 0.42
//   fmadd s1, s1, s3, s2                                   ; 0.03 + fraction * 0.42
//   fcmp s0, #0.0 / fcsel s0, s2, s1, mi                   ; a NEGATIVE input takes 0.03
float sensitivityToThreshold(float sensitivity) {
  const float clamped = sensitivity > 100.0f ? 100.0f : sensitivity;
  const float ramp = 0.03f + (clamped / 100.0f) * 0.42f;
  return sensitivity < 0.0f ? 0.03f : ramp;
}

// __ZN9chromakey14chromaDistanceERKNS_5YCbCrES2_, 8 instrs
//
//   ldp s0, s1, [x0, #0x4]   ; a.cb @ +0x4, a.cr @ +0x8   (Y at +0x0 is never read)
//   ldp s2, s3, [x1, #0x4]   ; b.cb, b.cr
//   fsub s0, s0, s2          ; dCb
//   fsub s1, s1, s3          ; dCr
//   fmul s1, s1, s1          ; dCr^2
//   fmadd s0, s0, s0, s1     ; dCb^2 + dCr^2
//   fsqrt s0, s0
float chromaDistance(const YCbCr& a, const YCbCr& b) {
  const float dcb = a.cb - b.cb;
  const float dcr = a.cr - b.cr;
  return std::sqrt(dcb * dcb + dcr * dcr);
}

// __ZN9chromakey8hueAngleEff, 15 instrs
//
//   mov v8, s1 / mov v9, s0 / mov v0, v1 / mov v1, v9 / bl _atan2f
//       ; the call is atan2f(second_param, first_param)
//   fcmp s8, #0.0                    ; s8 = the second parameter
//   fccmp s9, s1, #0x0, eq           ; if that was 0, compare the first with 0
//   fcsel s0, s1, s0, eq             ; both zero -> 0.0, else the atan2 result
//
// The guard is redundant (atan2f(0, 0) is already 0) but it is what the bundle does.
float hueAngle(float a, float b) {
  if (b == 0.0f && a == 0.0f) return 0.0f;
  return std::atan2(b, a);
}

// __ZN9chromakey11angularDiffEff, 18 instrs
//
//   fabd s0, s0, s1          ; |a - b|
//   s8 = 0x40c90fdb = 2*pi
//   bl _fmodf                ; fmodf(|a - b|, 2*pi)
//   fsub s1, s8, s0          ; 2*pi - d
//   s2 = 0x40490fdb = pi
//   fcmp s0, s2 / fcsel s0, s1, s0, gt    ; d > pi ? 2*pi - d : d
float angularDiff(float a, float b) {
  const float twoPi = 6.2831855f;             // 0x40c90fdb
  const float pi = 3.1415927f;                // 0x40490fdb
  const float d = std::fmod(std::fabs(a - b), twoPi);
  return d > pi ? twoPi - d : d;
}

// __ZN9chromakey10spillScoreERKNS_5YCbCrES2_f, 82 instrs
//
// Recovered instruction by instruction. The shape is a despill weight, and each factor is clamped
// rather than trusted, because a caller multiplies three of them together and expects [0,1]:
//
//   fmul/fmadd/fsqrt              ; pMag = hypot(p.cb, p.cr)
//   fcmp s12, 1e-6 / b.mi         ; an achromatic pixel scores 0 (0x358637bd is 1e-6)
//   (same for kMag, the key)      ; an achromatic key scores 0 too
//   bl _atan2f (twice)            ; hue of pixel and key, with the origin special-cased to 0
//   fabd / fmod 2pi / >pi mirror  ; angularDiff, reused rather than re-derived
//   fdiv by -pi/2, fadd 1.0       ; hueWeight = clamp(1 - d/(pi/2), 0, 1) — 0xbfc90fdb is -pi/2
//   ldr s2, [x19] / fabd s2, s13  ; |pixel.y - referenceY| — the PIXEL's luma, not the key's
//   fadd s3, s12, s12             ; 2 * pMag, so chroma above half-scale saturates
//   fmul twice, clamp             ; chroma * hueWeight * yWeight, clamped to [0,1]
float spillScore(const YCbCr& pixel, const YCbCr& key, float referenceY) {
  const float achromatic = 1e-6f;             // 0x358637bd
  const float chromeP = std::sqrt(pixel.cb * pixel.cb + pixel.cr * pixel.cr);
  if (chromeP < achromatic) return 0.0f;
  const float chromeK = std::sqrt(key.cb * key.cb + key.cr * key.cr);
  if (chromeK < achromatic) return 0.0f;

  const float hue = angularDiff(hueAngle(pixel.cb, pixel.cr), hueAngle(key.cb, key.cr));
  // The division is by -pi/2 and then +1, which is the same as 1 - 2hue/pi but is what the bundle does.
  const float hueWeight = clampf(hue / -1.5707964f + 1.0f, 0.0f, 1.0f);

  // Written as a compare-and-select rather than fminf: the bundle emits fcmp + fcsel (two
  // instructions), and std::fmin folds to a single fminnm, which shifts every later register.
  const float yDelta = std::fabs(pixel.y - referenceY);
  const float yWeight = yDelta > 1.0f ? 1.0f : yDelta;
  // Both clamps are written out. The shipped build kept the lower branch; clang can prove this
  // value non-negative (two square roots added) and elides it, which is where two of the three
  // instruction difference come from — same outputs, three fewer instructions.
  const float twiceChroma = chromeP + chromeP;
  const float chroma = twiceChroma < 0.0f ? 0.0f : (twiceChroma > 1.0f ? 1.0f : twiceChroma);

  return clampf(chroma * hueWeight * yWeight, 0.0f, 1.0f);
}

// __ZN9chromakey15uniformityScoreEPKfm, 203 instrs
//
// Recovered from the full listing. The instruction count is large only because clang vectorises both
// loops four lanes at a time (fcvtl/fcvtl2 pairs feeding fadd chains); the arithmetic is the eight
// lines below. Both passes accumulate in DOUBLE, which matters for a long sample set — a float
// accumulator drifts and this score is multiplied by 100 afterwards.
float uniformityScore(const float* samples, size_t count) {
  if (!samples || count == 0) return 0.0f;

  double total = 0.0;
  for (size_t i = 0; i < count; ++i) total += static_cast<double>(samples[i]);
  const double mean = total / static_cast<double>(count);

  double squares = 0.0;
  for (size_t i = 0; i < count; ++i) {
    const double delta = static_cast<double>(samples[i]) - mean;
    squares += delta * delta;
  }
  const double variance = squares / static_cast<double>(count);
  const double sd = std::sqrt(variance);

  const double spread = sd + sd;                       // 2 * sd
  const double capped = spread > 1.0 ? 1.0 : spread;   // the bundle clamps HIGH only
  const double percent = (1.0 - capped) * 100.0;
  const float value = static_cast<float>(percent);     // narrowed to float, then clamped as float
  // The bundle tests the HIGH bound first (fcmp s0, 100.0f then fcsel gt), and only then zero
  // (fcmp s0, #0.0 then fcsel mi). Written in that order it reproduces the listing exactly.
  const float hi = value > 100.0f ? 100.0f : value;
  return hi < 0.0f ? 0.0f : hi;
}

// __ZN9chromakey10percentileEPKfmf, 31 instrs
//
//   cbz x0 / cbz x1 / movi.2d v1, #0        ; null or empty -> 0.0
//   subs x8, x1, #0x1 / b.ne / ldr s0, [x0] ; count == 1 -> data[0]
//   fmov s1, #100.0 / fcmp / fcsel s2, s1, s0, gt / fdiv s1, s2, s1
//                                           ; s1 = min(p, 100) / 100
//   fcmp s0, #0.0 / movi.2d v0, #0 / fcsel s0, s0, s1, mi
//                                           ; p < 0 -> s0 = p ITSELF, not the fraction
//   ucvtf s1, x8 / fmul s0, s0, s1          ; idx = that * (count - 1)
//   fcvtmu x8, s0 / fcvtpu x9, s0           ; lo = trunc(idx), hi = ceil(idx)
//   cmp x8, x9 / b.ne                       ; equal -> data[lo]
//   ldr s2, [x0, x8, lsl #2]                ; lo value
//   ldr s1, [x0, x9, lsl #2] / fsub s1, s1, s2 / fsub s0, s0, (float)lo
//   fmadd s0, s1, s0, s2                    ; lo + (hi - lo) * frac
//
// A negative `p` reaches the index arithmetic unclamped, and is saved only by the
// saturating float-to-unsigned conversions: both land on 0, so it returns the
// minimum. `percentileUnsorted` clamps the same case to 0 explicitly instead.
float percentile(const float* samples, size_t count, float p) {
  // One early-out for both conditions: the shipped code materialises its 0.0 once
  // (movi.2d v1, #0) and sends both `cbz` branches to the same return.
  if (samples == nullptr || count == 0) return 0.0f;
  if (count == 1) return samples[0];

  const float clamped = p > 100.0f ? 100.0f : p;
  float fraction = clamped / 100.0f;
  if (p < 0.0f) fraction = p;

  const float index = fraction * static_cast<float>(count - 1);
  const size_t lo = static_cast<size_t>(index);              // fcvtmu: truncate
  const size_t hi = static_cast<size_t>(std::ceil(index));   // fcvtpu: toward +inf
  if (lo == hi) return samples[lo];

  const float low = samples[lo];
  const float high = samples[hi];
  return low + (high - low) * (index - static_cast<float>(lo));
}

// __ZN9chromakey18percentileUnsortedENSt3__16vectorIfNS0_9allocatorIfEEEEf, 53 instrs
//
//   ldr x0, [x0] / ldr x1, [x19, #0x8] / cmp / b.eq   ; empty -> 0.0
//   bl __sort<less<float>>                            ; sorts its OWN copy
//   ldp x8, x9, [x19] / cmp x8, #0x0 / ccmp / b.eq    ; null or empty again -> 0.0
//   cmp x9 - x8, #0x4 / ldr s9, [x8]                  ; one element -> that element
//   ... then the same index arithmetic as `percentile`, with the one difference:
//   fcmp s8, #0.0 / movi.2d v1, #0 / fcsel s0, s1, s0, mi
//                                                     ; p < 0 -> 0.0, not p
//
// The vector is taken BY VALUE, which is the whole reason the function can sort it:
// the name means "you do not have to sort it first", not "this does not sort".
float percentileUnsorted(std::vector<float> samples, float p) {
  if (samples.empty()) return 0.0f;
  std::sort(samples.begin(), samples.end());
  // Checked twice, before and after the sort — the shipped code re-reads begin and
  // end here (`ldp x8, x9, [x19]` then `cmp x8, #0` / `ccmp x9, x8, #0x4, ne`), which
  // is a null-or-empty test the compiler could not fold away.
  if (samples.data() == nullptr || samples.empty()) return 0.0f;
  if (samples.size() == 1) return samples[0];

  const float clamped = p > 100.0f ? 100.0f : p;
  float fraction = clamped / 100.0f;
  if (p < 0.0f) fraction = 0.0f;

  const float index = fraction * static_cast<float>(samples.size() - 1);
  const size_t lo = static_cast<size_t>(index);
  const size_t hi = static_cast<size_t>(std::ceil(index));
  if (lo == hi) return samples[lo];

  const float low = samples[lo];
  const float high = samples[hi];
  return low + (high - low) * (index - static_cast<float>(lo));
}

// __ZN9chromakey17gradientMagnitudeEPKfiilii, 47 instrs
//
//   cbz x0                                        ; null -> 0.0
//   subs w9, w1, #0x1 / b.lt                      ; width  < 1 -> 0.0
//   subs w8, w2, #0x1 / b.lt                      ; height < 1 -> 0.0
//   csinc w10, w4, wzr, gt / sub w10, w10, #0x1   ; xm1 = max(x - 1, 0)
//   add w11, w4, #0x1 / csinc w9, w9, w4, lt      ; xp1 = min(x + 1, width - 1)
//   csinc w11, w5, wzr, gt / sub w11, w11, #0x1   ; ym1 = max(y - 1, 0)
//   sxtw x12, w5 / mul x12, x3, x12 / add x12, x0, x12, lsl #2   ; row y
//   ldr s0, [x12, w10, uxtw #2]                  ; s0 = field[stride*y   + xm1]
//   ldr s1, [x12, w9, sxtw #2]                   ; s1 = field[stride*y   + xp1]
//   mul x12, x3, x11 / add x12, x0, x12, lsl #2                  ; row ym1
//   ldr s2, [x12, w4, sxtw #2]                   ; s2 = field[stride*ym1 + x]
//   csinc w8, w8, w5, lt                          ; yp1 = min(y + 1, height - 1)
//   sxtw x12, w8 / mul x12, x3, x12 / add x12, x0, x12, lsl #2   ; row yp1
//   ldr s3, [x12, w4, sxtw #2]                   ; s3 = field[stride*yp1 + x]
//   sub w9, w9, w10 / cmp #1 / csinc / ucvtf      ; dx = max(xp1 - xm1, 1) as float
//   sub w8, w8, w11 / cmp #1 / csinc / ucvtf      ; dy = max(yp1 - ym1, 1) as float
//   fsub s0, s1, s0 / fdiv s0, s0, s4             ; gx = (right - left) / dx
//   fsub s1, s3, s2 / fdiv s1, s1, s5             ; gy = (below - above) / dy
//   fmul s1, s1, s1 / fmadd s0, s0, s0, s1 / fsqrt
//
// The kernel is a CROSS. The horizontal difference is taken on the CENTRE ROW y
// (listing 0x2b68-0x2b78) and the vertical difference on the CENTRE COLUMN x
// (listing 0x2b7c-0x2b98). An earlier revision of this file read the horizontal
// pair from row ym1 and described the kernel as "anchored top-left"; that was a
// transcription error which had propagated into the notes and the self-check, and
// all three are corrected. The self-check now pins the shipped behaviour on grids
// where the two readings disagree, and the differ reports this function
// instruction-identical to the bundle.
float gradientMagnitude(const float* field, int width, int height, long rowStride, int x, int y) {
  if (field == nullptr) return 0.0f;
  if (width < 1) return 0.0f;
  if (height < 1) return 0.0f;

  const int xm1 = x > 1 ? x - 1 : 0;
  const int xp1 = (width - 1 < x + 1) ? width - 1 : x + 1;
  const int ym1 = y > 1 ? y - 1 : 0;
  const int yp1 = (height - 1 < y + 1) ? height - 1 : y + 1;

  const long stride = rowStride;
  const float left = field[stride * y + xm1];
  const float right = field[stride * y + xp1];
  const float above = field[stride * ym1 + x];
  const float below = field[stride * yp1 + x];

  const float dx = static_cast<float>(xp1 - xm1 > 1 ? xp1 - xm1 : 1);
  const float dy = static_cast<float>(yp1 - ym1 > 1 ? yp1 - ym1 : 1);

  const float gx = (right - left) / dx;
  const float gy = (below - above) / dy;
  return std::sqrt(gx * gx + gy * gy);
}

}  // namespace chromakey
