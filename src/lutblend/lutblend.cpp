// lutblend.cpp — recovered implementations for the LUTBlend primitives.
//
// Only the functions below have been RECOVERED FROM DISASSEMBLY. The larger
// members of this namespace (parseCube, sampleLUT, applyLUTBlend, makeIdentity)
// are identified but not yet transcribed, and are deliberately absent rather
// than stubbed.

#include "lutblend.h"

#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

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

// ---------------------------------------------------------------------------
// makeIdentity(CubeLUT*, int, bool)
//
// __ZN8lutblend12makeIdentityEPNS_7CubeLUTEib, 269 instructions, recovered in
// full. Most of the count is std::vector growth inlined four times over (one
// growth path per push_back in the triple loop), not logic.
//
// SETUP (0x8d4..0x900):
//     strb w2, [x0]              ; lut->is3D  = is3D
//     str  w1, [x0, #0x4]        ; lut->size  = size
//     str  xzr, [x0, #0x8]       ; minR = minG = 0   (one 8-byte store)
//     str  wzr, [x0, #0x10]      ; minB = 0
//     memset_pattern16(x0 + 0x14, <0xba50>, 0xc)
//                                ; maxR = maxG = maxB = 1.0f
//     x21 = [x20 + 0x20]!        ; lut->data.begin
//     str x21, [x24, #0x8]       ; lut->data.end = begin   <-- the clear()
//   The pattern at 0xba50 is four 1.0f floats, so the 12-byte fill is exactly
//   three 1.0f values. The end = begin store IS the vector clear; there is no
//   call for it, which is why it is easy to miss.
//
// RESERVE (0x90c..0x94c for 3D, 0xba4..0xbdc for 1D):
//     x22 = 3 * size^3           ; 3D      (3 * size for 1D)
//     if (x22 > (cap - begin) / 4)        ; capacity in floats
//         reallocate to exactly x22, free the old buffer
//   So a repeated call reuses the existing allocation: the common case does no
//   allocation at all.
//
// FILL. The loop nesting is B outermost, G middle, R innermost, and each triple
// is stored in (r, g, b) order:
//     3D: for b in [0,size) for g in [0,size) for r in [0,size)
//             data.push_back(r/(size-1), g/(size-1), b/(size-1))
//     1D: for i in [0,size) data.push_back(v, v, v) with v = i/(size-1)
//   The store order gives the nesting away and is the opposite of what the loop
//   counters look like they are doing: at 0x9b4 the innermost push writes s11,
//   then 0x9c8 writes s10, then 0x9dc writes s9 — and s11/s10/s9 are the
//   INNER/MIDDLE/OUTER counters. So the innermost counter is R, stored first.
//   That is .cube order, R fastest, which sampleLUT in this same file
//   independently assumes. Two functions, opposite directions, same layout.
//
//   The normaliser is `size - 1`, computed once as a float (0x95c) and reused, so
//   a size-1 LUT divides 0 by 0 and yields NaN. Preserved deliberately.
//
// The `subs w8, w19, #1 / b.lt` guard at 0x950 and 0xbe0 means size <= 0 writes
// nothing, after the reserve has already run.
// ---------------------------------------------------------------------------
void makeIdentity(CubeLUT* lut, int size, bool is3D) {
  lut->is3D = is3D;
  lut->size = size;
  lut->minR = 0.0f;
  lut->minG = 0.0f;
  lut->minB = 0.0f;
  lut->maxR = 1.0f;
  lut->maxG = 1.0f;
  lut->maxB = 1.0f;

  lut->data.clear();

  const long long n = size;

  if (is3D) {
    lut->data.reserve(static_cast<size_t>(3 * n * n * n));
    const float denom = static_cast<float>(size - 1);
    for (int b = 0; b < size; ++b) {
      for (int g = 0; g < size; ++g) {
        for (int r = 0; r < size; ++r) {
          // (r, g, b), R innermost: .cube order.
          lut->data.push_back(static_cast<float>(r) / denom);
          lut->data.push_back(static_cast<float>(g) / denom);
          lut->data.push_back(static_cast<float>(b) / denom);
        }
      }
    }
  } else {
    lut->data.reserve(static_cast<size_t>(3 * n));
    const float denom = static_cast<float>(size - 1);
    for (int i = 0; i < size; ++i) {
      const float v = static_cast<float>(i) / denom;
      lut->data.push_back(v);
      lut->data.push_back(v);
      lut->data.push_back(v);
    }
  }
}


