// echo.h — recovered from the compiled EchoTrails bundle.
//
// EchoTrails composites a LADDER of transformed copies of a frame back over itself,
// producing motion trails. Its engine works on 32-bit float RGBA images.
//
// Recovered from the demangled symbol table and disassembly of EchoTrails.plugin.
// The original C++ was destroyed on 2026-10-13 with no remote and no backup.
//
// NAMESPACE-COMPLETE: every `echo::` symbol in the bundle is transcribed, so
// tools/codegen-diff.mjs reports zero unrecovered symbols for this directory.
// test/codegen.test.mjs asserts that marker and that measurement agree.
//
// SCOPE. All SIX `echo::` functions are recovered in full (503 instructions). The
// namespace is complete. The host integration is not: `EffectMain` and the
// anonymous-namespace helper `WorldToImageSized(PF_LayerDef const*, bool, int, int,
// ImageF&)` (503 instructions) are identified only, with their bodies unread, and
// nothing is stubbed for them.
//
// Recovering `transformBilinear` also CORRECTED the `ImageF` layout, which had been
// under-specified as a raw pointer — see the struct below.

#pragma once

#include <vector>

namespace echo {

// ---------------------------------------------------------------------------
// [RECOVERED LAYOUT] ImageF — a 32-bit float RGBA image, 32 bytes.
//
//     ldrsw x8, [x0]        ; +0x00  width  (int)
//     ldrsw x9, [x0, #0x4]  ; +0x04  height (int)
//     ldr   x8, [x0, #0x8]  ; +0x08  pixels.data()
//
// CORRECTED. This was first modelled as `{int, int, float*}`, 16 bytes, on the strength
// of `ldr x8, [x0, #0x8]` in compositeOver — which is exactly what `std::vector::data()`
// compiles to, so a READ cannot tell a raw pointer from the FIRST FIELD of a vector.
// Recovering `transformBilinear` settled it, because that function RESIZES the image and
// a raw pointer has nothing to resize:
//
//     ldp  x21, x22, [x19, #0x8]     ; begin, end  -- two pointers, not one
//     str  x25,      [x19, #0x18]    ; capacity at +0x18
//     ldr  x0, [x19, #0x8] / cbz / bl __ZdlPv   ; and it DEALLOCATES the old buffer
//     sub  x1, x22, x21              ; end - begin is the SIZE, in bytes
//
// `end - begin` as a byte count and a capacity at +0x18 exist only on a vector. The
// element type is `float`, four per pixel: the resize path computes
// `nFloats = width*height*4` (`lsl x25, x8, #2`) and allocates `width*height*16` bytes
// (`lsl x22, x8, #4`), so pixels are tightly packed RGBA floats with no padding.
//
// The colour channels are PREMULTIPLIED: blendPixel divides each channel by that
// pixel's alpha before blending (`ldr s2, [x1]` / `fdiv s3, s2, s5` where s5 is the
// source alpha), and re-multiplies at the end. Treating the data as straight alpha
// would double-count coverage on every trail.
// ---------------------------------------------------------------------------
struct ImageF {
  int width = 0;
  int height = 0;
  std::vector<float> pixels;
};

static_assert(sizeof(ImageF) == 32,
              "ImageF is two ints plus a std::vector<float>: begin@0x8, end@0x10, cap@0x18");

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

// ---------------------------------------------------------------------------
// [RECOVERED LAYOUT] TrailStep — one rung of the ladder, 12 bytes, three floats.
//
// Pinned by the consume site in `EffectMain`, which loads fields 0 and 1 together
// (`ldp s0, s1, [x8, #-0x8]`), hands them to the transform, and then loads field 2
// twelve bytes earlier (`ldur s0, [x26, #-0xc]`, i.e. the record's own address) to hand
// to the composite:
//
//     transformBilinear(src, scratch, step.scale, step.angleDeg);
//     compositeOver(accum, transformed, mode, step.amount);
//
// So the FIRST two fields are the transform's scale and its rotation in DEGREES, and
// the third is the per-step composite opacity. The names come from that call site, not
// from a symbol — `TrailStep` and its fields appear nowhere in the binary.
// ---------------------------------------------------------------------------
struct TrailStep {
  float scale = 1.0f;
  float angleDeg = 0.0f;
  float amount = 0.0f;
};

static_assert(sizeof(TrailStep) == 12, "a ladder rung is three floats, stored contiguously");

// ---------------------------------------------------------------------------
// [exact] The ladder: `min(max(n, 0), 16)` rungs of a decaying, rotating trail.
//
//     count = min(max(n, 0), 16)
//     for j in [0, count):  rung[j] = { clampf(scale, 0, 1),
//                                       max(angleDeg, 1e-4f),
//                                       amountStep * (j + 1) }
//
// THREE THINGS THE BINARY SETTLES THAT A GUESS WOULD GET WRONG:
//
//  * The clamped count is `bic w8, w0, w0, asr #31` then a `csel` against 16, so it is
//    `min(max(n,0),16)` and NOT a cast. `n < 1` returns an EMPTY vector — not one rung,
//    not a clamped rung.
//
//  * The angle is FLOORED at 1e-4f, and that floor is load-bearing rather than
//    defensive. The consumer SKIPS the transform outright when a rung is
//    `scale == 1.0f && angleDeg == 0.0f`, so a floor of exactly 0 would let the
//    identity rung take a different path; 1e-4f keeps every rung on the resampling path
//    while being invisible at any real resolution.
//
//  * The step index is 1-BASED. The emitted loop decrements a counter initialised to
//    the count and stores at `buf + (iteration-1) * 12`, so rung 0 is written with
//    `i = 1` and the last rung with `i = count`. The {1, 1, 0} fill that precedes it is
//    therefore UNOBSERVABLE — every rung is overwritten — which is why the two readings
//    of that fill (a fill-constructor versus default member initialisers) cannot be
//    told apart and do not need to be.
//
// Until this function was read, its body was listed as unread and one register holding
// an argument was recorded as unresolvable. Reading it settled that too: the prologue
// spills `q0` and reloads it into `q6` (`ldr q6, [sp, #0x10]`), so `s6`, `s7` and `s8`
// hold arguments 0, 1 and 2 respectively, and the `mov.s v6[1], v1[0]` that made the
// lane look ambiguous re-loads argument 1 from a second saved copy.
// ---------------------------------------------------------------------------
std::vector<TrailStep> buildLadder(int n, float scale, float angleDeg, float amountStep);

// ---------------------------------------------------------------------------
// [exact] One bilinear sample of `src` at (x, y) into four floats.
//
//     x0i = floor(x),  y0i = floor(y)
//     fx  = x - x0i,   fy  = y - y0i
//     out = (1-fx)(1-fy)*P(x0i,y0i) + fx(1-fy)*P(x0i+1,y0i)
//         + (1-fx)fy*P(x0i,y0i+1) + fx*fy*P(x0i+1,y0i+1)
//
// with each of the four taps INDEPENDENTLY SKIPPED when it falls outside
// [0,width) x [0,height), and NO renormalisation of the surviving weights. That matters
// at the edges: sampling half a pixel off the left border yields a pixel at half
// brightness, because the missing tap contributes zero rather than being clamped to the
// edge texel. Clamping the coordinate instead — the usual alternative — would give a
// different image, so this is a behaviour, not an implementation detail.
//
// `out` is zeroed first, so every tap adds into zero rather than into caller data.
// ---------------------------------------------------------------------------
void sampleBilinear(const ImageF& src, float x, float y, float* out);

// ---------------------------------------------------------------------------
// [exact] Rotate and scale `src` about its centre into `dst`, bilinear-sampled.
//
//     dst is resized to src's dimensions and zero-filled when they differ
//     (and zero-filled again unconditionally, so the two zeroes are real)
//     if (scale <= 0) return                     // dst is left zeroed
//
//     radians = angleDeg * pi / 180
//     (sn, cs) = sincosf(radians)
//     cx = (width - 1) * 0.5,  cy = (height - 1) * 0.5
//     inv = 1 / scale
//     for each dst pixel (x, y):
//         dx = x - cx,  dy = y - cy
//         sx = ( dx*cs + dy*sn) * inv + cx
//         sy = (-dx*sn + dy*cs) * inv + cy
//         sampleBilinear(src, sx, sy, &dst.pixels[(y*width + x) * 4])
//
// Three details worth naming. The sample is taken at INTEGER pixel centres but the
// centre of rotation is `(dimension - 1) * 0.5` — half a pixel inside the true centre —
// because the two are computed from different expressions, and reproducing one from the
// other would shift the whole image by half a pixel. The `1 / scale` is a genuine
// DIVISION rather than a multiply by a reciprocal, so the two differ in the last bit.
// And the sign convention is `-angleDeg` in the usual right-handed sense: `sx` adds
// `dy*sn` while `sy` subtracts `dx*sn`.
//
// A non-positive `scale` is not an error and does not fall back to a copy: it returns
// with `dst` zeroed, i.e. a black frame.
//
// The resize is guarded on the DIMENSIONS, not on the buffer's size, so an `ImageF` whose
// width and height already match the source is left at whatever `pixels.size()` it has —
// zeroed by the unconditional fill, then written only for the first `width * height`
// pixels. For a buffer that is too SMALL that is an out-of-bounds write. It is not
// reachable from any recovered call site, because the plugin only ever builds its images
// through the same constructor path, but it is a real hazard in the recovered contract
// and is recorded rather than quietly guarded against, since adding a check would change
// the emitted instruction stream.
// ---------------------------------------------------------------------------
void transformBilinear(const ImageF& src, ImageF& dst, float scale, float angleDeg);

}  // namespace echo
