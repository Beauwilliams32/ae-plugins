// motioncurvex.cpp — implementations for the MotionCurveX core (row AE-06).
//
// From scratch, not a transcription: the signatures are exact (see the header), the bodies are
// written here and their required behaviour is asserted in selfcheck.cpp. Where a choice could have
// gone either way the header states the choice, and each one exists for a reason a stabiliser cares
// about — an odd frame, an empty pyramid level, a flat SAD surface, an outlier frame.

#include "motioncurvex.h"

#include <algorithm>
#include <cmath>

namespace motioncurve {

float clampf(float value, float lo, float hi) {
  // The fleet's recovered idiom, not a min/max pair: with lo > hi this returns `lo`, which is what
  // the transcribed copies do (lutblend's self-check pins the inverted case for exactly that reason).
  return value < lo ? lo : (value > hi ? hi : value);
}

float quadraticPeakOffset(float before, float at, float after) {
  // Vertex of the parabola through (-1, before), (0, at), (1, after):
  //   offset = 0.5 * (before - after) / (before - 2*at + after)
  // For a SAD surface the peak is a MINIMUM, so the curvature is positive; a flat or
  // upward-opening (negative) curvature has no interior vertex to report, and 0 keeps the
  // integer estimate rather than inventing half a pixel.
  const float curvature = before - 2.0f * at + after;
  if (!(curvature > 1e-12f)) return 0.0f;
  const float offset = 0.5f * (before - after) / curvature;
  return clampf(offset, -0.5f, 0.5f);
}

// The shipped bundle carries this one in an ANONYMOUS namespace
// (`motioncurve::(anonymous namespace)::medianOfWindow`), so the internal copy lives there and the
// public entry point below delegates to it. That is not cosmetic: the codegen differ matches on the
// demangled symbol, and an internal-linkage copy is what the original had.
namespace {

float medianOfWindowInternal(const std::vector<float>& values, int start, int count) {
  if (values.empty() || count <= 0) return 0.0f;
  // `start` is clamped into the vector FIRST, then `count` elements are taken from there — so a
  // negative start reads from the beginning rather than returning nothing.
  const int size = static_cast<int>(values.size());
  const int begin = std::min(std::max(0, start), size);
  const int end = std::min(size, begin + count);
  if (end <= begin) return 0.0f;
  std::vector<float> window(values.begin() + begin, values.begin() + end);
  std::sort(window.begin(), window.end());
  // UPPER middle for an even count, as the recovered beatsnap::medianOfWindow does: count/2.
  return window[window.size() / 2];
}

}  // namespace

float medianOfWindow(const std::vector<float>& values, int start, int count) {
  return medianOfWindowInternal(values, start, count);
}

Image downsample2x(const Image& in) {
  Image out;
  if (in.width < 2 || in.height < 2) return out;
  if (in.pixels.size() != static_cast<size_t>(in.width) * static_cast<size_t>(in.height)) return out;
  out.width = in.width / 2;
  out.height = in.height / 2;
  out.pixels.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height));
  for (int y = 0; y < out.height; ++y) {
    for (int x = 0; x < out.width; ++x) {
      const size_t x0 = static_cast<size_t>(2 * x);
      const size_t y0 = static_cast<size_t>(2 * y);
      const size_t row0 = y0 * static_cast<size_t>(in.width);
      const size_t row1 = row0 + static_cast<size_t>(in.width);
      const float sum = in.pixels[row0 + x0] + in.pixels[row0 + x0 + 1] +
                        in.pixels[row1 + x0] + in.pixels[row1 + x0 + 1];
      out.pixels[static_cast<size_t>(y) * static_cast<size_t>(out.width) + static_cast<size_t>(x)] = sum * 0.25f;
    }
  }
  return out;
}

std::vector<Image> buildPyramid(const Image& source, int levels) {
  std::vector<Image> pyramid;
  if (levels < 1) return pyramid;
  pyramid.reserve(static_cast<size_t>(levels));
  pyramid.push_back(source);
  for (int level = 1; level < levels; ++level) {
    const Image next = downsample2x(pyramid.back());
    // Stop early rather than pushing an empty level: a caller walking the pyramid wants every
    // level it is handed to be usable.
    if (next.width < 1 || next.height < 1) break;
    pyramid.push_back(next);
  }
  return pyramid;
}

