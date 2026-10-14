// lutblend.cpp — recovered implementations for the LUTBlend primitives.
//
// Only the functions below have been RECOVERED FROM DISASSEMBLY. The larger
// members of this namespace (parseCube, sampleLUT, applyLUTBlend, makeIdentity)
// are identified but not yet transcribed, and are deliberately absent rather
// than stubbed.

#include "lutblend.h"

#include <utility>

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

// ---------------------------------------------------------------------------
// sampleLUT(const CubeLUT& lut, float r, float g, float b, float* out)
//
// __ZN8lutblend9sampleLUTERKNS_7CubeLUTEfffPf, 193 instructions, recovered in
// full. No external calls at all — it is pure arithmetic, which is why it could
// be transcribed with confidence.
//
// PHASE 1 - domain normalisation, per channel (0x2fc0..0x303c). Each input is
//   mapped from the LUT's declared domain into [0,1] and clamped:
//     ldp s3, s4, [x0, #0x8]      ; minR, minG
//     ldp s6, s5, [x0, #0x10]     ; minB, maxR
//     fsub s0, s0, s3 / fsub s3, s5, s3 / fdiv   ; (r-minR)/(maxR-minR)
//     ldp s1, s7, [x0, #0x18]     ; maxG, maxB
//     fmov s3, #1.0
//     fcmp/fcsel s5, s3, s0, gt   ; min(x, 1)
//     fcmp/fcsel s5, s4, s5, mi   ; (x < 0) ? s4 : x
//
//   That second fcsel deserves a note: it clamps to s4, not to a literal zero,
//   because earlier at 0x2ff0 the compiler emits `movi.2d v4, #0` — which zeroes
//   the ENTIRE 128-bit v4, and s4 is v4's low lane. So s4 is a zero source that
//   the compiler reuses rather than reloading an immediate. Read literally it
//   looks like a clamp to minG, which is only equivalent when minG == 0; the
//   vector zeroing is what makes it correct. Worth knowing before "fixing" it.
//
//   The three normalised values are then scaled into LUT index space:
//     sub  w9, w8, #0x1           ; size - 1
//     scvtf s17, w9
//     fmul s1, s5, s17            ; fr = clamp(nr) * (size-1)
//     fmul s0, s0, s17            ; fg
//     fmul s2, s2, s17            ; fb
//
// PHASE 2a - the 3D path (0x3044..0x3234), selected by `tbz w10,#0x0` (is3D).
//   Lower/upper lattice indices, with the upper one clamped:
//     fcvtzs w10, s1              ; i0 = (int)fr
//     cmp w9, w10 / cinc w11, w10, gt   ; i1 = (i0 < size-1) ? i0+1 : i0
//   and the fractional weights tr, tg, tb as `fr - (float)i0` etc.
//
//   The index arithmetic is the standard .cube layout with R fastest:
//     smull x16, w8, w8           ; size * size
//     smaddl x2, w8, w12, x17     ; size*j0 + i0
//     mul  x14, x16, k0
//     add  x9, x14, x2            ; k*size^2 + j*size + i
//     mov  w3, #0xc / madd        ; * 12 bytes per RGB triple
//   Note the stride is 12 BYTES (three floats), so neighbouring R samples are 12
//   apart, not 4.
//
//   Then eight corner loads interpolated R, then G, then B:
//     ldr s3,[x9] / ldr s4,[x10] / fsub / fmadd s3, s4, s1, s3
//   i.e. lerp(lerp(lerp(c000,c100,tr), lerp(c010,c110,tr), tg), ..., tb).
//
// PHASE 2b - the 1D path (0x3218..0x32bc). This is NOT a single shared axis:
//   each output channel is a curve indexed by ITS OWN input coordinate.
//     entry i0 -> data[i0*12 + 0], entry i1 -> data[i1*12 + 0]
//     fmadd -> out[0] = lerp along the R coordinate
//     entry j0 -> data[j0*12 + 4], entry j1 -> ... +4
//     fmadd -> out[1] = lerp along the G coordinate
//     entry k0 -> data[k0*12 + 8], ... +8
//     fmadd -> out[2] = lerp along the B coordinate
//   So a 1D LUT is three independent tone curves. Applying one channel's
//   coordinate to all three would be a plausible-looking mistake.
//
// The caller must ensure valid() holds: this indexes data arithmetically with no
// bounds checks of its own, which is exactly why valid() insists the data length
// is exact.
// ---------------------------------------------------------------------------
void sampleLUT(const CubeLUT& lut, float r, float g, float b, float* out) {
  const int last = lut.size - 1;
  const float scale = static_cast<float>(last);

  auto normalise = [](float v, float lo, float hi) {
    const float n = (v - lo) / (hi - lo);
    const float hiC = n > 1.0f ? 1.0f : n;
    return n < 0.0f ? 0.0f : hiC;
  };
  auto indexOf = [&](float scaled) {
    int i0 = static_cast<int>(scaled);          // fcvtzs truncates toward zero
    if (i0 > last) i0 = last;                   // defensive; clamped input prevents it
    const int i1 = i0 < last ? i0 + 1 : i0;
    return std::pair<int, int>(i0, i1);
  };

  const float fr = normalise(r, lut.minR, lut.maxR) * scale;
  const float fg = normalise(g, lut.minG, lut.maxG) * scale;
  const float fb = normalise(b, lut.minB, lut.maxB) * scale;

  const auto [i0, i1] = indexOf(fr);
  const auto [j0, j1] = indexOf(fg);
  const auto [k0, k1] = indexOf(fb);

  const float tr = fr - static_cast<float>(i0);
  const float tg = fg - static_cast<float>(j0);
  const float tb = fb - static_cast<float>(k0);

  const std::vector<float>& d = lut.data;
  auto at = [&](int i, int j, int k, int c) -> float {
    const size_t idx = static_cast<size_t>(k) * static_cast<size_t>(lut.size) * lut.size +
                       static_cast<size_t>(j) * lut.size + static_cast<size_t>(i);
    return d[idx * 3 + static_cast<size_t>(c)];
  };

  if (lut.is3D) {
    for (int c = 0; c < 3; ++c) {
      const float c00 = lerpf(at(i0, j0, k0, c), at(i1, j0, k0, c), tr);
      const float c10 = lerpf(at(i0, j1, k0, c), at(i1, j1, k0, c), tr);
      const float c01 = lerpf(at(i0, j0, k1, c), at(i1, j0, k1, c), tr);
      const float c11 = lerpf(at(i0, j1, k1, c), at(i1, j1, k1, c), tr);
      const float c0 = lerpf(c00, c10, tg);
      const float c1 = lerpf(c01, c11, tg);
      out[c] = lerpf(c0, c1, tb);
    }
    return;
  }

  // 1D: each channel is a curve indexed by its own coordinate.
  const int idxR[2] = {i0, i1};
  const int idxG[2] = {j0, j1};
  const int idxB[2] = {k0, k1};
  const float fracR = tr, fracG = tg, fracB = tb;
  const int* idx[3] = {idxR, idxG, idxB};
  const float frac[3] = {fracR, fracG, fracB};
  for (int c = 0; c < 3; ++c) {
    const float lo = d[static_cast<size_t>(idx[c][0]) * 3 + static_cast<size_t>(c)];
    const float hi = d[static_cast<size_t>(idx[c][1]) * 3 + static_cast<size_t>(c)];
    out[c] = lerpf(lo, hi, frac[c]);
  }
}


}  // namespace lutblend
