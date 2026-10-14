// chromakey.h — reconstructed from the compiled ChromaKeyAssist bundle.
//
// Every declaration below is copied from the demangled Mach-O symbol table of
// ChromaKeyAssist.plugin (bundle id com.williamsmedia.aftereffects.ChromaKeyAssist),
// not from source: the original C++ was destroyed on 2026-10-13 with no remote and
// no backup.
//
// INCOMPLETE — the scalar and statistics layer is here, and the four per-pixel or
// host functions are not. `tools/codegen-diff.mjs chromakey` names what is missing,
// and test/chromakey.test.mjs asserts that the recovered set and the outstanding set
// it reports agree with what this file says.
//
// RECOVERED HERE (9 functions, 217 instructions):
//   clampf, rgbToYCbCr, sensitivityToThreshold, chromaDistance, hueAngle,
//   angularDiff, percentile, percentileUnsorted, gradientMagnitude
//
// NOT RECOVERED:
//   analyzeFrame<unsigned char>  (752)  — the per-frame statistics pass
//   analyzeFrame<unsigned short> (770)  — the same, 16-bit
//   renderView<unsigned char>    (347)  — the key output
//   renderView<unsigned short>   (350)  — the same, 16-bit
//   StoreReadout(PF_InData*, const Stats&) (70) — host integration, and it must
//     stay out of `src/`: it takes a `PF_InData*`, which is an Adobe SDK type, and
//     this directory holds transcriptions of the shipped DSP only.
//
// SIGNATURE FIDELITY: all nine are [exact] — taken verbatim from symbol mangling.
// `RGB` and `YCbCr` are [inferred] layouts, each established from the offsets the
// functions themselves use, and pinned by static_assert.

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

// [exact] The same statistic for a caller who has not sorted anything: takes the
// vector BY VALUE and sorts its own copy, which is why the name says "unsorted".
//
// Differs from `percentile` in exactly one place: a negative `p` clamps to 0 here
// rather than being carried into the index arithmetic. Both end up returning the
// minimum, by different routes.
float percentileUnsorted(std::vector<float> samples, float p);

// [exact] Gradient magnitude of a scalar field at (x, y):
//
//   gx = (field(ym1, xp1) - field(ym1, xm1)) / max(xp1 - xm1, 1)
//   gy = (field(yp1, x)   - field(ym1, x))   / max(yp1 - ym1, 1)
//   return sqrt(gx*gx + gy*gy)
//
// where xm1/xp1 and ym1/yp1 are the coordinates clamped to [0, width-1] and
// [0, height-1]. Note the asymmetry, which is the shipped behaviour and not a
// transcription slip: the horizontal difference is taken on the row ABOVE (ym1)
// while the vertical difference is taken on the centre COLUMN, so the kernel is
// anchored at the top-left rather than centred. Both denominators are floored at
// 1, which is what stops a degenerate neighbourhood from dividing by zero.
//
// `rowStride` is in ELEMENTS, not bytes: the code scales it by 4 to reach a row,
// whatever the original parameter was called. A null field, or a width or height
// below 1, returns 0.
float gradientMagnitude(const float* field, int width, int height, long rowStride, int x, int y);

}  // namespace chromakey
