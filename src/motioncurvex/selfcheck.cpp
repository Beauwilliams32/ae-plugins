// selfcheck.cpp — property checks for the MotionCurveX core (row AE-06).
//
// Unlike the transcribed namespaces there is no shipped implementation to diff against, so this
// harness is the whole verification: every claim in motioncurvex.h is asserted here, on synthetic
// images whose answer is known analytically (a scene shifted by an exact number of pixels must be
// recovered to that number), plus the edge cases the header promises.

#include "motioncurvex.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "  PASS" : "  FAIL", what);
  if (!ok) ++failures;
}

void checkNear(float got, float want, float tol, const char* what) {
  const bool ok = std::fabs(got - want) <= tol;
  std::printf("%s %s  (got %.6g want %.6g)\n", ok ? "  PASS" : "  FAIL", what, got, want);
  if (!ok) ++failures;
}

// A deterministic textured scene: a few sinusoids at different frequencies, so it has real
// structure to track rather than a flat field where every offset scores the same.
motioncurve::Image scene(int w, int h) {
  motioncurve::Image img;
  img.width = w;
  img.height = h;
  img.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const float fx = static_cast<float>(x);
      const float fy = static_cast<float>(y);
      img.pixels[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] =
          0.5f + 0.3f * std::sin(fx * 0.35f) + 0.2f * std::cos(fy * 0.27f + 0.6f) +
          0.1f * std::sin((fx + fy) * 0.17f);
    }
  }
  return img;
}

// The same scene shifted by (dx, dy), sampled inside the same frame: pixel (x, y) takes the source
// value at (x - dx, y - dy), and the uncovered border repeats the edge (a flat pad has no texture to
// attract the matcher).
motioncurve::Image shifted(const motioncurve::Image& src, int dx, int dy) {
  motioncurve::Image out;
  out.width = src.width;
  out.height = src.height;
  out.pixels.resize(src.pixels.size());
  for (int y = 0; y < src.height; ++y) {
    for (int x = 0; x < src.width; ++x) {
      int sx = x - dx;
      int sy = y - dy;
      if (sx < 0) sx = 0;
      if (sy < 0) sy = 0;
      if (sx >= src.width) sx = src.width - 1;
      if (sy >= src.height) sy = src.height - 1;
      out.pixels[static_cast<size_t>(y) * static_cast<size_t>(out.width) + static_cast<size_t>(x)] =
          src.pixels[static_cast<size_t>(sy) * static_cast<size_t>(src.width) + static_cast<size_t>(sx)];
    }
  }
  return out;
}

// An INDEPENDENT reference implementation of the rule the bundle's anonymous-namespace
// `medianOfWindow` implements: the window is the CLOSED range [centre - radius, centre + radius]
// (so `2 * radius + 1` samples, an odd count), the sample index is clamped into the vector at
// BOTH ends (the edge sample is REPEATED, not dropped), and the median is the true middle of the
// window.
//
// It is written with `std::sort` rather than `std::nth_element` so that it shares no code with
// the implementation under test: a reference that used the same algorithm could agree with a
// wrong implementation for the wrong reason. It is only ever used with `radius >= 1`, which is
// the range the shipped helper is defined over.
float referenceMedianOfWindow(const std::vector<float>& values, int centre, int radius) {
  const int last = static_cast<int>(values.size()) - 1;
  std::vector<float> window;
  for (int i = centre - radius; i <= centre + radius; ++i) {
    int index = i < 0 ? 0 : i;
    if (index > last) index = last;
    window.push_back(values[static_cast<size_t>(index)]);
  }
  std::sort(window.begin(), window.end());
  return window[window.size() / 2];
}

