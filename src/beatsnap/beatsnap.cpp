// beatsnap.cpp — transcribed from the compiled BeatSnap bundle.
//
// These five are the fleet's shared DSP. They are machine-code-identical to the
// `spectra::` copies in the shipped binaries — tools/dsp-duplication.mjs compares
// every copy in every installed bundle and reports no drift — so the source below
// is the same source under a different namespace. `tools/codegen-diff.mjs beatsnap`
// re-checks each one against BeatSnap's own instructions, which is what makes this
// a recovery rather than a copy of a copy.
//
// Nothing else from this bundle is here: BeatSnap's own onset detection is not
// recovered yet, and inventing it would be worse than leaving it out.

#include "beatsnap.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace beatsnap {

float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

bool isPowerOfTwo(int n) { return n < 1 ? false : (n & (n - 1)) == 0; }

std::vector<float> hannWindow(int n) {
  std::vector<float> w;
  if (n < 1) return w;
  w.assign(static_cast<size_t>(n), 0.0f);
  if (n == 1) {
    w[0] = 1.0f;
    return w;
  }
  const float twoPi = 6.28318530717958647692f;
  const float nf = static_cast<float>(n);
  for (int i = 0; i < n; ++i) {
    w[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(twoPi * static_cast<float>(i) / nf));
  }
  return w;
}

void fftRadix2(std::vector<float>& re, std::vector<float>& im) {
  const size_t n = re.size();
  if (n < 2) return;
  if (im.size() != re.size()) return;
  if (!isPowerOfTwo(static_cast<int>(n))) return;

  // Bit-reversal permutation (in-body, not the caller's job).
  for (size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for (; (j & bit) != 0; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      std::swap(re[i], re[j]);
      std::swap(im[i], im[j]);
    }
  }

  const float twoPi = 6.28318530717958647692f;  // 0x40c90fdb

  for (size_t len = 2; len <= n; len <<= 1) {
    const float angle = -twoPi / static_cast<float>(len);  // forward convention
    const float sinA = std::sin(angle);
    const float cosA = std::cos(angle);
    const size_t half = len >> 1;

    for (size_t i = 0; i < n; i += len) {
      float wRe = 1.0f, wIm = 0.0f;
      for (size_t j = 0; j < half; ++j) {
        const size_t lo = i + j;
        const size_t hi = lo + half;
        const float aRe = re[lo], bRe = re[hi];
        const float aIm = im[lo], bIm = im[hi];

        const float tRe = wRe * bRe - wIm * bIm;
        const float tIm = wRe * bIm + wIm * bRe;

        re[lo] = aRe + tRe;
        im[lo] = aIm + tIm;
        re[hi] = aRe - tRe;
        im[hi] = aIm - tIm;

        // Rotate the twiddle by the base angle.
        const float nwRe = wRe * cosA - wIm * sinA;
        const float nwIm = wRe * sinA + wIm * cosA;
        wRe = nwRe;
        wIm = nwIm;
      }
    }
  }
}

std::vector<float> downmixToMono(const float* interleaved, size_t frames, int channels) {
  std::vector<float> out;
  if (interleaved == nullptr) return out;
  if (channels < 1) return out;

  out.resize(frames);
  const int ch = channels;
  const float scale = static_cast<float>(ch);
  for (size_t i = 0; i < frames; ++i) {
    float sum = 0.0f;
    for (int c = 0; c < ch; ++c) {
      sum += interleaved[i * static_cast<size_t>(ch) + static_cast<size_t>(c)];
    }
    out[i] = sum / scale;
  }
  return out;
}


