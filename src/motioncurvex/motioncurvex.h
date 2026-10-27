// motioncurvex.h — MotionCurveX (motion-stabilisation core), rebuilt from scratch.
//
// Row AE-06. Unlike the rest of the recovered fleet this module is NOT a transcription of shipped
// code: the original source was destroyed on 2026-10-13 and, for this bundle, only the SYMBOLS
// survived in readable form. What the binary does tell us exactly is the architecture —
// `specs/MotionCurveX.json` lists eleven `motioncurve::` functions with their demangled signatures,
// and those signatures are what this header declares. The bodies are written here, from the shape
// the signatures imply, and every behaviour they must have is asserted in `selfcheck.cpp`.
//
//     motioncurve::medianOfWindow(vector<float> const&, int, int)
//     motioncurve::accumulate(vector<Vec2> const&)
//     motioncurve::buildPyramid(Image const&, int)
//     motioncurve::clampf(float, float, float)
//     motioncurve::cropCenter(Image const&, float)
//     motioncurve::downsample2x(Image const&)
//     motioncurve::estimateTranslation(Image const&, Image const&, float)
//     motioncurve::quadraticPeakOffset(float, float, float)
//     motioncurve::sadAt(Image const&, Image const&, int, int)
//     motioncurve::smoothMovingAverage(vector<Vec2> const&, int)
//     motioncurve::temporalMedianFilter(vector<Vec2> const&, int)
//
// [exact] The names, parameters and return types above come from symbol mangling and are not
// guesses. [MODELLED] `Image` and `Vec2` are not: their layouts are not in any surviving symbol,
// so they are modelled as the simplest thing that supports the signatures, and the header says so
// rather than implying a recovery that did not happen.
//
// The pipeline the signatures describe is translation-only stabilisation, the classic block of a
// motion tracker:
//
//   1. `LumaFromWorld8` turns AE's 8-bit world into floats; `downsample2x` halves an image and
//      `buildPyramid` repeats it, so a large displacement becomes a small one at the coarsest level.
//   2. `sadAt` scores a candidate offset (sum of absolute differences over the overlapping area);
//      `estimateTranslation` searches that score and refines the winner to sub-pixel accuracy with
//      `quadraticPeakOffset`, walking from the coarsest pyramid level down, doubling each level's
//      estimate as the seed for the next.
//   3. `accumulate` turns per-frame deltas into a path, `smoothMovingAverage` (and, as an outlier
//      alternative, `temporalMedianFilter` over a window from `medianOfWindow`) smooths that path,
//      and the difference between the two is what a stabiliser would apply.
//
// Bounds matter more than they look: every function here either clamps its window to the vector or
// says what it does at the edges, because a tracker that reads one element past the end of a frame's
// motion data is the classic stabilisation crash.

#pragma once
#include <cstddef>
#include <vector>

namespace motioncurve {

// ---------------------------------------------------------------------------
// [MODELLED] A 2D vector in pixels. Positive x is right, positive y is DOWN, matching image
// coordinates and AE's own screen axes.
// ---------------------------------------------------------------------------
struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;
};

// ---------------------------------------------------------------------------
// [MODELLED] A single-channel float image: luma at the tracker's working precision.
//
// Rows are contiguous, `pixels.size() == width * height`, and a default-constructed Image is
// empty rather than 1x1. Every function in this module treats an inconsistent Image (size not
// equal to width*height) as empty rather than indexing past the end.
// ---------------------------------------------------------------------------
struct Image {
  int width = 0;
  int height = 0;
  std::vector<float> pixels;
};

// [exact] Scalar clamp, same branchless idiom as the other recovered namespaces.
float clampf(float value, float lo, float hi);

// [exact] Sub-pixel peak of a parabola through three samples, where `at` is the centre sample and
// `before`/`after` are its neighbours. Returns the offset in [-0.5, 0.5] `before to after`, and 0
// when the three points do not form a downward-opening peak (a flat or inverted triple has no
// meaningful vertex, and returning 0 leaves the integer estimate alone). Those two facts are the
// function's whole contract and are pinned in the self-check.
float quadraticPeakOffset(float before, float at, float after);

// [exact] Median of `count` values starting at `start`. The window is clamped to the vector, and an
// empty or entirely out-of-range window returns 0. An even count returns the UPPER middle (the
// same convention beatsnap::medianOfWindow was recovered with, whose notes state it explicitly),
// NOT the mean of the two middles.
float medianOfWindow(const std::vector<float>& values, int start, int count);

// [exact] Halve an image by 2x2 box averaging. An odd width or height drops the last column/row
// rather than blending across the edge, and an image smaller than 2x2 gives an empty result.
Image downsample2x(const Image& in);

// [exact] `levels` images, each half the previous, starting with `source` itself as level 0.
// `levels < 1` yields an empty vector; the pyramid stops early when an image would become empty.
std::vector<Image> buildPyramid(const Image& source, int levels);

// [exact] Sum of absolute differences between `a` and `b` after shifting `b` by (dx, dy), over the
// area the two images still share. Nothing overlapping (or an empty input) returns 0, which the
// caller reads as "this offset explains nothing" — the search rejects such offsets before relying
// on the score.
float sadAt(const Image& a, const Image& b, int dx, int dy);

// [exact] Crop the centred `fraction` of an image, e.g. 0.5 keeps the middle half of both axes.
// The fraction is clamped to (0, 1], the crop is at least 1x1, and the result is centred by integer
// division — a crop that cannot be perfectly centred keeps the extra pixel on the right/bottom.
Image cropCenter(const Image& in, float fraction);

// [exact] Cumulative sum: `out[i] = sum(deltas[0..i])`. The result has the same length as the
// input, and an empty input gives an empty result.
std::vector<Vec2> accumulate(const std::vector<Vec2>& deltas);

// [exact] Centred moving average over a window of `window` samples, with the window clamped at the
// ends (so the first and last samples average fewer points rather than being dropped). `window < 1`
// is treated as 1, i.e. a copy.
std::vector<Vec2> smoothMovingAverage(const std::vector<Vec2>& path, int window);

// [exact] Per-component median over a centred window of `window` samples, clamped at the ends.
// Unlike the moving average this is outlier-proof: one bad frame cannot drag the path. Ties take the
// upper middle, matching medianOfWindow.
std::vector<Vec2> temporalMedianFilter(const std::vector<Vec2>& path, int window);

// [exact] Translation between two frames, in pixels, with sub-pixel refinement.
//
// `maxShift` bounds the search at the coarsest level; each refinement level doubles the previous
// level's estimate. Ties are broken toward the smaller shift (the earlier offset in scan order),
// which keeps a static scene at exactly (0, 0) rather than drifting by a pixel of noise.
Vec2 estimateTranslation(const Image& previous, const Image& current, float maxShift);

}  // namespace motioncurve
