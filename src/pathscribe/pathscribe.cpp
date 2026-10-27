// pathscribe.cpp — transcribed from the compiled PathScribe bundle.
//
// The analysis pipeline is the fleet's shared DSP, machine-code-identical to
// SpectraKeys'; the comments below cite PathScribe's own mangled symbols, which is
// what the differ compares against. The three drawing helpers at the end are
// PathScribe's own.

#include "pathscribe.h"

#include <algorithm>
#include <cmath>

namespace pathscribe {


// ---------------------------------------------------------------------------
float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}


// ---------------------------------------------------------------------------
bool isPowerOfTwo(int n) { return n < 1 ? false : (n & (n - 1)) == 0; }


// ---------------------------------------------------------------------------
float linearToDb(float linear) {
  return 20.0f * std::log10(linear < 1e-9f ? 1e-9f : linear);
}


// ---------------------------------------------------------------------------
float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }


// ---------------------------------------------------------------------------
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


// ---------------------------------------------------------------------------
float hannCoherentGain() { return 0.5f; }


// ---------------------------------------------------------------------------
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


// ---------------------------------------------------------------------------
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
Spectrogram analyse(const float* mono, size_t frames, const AnalysisConfig& config) {
  Spectrogram out;
  if (mono == nullptr || frames == 0) return out;

  const double sampleRate = config.sampleRate;
  const int fftSize = config.fftSize;
  const double hopRateHz = config.hopRateHz;
  if (!(sampleRate > 0.0)) return out;
  if (!(hopRateHz > 0.0)) return out;
  if (fftSize < 2) return out;
  if (!isPowerOfTwo(fftSize)) return out;

  const double samplesPerHop = sampleRate / hopRateHz;
  out.hzPerBin = sampleRate / static_cast<double>(fftSize);
  out.binCount = fftSize / 2;

  const double frameCountRaw = static_cast<double>(frames) / samplesPerHop;
  // fcvtps: round toward +inf, i.e. a ceiling.
  const int frameCount = static_cast<int>(std::ceil(frameCountRaw));
  out.frameCount = frameCount;
  if (frameCount < 1) { out.frameCount = 0; return out; }

  // Hann window over the full FFT length (inlined in the binary).
  std::vector<float> window(static_cast<size_t>(fftSize));
  const float twoPi = 6.28318530717958647692f;
  const float nf = static_cast<float>(fftSize);
  for (int k = 0; k < fftSize; ++k) {
    window[static_cast<size_t>(k)] =
        0.5f * (1.0f - std::cos(twoPi * static_cast<float>(k) / nf));
  }

  // frames.assign(frameCount, vector<float>(binCount, 0.0f)) — one assign, as compiled.
  const std::vector<float> zeroRow(static_cast<size_t>(out.binCount), 0.0f);
  out.frames.assign(static_cast<size_t>(frameCount), zeroRow);

  std::vector<float> re(static_cast<size_t>(fftSize));
  std::vector<float> im(static_cast<size_t>(fftSize));
  const float scale = 4.0f / nf;  // 2.0 / (fftSize * 0.5)

  for (int i = 0; i < frameCount; ++i) {
    // Centre of this frame, rounded half away from zero like fcvtas.
    const double centre = std::round((static_cast<double>(i) + 0.5) * samplesPerHop);
    const long long start = static_cast<long long>(centre) - (fftSize / 2);

    for (int k = 0; k < fftSize; ++k) {
      const long long idx = start + k;
      float sample = 0.0f;
      if (idx >= 0 && static_cast<size_t>(idx) < frames) {
        sample = mono[static_cast<size_t>(idx)];
      }
      re[static_cast<size_t>(k)] = sample * window[static_cast<size_t>(k)];
      im[static_cast<size_t>(k)] = 0.0f;
    }

    fftRadix2(re, im);

    std::vector<float>& row = out.frames[static_cast<size_t>(i)];
    for (int k = 0; k < out.binCount; ++k) {
      const float r = re[static_cast<size_t>(k)];
      const float m = im[static_cast<size_t>(k)];
      row[static_cast<size_t>(k)] = scale * std::sqrt(r * r + m * m);
    }
  }
  return out;
}


// ---------------------------------------------------------------------------
Spectrogram::~Spectrogram() = default;

// ---------------------------------------------------------------------------
// Spectrogram::hzToBin(float hz) const
//

// __ZN10pathscribe12spanCoverageEfff, 16 instrs
//
//   fcmp s2, s1 / fcsel s3, s1, s2, mi / fcsel s1, s2, s1, mi
//       ; s3 = max(span, start), s1 = min(span, start)
//   fcmp s0, s1 / fcsel s1, s1, s0, mi
//       ; left = max(frameIndex, min(span, start))
//   fmov s2, #1.0 / fadd s0, s0, s2
//   fcmp s3, s0 / fcsel s0, s3, s0, mi
//       ; right = min(max(span, start), frameIndex + 1)   <- MIN, not max
//   fsub s0, s0, s1
//       ; width = right - left
//   fcmp s0, #1.0 / fcsel s1, s2, s0, gt / fcmp s0, #0.0 / fcsel s0, s0, s1, mi
//       ; clamped to [0, 1], with the LOW bound tested last -- clampf's structure
//
// The overlap of the frame [frameIndex, frameIndex + 1) with the interval
// [min(edge0, edge1), max(edge0, edge1)], as a fraction of one frame.
//
// The third parameter is on the LEFT of both comparisons below, because that is the
// operand order in the instruction stream (`fcmp s2, s1` compares the third against the
// second) and it is what makes this compile to the bundle's instructions rather than
// merely to its arithmetic.
float spanCoverage(float frameIndex, float edge0, float edge1) {
  const float hi = edge1 < edge0 ? edge0 : edge1;
  const float lo = edge1 < edge0 ? edge1 : edge0;
  const float left = frameIndex < lo ? lo : frameIndex;
  // MIN, not max: `fcsel s0, s3, s0, mi` selects s3 when s3 < s0, and s3 is the span's
  // right edge while s0 is frameIndex + 1. The first transcription of this line read
  // the select the other way round and produced max, which the differ caught at 1 of 16
  // positions -- a semantic error that a self-consistent self-check would have agreed
  // with, which is exactly why the differ is the first gate and not the second.
  const float right = hi < frameIndex + 1.0f ? hi : frameIndex + 1.0f;
  const float width = right - left;
  return width < 0.0f ? 0.0f : (width > 1.0f ? 1.0f : width);
}

