// maskflow.cpp — transcribed from the compiled MaskFlow bundle.

#include "maskflow.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace maskflow {

// The shared copy, machine-code-identical to the fleet's other four carriers.
//
float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

// __ZN8maskflow26edgeCostScaleFromToleranceEf, 12 instrs
//
//   fmov s1, #100.0 / fcmp s0, s1 / fcsel s2, s1, s0, gt   ; min(tolerance, 100)
//   fsub s2, s1, s2                                        ; 100 - that
//   fdiv s1, s2, s1                                        ; / 100
//   fmov s2, #40.0 / fmul s1, s1, s2                       ; * 40
//   fcmp s0, #0.0 / fcsel s0, s2, s1, mi                   ; a NEGATIVE tolerance is 40.0
float edgeCostScaleFromTolerance(float tolerance) {
  const float clamped = tolerance > 100.0f ? 100.0f : tolerance;
  const float scale = (100.0f - clamped) / 100.0f * 40.0f;
  return tolerance < 0.0f ? 40.0f : scale;
}

// __ZN8maskflow10smoothstepEfff, 19 instrs
//
//   fsub s3, s2, s0 / fsub s4, s1, s0 / fdiv s3, s3, s4    ; t = (x - edge0) / (edge1 - edge0)
//   fmov s4, #1.0 / fcmp s3, s4 / fcsel s5, s4, s3, gt     ; cap at 1
//   fcmp s3, #0.0 / movi.2d v3, #0 / fcsel s5, s3, s5, mi  ; NOT floored: below zero the
//                                                          ; select returns t unchanged
//   fmul s6, s5, s5 / fmov s7, #3.0 / fmov s16, #-2.0
//   fmadd s5, s5, s16, s7 / fmul s5, s6, s5                ; t*t*(3 - 2t)
//   fcmp s2, s0 / fcsel s2, s3, s4, mi                     ; the degenerate-edge value
//   fcmp s0, s1 / fcsel s0, s5, s2, ne                     ; edge0 == edge1 ? that : the polynomial
float smoothstep(float edge0, float edge1, float x) {
  const float t = (x - edge0) / (edge1 - edge0);
  // Written as the two selects are: `t < 0 ? t : (t > 1 ? 1 : t)`. The lower branch
  // returns `t` itself, which is a clamp that clamps nothing — almost certainly a typo
  // for `0` in the original — and it is why this EXTRAPOLATES below edge0 instead of
  // flattening. Correcting it to a real clamp would change the effect's output.
  const float clamped = t < 0.0f ? t : (t > 1.0f ? 1.0f : t);
  const float polynomial = clamped * clamped * (3.0f - 2.0f * clamped);
  // ONE EXPRESSION, not an if and two returns. The shipped code computes both candidates
  // and selects at the end (`fcsel s0, s5, s2, ne`); writing it as a branch emitted an
  // early branch instead, and the arm order moves the count by two: 20 instructions with
  // the polynomial first, 18 with it second, against the bundle's 19. The behaviour is
  // the same either way and the count is recorded rather than chased.
  return edge0 == edge1 ? (x < edge0 ? t : 1.0f) : polynomial;
}

// __ZN8maskflow21computeDownsampleDimsEiii, 32 instrs
//
//   cmp w0, #0x1 / b.lt <invalid> / cmp w1, #0x0 / b.le <invalid>
//       ; width < 1 or height < 1 takes the invalid path
//   cmp w0, w1 / csel w8, w0, w1, hi        ; longest = max(width, height)
//   fmov s0, #1.0                           ; the default scale
//   cmp w8, w2 / b.le <dims>                ; no downsampling needed
//   scvtf s0, w2 / ucvtf s1, w8 / fdiv s0, s0, s1   ; scale = maxDim / longest
//   ucvtf s1, w0 / fmul s1, s0, s1 / fcvtas x8, s1  ; ROUND, ties away from zero
//   cmp w8, #0x1 / csinc w8, w8, wzr, gt             ; floored at 1
//   ... the same for height ...
//   fmov w1, s0                             ; the float rides in w1, i.e. the low half of x1
//   <invalid>: w8 = max(width,1) / w9 = max(height,1) / w1 = 0x3f800000 (1.0f)
//   <both>:    mov w8, w8 / orr x0, x8, x9, lsl #32 / ret
//              ; x0 = width | (height << 32), x1 = the scale
DownsampleDims computeDownsampleDims(int width, int height, int maxDim) {
  if (width < 1 || height < 1) {
    return {std::max(width, 1), std::max(height, 1), 1.0f};
  }
  const int longest = width > height ? width : height;
  float scale = 1.0f;
  if (longest > maxDim) scale = static_cast<float>(maxDim) / static_cast<float>(longest);
  return {
      std::max(static_cast<int>(std::lround(scale * static_cast<float>(width))), 1),
      std::max(static_cast<int>(std::lround(scale * static_cast<float>(height))), 1),
      scale,
  };
}


