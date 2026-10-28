// chromakey.h — reconstructed from the compiled ChromaKeyAssist bundle.
//
// Every declaration below is copied from the demangled Mach-O symbol table of
// ChromaKeyAssist.plugin (bundle id com.williamsmedia.aftereffects.ChromaKeyAssist),
// not from source: the original C++ was destroyed on 2026-10-13 with no remote and
// no backup.
//
// NAMESPACE-COMPLETE
//
// Every `chromakey::` symbol in the bundle is transcribed, so this header is a full
// reconstruction rather than a partial one. The last one to land was
// `analyzeFrame<T>` (752 instructions for the 8-bit instantiation and 770 for the
// 16-bit one, 2026-10-28), which also pinned the `View` and `Stats` layouts.
//
// RECOVERED HERE (15 functions):
//   clampf, rgbToYCbCr, sensitivityToThreshold, chromaDistance, hueAngle,
//   angularDiff, percentile, percentileUnsorted, gradientMagnitude
//   spillScore, uniformityScore
//   renderView<unsigned char>, renderView<unsigned short>
//   analyzeFrame<unsigned char>, analyzeFrame<unsigned short>
//
// NOT A `chromakey::` SYMBOL, and deliberately out of `src/`:
//   StoreReadout(PF_InData*, const Stats&) (70) — host integration. It takes a
//     `PF_InData*`, which is an Adobe SDK type, and this directory holds
//     transcriptions of the shipped DSP only.
//
// MEASUREMENT: `node tools/codegen-diff.mjs chromakey` reports SEVEN
// instruction-identical, EIGHT behavioural, none absent. The seven identical are
// clampf, rgbToYCbCr, sensitivityToThreshold, chromaDistance, hueAngle,
// angularDiff and gradientMagnitude. The eight behavioural are percentile,
// percentileUnsorted, spillScore, uniformityScore, renderView<unsigned char>,
// renderView<unsigned short>, analyzeFrame<unsigned char> and
// analyzeFrame<unsigned short>.
//
// SIGNATURE FIDELITY: all fifteen are [exact] — taken verbatim from symbol
// mangling. `RGB` and `YCbCr` are [inferred] layouts, each established from the
// offsets the functions themselves use, and pinned by static_assert. `View`,
// `Params`, `Stats` and `ViewMode` are [inferred] layouts and [invented] names
// where the bundle carries only the type name.

#pragma once
#include <cstddef>
#include <vector>