// The same rule applied to a whole path, per axis, which is exactly how the bundle's
// `temporalMedianFilter` uses the helper.
std::vector<motioncurve::Vec2> referenceTemporalMedianFilter(
    const std::vector<motioncurve::Vec2>& path, int window) {
  if (window < 1 || path.size() <= 1) return path;
  const int last = static_cast<int>(path.size()) - 1;
  std::vector<motioncurve::Vec2> out(path.size());
  for (size_t i = 0; i < path.size(); ++i) {
    std::vector<float> xs;
    std::vector<float> ys;
    for (int j = static_cast<int>(i) - window; j <= static_cast<int>(i) + window; ++j) {
      int index = j < 0 ? 0 : j;
      if (index > last) index = last;
      xs.push_back(path[static_cast<size_t>(index)].x);
      ys.push_back(path[static_cast<size_t>(index)].y);
    }
    std::sort(xs.begin(), xs.end());
    std::sort(ys.begin(), ys.end());
    out[i].x = xs[xs.size() / 2];
    out[i].y = ys[ys.size() / 2];
  }
  return out;
}

// A deterministic pseudo-random source, so the randomised comparison below is reproducible and
// does not depend on the platform's `rand()`.
class Lcg {
 public:
  explicit Lcg(uint32_t seed) : state_(seed) {}
  uint32_t next() {
    state_ = state_ * 1664525u + 1013904223u;
    return state_;
  }
  float unit() { return static_cast<float>(next() >> 8) / 16777216.0f; }  // [0, 1)
  int below(int n) { return static_cast<int>(next() % static_cast<uint32_t>(n)); }