// __ZN8maskflow19downsampleBoxFilterERKNS_5ImageEii, 126 instrs
//
// The rect arithmetic is integer division per output pixel (not a running accumulator), which is
// why the mapping is exact for any output size rather than only for integer ratios.
Image downsampleBoxFilter(const Image& src, int outW, int outH) {
  Image out;
  const int ow = outW > 1 ? outW : 1;
  const int oh = outH > 1 ? outH : 1;
  out.width = ow;
  out.height = oh;
  out.pixels.assign(static_cast<size_t>(ow) * static_cast<size_t>(oh), RGB{0.0f, 0.0f, 0.0f});
  if (src.width < 1 || src.height < 1) return out;

  for (int oy = 0; oy < oh; ++oy) {
    const int iy0 = (src.height * oy) / oh;
    const int iy1 = (src.height * (oy + 1)) / oh;
    int iyEnd = iy0 + 1 > iy1 ? iy0 + 1 : iy1;      // csinc: at least one source row
    if (iyEnd > src.height) iyEnd = src.height;
    for (int ox = 0; ox < ow; ++ox) {
      const int ix0 = (src.width * ox) / ow;
      const int ix1 = (src.width * (ox + 1)) / ow;
      int ixEnd = ix0 + 1 > ix1 ? ix0 + 1 : ix1;
      if (ixEnd > src.width) ixEnd = src.width;

      float r = 0.0f, g = 0.0f, b = 0.0f;
      int count = 0;
      for (int y = iy0; y < iyEnd; ++y) {
        const RGB* row = &src.pixels[static_cast<size_t>(y) * static_cast<size_t>(src.width)];
        for (int x = ix0; x < ixEnd; ++x) {
          r += row[x].r;
          g += row[x].g;
          b += row[x].b;
          ++count;
        }
      }
      const float divisor = static_cast<float>(count > 1 ? count : 1);   // an empty rect is black
      out.pixels[static_cast<size_t>(oy) * static_cast<size_t>(ow) + static_cast<size_t>(ox)] =
          RGB{r / divisor, g / divisor, b / divisor};
    }
  }
  return out;
}

// __ZN8maskflow16upsampleBilinearERKNSt3__16vectorIfNS0_9allocatorIfEEEEiiii, 168 instrs
//
// The inverse of the box filter, and not a mirror of it: this one samples the source grid at
// fractional positions instead of averaging rectangles. The loop reads, per output pixel,
//
//     fx = sx * x,  fy = sy * y        sx = (srcW-1) / max(dstW-1, 1)
//     x0 = trunc(fx), wx = fx - x0     sy = (srcH-1) / max(dstH-1, 1)
//     x1 = min(x0+1, srcW-1)           y1 = min(y0+1, srcH-1)
//     top = a + (b-a)*wx ; bot = c + (d-c)*wx ; out = top + (bot-top)*wy
//
// which is why the endpoints land exactly on the source corners (at x = dstW-1 the product is
// srcW-1 by construction) and the `+1` neighbour is clamped down to the last column instead.
// Note the SCALE runs src -> dst (a source-pixel step), the opposite direction to
// computeDownsampleDims' output scale — the one number here a rewrite gets backwards.
//
// Three degenerate shapes have their own answer rather than a division by zero:
//   * dstW or dstH < 1, or srcW or srcH < 1 -> dstW*dstH zeros
//   * srcW*srcH > src.size()               -> the same zeros (a short source is not read)
//   * a 1x1 SOURCE                          -> every output pixel is src[0], broadcast
// and a 1-wide or 1-high source is handled by the general loop, because a zero scale leaves
// x0 = wx = 0 and collapses the blend to a copy of column (or row) zero.
std::vector<float> upsampleBilinear(const std::vector<float>& src, int srcW, int srcH, int dstW,
                                    int dstH) {
  std::vector<float> out;
  const size_t count = static_cast<size_t>(dstW > 0 ? dstW : 0) *
                       static_cast<size_t>(dstH > 0 ? dstH : 0);
  out.assign(count, 0.0f);
  if (srcW < 1 || srcH < 1 || dstW < 1 || dstH < 1) return out;
  if (static_cast<size_t>(srcW) * static_cast<size_t>(srcH) > src.size()) return out;
  if (srcW == 1 && srcH == 1) {                      // 1x1 source: a constant image
    std::fill(out.begin(), out.end(), src[0]);
    return out;
  }

  float sx = 0.0f;
  float sy = 0.0f;
  if (srcW >= 2) sx = static_cast<float>(srcW - 1) / static_cast<float>(dstW - 1 > 1 ? dstW - 1 : 1);
  if (srcH >= 2) sy = static_cast<float>(srcH - 1) / static_cast<float>(dstH - 1 > 1 ? dstH - 1 : 1);

  for (int y = 0; y < dstH; ++y) {
    const float fy = sy * static_cast<float>(y);
    const int y0 = static_cast<int>(fy);             // fcvtzs: truncate, not round
    const float wy = fy - static_cast<float>(y0);
    const int y1 = y0 + 1 < srcH ? y0 + 1 : srcH - 1;
    const float* row0 = &src[static_cast<size_t>(y0) * static_cast<size_t>(srcW)];
    const float* row1 = &src[static_cast<size_t>(y1) * static_cast<size_t>(srcW)];
    float* dst = &out[static_cast<size_t>(y) * static_cast<size_t>(dstW)];
    for (int x = 0; x < dstW; ++x) {
      const float fx = sx * static_cast<float>(x);
      const int x0 = static_cast<int>(fx);
      const float wx = fx - static_cast<float>(x0);
      const int x1 = x0 + 1 < srcW ? x0 + 1 : srcW - 1;
      const float top = row0[x0] + (row0[x1] - row0[x0]) * wx;
      const float bot = row1[x0] + (row1[x1] - row1[x0]) * wx;
      dst[x] = top + (bot - top) * wy;
    }
  }
  return out;
}

