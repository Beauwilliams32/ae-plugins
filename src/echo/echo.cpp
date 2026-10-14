// echo.cpp — recovered implementations from the EchoTrails bundle.

#include "echo.h"

namespace echo {

// ---------------------------------------------------------------------------
// clampf(float, float, float)
//
// __ZN4echo6clampfEfff: the same five instructions as the other five copies in the
// fleet, with the low bound tested last so it wins on an inverted range.
// ---------------------------------------------------------------------------
float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

// ---------------------------------------------------------------------------
// blendPixel(float*, const float*, BlendMode, float)
//
// __ZN4echo10blendPixelEPfPKfNS_9BlendModeEf, 106 instructions, recovered in full.
//
// WEIGHTS. After clamping, the binary computes three weights once and reuses them for
// every channel:
//
//     0x91c  s7  = 1 - a
//     0x920  s16 = 1 - d
//     0x928  s16 = a * s16        ; a*(1-d)
//     0x92c  s18 = a * d
//            s17 = d * s7         ; d*(1-a)
//
// so the colour result is `a*(1-d)*src + a*d*BLEND + d*(1-a)*dst` for every mode.
//
// MODES, from the switch at 0x930 which tests 4, then 3, then 2, and falls through
// to source-over:
//
//   over (default, and 0/1)  BLEND is unused: the code takes the `a*(1-d)*src` term
//                            and adds `a*d*src`, collapsing to `a*src`.
//   add (2)      0x948      BLEND = src + dst, computed as
//                           `s20 = (dst+src)*a*d; out = a*(1-d)*src + s20 + ...`
//   screen (3)   0x974      BLEND = src + dst - src*dst, and this arm is the only
//                           one the compiler VECTORISED (fadd.2s / fmls.2s / fmla.2s
//                           over R and G together, then a scalar B) — same maths,
//                           different shape.
//   lighten (4)  0x9a0      BLEND = max(src, dst)
//
// ALPHA is the same on every path: `s0 = d*s7 + a`, i.e. `a + d*(1-a)`.
//
// The unpremultiply guards are strict `> 0` (0x8a8, 0x8c0, 0x8d0, 0x8e0, 0x8f4,
// 0x908), so a fully transparent pixel keeps its stored colour instead of dividing
// by zero — and because the weights multiply those stored values by zero anyway, the
// result is unaffected. That is worth preserving rather than "cleaning up": a NaN
// here would propagate through every subsequent composite in the ladder.
// ---------------------------------------------------------------------------
void blendPixel(float* dst, const float* src, BlendMode mode, float amount) {
  const float srcA = src[3];
  const float dstA = dst[3];

  const float a = clampf(amount * srcA, 0.0f, 1.0f);
  const float d = clampf(dstA, 0.0f, 1.0f);

  if (a <= 0.0f) return;

  // Unpremultiply. Only when the alpha is strictly positive.
  float sr = 0.0f, sg = 0.0f, sb = 0.0f;
  float dr = 0.0f, dg = 0.0f, db = 0.0f;
  if (srcA > 0.0f) { sr = src[0] / srcA; sg = src[1] / srcA; sb = src[2] / srcA; }
  if (dstA > 0.0f) { dr = dst[0] / dstA; dg = dst[1] / dstA; db = dst[2] / dstA; }

  const float wSrc = a * (1.0f - d);   // s16
  const float wBoth = a * d;           // s18
  const float wDst = d * (1.0f - a);   // s17

  float br = sr, bg = sg, bb = sb;     // the blend term, per mode
  switch (mode) {
    case BlendMode::Add:     br = sr + dr;           bg = sg + dg;           bb = sb + db;           break;
    case BlendMode::Screen:  br = sr + dr - sr * dr; bg = sg + dg - sg * dg; bb = sb + db - sb * db; break;
    case BlendMode::Lighten: br = sr > dr ? sr : dr; bg = sg > dg ? sg : dg; bb = sb > db ? sb : db; break;
    default:
      // Source-over: the middle term is the source itself, which is what makes the
      // first two terms collapse to a*src.
      br = sr; bg = sg; bb = sb;
      break;
  }

  dst[0] = wSrc * sr + wBoth * br + wDst * dr;
  dst[1] = wSrc * sg + wBoth * bg + wDst * dg;
  dst[2] = wSrc * sb + wBoth * bb + wDst * db;
  dst[3] = d * (1.0f - a) + a;
}

// ---------------------------------------------------------------------------
// compositeOver(ImageF&, const ImageF&, BlendMode, float)
//
// __ZN4echo13compositeOverERNS_6ImageFERKS0_NS_9BlendModeEf, 37 instructions,
// recovered in full.
//
//     0xa10  if (dst.width  != src.width)  return;    ; silent, no error
//     0xa40  if (dst.height != src.height) ...        ; and the count is 0
//     0xa4c  x22 = height * width                     ; the pixel count
//     0xa50  if (count == 0) return;
//     0xa60  loop over pixels, stride 0x10 bytes, calling blendPixel
//
// The two size tests are fused: the height comparison is a `ccmp` guarded on the
// width comparison having been equal, so the loop count is computed unconditionally
// and then re-tested against zero. The effect is a single up-front bail.
// ---------------------------------------------------------------------------
void compositeOver(ImageF& dst, const ImageF& src, BlendMode mode, float amount) {
  if (dst.width != src.width) return;
  if (dst.height != src.height) return;

  long long count = static_cast<long long>(dst.height) * static_cast<long long>(dst.width);
  if (count == 0) return;

  float* d = dst.data;
  const float* s = src.data;
  for (long long i = 0; i < count; ++i) {
    blendPixel(d, s, mode, amount);
    d += 4;   // 0x10 bytes
    s += 4;
  }
}

}  // namespace echo
