// colorpulse.h — recovered from the compiled ColorPulse bundle.
//
// ColorPulse drives a colour from a sampled region of another layer, smooths it over
// time, boosts its saturation, and applies a brightness. Five of its ten functions are
// recovered here.
//
// Recovered from the demangled symbol table and disassembly of ColorPulse.plugin.
// The original C++ was destroyed on 2026-10-13 with no remote and no backup.
//
// SCOPE. Six of ten. `clampf`, `smoothScalar`, `luma709`, `smoothColor`,
// `saturationBoost` (88 instructions) and `regionBounds` (57) are recovered in full.
// The four colour analysers — `averageColor` (201), `sampleColor` (215),
// `brightestRegionColor` (223), `dominantColor` (248) — are NOT: they are identified with
// signatures and sizes known and bodies unread. Nothing is stubbed for them.

#pragma once

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