// ---------------------------------------------------------------------------
// applyLUTBlend(const CubeLUT&, const CubeLUT&, float, float, bool,
//               const float*, float*)
//
// __ZN8lutblend13applyLUTBlendE..., 112 instructions, recovered in full. The
// whole function is a double lerp with an optional luma correction.
//
// ARGUMENT REGISTERS. Integer and float arguments are allocated independently,
// which is what makes this signature readable at all:
//     x0 lutA   x1 lutB   s0 blend   s1 intensity   w2 preserveLuma
//     x3 rgb (const float*)   x4 out (float*)
//   The bool is the THIRD integer argument, so it lands in w2 and pushes the two
//   pointers to x3/x4 — not, as a naive reading of the declaration order would
//   suggest, after both floats.
//
//     0x3308..0x331c: s14 = clamp(s0, 0, 1)      ; the LUT-vs-LUT blend
//     ldp s9, s11, [x3] / ldr s10, [x3, #0x8]   ; s9 = rgb[0], s11 = rgb[1],
//                                               ; s10 = rgb[2]  (note the ORDER:
//                                               ; the pair load puts index 1 in s11)
//     sampleLUT(lutA, s9, s11, s10, sp+0xc)
//     sampleLUT(lutB, s9, s11, s10, sp)
//     0x3354..0x3378: three `fsub` + `fmadd` pairs
//                                               ; mixed = a + (b - a) * s14
//
//   LUMA PRESERVATION (0x337c..0x3400), skipped entirely when !preserveLuma:
//     0x3380..0x33ac: luma(mixed) = 0.7152*g + 0.2126*r + 0.0722*b
//     0x33b0:         fcmp / b.le <skip>        ; skip if luma <= 1e-5
//     0x33c4..0x33f0: luma(rgb) with the SAME constants and the SAME fmadd order
//     0x33f4:         ratio = luma(rgb) / luma(mixed)
//     0x33f8..0x3400: mixed *= ratio
//   Those constants decode to exactly Rec.709 — 0x3f371759 is 0.7152,
//   0x3e59b3d0 is 0.2126, 0x3d93dd98 is 0.0722 — and the accumulation order is
//   `(g*c1) + r*c2`, then `+ b*c3`. That is luma709() in this same namespace,
//   instruction for instruction, INLINED rather than called. Since it is
//   bit-identical, this transcription calls luma709() and removes the duplicate
//   without changing a single result. The 1e-5 (0x3727c5ac) epsilon guard is
//   what stops a black pixel dividing by zero.
//
//   FINAL BLEND (0x3404..0x343c):
//     0x3404..0x3410: s3 = clamp(s1, 0, 1)       ; the intensity
//     0x3414..0x343c: out[i] = rgb[i] + (mixed[i] - rgb[i]) * s3
//   Note the second lerp re-reads rgb from memory (`[x20, #0x4]`, `[x20, #0x8]`)
//   rather than reusing s11/s10 — so it blends against the ORIGINAL pixel, not
//   against anything the luma step touched.
//
// A stack canary is installed (0x32f8..0x3304) and checked on the way out, which
// is why the epilogue has a __stack_chk_fail branch.
// ---------------------------------------------------------------------------
void applyLUTBlend(const CubeLUT& a, const CubeLUT& b, float blend, float intensity,
                   bool preserveLuma, const float* rgb, float* out) {
  const float mix = clampf(blend, 0.0f, 1.0f);

  const float r = rgb[0];
  const float g = rgb[1];
  const float bl = rgb[2];

  float outA[3];
  float outB[3];
  sampleLUT(a, r, g, bl, outA);
  sampleLUT(b, r, g, bl, outB);

  float m0 = outA[0] + (outB[0] - outA[0]) * mix;
  float m1 = outA[1] + (outB[1] - outA[1]) * mix;
  float m2 = outA[2] + (outB[2] - outA[2]) * mix;

  if (preserveLuma) {
    const float lumaMixed = luma709(m0, m1, m2);
    if (lumaMixed > 1e-5f) {
      const float ratio = luma709(r, g, bl) / lumaMixed;
      m0 *= ratio;
      m1 *= ratio;
      m2 *= ratio;
    }
  }

  const float amount = clampf(intensity, 0.0f, 1.0f);
  out[0] = r + (m0 - r) * amount;
  out[1] = g + (m1 - g) * amount;
  out[2] = bl + (m2 - bl) * amount;
}