namespace chromakey {

// ---------------------------------------------------------------------------
// Recovered value types.
//
// Only the names of these types survive in the binary. The layouts are inferred
// from the loads and stores in the functions below, which is enough to pin them:
// `rgbToYCbCr` reads a contiguous 12 bytes and writes {Y, Cb, Cr}, and
// `chromaDistance` reads +0x4 and +0x8 of a `YCbCr` and never its Y.
// ---------------------------------------------------------------------------

/** [inferred] A colour, three 32-bit floats. */
struct RGB {
  float r;
  float g;
  float b;
};

/** [inferred] Luma and two chroma differences: Y at +0x0, Cb at +0x4, Cr at +0x8. */
struct YCbCr {
  float y;
  float cb;
  float cr;
};

static_assert(sizeof(RGB) == 12, "rgbToYCbCr loads exactly three packed floats");
static_assert(sizeof(YCbCr) == 12, "chromaDistance reads Cb at +0x4 and Cr at +0x8");

// ---------------------------------------------------------------------------
// Shared with the rest of the fleet.
// ---------------------------------------------------------------------------

// [exact] Three-way clamp. The LOW bound is tested last, so an inverted range
// yields `lo` — the fleet-wide `clampf` idiom, one of fifteen copies.
float clampf(float value, float lo, float hi);

// ---------------------------------------------------------------------------
// Colour.
// ---------------------------------------------------------------------------

// [exact] Rec.601 luma and the two chroma differences, in floats:
//
//   Y  = 0.587*G then += 0.299*R then += 0.114*B   (G first, in that order)
//   Cb = (B - Y) * 0.564334
//   Cr = (R - Y) * 0.713267
//
// The three luma coefficients and the two chroma scales decode from the shipped
// immediates exactly (0x3f1645a2, 0x3e991687, 0x3de978d5, 0x3f107831, 0x3f3698ab);
// the chroma scales are 1/1.772 and 1/1.402 written as six-decimal literals, NOT
// as divisions — `1.0f/1.772f` is three ulps away from the shipped constant.
// The accumulation ORDER matters to the last bit and is kept.
YCbCr rgbToYCbCr(const RGB& rgb);

// [exact] Maps a 0..100 sensitivity onto a chroma threshold:
//
//   threshold = 0.03 + 0.42 * (min(sensitivity, 100) / 100)
//
// with a NEGATIVE sensitivity short-circuiting to 0.03. The saturation is one-sided:
// there is no lower clamp, and an input above 100 stops at the same 0.45 as 100.
float sensitivityToThreshold(float sensitivity);

// [exact] Euclidean distance in chroma only: `sqrt(dCb^2 + dCr^2)`. Y is not read,
// so two colours of identical chroma are at distance 0 however far apart they are in
// brightness — which is the point, and is pinned by the self-check.
float chromaDistance(const YCbCr& a, const YCbCr& b);

// [exact] `atan2f(second, first)`, with the origin special-cased to 0.
//
// The argument order is the thing to get right: the FIRST parameter is the
// denominator (the "a" of atan2) and the SECOND is the numerator, so
// `hueAngle(y, x)` returns `atan2f(x, y)`.
float hueAngle(float a, float b);

// [exact] Circular distance between two angles in radians, in [0, pi]:
//
//   d = fmodf(fabsf(a - b), 2*pi);  return d > pi ? 2*pi - d : d;
//
// Both constants decode exactly (0x40c90fdb is 2*pi, 0x40490fdb is pi).
float angularDiff(float a, float b);

// ---------------------------------------------------------------------------
// Keying scores.
// ---------------------------------------------------------------------------

// [exact] How much of `key`'s hue a pixel still carries — the weight a despill uses to decide
// which pixels are "key-coloured". 82 instructions, read in full from the bundle:
//
//   pMag = sqrt(cb^2 + cr^2);  if (pMag < 1e-6) return 0;      // achromatic pixel
//   kMag = sqrt(key.cb^2 + key.cr^2);  if (kMag < 1e-6) return 0;   // achromatic key
//   hue = angularDiff(hueAngle(p.cb, p.cr), hueAngle(key.cb, key.cr));
//   hueWeight = clamp(1 - hue / (pi/2), 0, 1);                 // 0xbfc90fdb is -pi/2, so this is a
//                                                              // division by a negative constant
//   yWeight   = min(fabsf(p.y - referenceY), 1);               // how far this pixel's luma sits from
//                                                              // the reference the caller passes
//   chroma    = clamp(2 * pMag, 0, 1);                         // chroma above half-scale saturates
//   return clamp(chroma * hueWeight * yWeight, 0, 1);
//
// Two details are easy to get wrong and are pinned by the self-check: the hue term divides by
// -pi/2 and adds 1 (so a hue 90 degrees away already scores 0), and `referenceY` is compared with
// the PIXEL's luma, not with the key's.
float spillScore(const YCbCr& pixel, const YCbCr& key, float referenceY);

// ---------------------------------------------------------------------------
// Statistics.
// ---------------------------------------------------------------------------

// [exact] The `p`th percentile of `count` samples, interpolating between the two
// nearest order statistics. Takes a raw pointer, so the samples are read in place
// and the caller's array is untouched.
//
// `p` is clamped to 100 but NOT to 0: a negative `p` is used as-is, and only the
// saturating float-to-unsigned conversions keep it from indexing outside the array
// — the index lands on 0, so a negative `p` returns the minimum. `percentileUnsorted`
// below takes the other branch of the same comparison and clamps to 0 instead.
float percentile(const float* samples, size_t count, float p);

// [exact] How evenly a set of samples is spread, as a 0-100 percentage: 100 is perfectly uniform,
// 0 is "so spread out that twice the standard deviation already reaches 1.0". 203 instructions, read
// in full from the bundle:
//
//   if (!samples || count == 0) return 0;              // both null checks are cbz before anything else
//   mean = sum(x as double) / count                    // accumulated in d-registers, i.e. double
//   var  = sum((x - mean)^2) / count
//   sd   = sqrt(var)
//   t    = min(2 * sd, 1.0)                            // no lower clamp: 2*sd cannot be negative
//   v    = (1.0 - t) * 100.0
//   return clamp(v, 0, 100)                            // 0x4059.. is 100.0 and 0x42c80000 is 100.0f
//
// The vectorised prologue is clang unrolling four lanes; the arithmetic above is what it computes.
float uniformityScore(const float* samples, size_t count);

// [exact] The same statistic for a caller who has not sorted anything: takes the
// vector BY VALUE and sorts its own copy, which is why the name says "unsorted".
//
// Differs from `percentile` in exactly one place: a negative `p` clamps to 0 here
// rather than being carried into the index arithmetic. Both end up returning the
// minimum, by different routes.
float percentileUnsorted(std::vector<float> samples, float p);

// [exact] Gradient magnitude of a scalar field at (x, y):
//
//   gx = (field(y, xp1) - field(y, xm1)) / max(xp1 - xm1, 1)
//   gy = (field(yp1, x)   - field(ym1, x))   / max(yp1 - ym1, 1)
//   return sqrt(gx*gx + gy*gy)
//
// where xm1/xp1 and ym1/yp1 are the coordinates clamped to [0, width-1] and
// [0, height-1]. The kernel is a CROSS: the horizontal difference is taken on
// the CENTRE row y and the vertical difference on the CENTRE column x. The
// arm64 listing says so directly — the two horizontal loads read from the row
// base `field + stride*y*4` (listing 0x2b68-0x2b78) and the two vertical loads
// from the row bases `field + stride*ym1*4` and `field + stride*yp1*4` at
// column x (listing 0x2b7c-0x2b98).
//
// This was WRONG in an earlier revision of this file, which had the horizontal
// difference on row ym1 and called the kernel "anchored top-left". That was a
// transcription error, not a shipped quirk, and it had propagated into
// RECOVERY-NOTES.md and into the self-check; all three are corrected.
//
// Both denominators are floored at 1, which is what stops a degenerate
// neighbourhood from dividing by zero. The floor is visible in the listing as
// `csinc w9, w9, wzr, gt` (listing 0x2ba4) — CSINC adds 1 in its else arm, so
// the false branch yields 1 and not 0.
//
// `rowStride` is in ELEMENTS, not bytes: the code scales it by 4 to reach a row,
// whatever the original parameter was called. A null field, or a width or height
// below 1, returns 0.
float gradientMagnitude(const float* field, int width, int height, long rowStride, int x, int y);

// ---------------------------------------------------------------------------
// The two per-pixel template families.
//
// `analyzeFrame` and `renderView` are function templates in the shipped bundle, so
// the symbol table holds one symbol per instantiation (`analyzeFrameIhE…` and
// `analyzeFrameItE…`) and the differ counts them separately. The types below are
// therefore [exact] in name and [inferred] in layout — the names come from the
// mangling (`View`, `Params`, `Stats`, `ViewMode`) and the layouts from the offsets
// the shipped code loads and stores at.
// ---------------------------------------------------------------------------

/** [inferred] A pixel buffer: a pointer, a width, a height, and a row stride.
 *
 *  `rowBytes` is in ELEMENTS of `T`, not bytes: the shipped code scales it by
 *  `sizeof(T)` itself (arm64 listing of `renderView<unsigned short>` at 0x2cac:
 *  `lsl x17, x17, #1`, i.e. `rowBytes * 2` bytes for the 16-bit instantiation),
 *  and the 8-bit instantiation uses it unscaled. `gradientMagnitude` says the same
 *  thing about its `rowStride`.
 */
template <typename T>
struct View {
  T* data;
  int width;
  int height;
  long rowBytes;
};

/** [inferred] The key colour and the sensitivity slider. */
struct Params {
  float keyR;
  float keyG;
  float keyB;
  float sensitivity;
};

/** [inferred] The per-frame statistics `analyzeFrame` returns. */
struct Stats {
  float uniformity;
  float meanSpill;
  float p5;
  float p95;
  float keyedFraction;
};

// The layouts above are inferred from the offsets the shipped code uses, and
// these asserts pin them so a silent drift fails the build rather than the
// differential.
//
// `View<T>` — the shipped `analyzeFrame` loads `data` at +0x0, `width` at +0x8,
// `height` at +0xc and `rowBytes` at +0x10 (arm64 listing of
// `analyzeFrame<unsigned char>`: `ldr x10, [x28]`, `ldr w9, [x28, #0x8]`,
// `ldr w8, [x28, #0xc]`, `ldr x11, [x28, #0x10]`).
static_assert(sizeof(View<unsigned char>) == 24, "the shipped View is 24 bytes");
static_assert(offsetof(View<unsigned char>, data) == 0, "View::data is at +0x0");
static_assert(offsetof(View<unsigned char>, width) == 8, "View::width is at +0x8");
static_assert(offsetof(View<unsigned char>, height) == 12, "View::height is at +0xc");
static_assert(offsetof(View<unsigned char>, rowBytes) == 16, "View::rowBytes is at +0x10");
static_assert(sizeof(View<unsigned short>) == 24, "the View layout does not depend on T");

// `Params` — the shipped code reads the key colour and the sensitivity as four
// consecutive floats (arm64 listing of `analyzeFrame<unsigned char>` at
// 0x8ac..0x900).
static_assert(sizeof(Params) == 16, "the shipped Params is four floats");
static_assert(offsetof(Params, keyR) == 0, "Params::keyR is at +0x0");
static_assert(offsetof(Params, keyG) == 4, "Params::keyG is at +0x4");
static_assert(offsetof(Params, keyB) == 8, "Params::keyB is at +0x8");
static_assert(offsetof(Params, sensitivity) == 12, "Params::sensitivity is at +0xc");

// `Stats` — the shipped body zeroes twenty bytes through the hidden return
// pointer (0x808/0x80c) and then stores uniformity at +0x0, the mean spill at
// +0x4, p5 and p95 as ONE eight-byte store at +0x8 (0x1270: `str d0, [x21, #0x8]`)
// and the keyed fraction at +0x10 (0xda8).
static_assert(sizeof(Stats) == 20, "the shipped Stats is twenty bytes");
static_assert(offsetof(Stats, uniformity) == 0, "Stats::uniformity is at +0x0");
static_assert(offsetof(Stats, meanSpill) == 4, "Stats::meanSpill is at +0x4");
static_assert(offsetof(Stats, p5) == 8, "Stats::p5 is at +0x8");
static_assert(offsetof(Stats, p95) == 12, "Stats::p95 is at +0xc");
static_assert(offsetof(Stats, keyedFraction) == 16, "Stats::keyedFraction is at +0x10");

/** [inferred] The view mode `renderView` dispatches on.
 *
 *  The enumerator NAMES are unrecoverable — the shipped bundle passes this type
 *  by value and the mangling records only `ViewMode` — so the names below are
 *  inventions chosen to describe what each value does. The VALUES are exact: they
 *  are the six integers the shipped dispatch compares against, read from the
 *  arm64 listing of `renderView<unsigned char>` at 0x2654, 0x279c, 0x27a4, 0x27ac,
 *  0x282c and 0x2834.
 *
 *  `ModeDefault` is also what an out-of-range mode falls back to, and it is what
 *  mode 0 does.
 */
enum ViewMode {
  ModeDefault = 0,   // pass the source colour through, scaled
  ModeRamp = 1,      // opacity ramp driven by the alpha buffer
  ModeDespill = 2,   // despill driven by the spill buffer
  ModeGradient = 3,  // edge-aware ramp, driven by the alpha buffer's gradient
  ModeMatte = 4,     // flat grey matte driven by the alpha buffer
  ModeVerbatim = 5,  // copy the source through untouched
};

// [exact] The per-frame statistics pass.
//
// Reads the frame, works out which pixels are inside the key's chroma radius,
// and returns the aggregate statistics. `outDistances` and `outScores` receive
// one float per pixel when they are non-null.
//
// The signature is [exact] — taken from the symbol mangling
// `_ZN9chromakey12analyzeFrameIhEENS_5StatsERKNS_4ViewIKT_EEfRKNS_6ParamsEPfSB_`
// and its `It` sibling.
//
// [behavioural] — transcribed from the shipped instruction stream (arm64 listing
// `/tmp/ae/chromakeyassist.asm`: `analyzeFrame<unsigned char>` body 0x7d8-0x12f8,
// 752 instructions; `analyzeFrame<unsigned short>` body 0x19b4-0x25bc, 770), but
// NOT instruction-identical: the differ reports shipped=752 mine=703 for the 8-bit
// instantiation and shipped=770 mine=708 for the 16-bit one.
//
// WHAT IT DOES, in the order the listing runs it:
//
//   * the returned Stats is zeroed first (0x808/0x80c);
//   * a width or height below 1 returns it as it stands, and neither output
//     buffer is touched (0x810..0x828 -> ret 0x12cc);
//   * the threshold comes from `sensitivityToThreshold` (0x840..0x874);
//   * the KEY colour is converted to luma and chroma WITHOUT being divided by
//     the scale (0x8ac..0x900), while every PIXEL channel is divided by the
//     scale first (0x988/0x98c/0x998) — an asymmetry that is easy to miss;
//   * pass one computes each pixel's chroma distance, whether it is inside the
//     threshold (0x91c..0xa0c), and the mean luma of the KEYED pixels, or 0.5
//     when nothing is keyed (0xa10..0xa28);
//   * pass two scores every pixel that is NOT keyed (0xb38..0xd94); a keyed
//     pixel scores zero and its distance joins the keyed set;
//   * the keyed fraction is the keyed count over the pixel count (0xd98) and the
//     mean spill is the mean score over the pixels that are NOT keyed
//     (0xdac..0xdc4);
//   * the uniformity is `uniformityScore` over the KEYED distances only
//     (0xdc8..0x110c);
//   * p5 and p95 are percentiles over ALL the distances, with a FUSED
//     multiply-add for the interpolation (0x1110..0x1238), then divided by
//     sqrt(2) and multiplied by 100, clamped to [0, 100] (0x123c..0x1270);
//   * the two optional output buffers are copied out (0x1274..0x129c).
//
// The NUMBERS are exact. `test/chromakey.test.mjs` dlopens the installed
// ChromaKeyAssist bundle, calls the shipped `analyzeFrame` at its own body
// offsets (0x7d8 and 0x19b4) IN PLACE, and compares the returned Stats AND
// both output buffers byte-for-byte against this implementation over 8100
// randomised and edge-cased frames per width. Measured: `an_mism8=0`,
// `an_statmism8=0`, `an_mism16=0`, `an_statmism16=0` over 16200 vectors.
//
// The harness is mutation-sensitive, and the zero is a measurement rather than
// a stub: changing the 5th-percentile fraction (0.05f -> 0.06f) in this source
// makes the same harness report 1110 (8-bit) and 1107 (16-bit) mismatches, and
// changing the achromatic shortcut from `||` to `&&` makes it report 962 and
// 1036.
template <typename T>
Stats analyzeFrame(const View<const T>& src, float scale, const Params& params,
                    float* outDistances, float* outScores);

// [exact] The key output.
//
// Renders `src` into `dst` according to `mode`. `alpha` and `spill` are optional
// per-pixel buffers of `W * H` floats each (one row of `W` floats per row);
// `alpha` is the key matte and `spill` is the despill weight.
//
// [behavioural] — transcribed from the shipped instruction stream (arm64 listing
// `/tmp/ae/chromakeyassist.asm`: `renderView<unsigned char>` body 0x25bc-0x2b24,
// 347 instructions; `renderView<unsigned short>` body 0x2be4-0x3158, 350), but
// NOT instruction-identical: the differ reports shipped=347 mine=368 for the 8-bit
// instantiation and shipped=350 mine=363 for the 16-bit one.
//
// The NUMBERS are exact. `test/chromakey.test.mjs` dlopens the installed
// ChromaKeyAssist bundle, calls the shipped `renderView` at its own exported
// offsets, and compares the whole destination buffer byte-for-byte against this
// implementation over randomised and edge-cased frames. The harness is
// mutation-sensitive: changing one constant in this source (the mode-2 spill
// threshold, 0.02f -> 0.03f) makes it report 35 and 14 mismatches respectively.
template <typename T>
void renderView(const View<const T>& src, View<T>& dst, float scale,
                const Params& params, ViewMode mode, float mix,
                const float* alpha, const float* spill);

}  // namespace chromakey
