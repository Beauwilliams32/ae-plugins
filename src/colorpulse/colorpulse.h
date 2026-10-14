// colorpulse.h — recovered from the compiled ColorPulse bundle.
//
// ColorPulse drives a colour from a sampled region of another layer, smooths it over
// time, boosts its saturation, and applies a brightness. Five of its ten functions are
// recovered here.
//
// Recovered from the demangled symbol table and disassembly of ColorPulse.plugin.
// The original C++ was destroyed on 2026-10-13 with no remote and no backup.
//
// SCOPE. Seven of ten. `clampf`, `smoothScalar`, `luma709`, `smoothColor`,
// `saturationBoost` (88 instructions), `regionBounds` (57) and `averageColor` (201) are
// recovered in full. Three are NOT: `brightestRegionColor` (223), `dominantColor` (248)
// and `sampleColor` (215). Nothing is stubbed for them — and `sampleColor` CANNOT be
// implemented before the other two, because it tail-calls both; that is a dependency, not
// a decision.

#pragma once

#include <cstddef>

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
// [PARTLY RECOVERED — the dispatcher, read but NOT implemented]
//
//     RGB sampleColor(const RGB* pixels, int width, int height, Mode mode);
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
// The two tail calls are why this declaration has no definition: the bundle's own
// `sampleColor` is 215 instructions, of which about 201 are `averageColor` inlined and
// the remainder is this dispatch. It goes in AFTER the two it calls, not around them.
// ---------------------------------------------------------------------------
enum class Mode : int {
  Average = 0,           // and the default arm
  Dominant = 2,
  BrightestRegion = 3,
};

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
