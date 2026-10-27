// maskflow.h — reconstructed from the compiled MaskFlow bundle.
//
// Every declaration below is copied from the demangled Mach-O symbol table of
// MaskFlow.plugin, not from source: the original C++ was destroyed on 2026-10-13 with no
// remote and no backup.
//
// INCOMPLETE — the geometry helpers and the matte threshold are here; the field builder and
// the entry point are not.
//   recovered: clampf, edgeCostScaleFromTolerance, smoothstep, computeDownsampleDims,
//              downsampleBoxFilter (+ the Image/RGB layout it pinned), upsampleBilinear,
//              buildMatte (+ the ArrivalField layout it pinned)
//   outstanding: computeArrivalField (290), computeMatteFullRes (151)
//
// Those two need `Params`, which is not recovered yet — which is why they are absent rather
// than stubbed:
//
//   computeArrivalField(const Image&, float, float, float, bool)
//   computeMatteFullRes(const Image&, const Params&)
//
// Everything declared here takes nothing but ints, floats, a std::vector and the two structs
// whose layout the code itself pins.

#pragma once
#include <vector>
#include <cstddef>

namespace maskflow {

// ---------------------------------------------------------------------------
// [inferred] The result of a downsample decision.
//
// Only its SHAPE is recovered, from how the function returns it: the two ints leave in
// x0 packed as `(uint32)width | ((uint64)height << 32)` and the float rides in x1, so the
// struct is twelve bytes with the float last. Its NAME is not in the binary.
// ---------------------------------------------------------------------------

struct DownsampleDims {
  int width;
  int height;
  float scale;
};

static_assert(sizeof(DownsampleDims) == 12, "two ints in x0 and a float in x1");
static_assert(offsetof(DownsampleDims, height) == 4, "the second int is the upper half of x0");
static_assert(offsetof(DownsampleDims, scale) == 8, "the float is the start of x1");

// ---------------------------------------------------------------------------
// [exact] Three-way clamp. Shared with the fleet — and this bundle is a carrier the
// duplication check had not been scanning, because MaskFlow had no recovered namespace
// for `tools/namespaces.mjs` to map it through. Mapping it is what put this copy into
// that comparison.
// ---------------------------------------------------------------------------

float clampf(float value, float lo, float hi);

// ---------------------------------------------------------------------------
// [exact] The geometry helpers.
// ---------------------------------------------------------------------------

/**
 * [exact] A tolerance, 0..100, turned into an edge-cost scale that runs the other way:
 *
 *     tolerance >= 100 -> 0        tolerance <= 0 -> 40
 *     otherwise        -> (100 - tolerance) / 100 * 40
 *
 *     and a NEGATIVE tolerance short-circuits to 40 — the same value as 0 — rather than
 *     extrapolating past it.
 *
 * So a higher tolerance means a LOWER edge cost, which is the direction the name does not
 * give away.
 */
float edgeCostScaleFromTolerance(float tolerance);

/**
 * [exact] The classic smoothstep polynomial `t*t*(3 - 2t)` on
 * `t = (x - edge0) / (edge1 - edge0)`, with two details that a rewrite would lose:
 *
 *   * The clamp is ONE-SIDED. `t` is capped at 1 but is NOT floored at 0, so below
 *     `edge0` this EXTRAPOLATES: at t = -1 it returns 5, not 0. A `std::clamp(t, 0, 1)`
 *     is a different function.
 *   * When `edge0 == edge1` the division is degenerate and the function returns
 *     `x < edge0 ? t : 1.0f` instead of the polynomial — a branch on the equality of the
 *     two edges, not on the result.
 */
float smoothstep(float edge0, float edge1, float x);

/**
 * [exact] The dimensions to downsample to so the longest side fits `maxDim`, and the
 * scale that gets them there:
 *
 *     width < 1 or height < 1  -> { max(width,1), max(height,1), 1.0f }
 *     longest = max(width, height)
 *     scale   = longest > maxDim ? maxDim / longest : 1.0f
 *     -> { max(round(scale * width), 1), max(round(scale * height), 1), scale }
 *
 * The rounding is `fcvtas` — nearest, ties AWAY from zero — which is `lround`, not
 * banker's rounding. Both dimensions are floored at 1 rather than allowed to reach 0, so
 * a very small `maxDim` still names a real image.
 */
DownsampleDims computeDownsampleDims(int width, int height, int maxDim);

// ---------------------------------------------------------------------------
// [inferred name, recovered layout] The distance field `buildMatte` consumes.
//
// `buildMatte`'s own loads pin it: `ldpsw x8, x9, [x0]` takes a signed 32-bit width and
// height off the front, `ldr x9, [x21, #0x8]` reads the value buffer at +0x8, and
// `ldr s2, [x21, #0x20]` reads a float immediately after the 24-byte vector — so the struct
// is 40 bytes, 8-aligned. `computeArrivalField` writes that +0x20 float with `str s9, [x8,
// #0x20]` after a `std::vector<std::pair<float, size_t>>` front of (arrival, index) pairs, so
// the member is the LARGEST arrival value in the field; the name `span` is not in the binary.
// ---------------------------------------------------------------------------
struct ArrivalField {
  int width = 0;
  int height = 0;
  std::vector<float> values;   // one arrival time per pixel, row-major
  float span = 0.0f;           // [inferred name] the largest arrival value in `values`
};

static_assert(sizeof(ArrivalField) == 40, "two ints, a 24-byte vector, then one float");
static_assert(offsetof(ArrivalField, values) == 8, "the buffer pointer the listing loads at +0x8");
static_assert(offsetof(ArrivalField, span) == 0x20, "the float the listing loads at +0x20");

// ---------------------------------------------------------------------------
// [inferred names, recovered layout] A colour image: two ints then a vector.
//
// `downsampleBoxFilter` pins it: the width is at +0x0, the height at +0x4, the pixel buffer
// pointer at +0x8, and each RGB is 12 bytes (`mov w13, #0xc` is the stride multiplier). A row is
// therefore `width * 12` bytes, which the listing computes as `(w + w*2) * 4`.
// ---------------------------------------------------------------------------
struct RGB {
  float r;
  float g;
  float b;
};

struct Image {
  int width = 0;
  int height = 0;
  std::vector<RGB> pixels;
};

static_assert(sizeof(RGB) == 12, "the box filter strides rows by 12 bytes per pixel");

/**
 * [exact] Downsample by an integer box average: each output pixel is the mean of the source
 * rectangle that maps onto it. 126 instructions, read in full (row AE-05, 2026-10-27).
 *
 *     ow = max(outW, 1);  oh = max(outH, 1)
 *     out.resize(ow * oh, black)
 *     for oy: iy0 = (height * oy) / oh;  iy1 = (height * (oy+1)) / oh
 *             iyEnd = min(height, max(iy0 + 1, iy1))        ; so every row maps to >= 1 pixel
 *     for ox: the same in x, then the mean of src[ix0..ixEnd) x [iy0..iyEnd)
 *
 * The `+ 1` in the max is what makes the mapping TOTAL: with more output pixels than source ones a
 * rect can only be one pixel wide, never empty — and if it somehow is empty the divisor is floored
 * at 1 (`csinc`), so the pixel comes out BLACK rather than NaN.
 */
Image downsampleBoxFilter(const Image& src, int outW, int outH);

/**
 * [exact] The bilinear inverse of the box filter: `dstW x dstH` floats sampled from a
 * `srcW x srcH` plane at fractional positions. 168 instructions, read in full
 * (row AE-05, 2026-10-27).
 *
 *     sx = srcW >= 2 ? (srcW-1) / max(dstW-1, 1) : 0        ; a SOURCE-space step, not an
 *     sy = srcH >= 2 ? (srcH-1) / max(dstH-1, 1) : 0        ; output-space scale
 *     fx = sx * x; x0 = trunc(fx); wx = fx - x0; x1 = min(x0+1, srcW-1)
 *     top = a + (b-a)*wx        a,b = row y0 at x0,x1
 *     bot = c + (d-c)*wx        c,d = row y1 at x0,x1
 *     out = top + (bot - top)*wy
 *
 * The truncation is `fcvtzs` (toward zero), and only the `+1` neighbour is clamped — `x0`
 * itself is not, because the scale is built so the last output column lands exactly on
 * `srcW-1`. A 1x1 SOURCE broadcasts `src[0]`; a 1-wide or 1-high source falls through the
 * general loop, where the zero scale collapses the blend to a copy.
 *
 * Every guard returns `dstW*dstH` ZEROS rather than an empty vector: a non-positive
 * dimension (which also makes that product empty), or a source too short for the declared
 * `srcW*srcH`.
 */
std::vector<float> upsampleBilinear(const std::vector<float>& src, int srcW, int srcH, int dstW,
                                    int dstH);

/**
 * [exact] The field thresholded into a matte: `width*height` floats in 0..1. 169 instructions,
 * read in full (row AE-05, 2026-10-27).
 *
 *     tol  = tolerance > 100 ? 100 : tolerance ; then / 100
 *            ...and a NEGATIVE tolerance becomes 100.0f — not 1.0f — so it lands past the
 *            top of the range and takes the degenerate branch below
 *     half = max(halfWidth, 1e-4f)                  ; floored, so `den` is never zero by
 *                                                   ; construction — only by cancellation
 *     lo = tol*field.span - half ; hi = tol*field.span + half ; den = hi - lo
 *     degenerate = tol <= 0 || tol >= 1
 *
 *     per value v:
 *       special = v == +inf || isnan(v)              ; NOT -inf: that one flows through the
 *                                                   ; ramp and clamps to 0 like any tiny value
 *       fill    = (tol > 0 && !special) ? 1.0f : 0.0f
 *       if (special || degenerate) out = fill
 *       else if (lo == hi)         out = (v < lo) ? 0.0f : 1.0f     ; a hard step
 *       else                       out = t*t*(3-2t), t = clamp01((v - lo) / den)
 *       out = invert ? out : 1 - out
 *
 * Three consequences worth more than the arithmetic:
 *
 *   * The invert flag is applied to the *fill* as well as to the ramp, so a degenerate call
 *     with `invert` true is all ZEROS where it would otherwise be all ones, and the special
 *     values flip with it.
 *   * A negative tolerance does not clamp to the bottom of the range: it is the one input that
 *     makes `tol` exceed 1, which forces the degenerate branch and turns the whole matte into
 *     the fill — 1, or 0 for a `+inf`/NaN element.
 *   * `lo == hi` is unreachable for a normal `halfWidth` (the 1e-4 floor sees to that). It is
 *     there for CANCELLATION: with a `span` large enough relative to `half`, `span - half` and
 *     `span + half` round to the same float and `den` becomes zero. Where a hand-written
 *     version would divide by it, this one returns the step.
 *
 * The `values` buffer is NOT length-checked against `width*height` — it is indexed to that
 * count and trusted, so a short field reads past its end.
 */
std::vector<float> buildMatte(const ArrivalField& field, float tolerance, float halfWidth,
                              bool invert);

}  // namespace maskflow
