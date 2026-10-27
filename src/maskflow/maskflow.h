// maskflow.h — reconstructed from the compiled MaskFlow bundle.
//
// Every declaration below is copied from the demangled Mach-O symbol table of
// MaskFlow.plugin, not from source: the original C++ was destroyed on 2026-10-13 with no
// remote and no backup.
//
// NAMESPACE-COMPLETE: every `maskflow::` symbol in the bundle is transcribed, so this header is a
// full reconstruction rather than a partial one. The last one to land was `computeMatteFullRes`
// (151 instructions, 2026-10-27), which also pinned `Params` — the third and last struct. All nine
// are here; three compile instruction-identical to the bundle and the other six are one to three
// instructions off, so treat the BEHAVIOUR (pinned by 162 self-check assertions) as the recovered
// fact rather than the instruction stream.

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
// [inferred names, recovered layout] The effect's parameters, as `computeMatteFullRes` reads
// them. Every offset below is a load or store in that function; none is a guess about ORDER.
//
//   ldp s0, s1, [x22]        ; +0x00, +0x04 — the seed point, SCALED by the downsample factor
//   ldr s2,    [x22, #0x8]   ; +0x08 — the arrival tolerance, a percentage of the field's span
//   ldp s0, s1, [x22, #0xc]  ; +0x0c, +0x10 — the matte's threshold and its band's half width
//   ldrb w1,   [x22, #0x14]  ; +0x14 — invert
//   ldrb w1,   [x22, #0x15]  ; +0x15 — block across colour edges
//
// Only the first half width is scaled by the downsample factor, and only the band has a floor
// (0.01f, applied here on top of `buildMatte`'s own 1e-4): the two percentages are scale-free.
//
// The NAMES are not in the binary and the TAIL is not readable from this function — nothing
// touches +0x16 or beyond, so the model stops at 0x16 and pads to an 8-byte multiple. Sizes and
// order below 0x16 are exact.
// ---------------------------------------------------------------------------
struct Params {
  float seedColumn = 0.0f;        // +0x00
  float seedRow = 0.0f;           // +0x04
  float edgeTolerance = 100.0f;   // +0x08 — percentage; 100 is "ignore colour"
  float threshold = 50.0f;        // +0x0c — percentage of the field's span
  float band = 0.01f;             // +0x10 — scaled by the downsample factor, floored at 0.01
  bool invert = false;            // +0x14
  bool blockAcrossEdges = false;  // +0x15
  char reserved[2] = {0, 0};      // +0x16 — [inferred] padding to 0x18
};

static_assert(offsetof(Params, seedColumn) == 0x00, "the first float pair the listing loads");
static_assert(offsetof(Params, seedRow) == 0x04, "`ldp s0, s1, [x22]`");
static_assert(offsetof(Params, edgeTolerance) == 0x08, "`ldr s2, [x22, #0x8]`");
static_assert(offsetof(Params, threshold) == 0x0c, "the low half of `ldp s0, s1, [x22, #0xc]`");
static_assert(offsetof(Params, band) == 0x10, "the high half of the same pair");
static_assert(offsetof(Params, invert) == 0x14, "`ldrb w1, [x22, #0x14]`");
static_assert(offsetof(Params, blockAcrossEdges) == 0x15, "`ldrb w1, [x22, #0x15]`");
static_assert(sizeof(Params) == 0x18, "[inferred] 0x16 bytes of fields, padded to 8");

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

/**
 * [exact] The arrival-time field: a Dijkstra flood from one seed pixel over the 8-neighbourhood,
 * where crossing an edge costs the COLOUR DISTANCE between the two pixels. 290 instructions, read
 * in full (row AE-05, 2026-10-27).
 *
 *     seedRow = lround(clamp(row,    0, height-1))      ; fcvtas: ties AWAY from zero
 *     seedCol = lround(clamp(column, 0, width-1))
 *     values.assign(width*height, +infinity) ; values[seed] = 0 ; span = 0
 *
 *     colour = |from.rgb - to.rgb| * (1/sqrt3)          ; normalised, so 1.0 is a full-scale jump
 *     step   = k < 4 ? 1 : sqrt2                        ; the four 4-connected offsets come first
 *     cost   = step * (edgeCostScaleFromTolerance(tolerance) * colour + 1)
 *     if (blockAcrossEdges && colour > 0.45f) cost = +infinity
 *     candidate = here + cost ; keep it only if candidate < values[neighbour]
 *
 * `span` is the largest arrival the flood actually SETTLED, not the largest in the array — an
 * unreachable pixel keeps +infinity forever and is excluded, so `span` stays finite and is the
 * number `buildMatte` multiplies a tolerance by.
 *
 * The two guards that make the flood correct are the two that look redundant:
 *
 *   * `settled[i]` rejects a pixel reached by a better route after it was queued.
 *   * `here > values[i]` rejects a QUEUE ENTRY a later, cheaper push has made stale.
 *
 * Dropping either one expands pixels twice; dropping them together is a different algorithm.
 *
 * `blockAcrossEdges` REPLACES the cost with infinity rather than scaling it, and it is applied
 * after the multiply — so a block does not depend on the step length or the edge scale. +infinity
 * and NaN both fail the same `cost < infinity` test, which is how a NaN colour (rather than a
 * barrier) also abandons the edge.
 */
ArrivalField computeArrivalField(const Image& image, float column, float row, float tolerance,
                                 bool blockAcrossEdges);

/**
 * [exact] The entry point: downsample → arrival field → matte → upsample. 151 instructions, read
 * in full (row AE-05, 2026-10-27).
 *
 *     dims   = computeDownsampleDims(width, height, 640)      ; 640 is the cap, inlined
 *     small  = dims.width != width || dims.height != height
 *     src    = small ? downsampleBoxFilter(image, dims.width, dims.height) : image
 *     field  = computeArrivalField(src, scale * seedColumn, scale * seedRow,
 *                                  edgeTolerance, blockAcrossEdges)
 *     matte  = buildMatte(field, threshold, max(scale * band, 0.01f), invert)
 *     return small ? upsampleBilinear(matte, dims.width, dims.height, width, height) : matte
 *
 * Three things the scalings hide:
 *
 *   * The downscale cap is a CONSTANT 640, not a parameter — so a 641-pixel side is the threshold
 *     at which this function starts doing three extra passes.
 *   * The seed point is in FULL-RESOLUTION coordinates and is multiplied by the scale, so on a
 *     downscaled image it lands on the corresponding coarse pixel rather than being reinterpreted.
 *   * The band's half width is scaled but its 0.01f floor is applied AFTER the scale, so a
 *     downscaled matte has a relatively wider band than the full-resolution one it upsamples to.
 *     The threshold percentage is NOT scaled — it is a fraction of the field's own span, which
 *     the field already carries.
 *
 * The two percentages change meaning between the two calls: `edgeTolerance` scales the COLOUR
 * cost inside the flood (100 = free movement, 0 = edges cost 40× their length), while `threshold`
 * positions the matte's window inside the field (0 and ≥100 both collapse it — see `buildMatte`).
 */
std::vector<float> computeMatteFullRes(const Image& image, const Params& params);

}  // namespace maskflow
