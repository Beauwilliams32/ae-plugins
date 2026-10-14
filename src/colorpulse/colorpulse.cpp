// colorpulse.cpp — recovered implementations from the ColorPulse bundle.

#include "colorpulse.h"

namespace colorpulse {

// ---------------------------------------------------------------------------
// clampf(float, float, float)
//
// __ZN10colorpulse6clampfEfff: the same five instructions as the other six copies in
// the fleet, with the low bound tested last so it wins on an inverted range.
// ---------------------------------------------------------------------------
float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

// ---------------------------------------------------------------------------
// smoothScalar(float, float, float)
//
// __ZN10colorpulse12smoothScalarEfff, 10 instructions, recovered in full.
//
//     fmov  s3, #1.0 / fcmp s2, s3 / fcsel s4 = min(blend, 1)
//     fcmp  s2, #0.0 / fcsel s2 = (blend < 0) ? 0 : min(blend,1)   ; the clamp
//     fsub  s3, s3, s2                                             ; 1 - t
//     fmul  s1, s1, s3                                             ; previous * (1-t)
//     fmadd s0, s0, s2, s1                                         ; newest * t + that
//
// The clamp is the familiar two-`fcsel` form: `min(blend,1)` then a lower-bound guard,
// so a negative blend becomes 0 rather than passing through.
// ---------------------------------------------------------------------------
float smoothScalar(float newest, float previous, float blend) {
  const float t = clampf(blend, 0.0f, 1.0f);
  return newest * t + previous * (1.0f - t);
}

// ---------------------------------------------------------------------------
// luma709(const RGB&)
//
// __ZN10colorpulse7luma709ERKNS_3RGBE, 15 instructions, recovered in full.
//
//     ldp   s0, s1, [x0]        ; r, g
//     s1 = g * 0.7152f          ; 0x3f371759
//     s0 = r * 0.2126f + s1     ; fmadd, 0x3e59b3d0
//     s1 = [x0, #0x8]           ; b
//     s0 = b * 0.0722f + s0     ; fmadd, 0x3d93dd98
//
// This is the SAME function as lutblend::luma709 — same three constants, same
// `(g*c1 + r*c2) + b*c3` order — written against a different colour struct. The
// duplication is real and is asserted rather than quietly factored, because the two
// live in different namespaces recovered from different bundles.
// ---------------------------------------------------------------------------
float luma709(const RGB& c) {
  return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

// ---------------------------------------------------------------------------
// smoothColor(const RGB&, const RGB&, float)
//
// __ZN10colorpulse11smoothColorERKNS_3RGBES2_f, 17 instructions, recovered in full.
//
// The same lerp as smoothScalar, applied per channel:
//
//     d3 = [x0] (newest.r, newest.g)      d0 = [x1] (previous.r, previous.g)
//     v0 = d0 * (1-t)                     ; fmul.2s — BOTH lanes at once
//     v0 = v3 * t + v0                    ; fmla.2s
//     s3 = [x0+8] (newest.b)              s4 = [x1+8] (previous.b)
//     s1 = (1-t) * previous.b
//     s2 = newest.b * t + s1
//
// Red and green are computed as a two-lane SIMD operation and blue separately; the
// arithmetic is identical either way, which is why the scalar form below is exact
// rather than merely close.
// ---------------------------------------------------------------------------
RGB smoothColor(const RGB& newest, const RGB& previous, float blend) {
  const float t = clampf(blend, 0.0f, 1.0f);
  RGB out;
  out.r = newest.r * t + previous.r * (1.0f - t);
  out.g = newest.g * t + previous.g * (1.0f - t);
  out.b = newest.b * t + previous.b * (1.0f - t);
  return out;
}

// ---------------------------------------------------------------------------
// saturationBoost(const RGB&, float)
//
// __ZN10colorpulse15saturationBoostERKNS_3RGBEf, 41 instructions, recovered in full.
//
//     fcmp s0, #0.0 / b.ls <early>     ; amount <= 0 -> return c UNCHANGED
//     ... luma709 computed inline, same three constants ...
//     s2 = amount + 1.0                ; the scale factor
//     for each channel: s = luma + (ch - luma) * s2
//     then clamp each to [0,1]
//
// Two things worth stating because they are not what a rewrite would do:
//
//   * the guard is `<= 0` and it returns the INPUT components, not clamped ones. A
//     negative amount is therefore a no-op rather than a desaturation, and the
//     function is not symmetric about zero.
//   * the scale is `1 + amount`, so `amount` is a fraction: 1.0 doubles the distance
//     from the luma, not "100%". The parameter is presented as a percentage in the UI
//     and divided before it gets here.
//
// The luma is computed inline rather than by calling luma709, so this function does not
// appear in luma709's callers — the same inlining duplication seen in
// lutblend::applyLUTBlend.
// ---------------------------------------------------------------------------
RGB saturationBoost(const RGB& c, float amount) {
  if (amount <= 0.0f) return c;

  const float l = luma709(c);
  const float scale = amount + 1.0f;

  RGB out;
  out.r = clampf(l + (c.r - l) * scale, 0.0f, 1.0f);
  out.g = clampf(l + (c.g - l) * scale, 0.0f, 1.0f);
  out.b = clampf(l + (c.b - l) * scale, 0.0f, 1.0f);
  return out;
}

// ---------------------------------------------------------------------------
// regionBounds(Region, int, int)
//
// __ZN10colorpulse12regionBoundsENS_6RegionEii, 57 instructions, recovered in full.
//
//     mov  w9, w1 / orr x8, x9, x2, lsl #32   ; x8 = {width, height}
//     cmp  w0, #0x4 / b.eq 0x854              ; the BOTTOM THIRD arm is the odd one out
//
// Clang lowers the switch as: one arm for value 4, and the default plus values 2 and 3
// fused into a `csel` chain. Reading that chain back is what gives the rectangles, and it
// is worth doing carefully because the chain interleaves three cases:
//
//     cmp  w0, #0x2
//     csel w12, wzr, w12, ne        ; (w0==2) ? height/4 : 0
//     csel x8,  x8,  x11, ne        ; (w0==2) ? {w,h} : {3w/4, 3h/4}
//     csel w11, wzr, w10, ne        ; (w0==2) ? width/4  : 0
//     cmp  w0, #0x3
//     csel w10, wzr, w12, eq        ; (w0==3) ? 0 : <above>     -> minY
//     csel x8,  x9,  x8,  eq        ; (w0==3) ? {w, h/3} : <above>  -> maxX, maxY
//     csel w9,  wzr, w11, eq        ; (w0==3) ? 0 : <above>     -> minX
//
// so minX/minY are 0 for both 3 and the default, and only value 2 moves them inward; and
// maxX is `width` for 2 and 3 but `3w/4` for the default.
//
// The value-4 arm computes its own minY as `height - height/3`:
//
//     mov  w10, #0x55555555 / smull x10, w2, w10 / lsr x10, x10, #32
//     sub  w10, w10, w2 / asr w11, w10, #1 / add w10, w11, w10, lsr #31
//     add  w10, w10, w2
//
// which is `height - height/3` — checked numerically rather than assumed: at height 10 the
// sequence gives 7, which is 10 - 3 and NOT 2*10/3 = 6. The two differ for every height
// not divisible by 3, so the distinction matters.
//
// The join then applies the size floor, per axis and independently:
//
//     add  w11, w9, #0x1 / cmp w1, w11 / csinc w11, w1, w9, lt
//         ; csinc Wd, Wn, Wm, cond is cond ? Wn : Wm+1, so this is min(width, minX+1)
//     and  x12, x8, #0xffffffff00000000 / orr x11, x12, w11
//     cmp  w9, w8 / csel x8, x8, x11, lt      ; only replace maxX when minX >= maxX
//
// and the same again for the Y axis with `bfi x13, w12, #32, #32` to patch the high half.
// The halves are patched SEPARATELY rather than recomputing the whole pair, which is why
// the two axes look asymmetric in the disassembly and are not.
// ---------------------------------------------------------------------------
Rect regionBounds(Region region, int width, int height) {
  int minX = 0;
  int minY = 0;
  // `max` defaults to the FULL box, which is why the bundle materialises the pair
  // `{width, height}` with `orr x8, x9, x2, lsl #32` BEFORE it tests the selector: that
  // pair is a default value, not something only the arms need. Three of the four arms
  // leave it alone and only the default arm moves it inward.
  int maxX = width;
  int maxY = height;

  // Each arm computes its own divisions. Hoisting them into locals before the switch
  // makes clang evaluate `width / 4` ahead of the value-4 test, which costs four
  // instructions on a path that never uses it — the bundle computes the quarters and the
  // thirds only on the paths that need them, and computes the value-4 arm's `height -
  // height / 3` with a SEPARATE magic sequence rather than reusing the non-4 arm's.
  switch (region) {
    case Region::BottomRightThreeQuarters:
      minX = width / 4;
      minY = height / 4;
      break;
    case Region::TopThird:
      minX = 0;
      minY = 0;
      maxY = height / 3;
      break;
    case Region::BottomThird:
      minX = 0;
      minY = height - height / 3;
      break;
    default:
      // Region::TopLeftThreeQuarters, and everything the caller failed to narrow.
      minX = 0;
      minY = 0;
      maxX = width - width / 4;
      maxY = height - height / 4;
      break;
  }

  if (minX >= maxX) maxX = (width < minX + 1) ? width : minX + 1;
  if (minY >= maxY) maxY = (height < minY + 1) ? height : minY + 1;

  return Rect{minX, minY, maxX, maxY};
}

// ---------------------------------------------------------------------------
// averageColor(const RGB*, size_t)
//
// __ZN10colorpulse12averageColorEPKNS_3RGBEm, 201 instructions, recovered in full.
//
//     movi.2d v2/v1/v0, #0        ; r, g, b accumulators cleared
//     cbz   x0, 0xbdc             ; pixels == nullptr -> return {0,0,0}
//     cbz   x1, 0xbdc             ; count  == 0       -> return {0,0,0}
//     stp   d15, d14, [sp, #-0x40]!   ; and only THEN is a frame allocated
//
//     cmp x1, #0x3 / b.hi         ; <= 3 takes the scalar path
//     cmp x1, #0x10 / b.hs        ; >= 16 takes the unrolled ld3.4s path
//
//     ld3.4s { v11, v12, v13 }, [x11]   ; FOUR RGB records in one instruction
//     fcvtl2 v16.2d, v11.4s             ; and every lane widened to double
//
// The tail is the part worth reading closely:
//
//     ucvtf d3, x1 / fmov d4, #1.0 / fdiv d3, d4, d3   ; inv = 1.0 / count, ONCE
//     fmul  d0, d3, d0 / fcvt s0, d0                   ; r * inv
//     fmul  d1, d3, d1 / fcvt s1, d1                   ; g * inv
//     fmul  d2, d3, d2 / fcvt s2, d2                   ; b * inv
//
// A reciprocal computed once and multiplied three times. `sum / count` would be three
// `fdiv`s and would round differently, so the source has to say `1.0 / count`
// explicitly — clang does not make that substitution without fast-math. Reproduced as
// written rather than tidied into a division.
// ---------------------------------------------------------------------------
RGB averageColor(const RGB* pixels, size_t count) {
  RGB out{0.0f, 0.0f, 0.0f};
  if (pixels == nullptr) return out;
  if (count == 0) return out;

  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
  for (size_t i = 0; i < count; ++i) {
    r += static_cast<double>(pixels[i].r);
    g += static_cast<double>(pixels[i].g);
    b += static_cast<double>(pixels[i].b);
  }

  const double inv = 1.0 / static_cast<double>(count);
  out.r = static_cast<float>(r * inv);
  out.g = static_cast<float>(g * inv);
  out.b = static_cast<float>(b * inv);
  return out;
}

// ---------------------------------------------------------------------------
// brightestRegionColor(const RGB*, int, int, int)
//
// __ZN10colorpulse20brightestRegionColorEPKNS_3RGBEiii, 223 instructions, recovered in
// full.
//
//     cmp  w3, #0x1 / csinc w11, w3, wzr, gt   ; step = max(step, 1)
//     cmp  w1, w11 / csel w10, w1, w11, lo     ; cols = min(width,  step)
//     cmp  w2, w11 / csel w11, w2, w11, lo     ; rows = min(height, step)
//     fmov s1, #-1.00000000                    ; bestLuma starts BELOW every real luma
//
//     mul  w3, w9, w2 / udiv w3, w3, w11       ; y0 = gy * height / rows
//     add  w9, w9, #0x1
//     mul  w4, w9, w2 / udiv w4, w4, w11       ; y1 = (gy+1) * height / rows
//     udiv w5, w8, w11                         ; w5 is y0 AGAIN -- the compiler kept a
//                                              ; second copy rather than reusing w3
//     cmp  w3, w4 / b.ge <next column>         ; an empty row skips the whole row
//
//     mul  w7, w6, w1 / udiv w7, w7, w10       ; x1, from gx+1
//     mul  w19, w19, w1 / udiv w19, w19, w10   ; x0, from gx
//
// The `udiv`s are UNSIGNED divisions on values the guards have already made positive, and
// they FLOOR. That matters for the cell boundaries: with height 300 and rows 4 the bands
// are [0,75) [75,150) [150,225) [225,300), but with height 10 and rows 4 they are
// [0,2) [2,5) [5,7) [7,10) -- the boundaries are computed independently per edge rather
// than by accumulating a step, so no rounding error can accumulate.
//
// The cell mean reuses the reciprocal form:
//
//     ucvtf d17, x27 / fdiv d17, d3, d17       ; inv = 1.0 / count
//     fmul  d16, d16, d17 / fcvt s16, d16      ; r
//     fmul  d7, d7, d17 / fcvt s7, d7          ; g
//     fmul  d6, d6, d17 / fcvt s6, d6          ; b
//     fmov  s17, w13 / fmul  s17, s7, s17      ; g * 0.7152
//     fmov  s18, w14 / fmadd s17, s16, s18, s17 ; r * 0.2126 + that
//     fmov  s18, w15 / fmadd s17, s6, s18, s17  ; b * 0.0722 + that
//     fcmp  s17, s1 / b.le <next cell>          ; strictly greater, so ties keep the FIRST
//
// and the tail substitutes zero when nothing won:
//
//     fcmp s1, #0.0 / fcsel s0, s0, s2, ge      ; s2 was zeroed just above
//
// The comparison is `b.le` on the negative, i.e. `if (luma > bestLuma)`, so a cell whose
// luma EQUALS the running best does not displace it. On a uniform region every cell ties
// and the top-left cell is returned.
// ---------------------------------------------------------------------------
RGB brightestRegionColor(const RGB* pixels, int width, int height, int step) {
  if (pixels == nullptr) return RGB{0.0f, 0.0f, 0.0f};
  if (width < 1) return RGB{0.0f, 0.0f, 0.0f};
  if (height < 1) return RGB{0.0f, 0.0f, 0.0f};

  const int cells = step > 1 ? step : 1;
  const int cols = width < cells ? width : cells;
  const int rows = height < cells ? height : cells;

  RGB best{0.0f, 0.0f, 0.0f};
  float bestLuma = -1.0f;

  for (int gy = 0; gy < rows; ++gy) {
    const int y0 = static_cast<int>((static_cast<long long>(gy) * height) / rows);
    const int y1 = static_cast<int>((static_cast<long long>(gy + 1) * height) / rows);
    if (y0 >= y1) continue;

    for (int gx = 0; gx < cols; ++gx) {
      const int x0 = static_cast<int>((static_cast<long long>(gx) * width) / cols);
      const int x1 = static_cast<int>((static_cast<long long>(gx + 1) * width) / cols);

      double sr = 0.0;
      double sg = 0.0;
      double sb = 0.0;
      long long n = 0;
      for (int y = y0; y < y1; ++y) {
        const size_t rowBase = static_cast<size_t>(y) * static_cast<size_t>(width);
        for (int x = x0; x < x1; ++x) {
          const RGB& p = pixels[rowBase + static_cast<size_t>(x)];
          sr += static_cast<double>(p.r);
          sg += static_cast<double>(p.g);
          sb += static_cast<double>(p.b);
          ++n;
        }
      }
      if (n == 0) continue;

      const double inv = 1.0 / static_cast<double>(n);
      const RGB mean{static_cast<float>(sr * inv),
                     static_cast<float>(sg * inv),
                     static_cast<float>(sb * inv)};
      const float luma = luma709(mean);
      if (luma > bestLuma) {
        bestLuma = luma;
        best = mean;
      }
    }
  }

  if (!(bestLuma >= 0.0f)) return RGB{0.0f, 0.0f, 0.0f};
  return best;
}

}  // namespace colorpulse
