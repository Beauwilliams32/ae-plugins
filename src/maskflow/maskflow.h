// maskflow.h — reconstructed from the compiled MaskFlow bundle.
//
// Every declaration below is copied from the demangled Mach-O symbol table of
// MaskFlow.plugin, not from source: the original C++ was destroyed on 2026-10-13 with no
// remote and no backup.
//
// INCOMPLETE — the geometry helpers are here and the matte is not.
//   recovered: clampf, edgeCostScaleFromTolerance, smoothstep, computeDownsampleDims,
//              downsampleBoxFilter (+ the Image/RGB layout it pinned), upsampleBilinear
//   outstanding: buildMatte (169), computeMatteFullRes (151), computeArrivalField (290)
//
// The three outstanding functions all take or return `Params` or `ArrivalField`, neither of
// which is recovered yet — which is why they are absent rather than stubbed:
//
//   buildMatte(const ArrivalField&, float, float, bool)
//   computeArrivalField(const Image&, float, float, float, bool)
//   computeMatteFullRes(const Image&, const Params&)
//
// The six here take nothing but ints, floats and a std::vector.

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

}  // namespace maskflow
