// shake.cpp — transcribed from the compiled ShakeCapture bundle.

#include "shake.h"

#include <algorithm>
#include <cmath>

namespace shake {

float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

// __ZN5shake6medianENSt3__16vectorIfNS0_9allocatorIfEEEE, 43 instrs
//
//   ldr x0, [x0] / ldr x2, [x19, #0x8] / cmp x0, x2 / b.eq <zero>
//       ; begin == end -> 0.0f
//   sub x8, x2, x0 / asr x9, x8, #2 / lsr x20, x9, #1
//       ; count = (end - begin) / 4, then mid = count / 2
//   x1 = begin + mid * 4 / cmp x2, x1 / b.eq <skip>
//       ; the "middle is the end" guard
//   bl __nth_element(begin, begin + mid, end, less)
//       ; PARTIAL selection, not a sort
//   ldr s0, [x0, x20, lsl #2]                 ; data[mid]
//   tbnz w8, #0x2, <return>                   ; bit 2 of the BYTE count is the ODD test
//   add x2, x21, x0 / sub x1, x2, #0x4        ; the element one before mid
//   bl __nth_element(begin, begin + mid - 1, begin + mid, less)
//   ldur s0, [x8, #-0x4] / fadd s0, s8, s0 / fmov s1, #0.5 / fmul s0, s0, s1
//       ; (data[mid - 1] + data[mid]) * 0.5
//
// The odd test is `tbnz w8, #0x2` on the byte count: count * 4 has bit 2 set exactly when
// count is odd, which is a cheaper test than dividing again.
//
// ONE MEASURED DIFFERENCE, and it is the compiler's rather than the source's: the shipped
// bundle makes the second `__nth_element` call over a ONE-ELEMENT range
// `[begin + mid - 1, begin + mid)`, and this build elides it — libc++ defines
// `__nth_element` inline, and a one-element range is provably nothing, so the call
// disappears and the value is loaded directly. The arithmetic is the same; the shipped is
// 43 instructions and this is 32.
float median(std::vector<float> values) {
  if (values.empty()) return 0.0f;
  const size_t mid = values.size() / 2;
  std::nth_element(values.begin(), values.begin() + static_cast<ptrdiff_t>(mid), values.end());
  const float upper = values[mid];
  if (values.size() % 2 == 1) return upper;
  const float lower = values[mid - 1];
  return (lower + upper) * 0.5f;
}

}  // namespace shake
