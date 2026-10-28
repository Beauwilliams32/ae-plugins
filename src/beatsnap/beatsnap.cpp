// beatsnap.cpp — transcribed from the compiled BeatSnap bundle.
//
// These five are the fleet's shared DSP. They are machine-code-identical to the
// `spectra::` copies in the shipped binaries — tools/dsp-duplication.mjs compares
// every copy in every installed bundle and reports no drift — so the source below
// is the same source under a different namespace. `tools/codegen-diff.mjs beatsnap`
// re-checks each one against BeatSnap's own instructions, which is what makes this
// a recovery rather than a copy of a copy.
//
// BeatSnap's own onset detection follows below. It is NOT shared with any other
// bundle — `computeFlux`, `pickOnsets`, `detectOnsets` and `renderValues` exist
// only in BeatSnap.plugin — so each one was transcribed from this bundle's own
// instructions, with the listing quoted above each body as the evidence.

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

// ---------------------------------------------------------------------------
// computeFlux(const float*, size_t, const OnsetConfig&)
//
// __ZN8beatsnap11computeFluxEPKfmRKNS_11OnsetConfigE, 416 instrs
//
// The sret is zeroed first — `str xzr, [x8, #0x20]` / `movi.2d v0, #0` /
// `stp q0, q0, [x8]` — which is why an early return leaves an empty track
// with hopSeconds and windowSeconds still zero.
//
//   cbz x0, ret                 ; samples == nullptr
//   cbz x1, ret                 ; n == 0
//   ldr d8, [x2] / fcmp d8,#0.0 / b.ls ret          ; sampleRate <= 0
//   ldr w28, [x2, #0x8]
//   subs w8, w28, #0x1 / and w8, w28, w8
//   ccmp w8, #0x0, #0x0, ge     ; fft >= 1
//   ccmp w28, #0x2, #0x0, eq    ; fft != 2
//   ccmp w28, #0x4, #0x0, hs    ; fft >= 4
//   b.hs continue
//
// The four tests are ONE fused compare chain, so the source is a single
// condition: a power of two that is at least 4. `fft == 2` is excluded by the
// `fft < 4` arm, which is LAST because it is the final `b.hs`.
//
//   lsr x21, x28, #2             ; hopSize = fft >> 2
//   lsr x19, x28, #1             ; bins   = fft >> 1
//   subs x8, x1, x28 / udiv x8, x8, x21 / csinc w27, wzr, w8, lo
//                                ; hops = (n < fft) ? 0 : (n - fft)/hopSize + 1
//   ucvtf d0, w21 / fdiv d0, d0, d8      ; hopSeconds   = hopSize / rate
//   fmov d1, #0.5 / fmul d1, d9, d1 / fdiv d1, d1, d8
//                                ; windowSeconds = (fft * 0.5) / rate
//   stp d0, d1, [x8, #0x18]      ; BOTH DOUBLES ARE STORED HERE
//   cmp w27, #0x1 / b.lt ret     ; hops < 1 -> empty flux, but the two
//                                ;   doubles above are ALREADY SET
//
// That ordering is the easiest thing in this function to get wrong: a signal
// too short to yield a single frame still comes back with its hop and window
// times filled in. Only the four guards above return with them at zero.
//
//   bl __ZNSt3__16vectorIfNS_9allocatorIfEEE6assignEmRKf
//                                ; track.flux.assign(hops, 0.0f), with the
//                                ;   sret ITSELF as the vector's `this` —
//                                ;   which is what puts the vector at +0x00
//
// The window is built INLINE: the listing contains no call to `hannWindow`,
// only `__Znwm` + `_bzero` (which is what `std::vector<float> v(n)` emits)
// and four `_cosf` calls per group of four samples:
//
//   mov w8, #0xfdb / movk w8, #0x40c9   ; 0x40c90fdb = 2*pi as a float
//   fmul.4s v0, v0, v2                  ; index * 2pi
//   fdiv.4s v0, v0, v1                  ; / (float)fft
//   fsub.4s v0, v0, v1                  ; 1 - cos
//   movi.4s v1, #0x3f, lsl #24          ; 0x3F000000 = 0.5f
//   fmul.4s v0, v0, v1                  ; * 0.5f
//
// The band, in bins. The pair is min/max'd BEFORE it is used, the two bin
// indices are clamped, and they are then SORTED — an upside-down band works:
//
//   ldp s1, s0, [x22, #0xc]   ; bandLo, bandHi
//   fcsel s2, s0, s1, mi      ; min
//   fcsel s0, s0, s1, mi      ; max
//   fdiv d8, d8, d9           ; binHz = rate / fft
//   fcvtas x8, d1             ; lround(minHz / binHz)
//   fcvtas x11, d0            ; lround(maxHz / binHz)
//
// The clamp is the awkward one — the upper bound is an UNSIGNED min while the
// lower bound is a SIGNED floor, and BOTH look at the original value:
//
//   sub  w9, w19, #0x1        ; bins - 1
//   cmp  w8, w9 / csel w10, w8, w9, lo    ; unsigned min
//   cmp  w8, #0x0 / csel w8, wzr, w10, lt ; signed floor on the RAW value
//
// so the net rule is `raw < 0 ? 0 : min(raw, bins - 1)`. That is NOT the
// namespace's `clampf`, which tests its low bound last; the source here is an
// explicit expression.
//
// The FFT is the namespace's own `fftRadix2`, called with two local vectors:
//
//   sub x0, x29, #0x90 / sub x1, x29, #0xa8 / bl beatsnap::fftRadix2
//
// and the magnitudes are `sqrt(re^2 + im^2)` (`fmul` / `fmadd` / `fsqrt`) into
// two ping-ponged `std::vector<float>` buffers of `bins` samples each.
//
// The flux is the POSITIVE difference between consecutive magnitude frames,
// summed over the band in FLOAT arithmetic:
//
//   fsub s1, s1, s2 / fadd s2, s0, s1 / fcmp s1, #0.0 / fcsel s0, s2, s0, gt
//
// and `flux[0]` is a literal zero (`str wzr, [x8]`) — the first frame has no
// predecessor to be compared with.
// ---------------------------------------------------------------------------
FluxTrack computeFlux(const float* samples, size_t n, const OnsetConfig& config) {
  FluxTrack track;
  if (samples == nullptr) return track;
  if (n == 0) return track;
  if (config.sampleRate <= 0.0) return track;

  const int fft = config.fftSize;
  if (!isPowerOfTwo(fft) || fft < 4) return track;

  const size_t hopSize = static_cast<size_t>(fft) >> 2;
  const size_t bins = static_cast<size_t>(fft) >> 1;
  const double rate = config.sampleRate;

  int hops = 0;
  if (n >= static_cast<size_t>(fft)) {
    hops = static_cast<int>((n - static_cast<size_t>(fft)) / hopSize + 1);
  }

  // These two are stored before the `hops < 1` test — see the note above.
  track.hopSeconds = static_cast<double>(hopSize) / rate;
  track.windowSeconds = (static_cast<double>(fft) * 0.5) / rate;
  if (hops < 1) return track;

  track.flux.assign(static_cast<size_t>(hops), 0.0f);

  // The window is built inline rather than by calling `hannWindow`.
  const float twoPi = 6.28318530717958647692f;
  const float nf = static_cast<float>(fft);
  std::vector<float> window(static_cast<size_t>(fft));
  for (int j = 0; j < fft; ++j) {
    window[static_cast<size_t>(j)] =
        0.5f * (1.0f - std::cos(twoPi * static_cast<float>(j) / nf));
  }

  // The band, in bins: min/max'd, clamped, then sorted.
  const float bandLoHz = config.bandLoHz < config.bandHiHz ? config.bandLoHz : config.bandHiHz;
  const float bandHiHz = config.bandLoHz < config.bandHiHz ? config.bandHiHz : config.bandLoHz;
  const double binHz = rate / static_cast<double>(fft);
  const long lastBin = static_cast<long>(bins) - 1;
  long rawLo = std::lround(static_cast<double>(bandLoHz) / binHz);
  long rawHi = std::lround(static_cast<double>(bandHiHz) / binHz);
  rawLo = rawLo < 0 ? 0 : (rawLo > lastBin ? lastBin : rawLo);
  rawHi = rawHi < 0 ? 0 : (rawHi > lastBin ? lastBin : rawHi);
  const long binLo = rawLo < rawHi ? rawLo : rawHi;
  const long binHi = rawLo < rawHi ? rawHi : rawLo;

  std::vector<float> re(static_cast<size_t>(fft));
  std::vector<float> im(static_cast<size_t>(fft));
  std::vector<float> magCur(bins);
  std::vector<float> magPrev(bins);

  for (int frame = 0; frame < hops; ++frame) {
    const size_t start = static_cast<size_t>(frame) * hopSize;
    for (int k = 0; k < fft; ++k) {
      re[static_cast<size_t>(k)] = samples[start + static_cast<size_t>(k)]
                                 * window[static_cast<size_t>(k)];
      im[static_cast<size_t>(k)] = 0.0f;
    }
    fftRadix2(re, im);

    for (size_t k = 0; k < bins; ++k) {
      const float r = re[k];
      const float m = im[k];
      magCur[k] = std::sqrt(r * r + m * m);
    }

    if (frame == 0) {
      track.flux[0] = 0.0f;
    } else {
      float acc = 0.0f;
      for (long k = binLo; k <= binHi; ++k) {
        const float d = magCur[static_cast<size_t>(k)] - magPrev[static_cast<size_t>(k)];
        if (d > 0.0f) acc += d;
      }
      track.flux[static_cast<size_t>(frame)] = acc;
    }
    magCur.swap(magPrev);
  }
  return track;
}

}  // namespace beatsnap
