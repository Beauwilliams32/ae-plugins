// beatgrid.cpp — transcribed from the compiled BeatGridRuler bundle.

#include "beatgrid.h"

#include <cmath>

namespace beatgrid {

// The three shared functions are the fleet's DSP, machine-code-identical to SpectraKeys'
// copies and to the ones already recovered in beatgrid's siblings. `tools/dsp-duplication.mjs`
// is what establishes that; `test/beatgrid.test.mjs` checks the copying by divergence
// parity against this bundle.

float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

bool isPowerOfTwo(int n) { return n < 1 ? false : (n & (n - 1)) == 0; }

// ---------------------------------------------------------------------------
// linearToDb(float linear)
//
// __ZN8beatgrid10linearToDbEf:
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

// __ZNK8beatgrid13OnsetEnvelope16frameTimeSecondsEi, 13 instrs
//
//   ldr d1, [x0, #0x18] / movi.2d v0, #0 / fcmp d1, #0.0 / b.ls <ret>
//       ; sampleRate <= 0 -> return 0.0
//   scvtf d0, w1                       ; (double)frame
//   ldr s2, [x0, #0x24] / sshll.2d v2, v2, #0x0 / scvtf d2, d2
//       ; the hop size at +0x24, SIGN-EXTENDED, then converted
//   ldr w8, [x0, #0x20] / scvtf d3, w8, #0x1
//       ; (double)the window size, fixed-point scaled by 1/2 -- the window's half
//   fmadd d0, d0, d2, d3 / fdiv d0, d0, d1
//       ; (frame * hopSize + windowSize / 2) / sampleRate
double OnsetEnvelope::frameTimeSeconds(int frame) const {
  if (sampleRate <= 0.0) return 0.0;
  return (static_cast<double>(frame) * static_cast<double>(hopSize)
          + static_cast<double>(windowSize) * 0.5) / sampleRate;
}

// __ZN8beatgrid17subdivisionFactorENS_11SubdivisionE, 8 instrs
//
//   sub w8, w0, #0x2 / fmov d0, #1.0 / cmp w8, #0x3 / b.hi <ret>
//       ; the default is 1.0, and the table is indexed by (value - 2) UNSIGNED, so
//       ; only 2, 3, 4 and 5 reach it and negatives wrap out of range
//   adrp x9, <page> / add x9, x9, #0x7e0 / ldr d0, [x9, w8, uxtw #3]
//       ; four doubles from the literal pool at 0x77e0 in __TEXT,__const:
//       ; worth reading rather than guessing, because the fourth is 4.0 and not a
//       ; fraction:  0x3fe0000000000000, 0x3fd0000000000000,
//       ;            0x3fc0000000000000, 0x4010000000000000
double subdivisionFactor(Subdivision subdivision) {
  switch (subdivision) {
    case Subdivision::Half: return 0.5;
    case Subdivision::Quarter: return 0.25;
    case Subdivision::Eighth: return 0.125;
    case Subdivision::FourBeats: return 4.0;
    default: return 1.0;
  }
}

// __ZN8beatgrid16phaseInLayerTimeEdddd, 24 instrs
//
//   movi.2d v4, #0 / fcmp d0, #0.0 / b.le <ret>     ; bpm <= 0 -> return 0.0
//   mov x8, #0x404e000000000000 / fmov d4, x8       ; 60.0
//   fadd d1, d1, d2 / fsub d1, d1, d3               ; (a + b) - c
//   fdiv d8, d4, d0                                 ; period = 60 / bpm
//   bl _fmod                                        ; fmod((a+b)-c, period)
//   fadd d1, d8, d0 / fcmp d0, #0.0 / fcsel d0, d1, d0, mi
//                                                   ; a negative remainder wraps UP
//   fsub d1, d0, d8 / fcmp d0, d8 / fcsel d4, d0, d1, lt
//                                                   ; and one subtraction if it reached
//                                                   ; the period, which fmod cannot do
//   mov.16b v0, v4                                  ; returns a DOUBLE
double phaseInLayerTime(double bpm, double a, double b, double c) {
  if (!(bpm > 0.0)) return 0.0;
  const double period = 60.0 / bpm;
  double phase = std::fmod((a + b) - c, period);
  if (phase < 0.0) phase = period + phase;
  return phase < period ? phase : phase - period;
}

// __ZN8beatgrid13phaseSawtoothEddNS_11SubdivisionEd, 31 instrs
//
//   mov v3, v0 / movi.2d v0, #0 / fcmp d3, #0.0 / b.ls <ret>   ; bpm <= 0 -> 0
//   mov x8, #0x404e000000000000 / fmov d5, x8                  ; 60.0
//   sub w8, w0, #0x2 / fmov d4, #1.0 / fdiv d3, d5, d3         ; 60 / bpm
//   cmp w8, #0x3 / b.hi <skip> / adrp+add+ldr                  ; the same four-entry table
//   fmul d8, d3, d4                                            ; period = that * factor
//   fcmp d8, #0.0 / b.ls <ret zero>                            ; period <= 0 -> 0
//   fsub d0, d2, d1                                            ; time - startTime
//   mov v1, v8 / bl _fmod                                      ; fmod(that, period)
//   fadd d1, d8, d0 / fcmp d0, #0.0 / fcsel d0, d1, d0, mi     ; wrap a negative up
//   fdiv d0, d0, d8 / fcvt s0, d0                              ; -> a FLOAT
float phaseSawtooth(double bpm, double startTime, Subdivision subdivision, double time) {
  if (!(bpm > 0.0)) return 0.0f;
  const double period = (60.0 / bpm) * subdivisionFactor(subdivision);
  if (!(period > 0.0)) return 0.0f;
  double phase = std::fmod(time - startTime, period);
  if (phase < 0.0) phase = period + phase;
  return static_cast<float>(phase / period);
}

// ---------------------------------------------------------------------------
// __ZN8beatgrid20estimatePhaseSecondsERKNS_13OnsetEnvelopeEf, 138 instrs
//
// The listing (/tmp/ae/beatgrid.asm lines 1405-1543) says:
//
//   ldp x20, x8, [x0]        ; the envelope's values, begin and end
//   cmp x20, x8 / b.eq       ; empty -> 0
//   fcmp s1, #0.0 / b.ls     ; bpm <= 0 -> 0
//   ldr d10, [x0, #0x28]     ; the frame rate
//   fcmp d10, #0.0 / b.ls    ; a non-positive frame rate -> 0
//   fdiv d8, d1, d0          ; period = 60 / bpm
//   ucvtf d2, x22            ; the frame count
//   fdiv d12, d2, d10        ; duration = count / frameRate
//   ldr d3, [x0, #0x18]      ; the sample rate
//   ldr w9, [x0, #0x20] / scvtf d4, w9, #0x1 / fdiv d11, d4, d3
//       ; halfWindow = (windowSize / 2) / sampleRate, and ZERO when the sample
//       ; rate is not positive
//   mov w0, #0x1000 / bl __Znwm / bl _bzero
//       ; a 512-entry DOUBLE score table, zeroed
//
// and then, for each of the 512 candidates, a score that is the sum of the envelope
// sampled every `period` seconds, linearly interpolated between frames:
//
//   fcvtms w12, d7           ; the frame index, truncated toward zero
//   cmp w10, w12 / b.gt      ; below the last frame -> interpolate
//   fsub s7, s2, s6          ; 1 - the fraction
//   fmadd s6, s16, s7, s6    ; values[f] * (1 - frac) + values[f+1] * frac
//
// The winner is then refined against its two neighbours:
//
//   fmadd d1, d1, d3, d0     ; p - 2c
//   fadd d3, d1, d2          ; p - 2c + n
//   fabs d4, d3 / fcmp d4, 1e-12
//   fsub d0, d0, d2 / fmul d0, d0, 0.5 / fdiv d0, d0, d3
//       ; shift = (p - n) / 2 / curvature, clamped to [-1, 1]
//
// and the phase is wrapped into [0, period) with `fmod`.
// ---------------------------------------------------------------------------
float estimatePhaseSeconds(const OnsetEnvelope& envelope, float bpm) {
  const std::vector<float>& values = envelope.values;
  if (values.empty()) return 0.0f;
  if (bpm <= 0.0f) return 0.0f;

  const double rate = envelope.framesPerSecond;
  if (rate <= 0.0) return 0.0f;

  const double period = 60.0 / static_cast<double>(bpm);
  const size_t count = values.size();
  const double duration = static_cast<double>(count) / rate;

  // The half window is the time from the START of a frame to its CENTRE. It is zero
  // when the sample rate is not positive, exactly as the listing guards it.
  double halfWindow = 0.0;
  if (envelope.sampleRate > 0.0) {
    halfWindow = (static_cast<double>(envelope.windowSize) * 0.5) / envelope.sampleRate;
  }

  std::vector<double> scores(512);
  double bestScore = -1.0;
  double bestTime = 0.0;
  size_t bestIndex = 0;

  for (size_t i = 0; i < 512; ++i) {
    const double t0 = static_cast<double>(i) * period * 0.5;
    double score = 0.0;
    double t = t0;
    while (t < duration) {
      const double frameF = rate * (t - halfWindow);
      float v = 0.0f;
      if (frameF >= 0.0) {
        const int f = static_cast<int>(frameF);
        if (f < static_cast<int>(count) - 1) {
          const float frac = static_cast<float>(frameF - static_cast<double>(f));
          v = values[static_cast<size_t>(f)] * (1.0f - frac)
            + values[static_cast<size_t>(f) + 1] * frac;
        } else if (count >= 1) {
          v = values[count - 1];
        }
      }
      score += static_cast<double>(v);
      t += period;
    }
    scores[i] = score;
    if (score > bestScore) {
      bestScore = score;
      bestIndex = i;
      bestTime = t0;
    }
  }

  // The neighbours of the winner, WRAPPED: 511 and 0 are neighbours of 0.
  const size_t prev = (bestIndex + 511) & 511;
  const size_t next = (bestIndex + 1) & 511;
  const double p = scores[prev];
  const double c = scores[bestIndex];
  const double n = scores[next];
  const double curvature = (p - 2.0 * c) + n;

  double shift = 0.0;
  if (std::fabs(curvature) >= 1e-12) {
    shift = (p - n) * 0.5 / curvature;
    if (shift < -1.0) shift = -1.0;
    if (shift > 1.0) shift = 1.0;
  }

  double phase = std::fmod(bestTime + shift * (0.5 * period), period);
  if (phase < 0.0) phase += period;
  return static_cast<float>(phase);
}

// ---------------------------------------------------------------------------
// __ZN8beatgrid16generateBeatGridEddNS_11SubdivisionEdd, 94 instrs
//
// The listing (/tmp/ae/beatgrid.asm lines 1611-1705) says:
//
//   stp xzr, xzr, [x8] / str xzr, [x8, #0x10]
//       ; the returned vector is emptied FIRST, so every early exit is an empty grid
//   fcmp d0, #0.0 / b.ls   ; bpm <= 0 -> empty
//   fcmp d3, #0.0 / b.ls   ; the region length <= 0 -> empty
//   fdiv d9, d4, d0        ; the beat period, 60 / bpm
//   ldr d1, [x9, w8, uxtw #3]   ; the same four-entry subdivision table
//   fmul d10, d9, d1       ; the marker period
//   fcmp d10, #0.0 / b.ls  ; a non-positive period -> empty
//   fadd d11, d2, d3       ; the end of the region
//   fsub d0, d2, d8 / fdiv d0, d0, d10 / frintp d0
//       ; ceil((regionStart - origin) / period)
//   fmadd d12, d0, d10, d8 ; ... * period + origin
//   fcmp d12, d0 / b.pl    ; and then advanced while it is before the region
//   fcmp d12, d11 / b.pl   ; past the end of the region -> empty
//
// and the marker number:
//
//   fsub d0, d12, d8 / fdiv d0, d0, d9   ; the beat index from the origin
//   fmul d1, d0, 0.25 / frinta d1, d1    ; rounded to the nearest quarter beat
//   cmp w20, #0x5 / fcsel d0, d1, d0, eq ; ... but only for the four-beat subdivision
//   fadd d0, d0, d14                    ; the marker number is that plus one
//   stp d12, d0, [sp]                   ; the marker: its time, then its number
// ---------------------------------------------------------------------------
std::vector<BeatMarker> generateBeatGrid(double bpm, double origin, Subdivision subdivision,
                                          double regionStart, double regionLength) {
  std::vector<BeatMarker> markers;
  if (bpm <= 0.0) return markers;
  if (regionLength <= 0.0) return markers;

  const double beatPeriod = 60.0 / bpm;
  const double period = beatPeriod * subdivisionFactor(subdivision);
  if (period <= 0.0) return markers;

  const double regionEnd = regionStart + regionLength;
  double t = std::ceil((regionStart - origin) / period) * period + origin;
  // The shipped code compares against `regionStart - 1e-9`, so a marker that lands a
  // hair before the region is still emitted rather than dropped by a rounding error.
  while (t < regionStart - 1e-9) t += period;
  if (t >= regionEnd) return markers;

  while (t < regionEnd) {
    double index = (t - origin) / beatPeriod;
    if (subdivision == Subdivision::FourBeats) {
      // The four-beat subdivision numbers its markers in QUARTER beats.
      index = std::nearbyint(index * 0.25);
    }
    index += 1.0;
    markers.push_back(BeatMarker{t, index});
    t += period;
  }
  return markers;
}

}  // namespace beatgrid
