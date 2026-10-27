// maskflow.cpp — transcribed from the compiled MaskFlow bundle.

#include "maskflow.h"

#include <algorithm>
#include <cmath>

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

}  // namespace maskflow