// __ZN10pathscribe10smoothStepEfffff, 25 instrs
//
//   mov v8, s1 / mov v9, s0
//   fcmp s1, s0 / fcsel s0, s3, s4, ge     ; span = (to >= from) ? 4th : 5th
//   fcmp s2, #0.0
//   fmov s1, #1e-5                         ; 0x3727c5ac, exactly 1e-5f
//   fccmp s0, s1, #0x0, hi                 ; only if (x > 0)
//   b.hi <exp path>
//   mov.16b v0, v8 / b <ret>               ; otherwise return `to`
// <exp path>:
//   fneg s1, s2 / fdiv s0, s1, s0 / bl _expf
//   fmov s1, #1.0 / fsub s0, s1, s0        ; 1 - exp(-x / span)
//   fsub s1, s8, s9 / fmadd s0, s1, s0, s9 ; from + (to - from) * that
//
// AN EXPONENTIAL APPROACH, NOT A POLYNOMIAL. Despite the name there is no cubic and
// no clamping of a progress value: the value moves from `from` toward `to` with time
// constant `span`, and the sign of the move chooses which of the two spans applies.
//
// The guard is the part to keep: when `x <= 0`, or when the chosen span is not
// greater than 1e-5f, the function returns **`to`** — the destination, not the
// starting value. Reading it as "x <= 0 means we are still at `from`" gives the wrong
// value on every call with a zero or negative time, and the self-check pins the real
// behaviour.
float smoothStep(float from, float to, float x, float spanRising, float spanFalling) {
  const float span = to >= from ? spanRising : spanFalling;
  if (!(x > 0.0f) || !(span > 1e-5f)) return to;
  return from + (to - from) * (1.0f - std::exp(-x / span));
}

// __ZN10pathscribe9binEdgeHzEiidd, 19 instrs
//
//   cmp w1, #0x1 / b.lt <a bare `ret`>
//       ; count < 1 returns the incoming d0 UNCHANGED -- the branch target is a
//       ; single `ret` at the end of the function, not a "return 0" tail
//   fmov d2, #1.0 / fcmp d0, d2 / fcsel d8, d2, d0, mi     ; lo = max(minHz, 1.0)
//   fcmp d1, d8 / fcsel d0, d8, d1, mi                     ; hi = max(maxHz, lo)
//   scvtf d1, w0 / ucvtf d2, w1 / fdiv d1, d1, d2          ; t = index / count
//   fdiv d0, d0, d8 / bl _pow                              ; pow(hi / lo, t)
//   fmul d0, d8, d0                                        ; lo * that
//
// GEOMETRIC, not linear: band edges that are equal RATIOS apart, which is what makes
// a spectrum analysis look even across the octaves. The 1.0 Hz floor is there so the
// ratio is never taken from zero. The exponent divides by `count`, not `count - 1`,
// so edge `count` lands exactly on `hi` and edge 0 exactly on `lo`.
double binEdgeHz(int index, int count, double minHz, double maxHz) {
  if (count < 1) return minHz;
  const double lo = minHz < 1.0 ? 1.0 : minHz;
  const double hi = maxHz < lo ? lo : maxHz;
  // The exponent is formed BEFORE the ratio: the shipped code does `scvtf`, `ucvtf`,
  // `fdiv` for `t` and only then `fdiv` for the ratio.
  const double t = static_cast<double>(index) / static_cast<double>(count);
  const double ratio = hi / lo;
  return lo * std::pow(ratio, t);
}


// __ZN10pathscribe15waveformColumnsEPKfmlli, 80 instrs
//
// A peak-hold reducer. Every constant in it is a recovered immediate: `end >> 1` centres the
// window on `start`, `fcvtms` floors the fractional span, and the final clamp is [-1, 1] because
// the samples are already normalised audio.
std::vector<float> waveformColumns(const float* samples, size_t count, long start, long end, int columns) {
  std::vector<float> out;
  if (columns < 1) return out;
  out.assign(static_cast<size_t>(columns), 0.0f);
  if (!samples || count == 0 || end < 1) return out;

  const double span = static_cast<double>(end) / static_cast<double>(columns);
  const long base = start - (end >> 1);
  for (int column = 0; column < columns; ++column) {
    const long a = static_cast<long>(std::floor(span * static_cast<double>(column)));
    const long b = static_cast<long>(std::floor(span * static_cast<double>(column + 1)));
    const long lo = base + a;
    const long hi = b > a ? base + b : lo + 1;      // csinc: never an empty slice
    float peak = 0.0f;
    for (long j = lo; j < hi; ++j) {
      if (j < 0 || static_cast<size_t>(j) >= count) continue;   // both bounds are tested in the listing
      const float value = samples[static_cast<size_t>(j)];
      if (std::fabs(value) > std::fabs(peak)) peak = value;     // the SIGNED sample, not the magnitude
    }
    out[static_cast<size_t>(column)] = peak > 1.0f ? 1.0f : (peak < -1.0f ? -1.0f : peak);
  }
  return out;
}

}  // namespace pathscribe