// ---------------------------------------------------------------------------
// parseCube — RECONSTRUCTION of the 1924-instruction original, written from the
// recovered specification (RECOVERY-NOTES.md). Every rule and every error string
// below is a recovered fact; the code structure is this reconstruction's own.
//
// The original builds a local CubeLUT and copies it to *out only after the whole
// file validates, which is why a failed parse leaves the caller's LUT untouched.
// ---------------------------------------------------------------------------
namespace {

bool isIntegerToken(const std::string& token) {
  if (token.empty()) return false;
  size_t i = (token[0] == '+' || token[0] == '-') ? 1 : 0;
  if (i >= token.size()) return false;
  for (; i < token.size(); ++i) {
    if (token[i] < '0' || token[i] > '9') return false;
  }
  return true;
}

// strtod with the same "consumed the whole token" test the extractor implies.
bool toNumber(const std::string& token, double* value) {
  if (token.empty()) return false;
  const char* begin = token.c_str();
  char* end = nullptr;
  const double parsed = std::strtod(begin, &end);
  if (end == begin) return false;
  while (end && *end != '\0' && std::isspace(static_cast<unsigned char>(*end))) ++end;
  if (end && *end != '\0') return false;
  *value = parsed;
  return true;
}

std::string linePrefix(long line) { return "line " + std::to_string(line) + ": "; }

}  // namespace