// __ZN8maskflow10buildMatteERKNS_12ArrivalFieldEffb, 169 instrs
//
//   fmov s0, #100.0 / fcmp s9,s0 / fcsel s1,s0,s9,gt / fdiv s1,s1,s0   ; min(tol,100)/100
//   fcmp s9, #0.0 / fcsel s1, s0, s1, mi        ; a NEGATIVE tolerance is 100.0f, not 0
//   fmov s2, #1e-4 / fcmp s8,s2 / fcsel s4,s2,s8,mi                   ; max(half, 1e-4)
//   ldr s2, [x21,#0x20] / fmul s5, s1, s2       ; centre = tol * field.span
//   fsub s3, s5, s4 / fadd s4, s4, s5 / fsub s5, s4, s3   ; lo, hi, hi - lo
//   fcmp s1,#0.0 / cset w8,ls ; fcmp s1,#1.0 / csinc w8,w8,wzr,lt     ; tol <= 0 || tol >= 1
//   ldr s17,[x9],#4 / fcmp s17,s6(inf) / cset w12,eq / csinc w12,w12,wzr,vc  ; +inf or NaN
//   csinc w13, w12, wzr, hi     ; (tol > 0) ? special : true
//   fcsel s18, s0, s2, ne       ; fill = 0 when that, 1 otherwise
//   fcmp s3, s4 / b.ne          ; lo != hi ? ramp : step
//
// The two float identities around the invert select (`fsub 1,x` then `fsub 1,that`) are the
// compiler's, not the source's: they exist because the shipped code computes both arms and
// selects. The arithmetic below is what it computes.
std::vector<float> buildMatte(const ArrivalField& field, float tolerance, float halfWidth,
                              bool invert) {
  const size_t count =
      static_cast<size_t>(field.width) * static_cast<size_t>(field.height);
  std::vector<float> out(count, 0.0f);
  if (count == 0) return out;                 // `cbz x24` — before the allocation, not after

  float tol = tolerance > 100.0f ? 100.0f : tolerance;
  tol = tol / 100.0f;
  // A negative tolerance does not clamp to the bottom of the range — it becomes 100.0f, i.e.
  // past the top, which is what forces the degenerate branch below.
  if (tolerance < 0.0f) tol = 100.0f;

  const float half = halfWidth < 1e-4f ? 1e-4f : halfWidth;
  const float centre = tol * field.span;
  const float lo = centre - half;
  const float hi = centre + half;
  const float den = hi - lo;
  const bool degenerate = tol <= 0.0f || tol >= 1.0f;

  for (size_t i = 0; i < count; ++i) {
    const float v = field.values[i];
    // Written as `!(v < +inf)` — a single comparison — because that is what the shipped code
    // computes: `fcmp v, inf` then `cset eq` / `csinc ...,vc`, true for +inf AND for NaN. A
    // -inf is NOT special; it flows through the ramp like any other tiny value and clamps to 0.
    const bool special = !(v < std::numeric_limits<float>::infinity());
    const float fill = (tol > 0.0f && !special) ? 1.0f : 0.0f;
    float value;
    if (special || degenerate) {
      value = fill;
    } else if (lo == hi) {
      // Unreachable for a normal halfWidth — the 1e-4 floor keeps `den` positive. It is here
      // for CANCELLATION, when span - half and span + half round to the same float.
      value = v < lo ? 0.0f : 1.0f;
    } else {
      const float raw = (v - lo) / den;
      // Capped at 1 first, floored at 0 second — the arm order `clampf` uses, so a NaN passes
      // through both selects untouched and the polynomial hands it back.
      const float capped = raw > 1.0f ? 1.0f : raw;
      const float t = capped < 0.0f ? 0.0f : capped;
      value = t * t * (3.0f - 2.0f * t);
    }
    // `fsub 1,x` then `fsub 1,that` then a select — so the INVERTED arm is 1 - (1 - value),
    // not `value`. That round trip is not the identity: for a value small enough that `1 - v`
    // rounds to 1, `1 - (1 - v)` is 0. It is what the binary computes, so it is what this does.
    const float inverted = 1.0f - value;
    out[i] = invert ? 1.0f - inverted : inverted;
  }
  return out;
}

}  // namespace maskflow
