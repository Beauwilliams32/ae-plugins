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
// ONE MEASURED DIFFERENCE, and it is the compiler's rather than the source's: the shipped
// bundle makes the second `__nth_element` call over a ONE-ELEMENT range
// `[begin + mid - 1, begin + mid)`, and this build elides it — libc++ defines
// `__nth_element` inline, and a one-element range is provably nothing, so the call
// disappears and the value is loaded directly. The arithmetic is the same; the shipped is
// 43 instructions and this is 32.
float median(std::vector<float> values) {
  if (values.empty()) return 0.0f;
  const size_t mid = values.size() / 2;
  std::nth_element(values.begin(), values.begin() + static_cast<ptrdiff_t>(mid), values.end());
  const float upper = values[mid];
  if (values.size() % 2 == 1) return upper;
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

}  // namespace shake
