// spectra.cpp — recovered implementations for the SpectraKeys DSP core.
//
// Only the functions below have been RECOVERED FROM DISASSEMBLY. Each one is
// reproduced together with the arm64 instructions it came from, so a reader can
// check the transcription rather than trust it.
//
// The remaining declarations in spectra.h are NOT implemented here. They are
// larger (FFT butterflies, spectrogram assembly) and are still pending
// recovery; leaving stubs that silently return zero would be worse than leaving
// them out, so they are simply absent.

#include "spectra.h"

#include <cmath>

namespace spectra {

// ---------------------------------------------------------------------------
// clampf(float value, float lo, float hi)
//
// __ZN7spectra6clampfEfff:
//     fcmp   s0, s2              ; value vs hi
//     fcsel  s2, s2, s0, gt      ; s2 = value > hi ? hi : value
//     fcmp   s0, s1              ; value vs lo
//     fcsel  s0, s1, s2, mi      ; s0 = value < lo ? lo : s2
//     ret
//
// The two fcsel instructions compose into exactly the ternary below. Verified
// numerically in spectra.test.mjs.
// ---------------------------------------------------------------------------
float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

// ---------------------------------------------------------------------------
// isPowerOfTwo(int n)
//
// __ZN7spectra10isPowerOfTwoEi:
//     sub  w8, w0, #1
//     tst  w0, w8                ; n & (n - 1)
//     cset w8, eq                ; == 0 means a power of two
//     cmp  w0, #1
//     csel w0, wzr, w8, lt       ; and n < 1 is NOT a power of two
//     ret
//
// Note 1 counts as a power of two here, and 0 and negatives do not. fftRadix2
// relies on this.
// ---------------------------------------------------------------------------
bool isPowerOfTwo(int n) { return n < 1 ? false : (n & (n - 1)) == 0; }

// ---------------------------------------------------------------------------
// linearToDb(float linear)
//
// __ZN7spectra10linearToDbEf:
//     mov   w8, #0x705f
//     movk  w8, #0x3089, lsl #16   ; w8 = 0x3089705f
//     fmov  s1, w8                 ; s1 = 1e-9f  (exact: 0x3089705f bit pattern)
//     fcmp  s0, s1
//     fcsel s0, s1, s0, mi         ; x = x < 1e-9f ? 1e-9f : x
//     bl    _log10f
//     fmov  s1, #20.00000000
//     fmul  s0, s0, s1             ; * 20
//     ret
//
// 0x3089705f is 1e-9f, not a tuned epsilon: it is the log(0) guard. Amplitude
// conversion, hence the factor 20 rather than 10.
// ---------------------------------------------------------------------------
float linearToDb(float linear) {
  return 20.0f * std::log10(linear < 1e-9f ? 1e-9f : linear);
}

// ---------------------------------------------------------------------------
// dbToLinear(float db)
//
// __ZN7spectra10dbToLinearEf:
//     fmov s1, #20.00000000
//     fdiv s0, s0, s1
//     b    ___exp10f               ; tail call, no frame
//
// Exact inverse of linearToDb apart from the 1e-9f floor.
// ---------------------------------------------------------------------------
float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }

// ---------------------------------------------------------------------------
// hannWindow(int n)
//
// __ZN7spectra10hannWindowEi. Recovered shape:
//
//   * throws away nothing and returns empty for n < 1
//   * operator new(n * 4) then bzero — so the vector is a plain float array
//   * n == 1 is special-cased to {1.0f} (0x3f800000)
//   * otherwise, per element i:
//         ucvtf s0, w20          ; (float)i
//         fmul  s0, s0, s8       ; i * 2pi      (s8 = 0x40c90fdb == 2*pi)
//         fdiv  s0, s0, s1       ; / (float)n
//         bl    _cosf
//         fsub  s0, s9, s0       ; 1.0 - cos(...)     (s9 = 1.0f)
//         fmul  s0, s0, s10      ; * 0.5              (s10 = 0.5f)
//
// This is the PERIODIC Hann window: it divides by n, not n - 1. The compiler
// also emitted a 4-wide NEON version of the same arithmetic (movi #0x3f lsl 24
// == 0.5f), which is why the loop is unrolled; the scalar tail at 0x964 is the
// authoritative statement of the formula and is what is reproduced here.
//
// The n == 1 special case is not redundant: the general formula would give
// 0.5 * (1 - cos(0)) == 0 for a single sample, which is a degenerate window.
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
// hannCoherentGain()
//
// __ZN7spectra16hannCoherentGainEv:
//     fmov s0, #0.50000000
//     ret
//
// A constant, and a correct one: the coherent gain of a Hann window is its mean,
// which is 0.5. It agrees with hannWindow() above, which is a useful internal
// consistency check on that recovery.
// ---------------------------------------------------------------------------
float hannCoherentGain() { return 0.5f; }

// ---------------------------------------------------------------------------
// Spectrogram::~Spectrogram()
//
// __ZN7spectra11SpectrogramD1Ev exists in the binary at 0x1690. Its body is the
// standard member teardown of the frames vector, so the defaulted destructor is
// behaviourally equivalent. It is defined out-of-line here only because the
// class declares it (the binary exports it as a real symbol) and the test
// instantiates Spectrogram on the stack, which needs the definition to link.
// ---------------------------------------------------------------------------
Spectrogram::~Spectrogram() = default;

// ---------------------------------------------------------------------------
// Spectrogram::hzToBin(float hz) const
//
// __ZNK7spectra11Spectrogram7hzToBinEf — recovered in full, all 56 bytes:
//
//     ldr   d1, [x0, #0x8]      ; d1 = this->hzPerBin
//     fcmp  d1, #0.0
//     b.ls  <ret_zero>          ; hzPerBin <= 0 -> return 0, not a divide
//     fcvt  d0, s0              ; (double)hz
//     fdiv  d0, d0, d1          ; hz / hzPerBin
//     fcvtas x8, d0             ; round to nearest, ties away from zero
//     ldr   w9, [x0]            ; w9 = this->binCount
//     sub   w9, w9, #0x1        ; binCount - 1
//     cmp   w9, w8
//     csel  w8, w9, w8, lt      ; clamp high: bin > binCount-1 -> binCount-1
//     bic   w0, w8, w8, asr #31 ; clamp low: clears the sign bit, i.e. <0 -> 0
//     ret
//   <ret_zero>: mov w0, #0x0 ; ret
//
// Two things worth recording beyond the formula:
//
//   * fcvtas rounds ties AWAY FROM ZERO, not to even. A tie at exactly half a
//     bin therefore goes up in magnitude; for non-negative input that means up.
//   * The low clamp is done with `bic ..., asr #31` rather than a compare: for a
//     negative value the arithmetic shift yields all ones, and clearing those
//     bits in the result gives 0. That is a branchless max(x, 0).
//
// The guard returns 0 rather than dividing, so a spectrogram whose bin width was
// never set reads as bin 0 instead of producing inf/NaN.
// ---------------------------------------------------------------------------
int Spectrogram::hzToBin(float hz) const {
  if (!(hzPerBin > 0.0)) return 0;
  int bin = static_cast<int>(std::llround(static_cast<double>(hz) / hzPerBin));
  const int lastBin = binCount - 1;
  if (lastBin < bin) bin = lastBin;
  if (bin < 0) bin = 0;
  return bin;
}

}  // namespace spectra