float sadAt(const Image& a, const Image& b, int dx, int dy) {
  if (a.width <= 0 || a.height <= 0 || b.width <= 0 || b.height <= 0) return 0.0f;
  if (a.pixels.size() != static_cast<size_t>(a.width) * static_cast<size_t>(a.height)) return 0.0f;
  if (b.pixels.size() != static_cast<size_t>(b.width) * static_cast<size_t>(b.height)) return 0.0f;

  // Overlap in a's coordinate space: b sampled at (x + dx, y + dy).
  const int x0 = std::max(0, -dx);
  const int x1 = std::min(a.width, b.width - dx);
  const int y0 = std::max(0, -dy);
  const int y1 = std::min(a.height, b.height - dy);
  if (x1 <= x0 || y1 <= y0) return 0.0f;

  float total = 0.0f;
  for (int y = y0; y < y1; ++y) {
    const size_t aRow = static_cast<size_t>(y) * static_cast<size_t>(a.width);
    const size_t bRow = static_cast<size_t>(y + dy) * static_cast<size_t>(b.width);
    for (int x = x0; x < x1; ++x) {
      const float diff = a.pixels[aRow + static_cast<size_t>(x)] - b.pixels[bRow + static_cast<size_t>(x + dx)];
      total += std::fabs(diff);
    }
  }
  return total;
}

Image cropCenter(const Image& in, float fraction) {
  Image out;
  if (in.width <= 0 || in.height <= 0) return out;
  if (in.pixels.size() != static_cast<size_t>(in.width) * static_cast<size_t>(in.height)) return out;
  const float f = clampf(fraction, 0.0f, 1.0f);
  const int w = std::max(1, std::min(in.width, static_cast<int>(std::lround(static_cast<float>(in.width) * f))));
  const int h = std::max(1, std::min(in.height, static_cast<int>(std::lround(static_cast<float>(in.height) * f))));
  const int x0 = (in.width - w) / 2;   // the extra pixel of an odd remainder stays on the right
  const int y0 = (in.height - h) / 2;
  out.width = w;
  out.height = h;
  out.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
  for (int y = 0; y < h; ++y) {
    const size_t src = static_cast<size_t>(y0 + y) * static_cast<size_t>(in.width) + static_cast<size_t>(x0);
    const size_t dst = static_cast<size_t>(y) * static_cast<size_t>(w);
    std::copy_n(in.pixels.begin() + static_cast<std::ptrdiff_t>(src), static_cast<std::ptrdiff_t>(w),
                out.pixels.begin() + static_cast<std::ptrdiff_t>(dst));
  }
  return out;
}

std::vector<Vec2> accumulate(const std::vector<Vec2>& deltas) {
  std::vector<Vec2> path;
  path.reserve(deltas.size());
  Vec2 running{};
  for (const Vec2& delta : deltas) {
    running.x += delta.x;
    running.y += delta.y;
    path.push_back(running);
  }
  return path;
}

std::vector<Vec2> smoothMovingAverage(const std::vector<Vec2>& path, int window) {
  std::vector<Vec2> out(path.size());
  const int n = static_cast<int>(path.size());
  if (n == 0) return out;
  const int half = std::max(0, (std::max(1, window) - 1) / 2);
  for (int i = 0; i < n; ++i) {
    // Clamped window: the ends average what exists rather than being dropped, so the smoothed path
    // covers every frame the input did.
    const int begin = std::max(0, i - half);
    const int end = std::min(n - 1, i + half);
    Vec2 sum{};
    for (int j = begin; j <= end; ++j) {
      sum.x += path[static_cast<size_t>(j)].x;
      sum.y += path[static_cast<size_t>(j)].y;
    }
    const float count = static_cast<float>(end - begin + 1);
    out[static_cast<size_t>(i)] = Vec2{sum.x / count, sum.y / count};
  }
  return out;
}

