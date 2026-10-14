// autoflickerfix.cpp — recovered implementations from the AutoFlickerFix bundle.
//
// Every `aff::` function below is transcribed from the shipped arm64 code. The comment
// above each one records the mangled symbol, the instruction count in the bundle, and
// the load-bearing details of the encoding. Where a form was chosen to match the
// binary's codegen rather than for readability, that is said out loud.

#include "autoflickerfix.h"

#include <algorithm>
#include <cmath>

namespace aff {

// ---------------------------------------------------------------------------
// clampf(float, float, float)   __ZN3aff6clampfEfff   5 instructions
//
//     fcmp  s0, s2                 ; value vs hi
//     fcsel s2, s2, s0, gt         ; s2 = value > hi ? hi : value
//     fcmp  s0, s1                 ; value vs lo   <- the ORIGINAL value, not s2
//     fcsel s0, s1, s2, mi         ; s0 = value < lo ? lo : s2
//     ret
//
// The nested ternary must be written high-bound-first for clang to emit that order.
// ---------------------------------------------------------------------------
float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

// ---------------------------------------------------------------------------
// dbToGainClamp(float)          __ZN3aff13dbToGainClampEf   13 instructions
//
//     fabs  s0, s0                 ; the SIGN OF db IS DISCARDED
//     fmov  s1, #20.0
//     fdiv  s0, s0, s1
//     bl    ___exp10f              ; s0 = gain = 10^(|db|/20)
//     mov.16b v1, v0               ; s1 = gain  <- the SECOND member of the result
//     fmov  s0, #1.0
//     fdiv  s2, s0, s1             ; s2 = 1 / gain
//     fcmp  s1, #0.0
//     fcsel s0, s2, s0, gt         ; s0 = gain > 0 ? 1/gain : 1.0
//     ret                          ; the aggregate comes back in s0 AND s1
//
// Returns `GainClamp{ 1/gain, gain }` — an 8-byte two-float aggregate, returned in the
// pair of registers a float return would use only one of. The `mov.16b v1, v0` is the
// fingerprint: a function returning a single float has nothing to copy into s1, and the
// call site stores both with one `stp` and never writes s1.
//
// `__builtin_exp10f` rather than `exp10f`: the bundle calls `___exp10f`, but `exp10f` is
// a Darwin extension that <math.h> only declares above strict-ANSI, so under -std=c++17
// the plain name does not exist. The builtin lowers to exactly that libm symbol, which a
// three-line probe confirmed before the transcription was written. `powf(10, x)` would
// have been the portable choice and would NOT have matched.
// ---------------------------------------------------------------------------
GainClamp dbToGainClamp(float db) {
  const float gain = __builtin_exp10f(fabsf(db) / 20.0f);
  return GainClamp{gain > 0.0f ? 1.0f / gain : 1.0f, gain};
}

// ---------------------------------------------------------------------------
// computeGain(float, float, float, const GainClamp&)
//                               __ZN3aff11computeGainEfffRKNS_9GainClampE   23 instructions
//
//     fcmp s0, 1e-6f / fccmp s1, 1e-6f, #0x0, hi / b.ls  -> return 1.0f
//
// The paired `fcmp`+`fccmp` is the short-circuit `a || b` on the two input guards: the
// second comparison only sets the flags if the first was `hi`, otherwise the immediate
// #0x0 is substituted (Z=1) and the branch is taken. So the guard really is
// "current <= 1e-6 OR target <= 1e-6", tested on the inputs rather than on the ratio.
//
//     1e-6f is materialised as mov #0x37bd / movk #0x3586, lsl #16 -- 0x358637bd.
//
//     fdiv  s0, s1, s0             ; s1 / s0  -- TARGET OVER CURRENT, i.e. arg2/arg1
//
// The division is the one place the argument order is visible in the body, and it is
// the reason the parameters are named current-then-target rather than the reverse.
//
// The lerp is `fmadd s0, s2, s0, s3`, i.e. t * (ratio - 1) + 1.0, so the multiply comes
// first in the source; writing `1.0f + t * (...)` makes clang emit a separate fadd.
// ---------------------------------------------------------------------------
float computeGain(float current, float target, float amount, const GainClamp& clamp) {
  if (current <= 1e-6f || target <= 1e-6f) return 1.0f;
  const float t = clampf(amount, 0.0f, 1.0f);
  const float gain = t * (target / current - 1.0f) + 1.0f;
  return clampf(gain, clamp.lo, clamp.hi);
}

// ---------------------------------------------------------------------------
// centerRegion(int, int, float)  __ZN3aff12centerRegionEiif   29 instructions
//
//     fcvtas x8, s1                ; 64-bit round-to-nearest-ties-AWAY, then used as w8
//
// `fcvtas` is `lroundf`, not a cast: a plain `(int)f` would be `fcvtzs` and would round
// 2.5 to 2 instead of 3. The 64-bit form is what clang emits for the `int` overload.
//
//     sub  w10, w0, w8 / add w10, w10, w10, lsr #31 / asr w11, w10, #1
//     bic  w10, w11, w10, asr #31
//
// That triple is signed division by two truncating toward zero, and the `bic` is
// "clamp the result to zero when dx is negative" -- the test is on `dx`, the halving is
// speculative. `dx/2 + rw` is then computed from the UNCLAMPED half and min'd with the
// dimension, so an over-wide fraction is bounded by the frame rather than by zero.
//
// Returned in x0/x1 as minX|minY<<32 and maxX|maxY<<32; there is no sret pointer.
// ---------------------------------------------------------------------------
RegionBounds centerRegion(int width, int height, float frac) {
  const float f = clampf(frac, 0.0f, 1.0f);
  const int rw = static_cast<int>(lroundf(f * static_cast<float>(width)));
  const int rh = static_cast<int>(lroundf(f * static_cast<float>(height)));
  const int dx = width - rw;
  const int dy = height - rh;
  const int halfW = dx / 2;
  const int halfH = dy / 2;
  return RegionBounds{
      std::max(halfW, 0),
      std::max(halfH, 0),
      std::min(halfW + rw, width),
      std::min(halfH + rh, height),
  };
}

// ---------------------------------------------------------------------------
// meanOf(const std::vector<float>&)   __ZN3aff6meanOfERKNSt3__16vectorIfNS0_9allocatorIfEEEE
//                               92 instructions
//
//     ldp x11, x9, [x0] / cmp / b.eq 0xcc4      ; 0xcc4 is `movi.2d v0, #0` then `ret`
//
// THE EMPTY CASE RETURNS 0.0f AND IS NOT THE TAIL OF THE DIVISION. It is worth stating
// because the first transcription of this function left the guard implicit and let an
// empty vector fall through to `0.0 / 0.0` — a NaN. The shipped code has its own `ret`
// on that path, and the codegen differ is what surfaced it: 93 instructions against the
// bundle's 92, with the extra one being a branch to a shared epilogue where the binary
// simply returns. Every other function in this namespace had been read correctly; this
// one was wrong in exactly the way a self-check written from the same misreading would
// have confirmed as right.
//
// The accumulator is double: the body is a chain of `fcvtl v.2d, v.4s` widening each
// float and `fadd d0, d0, dN` in nine distinct pairings, then `ucvtf d1, x8` and
// `fdiv d0, d0, d1` before a single `fcvt s0, d0` at the very end. Accumulating in
// float instead would not reproduce this and would drift on long inputs.
//
// The count is re-derived from the byte length (`sub x8, x9, x11` then `asr x8, x8, #2`)
// rather than kept live, which is what `.size()` compiles to here.
// ---------------------------------------------------------------------------
float meanOf(const std::vector<float>& values) {
  if (values.empty()) return 0.0f;
  double sum = 0.0;
  for (const float v : values) sum += v;
  return static_cast<float>(sum / static_cast<double>(values.size()));
}

// ---------------------------------------------------------------------------
// medianOf(std::vector<float>)   __ZN3aff8medianOfENSt3__16vectorIfNS0_9allocatorIfEEEE
//                               28 instructions
//
//     ldr  x0, [x0] / ldr x1, [x19, #0x8] / cmp / b.eq -> 0.0f
//     bl   __ZNSt3__16__sortIRNS_6__lessIffEEPfEEvT0_S5_T_   ; std::sort(begin, end)
//     ldp  x9, x8, [x19] / sub x10, x8, x9 / asr x8, x10, #3 / add x8, x9, x8, lsl #2
//     tbnz w10, #0x2                    ; bit 2 of the BYTE length == n is odd
//
// The middle index is computed in one go as `begin + (bytes/8)*4`, i.e. element n/2
// (integer division), and the parity test is on the byte count. Even length takes
// `ldp s0, s1, [x8, #-0x4]` -- elements n/2-1 and n/2 -- adds them and multiplies by
// the 0.5f in s8. A `0.5` double here would be `fmul d0` and is not what is there.
// ---------------------------------------------------------------------------
float medianOf(std::vector<float> values) {
  if (values.empty()) return 0.0f;
  std::sort(values.begin(), values.end());
  const size_t n = values.size();
  if (n % 2 != 0) return values[n / 2];
  return (values[n / 2 - 1] + values[n / 2]) * 0.5f;
}

// ---------------------------------------------------------------------------
// trimmedMean(const float*, size_t, float)
//                               __ZN3aff11trimmedMeanEPKfmf   230 instructions
//
//     cbz  x1 -> 0.0f ; subs x22, x1, #1 / b.ne -> main ; ldr s0, [x0]   ; n == 1
//
// n == 0 and n == 1 are answered before anything is allocated.
//
//     mov w8, #0xe148 / movk w8, #0x3efa, lsl #16    ; 0x3efae148 == 0.49f
//
// The trim fraction is clamped to [0, 0.49], so `k` can never reach half the array and
// the window always keeps at least one sample. 0.5 would let it empty.
//
//     ucvtf s1, x23 / fmul s0, s0, s1 / fcvtmu x9, s0    ; cnt = (size_t)(t * n)
//     lsr   x10, x22, #1                                 ; (n-1)/2
//     cmp   x23, x9, lsl #1 / csel x10, x9, x10, hi      ; k = n > 2*cnt ? cnt : (n-1)/2
//
// which is `std::min(cnt, (n-1)/2)` -- clang tested `2*cnt < n` instead of `cnt <= (n-1)/2`
// because it could prove them equivalent. Since k <= (n-1)/2 always holds, n - 2k >= 1
// always holds, and the whole-array branch at 0x7d0 is UNREACHABLE. It is kept because
// dropping it changes the emitted stream, and because "unreachable" is a claim about
// the arithmetic rather than something the compiler can see.
//
// The sum is over `buf[k .. n-k)` with a double accumulator, divided by n - 2k.
// ---------------------------------------------------------------------------
float trimmedMean(const float* data, size_t count, float trim) {
  if (count == 0) return 0.0f;
  if (count == 1) return data[0];

  std::vector<float> buf(data, data + count);
  std::sort(buf.begin(), buf.end());

  const float t = clampf(trim, 0.0f, 0.49f);
  const size_t cnt = static_cast<size_t>(t * static_cast<float>(count));
  const size_t k = std::min(cnt, (count - 1) / 2);

  size_t lo = k;
  size_t hi = count - k;
  if (hi <= lo) {
    lo = 0;
    hi = count;
  }

  double sum = 0.0;
  for (size_t i = lo; i < hi; ++i) sum += buf[i];
  return static_cast<float>(sum / static_cast<double>(hi - lo));
}

// ---------------------------------------------------------------------------
// trimmedMean(const std::vector<float>&, float)
//                               __ZN3aff11trimmedMeanERKNSt3__16vectorIfNS0_9allocatorIfEEEEf
//                               5 instructions -- a tail call, nothing else
//
//     ldp x8, x9, [x0] / sub x9, x9, x8 / asr x1, x9, #2 / mov x0, x8
//     b   __ZN3aff11trimmedMeanEPKfmf
//
// The trim value is already in s0, which is where the pointer form's third float
// argument goes, so no move is needed before the branch.
// ---------------------------------------------------------------------------
float trimmedMean(const std::vector<float>& values, float trim) {
  return trimmedMean(values.data(), values.size(), trim);
}

// ---------------------------------------------------------------------------
// medianFilter(const std::vector<float>&, int)
//                               __ZN3aff12medianFilterERKNSt3__16vectorIfNS0_9allocatorIfEEEEi
//                               97 instructions
//
//     stp xzr, xzr, [x8, #0x8] / str xzr, [x8]     ; the result vector starts empty
//     mov x0, x23 / bl __Znwm / bl _bzero           ; std::vector<float> out(n)
//
// The `bzero` is the tell that `out` is constructed with a SIZE and not reserved and
// appended to: value-initialised floats are zeroed in place.
//
// Per element i the loop computes, in 32-bit,
//
//     bic  w8, w27, w27, asr #31          ; lo = max(i - w, 0), signed
//     cmp  x25, x26 / csel x9, x25, x26, lo  ; hi = min(i + w, n-1), UNSIGNED
//     subs x23, x8, x20 / b.eq            ; window byte length == 0 -> out[i] = 0
//
// The two bounds use different signedness (a signed `max` against zero, an unsigned
// `min` against n-1), which is why the source below mixes `long long` and `size_t`
// rather than picking one. The `== 0` guard is unreachable for the same reason as
// trimmedMean's: lo <= hi always, so the window is never empty. It is kept.
//
// The scratch buffer is a std::vector, proved by the `__throw_length_error` call on a
// negative length -- a raw `new float[]` has no such path. Each window is memcpy'd
// whole and sorted from scratch.
// ---------------------------------------------------------------------------
std::vector<float> medianFilter(const std::vector<float>& values, int w) {
  const size_t n = values.size();
  std::vector<float> out(n);
  if (n == 0) return out;

  const long long wc = w < 0 ? 0 : w;
  for (size_t i = 0; i < n; ++i) {
    const long long loRaw = static_cast<long long>(i) - wc;
    const size_t lo = loRaw < 0 ? 0 : static_cast<size_t>(loRaw);
    const size_t hi = std::min(static_cast<size_t>(static_cast<long long>(i) + wc), n - 1);

    const size_t count = hi + 1 - lo;
    if (count == 0) {
      out[i] = 0.0f;
      continue;
    }

    std::vector<float> window(values.begin() + lo, values.begin() + lo + count);
    std::sort(window.begin(), window.end());

    const size_t m = window.size();
    out[i] = (m % 2 != 0) ? window[m / 2] : (window[m / 2 - 1] + window[m / 2]) * 0.5f;
  }
  return out;
}

// ---------------------------------------------------------------------------
// temporalTarget(const std::vector<float>&, int)
//                               __ZN3aff14temporalTargetERKNSt3__16vectorIfNS0_9allocatorIfEEEEi
//                               197 instructions
//
//     ldp x11, x8, [x0] / cmp x11, x8 / b.eq 0xe74     ; 0xe74 is `movi.2d v0,#0; ret`
//     sub sp, sp, #0x40                                ; ...and the frame comes AFTER
//     cmp w1, #0x0 / b.le 0xe7c                        ; w <= 0: sum the raw signal
//     bl  __ZN3aff12medianFilterE...                   ; w  > 0: sum the median-filtered signal
//
// The empty check sits BEFORE the stack frame, which is clang shrink-wrapping an early
// `return` that needs no frame — so the source has an explicit emptiness test at the top
// and does not rely on `meanOf` to supply it. That is the difference that took this
// transcription from 196 instructions to the bundle's 197: the initial `ldp`/`cmp`/`b.eq`
// trio ahead of everything else, with its own `ret` at index 26.
//
// The width test is written as `w <= 0` returning early, not as `w > 0` opening a block.
// The two are the same predicate but not the same encoding: the bundle has
// `cmp w1, #0x0 / b.le`, and the `w > 0` form canonicalises to `cmp w1, #0x1 / b.lt`.
// Both spellings are 197 instructions, so the count alone did not distinguish them.
//
// `meanOf` is inlined at BOTH sites -- the whole 92-instruction widening double
// reduction appears twice in this function, which is most of its 197 instructions. The
// filtered vector's destructor runs on the empty-result path too (`cbnz x0` guards the
// `__ZdlPv`), so that early return is `return meanOf(filtered);` and not a special case.
// ---------------------------------------------------------------------------
float temporalTarget(const std::vector<float>& values, int w) {
  if (values.empty()) return 0.0f;
  if (w <= 0) return meanOf(values);
  const std::vector<float> filtered = medianFilter(values, w);
  return meanOf(filtered);
}

}  // namespace aff
