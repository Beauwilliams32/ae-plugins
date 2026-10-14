// glitch.cpp — recovered implementations from the GlitchKit bundle.

#include "glitch.h"

namespace glitch {

// ---------------------------------------------------------------------------
// clampf(float, float, float)
//
// __ZN6glitch6clampfEfff:
//     fcmp  s0, s2 / fcsel s2, s2, s0, gt
//     fcmp  s0, s1 / fcsel s0, s1, s2, mi
//     ret
//
// Byte-for-byte the same as spectra::clampf, lutblend::clampf and
// typeriser::clampf. Four separate namespaces in four separate bundles converged
// on this exact branchless form, which is strong evidence all four transcriptions
// are right — and a test asserts they still agree.
// ---------------------------------------------------------------------------
float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

// ---------------------------------------------------------------------------
// clampi(int, int, int)
//
// __ZN6glitch6clampiEiii:
//     cmp  w0, w2 / csel w8, w0, w2, lt    ; w8 = min(value, hi)
//     cmp  w0, w1 / csel w0, w1, w8, lt    ; w0 = (value < lo) ? lo : w8
//     ret
//
// The integer twin of clampf, with the same shape: the low bound is tested LAST
// so it wins when the range is inverted, exactly as in the float version. Keeping
// that ordering matters — `min(max(v, lo), hi)` would return hi instead of lo for
// an inverted range.
// ---------------------------------------------------------------------------
int clampi(int value, int lo, int hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

// ---------------------------------------------------------------------------
// hashKey(unsigned, unsigned, unsigned)
//
// __ZN6glitch7hashKeyEjjj, 34 instructions, recovered in full.
//
// The shape is two rounds of the same three-step avalanche:
//
//     x = a * 0x85ebca6b;
//     y = a * 0x7af29ac0 + b;      ; madd
//     y += x >> 2;                 ; the shifted add stands in for a multiply
//     y += 0x9e3779b9;
//     x = y ^ x;
//
//     p = x * 0xc2b2ae35;
//     q = x * 0xacab8d40 + c;      ; madd — carries the third input in
//     q += p >> 2;
//     q += 0x27d4eb2f;
//
//     h  = q ^ p;
//     h ^= h >> 16;  h *= 0x7feb352d;
//     h ^= h >> 15;  h *= 0x846ca68b;
//     h ^= h >> 16;
//     return (p != q) ? h : 1;     ; cmp + csinc
//
// The tail is transcribed literally. `csinc w0, w10, wzr, ne` after `cmp w9, w8`
// is a conditional increment: the mixed value normally, but 1 when the two
// intermediate stages are equal. What that guard was for is NOT recoverable from
// the code, so it is reproduced rather than rationalised — an earlier draft
// "simplified" it to `return h;` on the assumption it was a zero-guard, which the
// comparison does not support.
//
// Note the constants are not the usual MurmurHash3 set end to end, and the
// two `madd + (x >> 2)` pairs are how the compiler emitted whatever the original
// multiplier was.
// ---------------------------------------------------------------------------
unsigned int hashKey(unsigned int a, unsigned int b, unsigned int c) {
  unsigned int x = a * 0x85ebca6bu;
  unsigned int y = a * 0x7af29ac0u + b;
  y += x >> 2;
  y += 0x9e3779b9u;
  x = y ^ x;

  const unsigned int p = x * 0xc2b2ae35u;
  unsigned int q = x * 0xacab8d40u + c;
  q += p >> 2;
  q += 0x27d4eb2fu;

  unsigned int h = q ^ p;
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  h ^= h >> 16;

  return (p != q) ? h : 1u;
}

// ---------------------------------------------------------------------------
// patternFrame(unsigned, float)
//
// __ZN6glitch12patternFrameEjf:
//     ucvtf  d1, w0        ; (double)seed, UNSIGNED
//     fcvt   d2, s0        ; (double)time
//     fmul   d1, d1, d2
//     fcvtzu w8, d1        ; truncate toward zero
//     fcmp   s0, #0.0
//     csel   w0, w8, wzr, hi
//     ret
//
// Two details worth keeping:
//   * the multiply is done in DOUBLE, not float, and only the product is
//     truncated. Doing it in float would round before the truncation and pick the
//     wrong frame at some time values.
//   * the guard is `hi`, i.e. strictly greater than zero (or unordered), so
//     time == 0 yields 0 rather than the seed. Negative times also yield 0.
// ---------------------------------------------------------------------------
unsigned int patternFrame(unsigned int seed, float time) {
  const double product = static_cast<double>(seed) * static_cast<double>(time);
  const unsigned int frame = static_cast<unsigned int>(product);
  // Written as a negated `<=` rather than `time > 0`, because that is what the
  // shipped code does: the original emits `csel w0, w8, wzr, hi` and a `time > 0`
  // ternary emits `gt` instead. `hi` is true for greater-than OR UNORDERED, so a
  // NaN time takes the product branch where `gt` would take the zero branch. The
  // two agree in practice only because fcvtzu(NaN) is 0 — matching the form keeps
  // the codegen identical rather than relying on that coincidence.
  if (!(time <= 0.0f)) return frame;
  return 0u;
}

}  // namespace glitch