// ---------------------------------------------------------------------------
// medianOfWindow(const vector<float>&, int, int)
//
// __ZN8beatsnap14medianOfWindowERKNSt3__16vectorIfNS0_9allocatorIfEEEEii, 58 instrs
//
//   ldp x8, x10, [x0]        ; begin, end
//   cmp x8, x10 / b.eq       ; empty -> 0.0f
//   sub w9, w1, w2           ; w9 = centre - radius
//   bic w9, w9, w9, asr #31  ; w9 = max(w9, 0)
//   ... lsr #2 / sub #1      ; w10 = size() - 1
//   add w11, w2, w1          ; w11 = radius + centre
//   csel w10, w11, w10, lt   ; w10 = min(w11, size() - 1)
//   cmp w10, w9 / b.lt       ; inverted or empty window -> 0.0f
//   ... __Znwm + memcpy      ; a COPY of the window onto the heap
//   bl __sort<less<float>>   ; sorted ascending
//   asr x8, x8, #3           ; count / 2   (n bytes = count * 4)
//   ldr s8, [x19, x8, lsl #2]; sorted[count / 2]
//
// The two consequences: the input vector is never reordered, and an even count
// yields the UPPER middle value rather than the mean of the two middles. The copy
// plus full sort also makes this O(k log k) per call, which the caller below does
// once per sample — a real cost, and the reason the improvement note in
// RECOVERY-NOTES.md can beat it without changing a single output value.
// ---------------------------------------------------------------------------
float medianOfWindow(const std::vector<float>& values, int centre, int radius) {
  if (values.empty()) return 0.0f;
  int lo = centre - radius;
  if (lo < 0) lo = 0;
  int hi = centre + radius;
  const int last = static_cast<int>(values.size()) - 1;
  if (hi > last) hi = last;
  if (hi < lo) return 0.0f;

  std::vector<float> window(values.begin() + lo, values.begin() + hi + 1);
  std::sort(window.begin(), window.end());
  const size_t count = window.size();
  return window[count / 2];
}

// ---------------------------------------------------------------------------
// adaptiveThreshold(const vector<float>&, int, float)
//
// __ZN8beatsnap17adaptiveThresholdERKNSt3__16vectorIfNS0_9allocatorIfEEEEif, 78 instrs
//
// The output is allocated with the input's BYTE size and bzeroed, so it has the
// same element count and starts at zero. Then:
//
//   ldr s1, [x25], #0x4 / fcmp s0, s1 / fcsel s0, s1, s0, mi
//       ; one pass taking the MAXIMUM, starting from s0 = 0 (movi.2d), so the
//       ; floor below is 0 on a track whose samples are all negative
//   fmov s1, #100.0 / fcsel s1, s1, s8, gt     ; scale saturates at 100, one-sided
//   fmov s2, -100.0 / fdiv s1, s1, s2
//   fmov s2, 3.6 / fmov s3, 5.0 / fmadd s1, s1, s2, s3
//       ; multiplier = 5.0 + (scale / -100.0) * 3.6, i.e. 5.0 down to 1.4
//   fcmp s8, #0.0 / fcsel s8, s3, s1, mi       ; a negative scale takes 5.0 flat
//   fmov s2, 0.05 / fmul s9, s0, s2            ; floor = trackMax * 0.05
//   bl medianOfWindow / fmul s0, s8, s0 / fcmp / fcsel s0, s9, s0, mi
//       ; threshold = max(multiplier * median(i), floor)
// ---------------------------------------------------------------------------
std::vector<float> adaptiveThreshold(const std::vector<float>& flux, int window, float scale) {
  std::vector<float> out(flux.size(), 0.0f);

  // The maximum starts at zero, not at flux[0]: an all-negative track therefore
  // has a floor of zero rather than of its largest (still negative) sample.
  float loudest = 0.0f;
  for (size_t i = 0; i < flux.size(); ++i) {
    if (flux[i] > loudest) loudest = flux[i];
  }

  float clamped = scale;
  if (clamped > 100.0f) clamped = 100.0f;
  float multiplier = 5.0f + (clamped / -100.0f) * 3.6f;
  if (scale < 0.0f) multiplier = 5.0f;

  const float floorValue = loudest * 0.05f;
  for (size_t i = 0; i < flux.size(); ++i) {
    const float threshold = multiplier * medianOfWindow(flux, static_cast<int>(i), window);
    out[i] = threshold < floorValue ? floorValue : threshold;
  }
  return out;
}

}  // namespace beatsnap
