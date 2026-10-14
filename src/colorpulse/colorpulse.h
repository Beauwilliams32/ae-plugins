// colorpulse.h — recovered from the compiled ColorPulse bundle.
//
// ColorPulse drives a colour from a sampled region of another layer, smooths it over
// time, boosts its saturation, and applies a brightness. Five of its ten functions are
// recovered here.
//
// Recovered from the demangled symbol table and disassembly of ColorPulse.plugin.
// The original C++ was destroyed on 2026-10-13 with no remote and no backup.
//
// SCOPE. `clampf`, `smoothScalar`, `luma709`, `smoothColor` and `saturationBoost` are
// recovered in full (88 instructions). `regionBounds` (57) and the four colour analysers
// — `averageColor` (201), `sampleColor` (215), `brightestRegionColor` (223),
// `dominantColor` (248) — are NOT: they are identified with signatures and sizes known
// and bodies unread. Nothing is stubbed for them.

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
