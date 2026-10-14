// echo.h — recovered from the compiled EchoTrails bundle.
//
// EchoTrails composites a LADDER of transformed copies of a frame back over itself,
// producing motion trails. Its engine works on 32-bit float RGBA images, and two of
// its six functions are recovered here.
//
// Recovered from the demangled symbol table and disassembly of EchoTrails.plugin.
// The original C++ was destroyed on 2026-10-13 with no remote and no backup.
//
// SCOPE. `clampf`, `blendPixel` and `compositeOver` are recovered in full.
// `buildLadder` (91 instructions), `sampleBilinear` (120) and `transformBilinear`
// (122) are NOT: they are identified, with their signatures and sizes known and their
// bodies unread. Nothing is stubbed for them — they are simply absent — and the trail
// geometry they implement is therefore not recovered.

#pragma once

namespace echo {

// ---------------------------------------------------------------------------
// [RECOVERED LAYOUT] ImageF — a 32-bit float RGBA image. Pinned by every access in
// compositeOver and blendPixel:
//
//     ldrsw x8, [x0]        ; +0x00  width  (int)
//     ldrsw x9, [x0, #0x4]  ; +0x04  height (int)
//     ldr   x8, [x0, #0x8]  ; +0x08  data   (float*, 4 floats per pixel)
//
// compositeOver strides the buffer by 0x10 BYTES per pixel, which is exactly four
// floats, so pixels are tightly packed RGBA with no padding.
//
// The colour channels are PREMULTIPLIED: blendPixel divides each channel by that
// pixel's alpha before blending (`ldr s2, [x1]` / `fdiv s3, s2, s5` where s5 is the
// source alpha), and re-multiplies at the end. Treating the data as straight alpha
// would double-count coverage on every trail.
// ---------------------------------------------------------------------------
struct ImageF {
  int width = 0;
  int height = 0;
  float* data = nullptr;
};

// ---------------------------------------------------------------------------
// [RECOVERED] BlendMode — the selector values are read off blendPixel's switch,
// which tests 4, 3 and 2 and falls through to source-over for anything else.
//
// Only one name survives in the source: `EchoTrails` has no symbol for the enum, so
// 0 and 1 are indistinguishable in the binary and are named for what they do.
// ---------------------------------------------------------------------------
enum class BlendMode : int {
  OverA = 0,   // standard source-over
  OverB = 1,   // identical to OverA in the binary
  Add = 2,     // src + dst
  Screen = 3,  // src + dst - src*dst
  Lighten = 4, // max(src, dst)
};

// ---------------------------------------------------------------------------
// [exact] Scalar clamp. The SIXTH identical copy of this idiom in the fleet, after
// spectra::, lutblend::, typeriser::, glitch:: and noisefield::.
// ---------------------------------------------------------------------------
float clampf(float value, float lo, float hi);

// ---------------------------------------------------------------------------
// [exact] Composite one pixel of `src` onto one pixel of `dst`, in place.
//
//     a = clamp(amount * src.alpha, 0, 1)
//     d = clamp(dst.alpha, 0, 1)
//     if (a <= 0) return                       // nothing to composite
//     srcRGB /= src.alpha   (when src.alpha > 0)
//     dstRGB /= dst.alpha   (when dst.alpha > 0)
//     out.rgb = a*(1-d) * src + a*d * BLEND(src,dst) + d*(1-a) * dst
//     out.a   = a + d*(1-a)
//
// The three terms are the recovered weights: `s16`, `s18` and `s17` in the binary.
// The alpha result is the same for every mode, so only the colour blend varies.
//
// TWO DETAILS THAT MATTER:
//   * the unpremultiply guards are `> 0`, not `>= 0`, so a fully transparent pixel's
//     colour is left as-is rather than dividing by zero;
//   * with mode OverA/OverB the middle term collapses to `a*d*src`, and the first and
//     middle terms then sum to exactly `a*src` — which is why `over` comes out as the
//     familiar `src*a + dst*d*(1-a)` rather than something merely close to it.
// ---------------------------------------------------------------------------
void blendPixel(float* dst, const float* src, BlendMode mode, float amount);

// ---------------------------------------------------------------------------
// [exact] Composite a whole image, pixel by pixel, in place.
//
// SILENTLY DOES NOTHING unless the two images agree on BOTH dimensions, and unless
// width*height is non-zero. There is no error return and no partial composite: the
// size test is made once up front and the function returns.
// ---------------------------------------------------------------------------
void compositeOver(ImageF& dst, const ImageF& src, BlendMode mode, float amount);

}  // namespace echo
