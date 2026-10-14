// noisefield.cpp — transcribed from the compiled NoiseField bundle.
//
// Three functions and one table. Each carries the instructions that establish it,
// because that is the whole recovery: a claim about the original that cannot be
// traced to an instruction or to bytes in the binary does not belong here.

#include "noisefield.h"

namespace noisefield {

// __ZN10noisefield6clampfEfff, 5 instrs
//
//   fcmp s0, s2 / fcsel s2, s2, s0, gt   ; s2 = (v > hi) ? hi : v
//   fcmp s0, s1 / fcsel s0, s1, s2, mi   ; s0 = (v < lo) ? lo : s2
float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

// __ZN10noisefield6clampiEiii, 5 instrs
//
//   cmp w0, w2 / csel w8, w0, w2, lt     ; w8 = (v < hi) ? v : hi
//   cmp w0, w1 / csel w0, w1, w8, lt     ; w0 = (v < lo) ? lo : w8
//
// The conditions are inverted relative to clampf's but the behaviour is the same:
// the low bound is applied last, so an inverted range yields `lo`.
int clampi(int value, int lo, int hi) {
  return value < lo ? lo : (value < hi ? value : hi);
}

// __ZN10noisefield15fbmAmplitudeSumEif, 15 instrs
//
//   cmp w0, #0x1 / csinc w8, w0, wzr, gt ; w8 = octaves > 1 ? octaves : 1
//   fmov s2, #1.0 / movi.2d v1, #0       ; amp = 1, sum = 0
//   fadd s1, s2, s1                      ; sum += amp
//   fmul s2, s0, s2                      ; amp *= gain
//   subs w8, w8, #1 / b.ne               ; n times
//   fmov s0, #1e-6                       ; 0x358637bd, exactly 1e-6f
//   fcmp s1, s0 / fmov s0, #1.0 / fcsel s0, s1, s0, gt
//                                        ; (sum > 1e-6) ? sum : 1.0
float fbmAmplitudeSum(int octaves, float gain) {
  const int count = octaves > 1 ? octaves : 1;
  // Declaration order and operand order both come from the instruction stream and
  // both matter to the last bit of the schedule: the shipped code builds 1.0f before
  // 0.0f (`fmov s2, #1.0` then `movi.2d v1, #0`), and accumulates as
  // `fadd s1, s2, s1` — that is `amplitude + sum`, not `sum + amplitude`.
  float amplitude = 1.0f;
  float sum = 0.0f;
  for (int i = 0; i < count; ++i) {
    sum = amplitude + sum;
    amplitude *= gain;
  }
  return sum > 1e-6f ? sum : 1.0f;
}

// __ZN10noisefield12_GLOBAL__N_16kGrad3E, in __TEXT,__const
//
// Read out of the arm64 slice's __const section at 0x3c28 as 36 32-bit words, each
// holding one small integer. The canonical twelve edge gradients of Perlin's improved
// noise, in order, which the self-check asserts entry by entry.
const int kGrad3[12][3] = {
    {1, 1, 0},   {-1, 1, 0},  {1, -1, 0},  {-1, -1, 0},
    {1, 0, 1},   {-1, 0, 1},  {1, 0, -1},  {-1, 0, -1},
    {0, 1, 1},   {0, -1, 1},  {0, 1, -1},  {0, -1, -1},
};

}  // namespace noisefield
