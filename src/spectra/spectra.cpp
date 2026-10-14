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

#include <algorithm>
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

// ---------------------------------------------------------------------------
// fftRadix2(std::vector<float>& re, std::vector<float>& im)
//
// __ZN7spectra9fftRadix2E..., 100 instructions, recovered in full. This is the
// most valuable single recovery in the fleet: tools/dsp-duplication.mjs proves
// the identical code is vendored into PathScribe and BeatSnap too, so one
// correct transcription covers three effects.
//
// Structure, stage by stage, with the instructions that establish each part.
//
// 1. GUARD (0x9cc..0xa14). Returns without touching anything unless all three
//    hold, and it compares BYTE LENGTHS for the size equality:
//        n = (re.end - re.data) / 4        asr x20, x8, #2
//        n >= 2                            cmp x20, #0x2 / b.lo <ret>
//        (im.end - im.data) == (re.end - re.data)   cmp x9, x8 / b.ne <ret>
//        isPowerOfTwo(n)                   subs w9,w8,#1 / and w8,w8,w9
//                                          ccmp / b.eq <proceed>
//
// 2. BIT-REVERSAL PERMUTATION (0xa18..0xa6c), so the caller does NOT pre-shuffle
//    and must pass natural order. j is built by the standard reversed counter:
//        mov x8,#0 (j=0) ; mov w9,#1 (i=1)
//      <0xa30> bit = n >> 1                    mov x10,x20 ; lsr x10,x10,#1
//              test j & bit                    tst  x8, x10
//              j ^= bit                        eor  x8, x8, x10
//              loop while the bit WAS set      b.ne <back to lsr>
//              j ^= bit                        (falls through on the last shift)
//        if (i >= j) skip the swap              cmp x9,x8 / b.hs
//      The swap moves BOTH arrays:
//        ldr s0,[x19,x9,lsl#2] / ldr s1,[x19,x8,lsl#2] / str s1.. / str s0..   (re)
//        ldr s0,[x21,x9,lsl#2] / ldr s1,[x21,x8,lsl#2] / str s1.. / str s0..   (im)
//
// 3. STAGES (0xa70..0xa8c): len starts at 2 and doubles while len <= n.
//        mov w22,#0x2 ; ... ; lsl x22,x22,#1 ; cmp x22,x20 ; b.hi <ret>
//    The twiddle base angle is loaded ONCE per stage as a single float:
//        mov w8,#0xfdb / movk w8,#0xc0c9,lsl#16
//      = 0xc0c90fdb = float(-2*pi). Negative, i.e. the FORWARD transform
//      convention, so no conjugation is applied anywhere.
//
// 4. PER-STAGE TWIDDLES (0xa90..0xaa8): one combined call, not sin and cos
//    separately:
//        ucvtf s0, x22        ; (float)len
//        fdiv  s0, s8, s0     ; -2*pi / len
//        bl    ___sincosf_stret   ; s0 = sin, s1 = cos
//        fneg  s2, s0         ; s2 = -sin, kept for the recurrence below
//        lsr   x9, x22, #1    ; half = len / 2
//        lsl   x10, x22, #2   ; stride = len * 4 bytes
//
// 5. BUTTERFLY (0xacc..0xb40), outer i += len, inner j in [0, half):
//        ldr s5, [x13]             ; a_re = re[i+j]
//        ldr s6, [x13, x9, lsl#2]  ; b_re = re[i+j+half]
//        ldr s7, [x14, x9, lsl#2]  ; b_im = im[i+j+half]
//        ldr s16,[x14]             ; a_im = im[i+j]
//        fnmul s17, s3, s7         ; t_re = -(w_im * b_im)
//        fmadd s17, s6, s4, s17    ;      + w_re * b_re
//        fmul  s7,  s4, s7         ; t_im = w_re * b_im
//        fmadd s6,  s6, s3, s7     ;      + w_im * b_re
//        fadd  -> re[i+j], im[i+j]           (a + t)
//        fsub  -> re[i+j+half], im[i+j+half] (a - t)
//
// 6. TWIDDLE RECURRENCE (0xb24..0xb30). The next twiddle is the current one
//    rotated by the base angle — a complex multiply by (cos, sin) — NOT a
//    recomputed sincos per element. Starting from (w_re, w_im) = (1, 0):
//        fmul   s5, s3, s2         ; w_im * (-sin)
//        fmul   s3, s1, s3         ; cos * w_im
//        fmadd  s3, s4, s0, s3     ; new_w_im = w_re*sin + w_im*cos
//        fmadd  s4, s4, s1, s5     ; new_w_re = w_re*cos - w_im*sin
//
//    Note two things a reimplementation commonly gets wrong: the recurrence
//    accumulates rounding error where per-element sincos would not, and the
//    -sin term (s2) is negated once per stage rather than per element.
//
// Verified in selfcheck.cpp against a naive DFT, which is the only check that
// can catch a sign or indexing error in the transform rather than just in the
// arithmetic. The test also asserts the guard rejects non-power-of-two input.
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
// rawBandLevel(const std::vector<float>& spectrum,
//              const Spectrogram& spectrogram,
//              const BandSpec& band)
//
// __ZN7spectra12rawBandLevelE..., 107 instructions, recovered in full. This is
// the function that turns a magnitude spectrum into the value the effect bakes
// as a keyframe, so it is the heart of "turn a spectrum into animatable numbers".
//
// PHASE 1 - guards (0x1704..0x1788):
//     ldp x8, x9, [x0]            ; spectrum begin/end
//     ldr w10, [x1]               ; binCount
//     cmp x8, x9 / ccmp w10,#1    ; spectrum non-empty AND binCount >= 1
//     b.lt <return 0.0>
//     ldr d1, [x1, #0x8]          ; hzPerBin
//     fcmp d1, #0.0 / b.ls        ; <= 0 -> both bin bounds become 0
//
// PHASE 2 - band edges, tolerant of a reversed range (0x1728..0x177c):
//     fcmp s2, s3 ; fcsel s4, s3, s2, mi   ; s4 = max(lowHz, highHz)
//     fcmp s3, s2 ; fcsel s2, s3, s2, mi   ; s2 = min(lowHz, highHz)
//   It SWAPS the edges rather than rejecting them, so a user who drags the band
//   backwards still gets a sensible level. Each edge is then mapped to a bin with
//   the same arithmetic as hzToBin - divide, fcvtas (ties away from zero), clamp
//   high against binCount-1 with csel, clamp low with bic/asr#31 - and the two
//   resulting bin indices are re-ordered the same way if they came out inverted.
//   If the high bin is at or past spectrum.size() the level is 0.
//
// PHASE 3 - per-bin accumulation (0x17c0..0x1800). One loop computes all three
//   measures at once and selects with chained fcsel, which is why the mode switch
//   costs nothing per iteration:
//     ldr s1, [x8, x13, lsl #2]   ; bin
//     fmadd s2, s1, s1, s0        ; sumSquares += bin*bin
//     fadd  s3, s0, s1            ; sum        += bin
//     fcsel s1, s1, s0, mi        ; peak = max(accumulator, bin)
//     cmp #1 / fcsel              ; mode 1 -> peak
//     cmp #2 / fcsel              ; mode 2 -> sum
//     cmp #3 / fcsel              ; mode 3 -> sumSquares
//   The three mode values come straight from the effect's own popup string
//   "Peak|Average|RMS".
//
// PHASE 4 - finalise the measure (0x1804..0x182c):
//     fdiv s2, s0, (float)count   ; Average = sum / count
//     fsqrt s3, s2                ; RMS     = sqrt(sumSquares / count)
//   then the same 1/2/3 selection picks between peak, average and RMS.
//
// PHASE 5 - dB and range mapping (0x1830..0x18a0). This is linearToDb INLINED,
//   recognisable by the identical 1e-9f guard (0x3089705f) and the *20 factor:
//     db = 20 * log10(max(level, 1e-9f))
//     db += band.gainDb
//     if (db < band.thresholdDb) return 0.0f
//     range = band.ceilingDb - band.floorDb
//     if (range <= 0.0f) return 0.0f
//     x = (db - band.floorDb) / range
//   then x is clipped at the top only:
//     fcsel s1, s1, s0, gt   ; s1 = (x > 1) ? 1.0f : x
//     fcsel s0, s0, s1, mi   ; s0 = (x < 0) ? x : s1
//
// OBSERVED EDGE CASE, preserved deliberately rather than "fixed" here.
// That last pair is NOT clampf. Where clampf returns the low bound for a value
// below it, this returns THE NEGATIVE VALUE ITSELF. A band level can therefore
// come out negative. In the shipped parameter set it is normally unreachable:
// the threshold test above returns 0 first whenever floorDb >= thresholdDb. But
// a user who sets floorDb < thresholdDb can reach it, and the effect would bake a
// negative value. This is a candidate improvement, and it is reproduced here
// exactly as compiled so the recovered behaviour and the suggestion stay
// distinguishable.
// ---------------------------------------------------------------------------
float rawBandLevel(const std::vector<float>& spectrum,
                   const Spectrogram& spectrogram,
                   const BandSpec& band) {
  if (spectrum.empty() || spectrogram.binCount < 1) return 0.0f;

  int loBin = 0;
  int hiBin = 0;

  if (spectrogram.hzPerBin > 0.0) {
    const float loHz = band.lowHz < band.highHz ? band.lowHz : band.highHz;
    const float hiHz = band.lowHz < band.highHz ? band.highHz : band.lowHz;

    const int lastBin = spectrogram.binCount - 1;
    auto toBin = [&](float hz) {
      int bin = static_cast<int>(std::llround(static_cast<double>(hz) / spectrogram.hzPerBin));
      if (lastBin < bin) bin = lastBin;
      if (bin < 0) bin = 0;
      return bin;
    };

    int a = toBin(loHz);
    int b = toBin(hiHz);
    loBin = a > b ? b : a;
    hiBin = a > b ? a : b;
  }

  if (hiBin >= static_cast<int>(spectrum.size())) return 0.0f;

  // Phase 3+4: all three measures accumulated in one pass.
  float accum = 0.0f;
  float peak = 0.0f;
  int count = 0;
  for (int bin = loBin; bin <= hiBin; ++bin) {
    const float v = spectrum[static_cast<size_t>(bin)];
    const float sumSquares = v * v + accum;
    const float sum = accum + v;
    const float newPeak = accum < v ? v : accum;
    if (band.mode == 1) accum = newPeak;
    else if (band.mode == 2) accum = sum;
    else if (band.mode == 3) accum = sumSquares;
    peak = newPeak;
    ++count;
  }

  if (count < 1) return 0.0f;

  const float average = accum / static_cast<float>(count);
  const float rms = std::sqrt(average);
  float level = accum;
  if (band.mode == 1) level = peak;
  else if (band.mode == 2) level = average;
  else if (band.mode == 3) level = rms;

  // Phase 5: inlined linearToDb, gain, threshold, then the floor/ceiling range.
  const float db = 20.0f * std::log10(level < 1e-9f ? 1e-9f : level) + band.gainDb;
  if (db < band.thresholdDb) return 0.0f;

  const float range = band.ceilingDb - band.floorDb;
  if (range <= 0.0f) return 0.0f;

  const float x = (db - band.floorDb) / range;
  const float clippedHigh = x > 1.0f ? 1.0f : x;
  return x < 0.0f ? x : clippedHigh;  // negative passes through — see note above
}


}  // namespace spectra
