// echo.cpp — recovered implementations from the EchoTrails bundle.

#include "echo.h"

#include <algorithm>
#include <cmath>

namespace echo {

// ---------------------------------------------------------------------------
// clampf(float, float, float)
//
// __ZN4echo6clampfEfff: the same five instructions as the other five copies in the
// fleet, with the low bound tested last so it wins on an inverted range.
// ---------------------------------------------------------------------------
float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

// ---------------------------------------------------------------------------
// blendPixel(float*, const float*, BlendMode, float)
//
// __ZN4echo10blendPixelEPfPKfNS_9BlendModeEf, 106 instructions, recovered in full.
//
// WEIGHTS. After clamping, the binary computes three weights once and reuses them for
// every channel:
//
//     0x91c  s7  = 1 - a
//     0x920  s16 = 1 - d
//     0x928  s16 = a * s16        ; a*(1-d)
//     0x92c  s18 = a * d
//            s17 = d * s7         ; d*(1-a)
//
// so the colour result is `a*(1-d)*src + a*d*BLEND + d*(1-a)*dst` for every mode.
//
// MODES, from the switch at 0x930 which tests 4, then 3, then 2, and falls through
// to source-over:
//
//   over (default, and 0/1)  BLEND is unused: the code takes the `a*(1-d)*src` term
//                            and adds `a*d*src`, collapsing to `a*src`.
//   add (2)      0x948      BLEND = src + dst, computed as
//                           `s20 = (dst+src)*a*d; out = a*(1-d)*src + s20 + ...`
//   screen (3)   0x974      BLEND = src + dst - src*dst, and this arm is the only
//                           one the compiler VECTORISED (fadd.2s / fmls.2s / fmla.2s
//                           over R and G together, then a scalar B) — same maths,
//                           different shape.
//   lighten (4)  0x9a0      BLEND = max(src, dst)
//
// ALPHA is the same on every path: `s0 = d*s7 + a`, i.e. `a + d*(1-a)`.
//
// The unpremultiply guards are strict `> 0` (0x8a8, 0x8c0, 0x8d0, 0x8e0, 0x8f4,
// 0x908), so a fully transparent pixel keeps its stored colour instead of dividing
// by zero — and because the weights multiply those stored values by zero anyway, the
// result is unaffected. That is worth preserving rather than "cleaning up": a NaN
// here would propagate through every subsequent composite in the ladder.
// ---------------------------------------------------------------------------
void blendPixel(float* dst, const float* src, BlendMode mode, float amount) {
  const float srcA = src[3];
  const float dstA = dst[3];

  const float a = clampf(amount * srcA, 0.0f, 1.0f);
  const float d = clampf(dstA, 0.0f, 1.0f);

  if (a <= 0.0f) return;

  // Unpremultiply. Only when the alpha is strictly positive.
  float sr = 0.0f, sg = 0.0f, sb = 0.0f;
  float dr = 0.0f, dg = 0.0f, db = 0.0f;
  if (srcA > 0.0f) { sr = src[0] / srcA; sg = src[1] / srcA; sb = src[2] / srcA; }
  if (dstA > 0.0f) { dr = dst[0] / dstA; dg = dst[1] / dstA; db = dst[2] / dstA; }

  const float wSrc = a * (1.0f - d);   // s16
  const float wBoth = a * d;           // s18
  const float wDst = d * (1.0f - a);   // s17

  float br = sr, bg = sg, bb = sb;     // the blend term, per mode
  switch (mode) {
    case BlendMode::Add:     br = sr + dr;           bg = sg + dg;           bb = sb + db;           break;
    case BlendMode::Screen:  br = sr + dr - sr * dr; bg = sg + dg - sg * dg; bb = sb + db - sb * db; break;
    case BlendMode::Lighten: br = sr > dr ? sr : dr; bg = sg > dg ? sg : dg; bb = sb > db ? sb : db; break;
    default:
      // Source-over: the middle term is the source itself, which is what makes the
      // first two terms collapse to a*src.
      br = sr; bg = sg; bb = sb;
      break;
  }

  dst[0] = wSrc * sr + wBoth * br + wDst * dr;
  dst[1] = wSrc * sg + wBoth * bg + wDst * dg;
  dst[2] = wSrc * sb + wBoth * bb + wDst * db;
  dst[3] = d * (1.0f - a) + a;
}

// ---------------------------------------------------------------------------
// compositeOver(ImageF&, const ImageF&, BlendMode, float)
//
// __ZN4echo13compositeOverERNS_6ImageFERKS0_NS_9BlendModeEf, 37 instructions,
// recovered in full.
//
//     0xa10  if (dst.width  != src.width)  return;    ; silent, no error
//     0xa40  if (dst.height != src.height) ...        ; and the count is 0
//     0xa4c  x22 = height * width                     ; the pixel count
//     0xa50  if (count == 0) return;
//     0xa60  loop over pixels, stride 0x10 bytes, calling blendPixel
//
// The two size tests are fused: the height comparison is a `ccmp` guarded on the
// width comparison having been equal, so the loop count is computed unconditionally
// and then re-tested against zero. The effect is a single up-front bail.
// ---------------------------------------------------------------------------
void compositeOver(ImageF& dst, const ImageF& src, BlendMode mode, float amount) {
  if (dst.width != src.width) return;
  if (dst.height != src.height) return;

  long long count = static_cast<long long>(dst.height) * static_cast<long long>(dst.width);
  if (count == 0) return;

  float* d = dst.pixels.data();
  const float* s = src.pixels.data();
  for (long long i = 0; i < count; ++i) {
    blendPixel(d, s, mode, amount);
    d += 4;   // 0x10 bytes
    s += 4;
  }
}

// ---------------------------------------------------------------------------
// buildLadder(int, float, float, float)
//
// __ZN4echo11buildLadderEifff, 91 instructions, recovered in full.
//
//     bic  w8, w0, w0, asr #31 / cmp #0x10 / csel w21, w8, w9, lt
//         ; count = min(max(n, 0), 16), branchlessly, and n < 1 bails to empty
//     stp  xzr, xzr, [x19] / str xzr, [x19, #0x10]   ; result starts EMPTY
//     cmp  w0, #0x1 / b.lt                            ; n < 1 -> return the empty vector
//
//     mov.16b v8, v2         ; argument 2 saved
//     stp    q1, q0, [sp]    ; BOTH argument registers spilled
//     ldr    q6, [sp, #0x10] ; and q0 reloaded into q6: s6=a0, s7=a1, s8=a2
//
// That reload is what the previous pass could not resolve. The two saved vectors are
// `q1 = {s1, s2, s3, s4}` at [sp] and `q0 = {s0, s1, s2, s3}` at [sp+0x10], so the
// reload puts argument 0 in s6, argument 1 in s7 and argument 2 in s8 — and the
// `mov.s v6[1], v1[0]` that made the upper lane look ambiguous then re-reads argument 1
// from the OTHER copy, which is consistency rather than a second value.
//
// The fill: the SIMD path stores three 16-byte literals at 0x3600/0x3610/0x3620. Read
// as FLOATS those are {0,1,1,0}, {1,0,1,1}, {1,1,0,1} — which is NOT a record, because
// 16 is not a multiple of 12 and the compiler packed eight records across the boundary.
// Read as the 96 BYTES they are, the sequence is eight consecutive {1.0, 1.0, 0.0}
// records, and the scalar tail writes the same triple with `fmov.2s v0, #1.0` +
// `str wzr, [x9, #8]`. That is a vector fill-constructor.
//
// The ladder loop then overwrites EVERY rung, including rung 0: `add x8, x0, #0x8`
// puts the store pointer eight bytes into the buffer, and `stur d3, [x8, #-0x8]` writes
// at the buffer's base. So the {1,1,0} fill is unobservable, and the step index runs
// 1..count rather than 0..count-1.
//
// The two float arguments are combined into one 2-lane select, which is the SLP
// vectoriser merging two scalar expressions that feed a single 8-byte store:
//
//     fcmp  s0, s1 / fcsel s3 = a0 > 1 ? 1 : a0          ; min(a0, 1)
//     ldr   d2, [.., #0x630]                             ; {0.0f, 1e-4f}
//     fcmgt.2s v4, v2, v0                                ; {0 > a0, 1e-4 > a1}
//     bsl   v3, v2, v5                                   ; v4 ? v2 : {min(a0,1), a1}
//
// and the constants are what identify the operations: 0.0f against lane 0 makes it
// clamp(a0, 0, 1), and 1e-4f against lane 1 makes it max(a1, 1e-4f). Neither constant is
// guessable from the instruction alone — the literal pool had to be read out of the
// binary, and the first reading of it (as floats) gave the wrong answer.
// ---------------------------------------------------------------------------
std::vector<TrailStep> buildLadder(int n, float scale, float angleDeg, float amountStep) {
  const int count = std::min(std::max(n, 0), 16);
  std::vector<TrailStep> ladder(static_cast<size_t>(count), TrailStep{1.0f, 1.0f, 0.0f});
  if (n < 1) return ladder;

  for (int i = 1; i <= count; ++i) {
    TrailStep& step = ladder[static_cast<size_t>(i - 1)];
    step.scale = clampf(scale, 0.0f, 1.0f);
    step.angleDeg = std::max(angleDeg, 1e-4f);
    step.amount = amountStep * static_cast<float>(i);
  }
  return ladder;
}

// ---------------------------------------------------------------------------
// sampleBilinear(const ImageF&, float, float, float*)
//
// __ZN4echo14sampleBilinearERKNS_6ImageFEffPf, 120 instructions, recovered in full.
//
//     frintm s2, s0 / fcvtms w8, s0    ; floor, in float AND as an int
//     frintm s3, s1 / fcvtms w9, s1
//     fcvtzs s2, s2 / scvtf s2, s2 / fsub s0, s0, s2   ; fx = x - floor(x)
//     fmov   s2, #1.0 / fsub s6, s2, s0 / fsub s7, s2, s1
//         ; s6 = 1-fx, s7 = 1-fy, and those two are REUSED as the products below
//     stp    xzr, xzr, [x1]            ; out[0..3] = 0
//
// The four taps each have their own guard and their own weight. The weights are the
// SAME two registers reassigned as the loop walks the corners:
//
//     tap(x0i,   y0i  ) weight (1-fx)*(1-fy)   ; s2 = s6*s7
//     tap(x0i+1, y0i  ) weight  fx   *(1-fy)   ; s7 = s0*s7
//     tap(x0i,   y0i+1) weight (1-fx)*fy       ; s6 = s6*s1
//     tap(x0i+1, y0i+1) weight  fx   *fy       ; s0 = s0*s1
//
// Each tap is skipped unless `0 <= xi < width` and `0 <= yi < height`, tested as
// `tbnz w,#31` for the lower bound and `cmp`/`ccmp` for the upper. SKIPPED, not clamped:
// there is no renormalisation afterwards, so a tap outside the image contributes zero
// and the sample darkens toward the border. Clamping the coordinate instead is the usual
// alternative and gives a different image, which is why this is a behaviour and not an
// implementation detail.
//
// The pixel address is `data + (yi * width + xi) * 16` bytes, matching ImageF's four
// floats per pixel.
// ---------------------------------------------------------------------------
void sampleBilinear(const ImageF& src, float x, float y, float* out) {
  const int x0i = static_cast<int>(std::floor(x));
  const int y0i = static_cast<int>(std::floor(y));
  const float fx = x - static_cast<float>(x0i);
  const float fy = y - static_cast<float>(y0i);
  // The complements are computed HERE, before the output is zeroed, because that is the
  // order the bundle emits (`fsub s6/s7` then `stp xzr, xzr, [x1]`). They are also
  // reused as the running products below, so they must live in locals rather than being
  // folded into each tap's weight.
  const float ox = 1.0f - fx;
  const float oy = 1.0f - fy;

  out[0] = 0.0f;
  out[1] = 0.0f;
  out[2] = 0.0f;
  out[3] = 0.0f;

  const float* data = src.pixels.data();
  const int w = src.width;
  const int h = src.height;
  const int x1i = x0i + 1;
  const int y1i = y0i + 1;

  // Each tap is a separate guarded accumulate, written out four times rather than
  // through a helper, because the binary has four inlined guard-and-fmadd chains and a
  // lambda would compile to something else.
  if (x0i >= 0 && y0i >= 0 && x0i < w && y0i < h) {
    const float* p = data + (static_cast<long long>(w) * y0i + static_cast<unsigned>(x0i)) * 4;
    const float wt = ox * oy;
    for (int c = 0; c < 4; ++c) out[c] += p[c] * wt;
  }
  if (x1i >= 0 && y0i >= 0 && x1i < w && y0i < h) {
    const float* p = data + (static_cast<long long>(w) * y0i + static_cast<unsigned>(x1i)) * 4;
    const float wt = fx * oy;
    for (int c = 0; c < 4; ++c) out[c] += p[c] * wt;
  }
  if (x0i >= 0 && y1i >= 0 && x0i < w && y1i < h) {
    const float* p = data + (static_cast<long long>(w) * y1i + static_cast<unsigned>(x0i)) * 4;
    const float wt = ox * fy;
    for (int c = 0; c < 4; ++c) out[c] += p[c] * wt;
  }
  if (x1i >= 0 && y1i >= 0 && x1i < w && y1i < h) {
    const float* p = data + (static_cast<long long>(w) * y1i + static_cast<unsigned>(x1i)) * 4;
    const float wt = fx * fy;
    for (int c = 0; c < 4; ++c) out[c] += p[c] * wt;
  }
}

// ---------------------------------------------------------------------------
// transformBilinear(const ImageF&, ImageF&, float, float)
//
// __ZN4echo17transformBilinearERKNS_6ImageFERS0_ff, 122 instructions, recovered in full.
//
//     ldr w8, [x1] / ldr w23, [x0] / cmp / b.ne   ; dst.width  != src.width  -> resize
//     ldr w8, [x19,#4] / ldr w24, [x20,#4] / cmp / b.ne
//     lsl x25, x8, #2   ; nFloats = width*height*4
//     lsl x22, x8, #4   ; nBytes  = width*height*16
//     bl  __Znwm / bl _bzero                     ; a ZERO-FILLED buffer
//     str x0, [x19, #0x10] / bl __ZdlPv          ; and the old one is DEALLOCATED
//     stp x21, x22, [x19, #8] / str x25, [x19, #0x18]  ; begin, end, capacity
//
// Those three stores are the whole reason the ImageF layout was corrected: a raw
// pointer has no end and no capacity to write.
//
//     sub x1, x22, x21 / cmp #1 / bl _bzero      ; then zero the buffer AGAIN
//
// The double zero is real, not a misreading: the resize path zeroes inside the
// allocation and then the shared path zeroes again immediately after. It is kept
// because removing it changes the emitted stream, and it is harmless.
//
//     fcmp s8, #0.0 / b.ls                       ; scale <= 0 -> return, dst left ZEROED
//
// A non-positive scale is not an error and does not fall back to a copy. It produces a
// black frame.
//
//     fmul s0, s9, pi / fdiv by 180 / bl ___sincosf_stret
//     scvtf/fadd #-1.0/fmul #0.5                 ; cx = (width-1)*0.5, cy same
//     fdiv s8, #1.0, s8                          ; inv = 1/scale -- a DIVISION
//
//     ucvtf/fsub -> dy, then sin*dy and cos*dy; ucvtf/fsub -> dx
//     fmadd s0, dx, cs, sin*dy ; fmadd s0, s0, inv, cx        -> sx
//     fmsub s1, dx, sn, cos*dy ; fmadd s1, s1, inv, cy        -> sy
//
// `fmsub` is `Sa - Sn*Sm`, so sy is `cos*dy - dx*sin`: the rotation is by -angleDeg in
// the usual right-handed sense. And the centre is `(dimension - 1) * 0.5` while the
// sample is taken at integer coordinates, i.e. half a pixel inside the true centre of
// the image rectangle — a half-pixel shift that reproducing one expression from the
// other would silently remove.
// ---------------------------------------------------------------------------
void transformBilinear(const ImageF& src, ImageF& dst, float scale, float angleDeg) {
  // The source dimensions are read ONCE into locals and reused. Reading `src.width` again
  // after writing `dst.width` forces a reload, because `dst` and `src` are two references
  // that may alias — the bundle keeps them live in registers (`ldr w23, [x0]` once, then
  // `smull x8, w23, w24`), which is only possible if the source does not write dst first.
  const int w = src.width;
  const int h = src.height;
  if (dst.width != w || dst.height != h) {
    dst.width = w;
    dst.height = h;
    // A vector CONSTRUCTION assigned in, not `resize`. The bundle inlines the whole
    // allocate-zero-install sequence (`__Znwm`, `_bzero`, two `__ZdlPv`-guarded pointer
    // stores) where a `resize` call stays out of line, and the `nBytes == 0` and
    // `nBytes >> 62` guards around it are the vector CONSTRUCTOR's, not resize's.
    dst.pixels = std::vector<float>(
        static_cast<size_t>(static_cast<long long>(w) * static_cast<long long>(h)) * 4u, 0.0f);
  }
  std::fill(dst.pixels.begin(), dst.pixels.end(), 0.0f);

  if (scale <= 0.0f) return;

  const float radians = angleDeg * 3.14159265358979323846f / 180.0f;
  const float sn = std::sin(radians);
  const float cs = std::cos(radians);

  const int dw = dst.width;
  const int dh = dst.height;
  if (dh < 1 || dw < 1) return;

  const float cx = static_cast<float>(w - 1) * 0.5f;
  const float cy = static_cast<float>(h - 1) * 0.5f;
  const float inv = 1.0f / scale;

  float* out = dst.pixels.data();
  for (int y = 0; y < dh; ++y) {
    const float dy = static_cast<float>(y) - cy;
    const float sxc = sn * dy;
    const float cxc = cs * dy;
    for (int x = 0; x < dw; ++x) {
      const float dx = static_cast<float>(x) - cx;
      const float sx = (dx * cs + sxc) * inv + cx;
      const float sy = (cxc - dx * sn) * inv + cy;
      sampleBilinear(src, sx, sy,
                     out + (static_cast<size_t>(y) * static_cast<size_t>(dw) +
                            static_cast<size_t>(x)) * 4u);
    }
  }
}

}  // namespace echo