std::vector<Vec2> temporalMedianFilter(const std::vector<Vec2>& path, int window) {
  std::vector<Vec2> out(path.size());
  const int n = static_cast<int>(path.size());
  if (n == 0) return out;
  const int half = std::max(0, (std::max(1, window) - 1) / 2);
  std::vector<float> xs;
  std::vector<float> ys;
  for (int i = 0; i < n; ++i) {
    const int begin = std::max(0, i - half);
    const int end = std::min(n - 1, i + half);
    xs.clear();
    ys.clear();
    for (int j = begin; j <= end; ++j) {
      xs.push_back(path[static_cast<size_t>(j)].x);
      ys.push_back(path[static_cast<size_t>(j)].y);
    }
    out[static_cast<size_t>(i)] = Vec2{medianOfWindowInternal(xs, 0, static_cast<int>(xs.size())),
                                       medianOfWindowInternal(ys, 0, static_cast<int>(ys.size()))};
  }
  return out;
}

Vec2 estimateTranslation(const Image& previous, const Image& current, float maxShift) {
  Vec2 estimate{};
  if (previous.width <= 0 || previous.height <= 0 || current.width <= 0 || current.height <= 0) return estimate;
  if (previous.width != current.width || previous.height != current.height) return estimate;
  if (previous.pixels.size() != static_cast<size_t>(previous.width) * static_cast<size_t>(previous.height)) return estimate;
  if (current.pixels.size() != static_cast<size_t>(current.width) * static_cast<size_t>(current.height)) return estimate;

  const int levels = 3;
  const std::vector<Image> previousPyramid = buildPyramid(previous, levels);
  const std::vector<Image> currentPyramid = buildPyramid(current, levels);
  const int top = static_cast<int>(previousPyramid.size()) - 1;
  if (top < 0) return estimate;

  // The estimate is always expressed in the units of the ALREADY-PROCESSED level. At the coarsest
  // level a displacement of N full-resolution pixels is only N / 2^top pixels, so the search bound
  // is divided by the same factor — otherwise the coarse search cannot see a pan it would see fine.
  const float scaling = static_cast<float>(1 << top);
  const int coarseLimit = std::max(1, static_cast<int>(std::ceil(std::fabs(maxShift) / scaling)));

  for (int level = top; level >= 0; --level) {
    const Image& prev = previousPyramid[static_cast<size_t>(level)];
    const Image& curr = currentPyramid[static_cast<size_t>(level)];

    // The coarsest level searches the whole bound; every finer level starts from twice the previous
    // estimate (one level down is two pixels for each pixel here) and searches one pixel around it.
    int centreX = 0;
    int centreY = 0;
    int span = coarseLimit;
    if (level != top) {
      centreX = static_cast<int>(std::lround(estimate.x * 2.0f));
      centreY = static_cast<int>(std::lround(estimate.y * 2.0f));
      span = 1;
    }

    float best = 0.0f;
    int bestX = centreX;
    int bestY = centreY;
    bool have = false;
    for (int dy = centreY - span; dy <= centreY + span; ++dy) {
      for (int dx = centreX - span; dx <= centreX + span; ++dx) {
        const float score = sadAt(prev, curr, dx, dy);
        // Strictly-less keeps the FIRST offset on a tie — smallest shift, earliest in scan order — so
        // a static scene reports exactly zero instead of a pixel of noise.
        if (!have || score < best) {
          best = score;
          bestX = dx;
          bestY = dy;
          have = true;
        }
      }
    }
    if (!have) break;

    const float subX = quadraticPeakOffset(sadAt(prev, curr, bestX - 1, bestY), best,
                                           sadAt(prev, curr, bestX + 1, bestY));
    const float subY = quadraticPeakOffset(sadAt(prev, curr, bestX, bestY - 1), best,
                                           sadAt(prev, curr, bestX, bestY + 1));
    estimate.x = static_cast<float>(bestX) + subX;
    estimate.y = static_cast<float>(bestY) + subY;
  }
  return estimate;
}

}  // namespace motioncurve
