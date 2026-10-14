// lutblend.cpp — recovered implementations for the LUTBlend primitives.
//
// Only the functions below have been RECOVERED FROM DISASSEMBLY. The larger
// members of this namespace (parseCube, sampleLUT, applyLUTBlend, makeIdentity)
// are identified but not yet transcribed, and are deliberately absent rather
// than stubbed.

#include "lutblend.h"

namespace lutblend {

// ---------------------------------------------------------------------------
// clampf(float value, float lo, float hi)
//
// __ZN8lutblend6clampfEfff:
//     fcmp  s0, s2             ; value vs hi
//     fcsel s2, s2, s0, gt     ; s2 = value > hi ? hi : value
//     fcmp  s0, s1             ; value vs lo
//     fcsel s0, s1, s2, mi     ; s0 = value < lo ? lo : s2
//     ret
//
// Instruction-for-instruction the same as spectra::clampf in the audio module,
// including the fcsel order that makes lo win when the range is inverted. Two
// namespaces written at different times producing the identical branchless form
// is good evidence both transcriptions are right.
// ---------------------------------------------------------------------------
float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

// ---------------------------------------------------------------------------
// lerpf(float a, float b, float t)
//
// __ZN8lutblend5lerpfEfff:
//     fsub  s1, s1, s0         ; b - a
//     fmadd s0, s1, s2, s0     ; (b - a) * t + a
//     ret
//
// A plain linear interpolation. Note it does NOT clamp t, so extrapolation is
// possible; callers that need clamping use clampf separately.
// ---------------------------------------------------------------------------
float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// ---------------------------------------------------------------------------
// luma709(float r, float g, float b)
//
// __ZN8lutblend7luma709Efff. The three coefficients are loaded as immediates:
//     mov w8,#0x1759 / movk w8,#0x3f37,lsl#16   -> 0x3f371759 = 0.7152000
//     mov w8,#0xb3d0 / movk w8,#0x3e59,lsl#16   -> 0x3e59b3d0 = 0.2126000
//     mov w8,#0xdd98 / movk w8,#0x3d93,lsl#16   -> 0x3d93dd98 = 0.0722000
// and they sum to exactly 1.0, so this is BT.709 luma with no normalisation
// step. Declared as b*0.0722 + g*0.7152 + r*0.2126 in source order, which is the
// order the multiplies are accumulated in (G first, then R, then B).
// ---------------------------------------------------------------------------
float luma709(float r, float g, float b) {
  return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// ---------------------------------------------------------------------------
// CubeLUT::valid() const
//
// __ZNK8lutblend7CubeLUT5validEv, 29 instructions, recovered in full:
//
//     ldr  w8, [x0, #0x4]        ; size
//     cmp  w8, #0x2 / b.lt <false>      ; 1. size >= 2
//     ldrb w9, [x0]              ; is3D
//     umull x10, w8, w8          ; size * size
//     cmp  w9, #0x0
//     csinc x9, x10, xzr, ne     ; x9 = is3D ? size*size : 1
//     mul  x8, x8, x9            ; * size
//     add  x8, x8, x8, lsl #1    ; * 3   -> expected float count
//     ldp  x10, x9, [x0, #0x20]  ; data.begin, data.end
//     sub  x9, x9, x10
//     cmp  x8, x9, asr #2        ; compare against data.size()
//     b.ne <false>               ; 2. exact match, no slack
//     ldr s0,[x0,#0x14] / ldr s1,[x0,#0x8] / fcmp / b.le <false>   ; 3. maxR > minR
//     ... same for 0x18 vs 0xc, and 0x1c vs 0x10
//
// Three conditions, and the second is the interesting one: the data length must
// be EXACTLY size * (is3D ? size*size : 1) * 3. A LUT with a truncated or padded
// table is rejected rather than partially used, which is the right call for
// something that will be indexed arithmetically.
//
// The csinc is worth reading carefully: `csinc x9, x10, xzr, ne` yields x10 when
// the condition is NE and xzr+1 == 1 otherwise, so it selects size*size for a 3D
// LUT and 1 for a 1D one — a branchless select of the entry multiplier.
// ---------------------------------------------------------------------------
bool CubeLUT::valid() const {
  if (size < 2) return false;

  const size_t entriesPerAxis = static_cast<size_t>(size);
  const size_t expectedFloats =
      entriesPerAxis * (is3D ? entriesPerAxis * entriesPerAxis : 1) * 3;
  if (data.size() != expectedFloats) return false;

  // The max must strictly exceed the min on every channel.
  if (!(maxR > minR)) return false;
  if (!(maxG > minG)) return false;
  if (!(maxB > minB)) return false;
  return true;
}

}  // namespace lutblend
