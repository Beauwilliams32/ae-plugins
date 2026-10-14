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
//       ; hopInterval <= 0 -> return 0.0
//   scvtf d0, w1                       ; (double)frame
//   ldr s2, [x0, #0x24] / sshll.2d v2, v2, #0x0 / scvtf d2, d2
//       ; the 16-bit field at +0x24, SIGN-EXTENDED, then converted
//   ldr w8, [x0, #0x20] / scvtf d3, w8, #0x1
//       ; (double)that field, fixed-point scaled by 1/2 -- so it counts half-frames
//   fmadd d0, d0, d2, d3 / fdiv d0, d0, d1
//       ; (frame * step + startHalf / 2) / hopInterval
double OnsetEnvelope::frameTimeSeconds(int frame) const {
  if (hopInterval <= 0.0) return 0.0;
  return (static_cast<double>(frame) * static_cast<double>(step)
          + static_cast<double>(startFrameHalf) / 2.0) / hopInterval;
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

}  // namespace beatgrid
