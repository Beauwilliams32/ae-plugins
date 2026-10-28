// shake.cpp — transcribed from the compiled ShakeCapture bundle.

#include "shake.h"

#include <algorithm>
#include <cmath>

namespace shake {

float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

// __ZN5shake6medianENSt3__16vectorIfNS0_9allocatorIfEEEE, 43 instrs
//
//   ldr x0, [x0] / ldr x2, [x19, #0x8] / cmp x0, x2 / b.eq <zero>
//       ; begin == end -> 0.0f
//   sub x8, x2, x0 / asr x9, x8, #2 / lsr x20, x9, #1
//       ; count = (end - begin) / 4, then mid = count / 2
//   x1 = begin + mid * 4 / cmp x2, x1 / b.eq <skip>
//       ; the "middle is the end" guard
//   bl __nth_element(begin, begin + mid, end, less)
//       ; PARTIAL selection, not a sort
//   ldr s0, [x0, x20, lsl #2]                 ; data[mid]
//   tbnz w8, #0x2, <return>                   ; bit 2 of the BYTE count is the ODD test
//   add x2, x21, x0 / sub x1, x2, #0x4        ; the element one before mid
//   bl __nth_element(begin, begin + mid - 1, begin + mid, less)
//   ldur s0, [x8, #-0x4] / fadd s0, s8, s0 / fmov s1, #0.5 / fmul s0, s0, s1
//       ; (data[mid - 1] + data[mid]) * 0.5
//
// The odd test is `tbnz w8, #0x2` on the byte count: count * 4 has bit 2 set exactly when
// count is odd, which is a cheaper test than dividing again.
//
// CORRECTED 2026-10-28 (row AE-05). The second `__nth_element` call is NOT over a
// one-element range. Its arguments are (begin, begin + mid - 1, begin + mid), so its RANGE
// is the lower partition `[begin, begin + mid)` and its nth is that partition's LAST
// element: it selects the largest of the `mid` smallest - the true lower middle - and
// leaves it at `data[mid - 1]`. The first transcription read the range as
// `[begin + mid - 1, begin + mid)` - one element, a no-op - and so read `data[mid - 1]`
// straight out of the partition, which libc++ leaves in an unspecified order.
//
// What the shipped bundle actually returns was measured directly, by calling its exported
// `shake::median`: on 8260 vectors (every length from 0 to 80 with heavy duplicates, 3000
// large random ones, and sorted and reversed inputs) it returns the exact median every
// time - the middle element on an odd count, the AVERAGE of the two middle elements on an
// even count. The first transcription's version disagrees with the true median on 10530
// of 20000 random even-count vectors, and that propagated straight into
// `trackTranslation`, whose medians are taken over an even number of blocks about half
// the time.
//
// This build therefore makes the same selection the shipped code makes: a second
// `nth_element` over the lower partition with its nth at the partition's last element,
// which is the true second smallest, and then the shipped arithmetic,
// (lower + upper) * 0.5f. The shipped calls libc++'s INTERNAL `__nth_element` and this
// calls the public `std::nth_element`, which reduces to the same algorithm - and the two
// agree bit for bit on 8000 random vectors.
float median(std::vector<float> values) {
  if (values.empty()) return 0.0f;
  const size_t mid = values.size() / 2;
  std::nth_element(values.begin(), values.begin() + static_cast<ptrdiff_t>(mid), values.end());
  const float upper = values[mid];
  if (values.size() % 2 == 1) return upper;
  // The shipped code's second selection: over the lower partition, with the nth at the
  // partition's LAST element - so the largest of the `mid` smallest, the true lower
  // middle, ends up at `mid - 1`.
  std::nth_element(values.begin(),
                   values.begin() + static_cast<ptrdiff_t>(mid - 1),
                   values.begin() + static_cast<ptrdiff_t>(mid));
  const float lower = values[mid - 1];
  return (lower + upper) * 0.5f;
}


// ---------------------------------------------------------------------------
// The filter chain — recovered instruction by instruction (2026-10-27, row AE-05).
// ---------------------------------------------------------------------------

// __ZN5shake13movingAverageERKNSt3__16vectorIfNS_9allocatorIfEEEEi, 155 instrs
//
// The instruction count is dominated by clang vectorising the inner sum; the shape is
//   half = (window > 1 ? window : 1) / 2
//   for i: sum<double>(v[max(i-half,0) .. min(i+half,n-1)]) / (that count)
// Note the DIVISOR is the count of samples actually in the window, not `window`, so the ends are
// averaged over fewer samples rather than being pulled toward zero.
std::vector<float> movingAverage(const std::vector<float>& values, int window) {
  std::vector<float> out(values.size(), 0.0f);       // allocated and bzero'd in the listing
  // The empty case is tested from the POINTERS (`subs` of end-begin), which is what the listing does;
  // writing `n <= 0` instead costs one extra instruction and shows up as a 156-vs-155 difference.
  if (values.empty()) return out;
  const int n = static_cast<int>(values.size());

  const int half = (window > 1 ? window : 1) / 2;    // csinc + lsr: window<=1 gives 0
  for (int i = 0; i < n; ++i) {
    // The listing computes the bounds with `bic` (max against zero) and `csel` (min against n-1)
    // rather than through std::max/std::min, and the count is `end - begin + 1` — the sample count,
    // not the window, which is why the ends average fewer samples.
    const int raw_begin = i - half;
    const int begin = raw_begin < 0 ? 0 : raw_begin;
    const int raw_end = i + half;
    const int end = raw_end < n ? raw_end : n - 1;
    const int count = end - begin + 1;
    double total = 0.0;                              // fcvtl per sample: accumulated in double
    for (int j = begin; j <= end; ++j) total += static_cast<double>(values[j]);
    out[static_cast<size_t>(i)] = static_cast<float>(total / static_cast<double>(count));
  }
  return out;
}

// __ZN5shake8highPassERKNSt3__16vectorIfNS_9allocatorIfEEEEi, 105 instrs
//
//   smooth = movingAverage(values, window)
//   out.resize(values.size()) / bzero
//   for i: out[i] = values[i] - smooth[i]
std::vector<float> highPass(const std::vector<float>& values, int window) {
  const std::vector<float> smooth = movingAverage(values, window);
  std::vector<float> out(values.size(), 0.0f);
  for (size_t i = 0; i < out.size(); ++i) out[i] = values[i] - smooth[i];
  return out;
}

// __ZN5shake13extractJitterERKNS_12ShakeProfileEi, 36 instrs
//
// Two calls, two 24-byte stores: the horizontal series then the vertical one.
ShakeProfile extractJitter(const ShakeProfile& profile, int window) {
  ShakeProfile jitter;
  jitter.horizontal = highPass(profile.horizontal, window);
  jitter.vertical = highPass(profile.vertical, window);
  return jitter;
}


// __ZN5shake10accumulateERKNSt3__16vectorINS_4Vec2ENS_9allocatorIS2_EEEE, 135 instrs
//
// The listing unrolls five at a time and precomputes a modulo-5 index, but the scalar tail
// (0x17d0) is the whole story: two resize(n + 1, 0.0f) calls, then
//     out.x[i + 1] = out.x[i] + path[i].x
//     out.y[i + 1] = out.y[i] + path[i].y
// which is a cumulative sum with a leading origin sample.
ShakeProfile accumulate(const std::vector<Vec2>& path) {
  ShakeProfile out;
  out.horizontal.resize(path.size() + 1, 0.0f);
  out.vertical.resize(path.size() + 1, 0.0f);
  for (size_t i = 0; i < path.size(); ++i) {
    out.horizontal[i + 1] = out.horizontal[i] + path[i].x;
    out.vertical[i + 1] = out.vertical[i] + path[i].y;
  }
  return out;
}

// __ZN5shake22normalizeUnitAmplitudeERNS_12ShakeProfileE, 125 instrs
//
//   peak = 0; for both channels: peak = max(peak, fabs(sample))
//   if (peak <= 1e-12f) return            ; 0x2b8cbccc == 9.99999996e-13
//   scale = 1.0f / peak; multiply every sample of both channels by it
//
// The threshold is what keeps a perfectly still camera (an all-zero profile) from turning the
// whole track into NaNs.
void normalizeUnitAmplitude(ShakeProfile& profile) {
  float peak = 0.0f;
  for (float value : profile.horizontal) peak = std::max(peak, std::fabs(value));
  for (float value : profile.vertical) peak = std::max(peak, std::fabs(value));
  if (peak <= 9.99999996e-13f) return;
  const float scale = 1.0f / peak;
  for (float& value : profile.horizontal) value *= scale;
  for (float& value : profile.vertical) value *= scale;
}


// __ZN5shake12sampleLoopedERKNSt3__16vectorIfNS0_9allocatorIfEEEEdNS_8LoopModeE, 92 instrs
//
// Read in full. The three mode branches share one tail: bring the position into [0, last],
// truncate it, and lerp between values[i] and values[i+1] (a single fmadd). Loop is the only
// branch that takes its indices modulo the length; Clamp and PingPong have already folded the
// position into range, so their i+1 is always a valid index.
float sampleLooped(const std::vector<float>& values, double t, LoopMode mode) {
  const size_t n = values.size();
  if (n == 0) return 0.0f;
  const size_t last = n - 1;
  if (n == 1) {
    // Recovered quirk: a single-sample series answers 0.0f in Clamp mode for any non-zero t.
    if (t == 0.0 || std::isnan(t)) return values[0];
    if (mode == LoopMode::Clamp) return 0.0f;
    return values[0];
  }

  double w = t;
  switch (mode) {
    case LoopMode::PingPong: {
      const double period = 2.0 * static_cast<double>(last);
      double folded = std::fmod(t, period);
      if (folded < 0.0) folded += period;
      if (folded > static_cast<double>(last)) folded = period - folded;
      w = folded;
      break;
    }
    case LoopMode::Loop: {
      const double period = static_cast<double>(n);
      double folded = std::fmod(t, period);
      if (folded < 0.0) folded += period;
      w = folded;
      break;
    }
    case LoopMode::Clamp:
      // The listing has BOTH out-of-range tests branching to the SAME zero return
      // (0x229c `b.mi 0x238c` for t < 0 and 0x22a8 `b.gt 0x238c` for t > last), so
      // Clamp does NOT hold the end sample outside the range - it answers 0.0f there.
      // The end samples are only returned AT the endpoints (0x22b0 `b.ls 0x2388` for
      // t <= 0 and 0x22b8 `b.ge 0x23a0` for t >= last).
      if (t < 0.0) return 0.0f;
      if (t > static_cast<double>(last)) return 0.0f;
      w = t;
      break;
    default:
      return 0.0f;                                  // LoopMode::None, and anything outside the enum
  }

  // Loop mode reaches the lerp without the endpoint tests: its index arithmetic wraps instead.
  if (mode != LoopMode::Loop) {
    if (w <= 0.0) return values[0];
    if (w >= static_cast<double>(last)) return values[last];
  }

  size_t index = static_cast<size_t>(w);            // truncation, matching fcvtzu
  const float frac = static_cast<float>(w - static_cast<double>(index));
  size_t next = index + 1;
  if (mode == LoopMode::Loop) {
    index %= n;
    next %= n;
  }
  const float a = values[index];
  const float b = values[next];
  return a + frac * (b - a);                        // one fmadd in the listing
}

// __ZN5shake15resampleProfileERKNSt3__16vectorIfNS_9allocatorIfEEEEdiNS_8LoopModeE, 50 instrs
//
//   out.resize(count, 0.0f)
//   if (step <= 0.0 || values.empty()) return out          ; the zero array, unsampled
//   for i: out[i] = sampleLooped(values, (double)i * step, mode)
std::vector<float> resampleProfile(const std::vector<float>& values, double step, int count, LoopMode mode) {
  std::vector<float> out;
  if (count < 1) return out;
  out.resize(static_cast<size_t>(count), 0.0f);
  if (step <= 0.0 || values.empty()) return out;
  for (int i = 0; i < count; ++i) {
    out[static_cast<size_t>(i)] = sampleLooped(values, static_cast<double>(i) * step, mode);
  }
  return out;
}


// __ZN5shake16trackTranslationEPKfS1_iiii, 960 instrs
//
// The largest function in the bundle and the effect's whole point: a block-matching search for
// how far the picture in `b` has moved relative to the picture in `a`.
//
//   0x830-0x864   the five guards: null a, null b, block < 4, radius < 1, and
//                 width/height < block + 2*radius  ->  {0, 0}
//   0x868-0x8f4   the per-block setup, and four constants:
//                 s9  = 0x358637bd = 1e-6f   (the flatness threshold)
//                 s10 = -2.0f                (the parabola's -2*best)
//                 s11 =  0.5f                (the parabola's half, and the clamp)
//                 s12 = -0.5f                (the clamp's other end)
//   0x900-0x9a4   the band loop (bandRow = radius, radius+block, ...) and the column loop
//                 (col = radius, radius+block, ...), both bounded by `+ block <= size - radius`
//   0x9a8-0xa10   the TEXTURE TEST on the template block: sum and sumsq in DOUBLE, then
//                     mean = sum/(block*block);  var = sumsq/(block*block) - mean*mean
//                 compared as a FLOAT against 1e-6f, and `b.mi` SKIPS the block when the
//                 variance is below it — a flat block carries no position information
//   0xa24-0xa90   the search: for each candidate the block is compared with the template and
//                 the ABSOLUTE DIFFERENCE is summed in FLOAT; the best cost updates only on a
//                 STRICTLY smaller cost (or while best is still negative, i.e. before the first
//                 candidate), so a TIE KEEPS THE EARLIER CANDIDATE — first minimum wins
//   0xc08-0xc18   the horizontal sub-pixel refinement runs only when -radius < bestDx < radius
//   0xc38/0xdd8   its two extra costs: the same block one column LEFT and one column RIGHT
//   0xf64-0xfa8   the parabola:  t = ((c1 - c2) * 0.5f) / (c1 + c2 - 2*best), clamped to
//                 [-0.5, 0.5]; the denominator is skipped when best <= 1e-12f or the
//                 denominator <= 1e-12f, and then the answer is the integer offset unchanged
//   0xfac-0xfb4   the vertical refinement runs only when -radius < bestDy < radius
//   0xfc0/0x1160  its two extra costs: the block one row ABOVE and one row BELOW
//   0x135c/0x1498 the two results are pushed into two std::vector<float>s
//   0x14f4-0x160c the medians: each vector is copied and `nth_element`-ed, exactly as
//                 `shake::median` does
//   0x1624        the return: {horizontal median, vertical median}
namespace {

// The absolute-difference cost of one block against one block, accumulated in FLOAT.
//
// The shipped code vectorises the subtraction and the absolute value but keeps the ADDS
// sequential and scalar, so the running total is the plain left-to-right sum of the row-major
// differences. Writing it this way gives the same number bit for bit: the fabd is exact, and a
// sequential float sum is a fixed sequence of roundings.
//
// `a` is the template and `b` the frame being searched.
float blockCost(const float* a, const float* b, int width, int block,
                int rowA, int colA, int rowB, int colB) {
  float cost = 0.0f;
  for (int i = 0; i < block; ++i) {
    const float* pa = a + static_cast<size_t>(rowA + i) * static_cast<size_t>(width) + static_cast<size_t>(colA);
    const float* pb = b + static_cast<size_t>(rowB + i) * static_cast<size_t>(width) + static_cast<size_t>(colB);
    for (int j = 0; j < block; ++j) {
      cost += std::fabs(pa[j] - pb[j]);
    }
  }
  return cost;
}

// The sub-pixel offset of a parabolic minimum through three costs.
//
//     if (best <= 1e-12f)              return 0
//     denom = fma(-2, best, c1) + c2
//     if (denom <= 1e-12f)             return 0
//     t = ((c1 - c2) * 0.5f) / denom, clamped to [-0.5f, 0.5f]
//
// The denominator is built as `(c1 - 2*best) + c2` — a single fused multiply-add and then a
// separate add — because that is the order the listing uses and float addition is not
// associative. The clamp compares the UNCLAMPED t against both ends, so it is a clamp and not
// a sequence of independent selections.
float parabolaOffset(float best, float c1, float c2) {
  if (best <= 9.99999996e-13f) return 0.0f;
  const float denom = std::fma(-2.0f, best, c1) + c2;
  if (denom <= 9.99999996e-13f) return 0.0f;
  float t = ((c1 - c2) * 0.5f) / denom;
  if (t > 0.5f) t = 0.5f;
  if (t < -0.5f) t = -0.5f;
  return t;
}

}  // namespace

Vec2 trackTranslation(const float* a, const float* b, int width, int height, int block, int radius) {
  if (a == nullptr || b == nullptr) return Vec2{0.0f, 0.0f};
  if (block < 4) return Vec2{0.0f, 0.0f};
  if (radius < 1) return Vec2{0.0f, 0.0f};
  if (width < block + 2 * radius) return Vec2{0.0f, 0.0f};
  if (height < block + 2 * radius) return Vec2{0.0f, 0.0f};

  std::vector<float> horizontal;
  std::vector<float> vertical;

  for (int bandRow = radius; bandRow + block <= height - radius; bandRow += block) {
    for (int col = radius; col + block <= width - radius; col += block) {
      // The TEXTURE TEST: a block with no variance has no position to find, and the shipped
      // code skips it outright rather than reporting a position for it.
      double sum = 0.0;
      double sumsq = 0.0;
      for (int i = 0; i < block; ++i) {
        const float* pa = a + static_cast<size_t>(bandRow + i) * static_cast<size_t>(width) + static_cast<size_t>(col);
        for (int j = 0; j < block; ++j) {
          const double v = static_cast<double>(pa[j]);
          sum += v;
          sumsq += v * v;
        }
      }
      const double cells = static_cast<double>(block) * static_cast<double>(block);
      const double mean = sum / cells;
      const double variance = std::fma(-mean, mean, sumsq / cells);
      if (static_cast<float>(variance) < 9.99999996e-07f) continue;   // 0x358637bd

      float best = -1.0f;
      int bestDx = 0;
      int bestDy = 0;
      for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
          const float cost = blockCost(a, b, width, block, bandRow, col,
                                       bandRow + dy, col + dx);
          // `best < 0` covers the first candidate, before any cost has been seen; after that a
          // candidate has to be STRICTLY cheaper, so a tie leaves the earlier one in place.
          if (best < 0.0f || cost < best) {
            best = cost;
            bestDx = dx;
            bestDy = dy;
          }
        }
      }

      float horizontalOffset = static_cast<float>(bestDx);
      if (bestDx > -radius && bestDx < radius) {
        const float left = blockCost(a, b, width, block, bandRow, col, bandRow + bestDy, col + bestDx - 1);
        const float right = blockCost(a, b, width, block, bandRow, col, bandRow + bestDy, col + bestDx + 1);
        horizontalOffset = static_cast<float>(bestDx) + parabolaOffset(best, left, right);
      }

      float verticalOffset = static_cast<float>(bestDy);
      if (bestDy > -radius && bestDy < radius) {
        const float above = blockCost(a, b, width, block, bandRow, col, bandRow + bestDy - 1, col + bestDx);
        const float below = blockCost(a, b, width, block, bandRow, col, bandRow + bestDy + 1, col + bestDx);
        verticalOffset = static_cast<float>(bestDy) + parabolaOffset(best, above, below);
      }

      horizontal.push_back(horizontalOffset);
      vertical.push_back(verticalOffset);
    }
  }

  // The medians are the shipped `shake::median`, so this is a call and not a re-implementation.
  // `shake::median` returns the exact median (see its own note), which is what the bundle
  // measurably returns on an even count as well.
  return Vec2{median(horizontal), median(vertical)};
}

}  // namespace shake
