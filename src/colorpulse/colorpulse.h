// colorpulse.h — recovered from the compiled ColorPulse bundle.
//
// ColorPulse drives a colour from a sampled region of another layer, smooths it over
// time, boosts its saturation, and applies a brightness. Five of its ten functions are
// recovered here.
//
// Recovered from the demangled symbol table and disassembly of ColorPulse.plugin.
// The original C++ was destroyed on 2026-10-13 with no remote and no backup.
//
// NAMESPACE-COMPLETE: every `colorpulse::` symbol in the bundle is transcribed, so
// tools/codegen-diff.mjs reports zero unrecovered symbols for this directory.
// test/codegen.test.mjs asserts that marker and that measurement agree.
//
// SCOPE. All TEN `colorpulse::` functions are recovered in full (1123 instructions). The
// host integration — `EffectMain` and the anonymous-namespace helper `GridFromWorld8`
// (91 instructions) — is identified only, and nothing is stubbed for it.

#pragma once

#include <cstddef>
#include <vector>

namespace colorpulse {

// ---------------------------------------------------------------------------
// [RECOVERED LAYOUT] RGB — three floats, pinned by `ldp s0, s1, [x0]` followed by
// `ldr s2, [x0, #0x8]`, so r@0, g@4, b@8 and the struct is 12 bytes.
//
// Returned in s0/s1/s2, i.e. as a homogeneous float aggregate, which is why every
// function below reads its inputs from memory and hands results back in registers.
// ---------------------------------------------------------------------------
struct RGB {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
};

static_assert(sizeof(RGB) == 12, "RGB is three floats, read as a pair plus a single");

// ---------------------------------------------------------------------------
// [RECOVERED] Region — which part of a rectangle to sample. A 4-BYTE enum, and the
// width is pinned by the ABI rather than by a symbol: `regionBounds` reads its selector
// from `w0` and its two dimensions from `w1` and `w2`, so the struct is passed in ONE
// register. An 8- or 16-byte struct would have consumed `w0`/`w1` or `x0`/`x1` and pushed
// the dimensions two registers along.
//
// THE ENUMERATOR NAMES ARE NOT IN THE BINARY. Only the selector's values and the
// rectangles they produce are, so they are named for the geometry they select. The call
// site narrows the value to `{2, 3, 4}` and maps everything else to 0 — reproduced here,
// because it means value 1 is never passed even if the original enum has a member for it.
// ---------------------------------------------------------------------------
enum class Region : int {
  TopLeftThreeQuarters = 0,        // [0, 3W/4] x [0, 3H/4]   -- the DEFAULT arm
  BottomRightThreeQuarters = 2,    // [W/4, W]  x [H/4, H]
  TopThird = 3,                    // [0, W]    x [0, H/3]
  BottomThird = 4,                 // [0, W]    x [2H/3, H]
};

// ---------------------------------------------------------------------------
// [RECOVERED LAYOUT] Rect — four ints, 16 bytes, returned in x0 and x1:
//
//     orr  x0, x8, x10, lsl #32     ; minX | (minY << 32)
//     csel x1, x8, x13, lt          ; maxX | (maxY << 32)
//
// The member ORDER is pinned the same way `aff::RegionBounds` was: by which value lands
// in the low half of each register. The caller reads it as `x1 - x0` componentwise to get
// a width and a height, which is what confirms that the low halves are the minima.
// ---------------------------------------------------------------------------
struct Rect {
  int minX = 0;
  int minY = 0;
  int maxX = 0;
  int maxY = 0;
};

static_assert(sizeof(Rect) == 16, "Rect is four ints returned in two registers");

// ---------------------------------------------------------------------------
// [exact] The sub-rectangle of a `width` x `height` box that `region` selects.
//
//     region 2:  [W/4,  H/4 ] .. [W,    H  ]     bottom-right three quarters
//     region 3:  [0,    0   ] .. [W,    H/3]     top third, full width
//     region 4:  [0,   2H/3 ] .. [W,    H  ]     bottom third, full width
//     anything else (0 in practice): [0, 0] .. [3W/4, 3H/4]   top-left three quarters
//
// followed by a floor on the SIZE, applied per axis and only when the rectangle came out
// empty or inverted:
//
//     if (minX >= maxX) maxX = min(width,  minX + 1);
//     if (minY >= maxY) maxY = min(height, minY + 1);
//
// TWO DETAILS WORTH NAMING. The `3W/4` upper bound is computed as `W - W/4`, not as a
// multiply, and the divisions are SIGNED integer divisions with truncation toward zero —
// `smull` by `0x55555556` with a `lsr #63` sign fixup for the thirds. And the floor is
// `min(dimension, min + 1)` rather than a bare `min + 1`, so a degenerate input yields a
// ZERO-area rectangle instead of one that runs off the edge; the caller then has to clamp
// to at least one pixel itself, which the EffectMain call site does.
//
// The `> =` test is what makes this a floor on the size and not a nudge: a rectangle whose
// bounds are exactly equal is already empty, so it is replaced outright.
// ---------------------------------------------------------------------------
Rect regionBounds(Region region, int width, int height);

// ---------------------------------------------------------------------------
// [exact] Arithmetic mean of a run of colours, accumulated in DOUBLE.
//
//     if (pixels == nullptr || count == 0) return {0, 0, 0};
//     r, g, b accumulated over `count` 12-byte RGB records
//     inv = 1.0 / (double)count
//     return { (float)(r * inv), (float)(g * inv), (float)(b * inv) }
//
// THE DIVISION IS A RECIPROCAL AND THEN A MULTIPLY, not `sum / count`. clang will not
// make that substitution on its own at -O2 without fast-math, so the source really does
// compute the reciprocal — `fdiv d3, d4, d3` appears ONCE and feeds three `fmul`s.
//
// WHAT THAT DOES AND DOES NOT CHANGE, measured rather than asserted. The two spellings
// differ as DOUBLES, and the self-check counts the counts where they do. They do NOT
// differ for any count from 2 to 4999 once narrowed to `float`: the reciprocal's error is
// about one ulp of double (2^-52) while the narrowing rounds at 2^-24, so it is almost
// always annihilated on the way out. An earlier version of this comment claimed the
// returned value "differs in the last bit for many counts", which the measurement
// disproved. The transcription keeps the reciprocal because that is what the bundle
// computes, not because a caller could tell.
//
// The zero return for a null pointer or an empty run comes from two `cbz` tests BEFORE
// the stack frame is even allocated, so it is an early return rather than a special case
// at the end.
//
// The accumulation is widened: the bundle loads with `ld3.4s` (four 12-byte RGB records
// at a time, as a structure-of-arrays) and immediately `fcvtl`s every lane to double
// before `fadd`. Summing in float would lose low bits over a large region.
// ---------------------------------------------------------------------------
RGB averageColor(const RGB* pixels, size_t count);

// ---------------------------------------------------------------------------
// [exact] The colour of the BRIGHTEST CELL of a region, on a `step` x `step` grid.
//
//     step = max(step, 1)
//     rows = min(height, step), cols = min(width, step)
//     bestLuma = -1.0f                      ; so the first non-empty cell always wins
//     for gy in [0, rows):
//       y0 = gy * height / rows,  y1 = (gy+1) * height / rows
//       if (y0 >= y1) continue              ; an empty row skips all its columns
//       for gx in [0, cols):
//         x0 = gx * width / cols,  x1 = (gx+1) * width / cols
//         cell = mean of the pixels in [y0,y1) x [x0,x1)   ; double sum, reciprocal mean
//         if (cell is empty) continue
//         luma = luma709(cell)
//         if (luma > bestLuma) { bestLuma = luma; best = cell }
//     return bestLuma >= 0 ? best : {0,0,0}
//
// `step` is a NUMBER OF DIVISIONS PER AXIS, not a cell size. `cols = min(width, step)` is
// how many COLUMNS the grid has, so step 1 is a single cell covering the whole region —
// its mean — and a step at or above the axis length is one cell per pixel. Two self-checks
// were written from the opposite reading first and failed, which is why it is spelled out.
//
// So it returns a REGION MEAN, not an individual pixel: the grid cell that is brightest
// on average supplies its own average colour. Cell boundaries are `axis * index / count`
// — integer division, floor, computed independently per edge, so no error accumulates.
//
// THE `-1` INITIALISER IS LOad-BEARING and explains the final guard. Colours are
// non-negative, so a luma is too, and `bestLuma = -1` guarantees the first non-empty
// cell is taken even if its luma is exactly 0 (a black cell). If NO cell is ever
// non-empty, `bestLuma` stays at -1 and the tail's `fcmp s1, #0.0` / `fcsel ... ge`
// substitutes zero — which is why that guard exists rather than an unconditional return.
//
// `luma709` is INLINED here by the bundle, with the same constant order as the standalone
// function: `g * 0.7152 + r * 0.2126`, then `+ b * 0.0722`. This transcription CALLS it
// rather than repeating the expression, which is bit-identical because the constants and
// the association both match.
// ---------------------------------------------------------------------------
RGB brightestRegionColor(const RGB* pixels, int width, int height, int step);

// ---------------------------------------------------------------------------
// [exact] The mean colour of the MOST POPULOUS quantisation bin.
//
//     if (pixels == nullptr || count == 0 || levels < 1) return {0,0,0}
//     bins = levels^3                     ; as size_t
//     std::vector<int> hist(bins)         ; zero-initialised
//     index(r,g,b) = ri*levels^2 + gi*levels + bi
//     ri = min(levels-1, (int)(clampf(p.r, 0, 1) * (float)levels))   -- and gi, bi
//
//     pass 1: hist[index(p)] += 1 for every pixel
//     best   = argmax over hist, FIRST index wins on a tie (the scan is `-1` initialised
//              and strictly greater, so it keeps the lowest bin)
//     pass 2: sum the r/g/b of every pixel whose index equals `best`, in DOUBLE
//     inv    = 1.0 / matched
//     return { sumR*inv, sumG*inv, sumB*inv }
//
// THE RETURN IS A MEAN OF THE WINNING BIN'S PIXELS, NOT THE BIN CENTRE. The bundle walks
// the pixel list a SECOND time to gather them. Reporting the bin centre instead would be
// the obvious shortcut and would be visibly wrong: an 8-level bin is a 1/8-wide cube.
//
// BLUE IS THE FASTEST AXIS — `index = ri*levels^2 + gi*levels + bi` — which is the
// opposite end from `.cube` LUT data (red fastest) elsewhere in this fleet, so it is worth
// being explicit rather than assuming the neighbouring convention.
//
// The quantiser truncates (`fcvtzs`) and clamps through the recovered `clampf`, so a
// channel of exactly 1.0 maps to `levels` and is then pulled back to `levels - 1` by the
// min. Both are needed: without the clamp a negative channel would index backwards, and
// without the min a 1.0 channel would index one past the end of the axis.
//
// THE CLAMP APPLIES TO THE BIN INDEX ONLY, so the RETURNED COLOUR CAN LEAVE [0, 1]. The
// second pass averages the RAW pixels of the winning bin, so an overrange pixel and a
// white pixel share the top bin and come back as their raw mean. Two self-checks were
// written expecting the clamped value and returned 1.5 and -2.5 instead, which is right.
// A caller working in 32-bit float must not assume this saturates.
// ---------------------------------------------------------------------------
RGB dominantColor(const RGB* pixels, size_t count, int levels);

// ---------------------------------------------------------------------------
// [exact] The colour analyser selector.
//
// Declared here and defined in the .cpp; the signature is the mangled one.
//
// `sampleColor` is a DISPATCHER, and reading it was worth more than the function: it
// enumerates `Mode` and pins the arguments of the two analysers that are still unread.
//
//     mov  x8, x1                      ; stash width, because x1 becomes the count
//     cmp  w2, #0x0
//     ccmp w1, #0x0, #0x4, gt          ; (width > 0 && height > 0)
//     umull x9, w2, w1                 ; count = width * height
//     csel x1, x9, xzr, gt             ; count, or 0 when either is non-positive
//     cmp  w3, #0x3 / b.eq 0x1368
//     cmp  w3, #0x2 / b.ne 0x1374
//     mov  w2, #0x8
//     b    __ZN10colorpulse13dominantColorEPKNS_3RGBEmi      ; TAIL CALL, depth 8
//     0x1368: mov x1, x8 / mov w3, #0x4
//     b    __ZN10colorpulse20brightestRegionColorEPKNS_3RGBEiii  ; TAIL CALL, arg 4
//     0x1374: <the INLINED averageColor body, 201 instructions>
//
// So the selector is:
//
//     mode == 3  ->  brightestRegionColor(pixels, width, height, 4)
//     mode == 2  ->  dominantColor(pixels, count, 8)
//     anything else (0 in practice) -> averageColor(pixels, width * height)
//
// THREE THINGS THIS ESTABLISHES:
//
//   * `Mode` has at least the values 2 and 3, and the default arm is the plain average.
//     The enumerator NAMES are not in the binary, so they are named here for what they
//     select. The call site in `EffectMain` was not read, so which value reaches the
//     effect's popup is still open.
//   * `brightestRegionColor` takes (pixels, WIDTH, HEIGHT, fourth) — the `mov x1, x8`
//     restores the width that the count computation overwrote, so the two ints are the
//     region dimensions and the fourth is a sampling step of 4. Its own body guards
//     `width < 1` and `height < 1`, which confirms the order.
//   * `dominantColor` takes (pixels, COUNT, third) with a third of 8, and its body guards
//     `count == 0` and `third < 1`. It allocates a histogram and uses `std::vector<int>`.
//
// The two tail calls are why this could not be written until both callees were: the
// bundle's own `sampleColor` is 215 instructions, of which about 201 are `averageColor`
// INLINED and the remainder is this dispatch, with both other paths emitted as real tail
// calls. That dependency is why `dominantColor` was recovered first.
// ---------------------------------------------------------------------------
enum class Mode : int {
  Average = 0,           // and the default arm
  Dominant = 2,
  BrightestRegion = 3,
};

RGB sampleColor(const RGB* pixels, int width, int height, Mode mode);

// ---------------------------------------------------------------------------
// [exact] Scalar clamp. The SEVENTH copy of this idiom in the fleet.
// ---------------------------------------------------------------------------
float clampf(float value, float lo, float hi);

// ---------------------------------------------------------------------------
// [exact] Temporal smoothing of one scalar, as a lerp on a clamped blend:
//
//     t = clamp(blend, 0, 1)
//     return newest * t + previous * (1 - t)
//
// Note the argument order: the newest value is FIRST and is weighted by `blend`, so
// blend 1 means "all new" and blend 0 means "all old". Swapping them would invert the
// smoothing, which is the kind of error a test can catch and a reading cannot.
// ---------------------------------------------------------------------------
float smoothScalar(float newest, float previous, float blend);

// ---------------------------------------------------------------------------
// [exact] Rec.709 luma. The constants are 0.2126, 0.7152 and 0.0722, and the
// ACCUMULATION ORDER is `(g*c1 + r*c2) + b*c3` — the same constants and the same order
// as `lutblend::luma709`, instruction for instruction apart from the RGB struct
// argument. A test cross-checks the two against each other.
// ---------------------------------------------------------------------------
float luma709(const RGB& c);

// ---------------------------------------------------------------------------
// [exact] Componentwise temporal smoothing of a colour, by the same lerp rule as
// smoothScalar and with the same argument order: newest first, weighted by `blend`.
//
// The body computes red and green TOGETHER as a two-lane SIMD operation and blue on its
// own — same arithmetic, different shape.
// ---------------------------------------------------------------------------
RGB smoothColor(const RGB& newest, const RGB& previous, float blend);

// ---------------------------------------------------------------------------
// [exact] Scale each channel away from the colour's own luma.
//
//     if (amount <= 0) return c;                       // unchanged, EARLY
//     l = luma709(c)
//     out.ch = clamp(l + (c.ch - l) * (1 + amount), 0, 1)
//
// So `amount` is a FRACTION, not a percentage: 1.0 doubles the distance from luma and
// 0 is a no-op. The guard is `<= 0`, and it returns the input values untouched rather
// than clamping them, so a negative amount cannot desaturate — it does nothing.
//
// Each channel is clamped to [0,1] individually, which is why an extreme amount
// produces flat primary colours rather than wrapping.
// ---------------------------------------------------------------------------
RGB saturationBoost(const RGB& c, float amount);

}  // namespace colorpulse