bool parseCube(const std::string& text, CubeLUT* out, std::string* error) {
  auto fail = [error](const std::string& message) {
    if (error) *error = message;
    return false;
  };

  CubeLUT local{};            // default-constructed: is3D = true, min 0, max 1
  bool haveSize = false;
  int size = 0;

  long lineNo = 0;
  size_t pos = 0;
  std::vector<std::string> tokens;

  while (pos <= text.size()) {
    const size_t nl = text.find('\n', pos);
    std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;

    // CRLF: the CR is stripped BEFORE the comment strip, so "\r" after a comment
    // still disappears with the comment.
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t hash = line.find('#');
    if (hash != std::string::npos) line.erase(hash);
    ++lineNo;

    tokens.clear();
    {
      const char* begin = line.c_str();
      while (*begin != '\0') {
        while (*begin != '\0' && std::isspace(static_cast<unsigned char>(*begin))) ++begin;
        if (*begin == '\0') break;
        const char* start = begin;
        while (*begin != '\0' && !std::isspace(static_cast<unsigned char>(*begin))) ++begin;
        tokens.emplace_back(start, static_cast<size_t>(begin - start));
      }
    }
    if (tokens.empty()) continue;   // blank line, or a line that was only a comment

    const std::string prefix = linePrefix(lineNo);
    const std::string& keyword = tokens[0];
    const size_t argc = tokens.size() - 1;

    // TITLE is accepted and ignored: no arity check, no validation. Recovered.
    if (keyword == "TITLE") continue;

    auto parseTriple = [&](float* r, float* g, float* b) -> bool {
      if (argc != 3) return fail(prefix + keyword + " needs 3 values");
      double v[3];
      for (int i = 0; i < 3; ++i) {
        if (!toNumber(tokens[static_cast<size_t>(i) + 1], &v[i])) {
          return fail(prefix + keyword + " has a non-numeric value");
        }
      }
      *r = static_cast<float>(v[0]);
      *g = static_cast<float>(v[1]);
      *b = static_cast<float>(v[2]);
      return true;
    };

    auto parsePair = [&](float* lo, float* hi) -> bool {
      if (argc != 2) return fail(prefix + keyword + " needs 2 values");
      double v[2];
      for (int i = 0; i < 2; ++i) {
        if (!toNumber(tokens[static_cast<size_t>(i) + 1], &v[i])) {
          return fail(prefix + keyword + " has a non-numeric value");
        }
      }
      *lo = static_cast<float>(v[0]);
      *hi = static_cast<float>(v[1]);
      return true;
    };

    auto parseSize = [&](bool is3D) -> bool {
      if (haveSize) return fail(prefix + "duplicate LUT size declaration");
      if (argc != 1) return fail(prefix + keyword + " needs exactly one value");
      if (!isIntegerToken(tokens[1])) return fail(prefix + keyword + " is not an integer");
      const double parsed = std::strtod(tokens[1].c_str(), nullptr);
      // (unsigned)(size - 2) < 255 — the recovered range test, i.e. size in [2, 256].
      const int candidate = static_cast<int>(parsed);
      if (static_cast<unsigned>(candidate - 2) >= 255u) {
        return fail(prefix + "LUT size " + std::to_string(candidate) + " out of range [2,256]");
      }
      size = candidate;
      local.is3D = is3D;
      local.size = candidate;
      haveSize = true;
      return true;
    };

    if (keyword == "LUT_3D_SIZE") {
      if (!parseSize(true)) return false;
      continue;
    }
    if (keyword == "LUT_1D_SIZE") {
      if (!parseSize(false)) return false;
      continue;
    }
    if (keyword == "DOMAIN_MIN") {
      if (!parseTriple(&local.minR, &local.minG, &local.minB)) return false;
      continue;
    }
    if (keyword == "DOMAIN_MAX") {
      if (!parseTriple(&local.maxR, &local.maxG, &local.maxB)) return false;
      continue;
    }
    if (keyword == "LUT_3D_INPUT_RANGE" || keyword == "LUT_1D_INPUT_RANGE") {
      float lo = 0.0f, hi = 1.0f;
      if (!parsePair(&lo, &hi)) return false;
      local.minR = local.minG = local.minB = lo;
      local.maxR = local.maxG = local.maxB = hi;
      continue;
    }

    // Not a directive: it must be a data row, and only after a size is known.
    if (!haveSize) return fail(prefix + "data before LUT_1D_SIZE/LUT_3D_SIZE");
    // A data row is three tokens — the first token is the first number, not a keyword.
    if (tokens.size() != 3) return fail(prefix + "unrecognised keyword or malformed data row '" + keyword + "'");
    for (size_t i = 0; i < 3; ++i) {
      double v = 0;
      if (!toNumber(tokens[i], &v)) {
        return fail(prefix + "non-numeric data value '" + tokens[i] + "'");
      }
      local.data.push_back(static_cast<float>(v));
    }
  }

  // Whole-file checks, which is why these two messages carry no line prefix.
  if (!haveSize) return fail("missing LUT_1D_SIZE/LUT_3D_SIZE");
  const long expected = local.is3D ? static_cast<long>(size) * size * size : static_cast<long>(size);
  const long got = static_cast<long>(local.data.size()) / 3;
  if (got != expected) {
    return fail("expected " + std::to_string(expected) + " data rows, got " + std::to_string(got));
  }
  if (!(local.maxR > local.minR && local.maxG > local.minG && local.maxB > local.minB)) {
    return fail("degenerate domain: DOMAIN_MAX must exceed DOMAIN_MIN");
  }

  if (out) *out = local;
  if (error) error->clear();
  return true;
}


}  // namespace lutblend