 private:
  uint32_t state_;
};

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  std::printf("motioncurve::clampf\n");
  checkNear(motioncurve::clampf(5.0f, 0.0f, 10.0f), 5.0f, 0.0f, "mid value passes through");
  checkNear(motioncurve::clampf(-3.0f, 0.0f, 10.0f), 0.0f, 0.0f, "below lo clamps to lo");
  checkNear(motioncurve::clampf(99.0f, 0.0f, 10.0f), 10.0f, 0.0f, "above hi clamps to hi");
  checkNear(motioncurve::clampf(-1.0f, 2.0f, 1.0f), 2.0f, 0.0f, "inverted range: lo wins");

  std::printf("motioncurve::quadraticPeakOffset\n");
  // A parabola with its vertex exactly at the centre sample.
  checkNear(motioncurve::quadraticPeakOffset(4.0f, 1.0f, 4.0f), 0.0f, 1e-6f, "symmetric triple has no offset");
  // Vertex shifted toward `before`: f(-1)=1, f(0)=0, f(1)=3 -> offset = 0.5*(1-3)/(1+3) = -0.25
  checkNear(motioncurve::quadraticPeakOffset(1.0f, 0.0f, 3.0f), -0.25f, 1e-6f, "toward the before sample");
  // The mirror case lands on +0.25.
  checkNear(motioncurve::quadraticPeakOffset(3.0f, 0.0f, 1.0f), 0.25f, 1e-6f, "toward the after sample");
  // A flat triple has no interior vertex.
  checkNear(motioncurve::quadraticPeakOffset(2.0f, 2.0f, 2.0f), 0.0f, 1e-6f, "flat triple reports no offset");
  // An upward-opening triple (a maximum) is not a minimum to refine.
  checkNear(motioncurve::quadraticPeakOffset(1.0f, 3.0f, 1.0f), 0.0f, 1e-6f, "upward-opening triple reports no offset");
  // The result never leaves the half-pixel the integer estimate covered.
  const float extreme = motioncurve::quadraticPeakOffset(0.0f, 100.0f, 100.001f);
  check(extreme >= -0.5f && extreme <= 0.5f, "the offset is always inside [-0.5, 0.5]");

  std::printf("motioncurve::(anonymous namespace)::medianOfWindow, through temporalMedianFilter\n");
  {
    // The bundle's eleventh symbol is INTERNAL: there is no public `motioncurve::medianOfWindow`
    // to call, so every check below goes through `temporalMedianFilter`, which is the helper's
    // only caller in the bundle.
    //
    // The values are chosen so that the two plausible readings of the window DISAGREE. The
    // window is CLOSED - `2 * radius + 1` samples - and the median is the true middle of it.
    const std::vector<float> v{10.0f, 90.0f, 50.0f, 20.0f, 80.0f, 5.0f};
    std::vector<motioncurve::Vec2> path;
    for (float x : v) path.push_back(motioncurve::Vec2{x, 0.0f});

    // radius 1 about sample 2 is the closed window {90, 50, 20} -> 50. A half-open window of
    // two samples would take the UPPER middle of {90, 50} -> 90, so this distinguishes them.
    const std::vector<motioncurve::Vec2> w1 = motioncurve::temporalMedianFilter(path, 1);
    checkNear(w1[2].x, 50.0f, 0.0f, "the window is closed: three samples, the true middle");
    checkNear(referenceMedianOfWindow(v, 2, 1), 50.0f, 0.0f,
              "the independent reference agrees on the same window");

    // The radius is the `window` ARGUMENT ITSELF, not half of it: a radius of zero would leave
    // the spike in place, a radius of one removes it.
    std::vector<motioncurve::Vec2> spike(5, motioncurve::Vec2{0.0f, 0.0f});
    spike[2] = motioncurve::Vec2{100.0f, -100.0f};
    const std::vector<motioncurve::Vec2> filtered = motioncurve::temporalMedianFilter(spike, 1);
    checkNear(filtered[2].x, 0.0f, 0.0f, "the radius is the window itself, not half of it");
    checkNear(filtered[2].y, 0.0f, 0.0f, "and the two axes are filtered independently");

    // The window really holds 2*window+1 samples. radius 2 about sample 2 of
    // {90, 80, 10, 20, 30} is the closed window of five -> 30; a window of four would give the
    // upper middle of {90, 80, 10, 20} -> 80.
    std::vector<motioncurve::Vec2> wide;
    for (float x : {90.0f, 80.0f, 10.0f, 20.0f, 30.0f, 5.0f, 5.0f, 5.0f}) {
      wide.push_back(motioncurve::Vec2{x, x});
    }
    const std::vector<motioncurve::Vec2> w2 = motioncurve::temporalMedianFilter(wide, 2);
    checkNear(w2[2].x, 30.0f, 0.0f, "a window of two holds five samples, not four");

    // Both ends of the path CLAMP: the edge sample is REPEATED, not dropped. At sample 0 the
    // window is {-1, 0, 1} -> {10, 10, 90} -> 10; dropping the out-of-range sample would give
    // the upper middle of {10, 90} -> 90.
    checkNear(w1[0].x, 10.0f, 0.0f, "the left end clamps by repeating the first sample");
    // At the last sample the window is {4, 5, 6} -> {80, 5, 5} -> 5; dropping would give 80.
    checkNear(w1[5].x, 5.0f, 0.0f, "the right end clamps by repeating the last sample");

    // A window below 1, and a path of one point or fewer, copy the path straight through.
    const std::vector<motioncurve::Vec2> zero = motioncurve::temporalMedianFilter(path, 0);
    check(zero.size() == path.size() && zero[0].x == path[0].x && zero[3].x == path[3].x,
          "a window of zero copies the path through");
    const std::vector<motioncurve::Vec2> single{motioncurve::Vec2{7.0f, 8.0f}};
    const std::vector<motioncurve::Vec2> one = motioncurve::temporalMedianFilter(single, 3);
    check(one.size() == 1 && one[0].x == 7.0f && one[0].y == 8.0f, "a path of one point is copied");
    check(motioncurve::temporalMedianFilter({}, 3).empty(), "an empty path stays empty");

    // The implementation and the INDEPENDENT reference agree bit-for-bit over a table of cases.
    const int windows[] = {1, 2, 3, 5};
    bool tableAgrees = true;
    for (int window : windows) {
      const std::vector<motioncurve::Vec2> got = motioncurve::temporalMedianFilter(path, window);
      const std::vector<motioncurve::Vec2> want = referenceTemporalMedianFilter(path, window);
      if (got.size() != want.size()) { tableAgrees = false; break; }
      for (size_t i = 0; i < got.size(); ++i) {
        if (got[i].x != want[i].x || got[i].y != want[i].y) { tableAgrees = false; break; }
      }
    }
    check(tableAgrees, "the implementation matches the independent reference over a table");

    // ...and over a randomised sweep, bit-for-bit.
    Lcg rng(20261028u);
    bool sweepAgrees = true;
    int mismatches = 0;
    for (int trial = 0; trial < 400; ++trial) {
      const int n = 1 + rng.below(40);
      const int window = 1 + rng.below(9);
      std::vector<motioncurve::Vec2> p;
      for (int i = 0; i < n; ++i) {
        p.push_back(motioncurve::Vec2{rng.unit() * 100.0f - 50.0f, rng.unit() * 100.0f - 50.0f});
      }
      const std::vector<motioncurve::Vec2> got = motioncurve::temporalMedianFilter(p, window);
      const std::vector<motioncurve::Vec2> want = referenceTemporalMedianFilter(p, window);
      if (got.size() != want.size()) { sweepAgrees = false; break; }
      for (size_t i = 0; i < got.size(); ++i) {
        if (got[i].x != want[i].x || got[i].y != want[i].y) { ++mismatches; sweepAgrees = false; }
      }
    }
    check(sweepAgrees, "the implementation matches the independent reference over 400 random paths");
    check(mismatches == 0, "and not one sample differed");
  }

  std::printf("motioncurve::downsample2x / buildPyramid\n");
  {
    const motioncurve::Image img = scene(9, 7);   // odd both ways on purpose
    const motioncurve::Image half = motioncurve::downsample2x(img);
    check(half.width == 4 && half.height == 3, "an odd size drops the last column and row");
    check(half.pixels.size() == 12, "and the payload matches");
    // 2x2 box average, checked on the first output pixel by hand.
    const float want = 0.25f * (img.pixels[0] + img.pixels[1] + img.pixels[9] + img.pixels[10]);
    checkNear(half.pixels[0], want, 1e-6f, "each output pixel is the mean of its 2x2 block");

    const motioncurve::Image tiny = motioncurve::downsample2x(scene(1, 1));
    check(tiny.width == 0 && tiny.height == 0 && tiny.pixels.empty(), "smaller than 2x2 gives an empty image");
    const motioncurve::Image ragged = motioncurve::downsample2x(motioncurve::Image{4, 4, {1.0f, 2.0f}});
    check(ragged.pixels.empty(), "an inconsistent image is treated as empty, not indexed past the end");

    const std::vector<motioncurve::Image> pyramid = motioncurve::buildPyramid(scene(32, 32), 4);
    check(pyramid.size() == 4, "levels are honoured");
    check(pyramid[0].width == 32 && pyramid[1].width == 16 && pyramid[3].width == 4, "each level halves the last");
    check(motioncurve::buildPyramid(scene(32, 32), 0).empty(), "levels < 1 is an empty pyramid");
    check(motioncurve::buildPyramid(scene(2, 2), 6).size() == 2, "the pyramid stops rather than pushing empty levels");
  }

  std::printf("motioncurve::sadAt\n");
  {
    const motioncurve::Image a = scene(16, 16);
    checkNear(motioncurve::sadAt(a, a, 0, 0), 0.0f, 0.0f, "an image against itself at zero shift is 0");
    const motioncurve::Image b = shifted(a, 0, 0);
    checkNear(motioncurve::sadAt(a, b, 0, 0), 0.0f, 1e-6f, "an identically built pair scores 0");
    // Shifting the SECOND image by the known offset makes the pair line up: this is the property the
    // search depends on, so it is checked directly rather than inferred from estimateTranslation.
    const motioncurve::Image moved = shifted(a, 3, -2);
    const float atWrong = motioncurve::sadAt(a, moved, 0, 0);
    const float atRight = motioncurve::sadAt(a, moved, 3, -2);
    check(atRight < atWrong, "the true offset scores lower than the wrong one");
    checkNear(atRight, 0.0f, 1e-5f, "and is exactly zero for an integer shift of a padded scene");
    checkNear(motioncurve::sadAt(motioncurve::Image{}, moved, 0, 0), 0.0f, 0.0f, "an empty image scores 0");
    checkNear(motioncurve::sadAt(a, moved, 100, 0), 0.0f, 0.0f, "an offset with no overlap scores 0");
  }

  std::printf("motioncurve::cropCenter\n");
  {
    const motioncurve::Image img = scene(10, 8);
    const motioncurve::Image half = motioncurve::cropCenter(img, 0.5f);
    check(half.width == 5 && half.height == 4, "half of each axis");
    checkNear(half.pixels[0], img.pixels[2 * 10 + 2], 0.0f, "the crop is centred (x0 = (10-5)/2 = 2)");
    const motioncurve::Image all = motioncurve::cropCenter(img, 1.0f);
    check(all.width == img.width && all.height == img.height, "fraction 1 is the whole image");
    check(all.pixels == img.pixels, "and its payload is identical");
    const motioncurve::Image one = motioncurve::cropCenter(img, 0.01f);
    check(one.width == 1 && one.height == 1, "a tiny fraction still gives at least 1x1");
    check(motioncurve::cropCenter(img, 0.0f).width == 1, "fraction 0 clamps to a 1-pixel crop");
    check(motioncurve::cropCenter(motioncurve::Image{}, 0.5f).pixels.empty(), "an empty image crops to empty");
  }

  std::printf("motioncurve::accumulate\n");
  {
    const std::vector<motioncurve::Vec2> deltas{{1.0f, 0.0f}, {0.0f, 2.0f}, {-1.0f, -1.0f}};
    const std::vector<motioncurve::Vec2> path = motioncurve::accumulate(deltas);
    check(path.size() == 3, "the path has one entry per delta");
    checkNear(path[0].x, 1.0f, 0.0f, "the first point is the first delta");
    checkNear(path[1].y, 2.0f, 0.0f, "the second accumulates the axis that moved");
    checkNear(path[2].x, 0.0f, 0.0f, "a delta can bring the path back to zero");
    checkNear(path[2].y, 1.0f, 0.0f, "and the other axis accumulates independently");
    check(motioncurve::accumulate({}).empty(), "an empty input is an empty path");
  }

  std::printf("motioncurve::smoothMovingAverage\n");
  {
    // A step: the smoothed path must be strictly between the two levels at the step and equal the
    // level away from it, and it must cover every input frame.
    std::vector<motioncurve::Vec2> path;
    for (int i = 0; i < 9; ++i) path.push_back(motioncurve::Vec2{i < 4 ? 0.0f : 8.0f, 0.0f});
    const std::vector<motioncurve::Vec2> smoothed = motioncurve::smoothMovingAverage(path, 3);
    check(smoothed.size() == path.size(), "every frame survives the smoothing");
    checkNear(smoothed[0].x, 0.0f, 0.0f, "the far-left sample is untouched by a step far away");
    checkNear(smoothed[8].x, 8.0f, 0.0f, "the far-right sample is untouched too");
    check(smoothed[4].x > 0.0f && smoothed[4].x < 8.0f, "the step is blended, not preserved");
    const std::vector<motioncurve::Vec2> same = motioncurve::smoothMovingAverage(path, 1);
    check(same[3].x == path[3].x && same[4].x == path[4].x, "window 1 is a copy");
    check(motioncurve::smoothMovingAverage({}, 5).empty(), "an empty path smooths to empty");
  }

  std::printf("motioncurve::temporalMedianFilter\n");
  {
    // A single spike is REMOVED by the median and NOT removed by the moving average: that contrast is
    // the reason both functions exist.
    std::vector<motioncurve::Vec2> path;
    for (int i = 0; i < 7; ++i) path.push_back(motioncurve::Vec2{0.0f, 0.0f});
    path[3] = motioncurve::Vec2{50.0f, -40.0f};
    const std::vector<motioncurve::Vec2> median = motioncurve::temporalMedianFilter(path, 3);
    checkNear(median[3].x, 0.0f, 0.0f, "a one-frame outlier is discarded by the median");
    checkNear(median[3].y, 0.0f, 0.0f, "on both axes");
    const std::vector<motioncurve::Vec2> average = motioncurve::smoothMovingAverage(path, 3);
    check(average[3].x > 1.0f, "a moving average keeps part of the outlier (they are different tools)");
    check(median.size() == path.size(), "every frame survives the filter");
    check(motioncurve::temporalMedianFilter({}, 3).empty(), "an empty path filters to empty");
  }

  std::printf("motioncurve::estimateTranslation\n");
  {
    const motioncurve::Image a = scene(48, 48);
    // Zero shift: a static scene must report exactly zero, not a pixel of noise.
    const motioncurve::Vec2 still = motioncurve::estimateTranslation(a, shifted(a, 0, 0), 6.0f);
    checkNear(still.x, 0.0f, 1e-4f, "a static scene reports exactly zero on x");
    checkNear(still.y, 0.0f, 1e-4f, "and on y");

    // Known integer shifts at a few magnitudes, including one larger than a single pyramid level's
    // two-pixel refinement step (which is what the pyramid exists for).
    struct Case { int dx; int dy; };
    const Case cases[] = {{2, 0}, {-3, 2}, {5, -4}, {0, 6}};
    for (const Case& c : cases) {
      const motioncurve::Vec2 got = motioncurve::estimateTranslation(a, shifted(a, c.dx, c.dy), 8.0f);
      char label[96];
      std::snprintf(label, sizeof label, "a shift of (%d, %d) is recovered", c.dx, c.dy);
      const bool ok = std::fabs(got.x - static_cast<float>(c.dx)) <= 1.01f &&
                      std::fabs(got.y - static_cast<float>(c.dy)) <= 1.01f;
      std::printf("%s %s  (got %.3f, %.3f)\n", ok ? "  PASS" : "  FAIL", label, got.x, got.y);
      if (!ok) ++failures;
    }

    // Mismatched and empty inputs are refused rather than guessed at.
    const motioncurve::Vec2 mismatch = motioncurve::estimateTranslation(a, scene(32, 32), 4.0f);
    check(mismatch.x == 0.0f && mismatch.y == 0.0f, "different sizes report zero rather than a guess");
    const motioncurve::Vec2 empty = motioncurve::estimateTranslation(motioncurve::Image{}, motioncurve::Image{}, 4.0f);
    check(empty.x == 0.0f && empty.y == 0.0f, "empty inputs report zero");
  }

  std::printf("pipeline: a synthetic pan, accumulated and smoothed\n");
  {
    // Five frames panning right by two pixels per frame. The deltas the tracker reports, accumulated,
    // must be a straight ramp, and the smoothed path must lag it — the shape a stabiliser relies on.
    const motioncurve::Image base = scene(48, 48);
    std::vector<motioncurve::Vec2> deltas;
    for (int frame = 1; frame <= 5; ++frame) {
      const motioncurve::Vec2 d = motioncurve::estimateTranslation(shifted(base, 2 * (frame - 1), 0),
                                                                  shifted(base, 2 * frame, 0), 8.0f);
      deltas.push_back(d);
    }
    const std::vector<motioncurve::Vec2> path = motioncurve::accumulate(deltas);
    check(path.size() == 5, "one path point per frame pair");
    const float total = path.back().x;
    check(total > 8.0f && total < 12.0f, "the accumulated pan is about ten pixels, as the frames moved");
    const std::vector<motioncurve::Vec2> smoothed = motioncurve::smoothMovingAverage(path, 3);
    check(smoothed.size() == path.size(), "the smoothed path covers every frame");
    check(std::fabs(smoothed.back().x - path.back().x) < 2.0f, "and stays near the path at the end, not wildly off");
  }

  std::printf("\n%s\n", failures == 0 ? "ALL MOTIONCURVEX CORE CHECKS PASSED"
                                      : "MOTIONCURVEX CORE HAS FAILING CHECKS");
  return failures == 0 ? 0 : 1;
}
