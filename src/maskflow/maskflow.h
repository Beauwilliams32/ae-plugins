// maskflow.h — reconstructed from the compiled MaskFlow bundle.
//
// Every declaration below is copied from the demangled Mach-O symbol table of
// MaskFlow.plugin, not from source: the original C++ was destroyed on 2026-10-13 with no
// remote and no backup.
//
// INCOMPLETE — the geometry helpers are here and the matte is not.
//   recovered: clampf, edgeCostScaleFromTolerance, smoothstep, computeDownsampleDims
//   outstanding: downsampleBoxFilter (126), upsampleBilinear (168), buildMatte (169),
//              computeMatteFullRes (151), computeArrivalField (290)
//
// The five outstanding functions all take `Image`, `Params` or `ArrivalField`, none of
// which is recovered yet — which is why they are absent rather than stubbed. The four
// here take nothing but ints and floats.

#pragma once
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

}  // namespace maskflow
