// autoflickerfix.h — recovered from the compiled AutoFlickerFix bundle.
//
// AutoFlickerFix measures how bright a region of a layer is, smooths that measurement
// over time to kill per-frame flicker, compares the smoothed target against the frame's
// own brightness, and converts the ratio into a bounded multiplicative gain. The shape
// of the DSP is unusually legible for a recovered namespace: `temporalTarget` feeds
// `computeGain`, and `GainClamp` is the ceiling that stops the correction from running
// away.
//
// Recovered from the demangled symbol table and disassembly of AutoFlickerFix.plugin.
// The original C++ was destroyed on 2026-10-13 with no remote and no backup.
//
// NAMESPACE-COMPLETE: every `aff::` symbol in the bundle is transcribed, so
// tools/codegen-diff.mjs reports zero unrecovered symbols for this directory.
// test/codegen.test.mjs asserts that marker and that measurement agree.
//
// SCOPE. All TEN `aff::` domain functions are recovered in full (719 instructions).
// What is NOT recovered is the host integration: `EffectMain` (681 instructions,
// present as a symbol) and the two anonymous-namespace helpers that touch the AE world
// — `SampleWorldStat(PF_LayerDef const*, aff::RegionBounds const&, bool)` (275) and
// `DrawStatsBar(PF_LayerDef*, float, aff::GainClamp const&)` (191). Those bodies are
// unread and nothing is stubbed for them.

#pragma once

#include <cstddef>
#include <vector>

namespace aff {

// ---------------------------------------------------------------------------
// [RECOVERED LAYOUT] GainClamp — the bounds `computeGain` clamps its result into.
//
// Pinned by `ldp s1, s2, [x0]` at the tail of `computeGain`: the two floats are read
// as an adjacent pair, and the SECOND one is the ceiling (`fcmp`/`fcsel` against it
// uses `gt`), so lo@0, hi@4 and the struct is 8 bytes.
//
// It is a SYMMETRIC DECIBEL WINDOW, and that is recovered rather than assumed — see
// `dbToGainClamp`, which builds the whole struct.
// ---------------------------------------------------------------------------
struct GainClamp {
  float lo = 0.0f;
  float hi = 1.0f;
};

static_assert(sizeof(GainClamp) == 8, "GainClamp is two floats read as one ldp");

// ---------------------------------------------------------------------------
// [RECOVERED LAYOUT] RegionBounds — a rectangle in pixel coordinates.
//
// `centerRegion` returns this in x0 and x1, not through memory: the tail is
//
//     orr  x0, x10, x12, lsl #32     ; minX | (minY << 32)
//     orr  x1,  x8,  x9, lsl #32     ; maxX | (maxY << 32)
//
// so the member ORDER is pinned by which value lands in the low half of each register.
// ---------------------------------------------------------------------------
struct RegionBounds {
  int minX = 0;
  int minY = 0;
  int maxX = 0;
  int maxY = 0;
};

static_assert(sizeof(RegionBounds) == 16,
              "RegionBounds is four ints returned in two registers");

// ---------------------------------------------------------------------------
// [exact] Scalar clamp. The EIGHTH copy of this idiom in the fleet, and the sixth
// recovered one.
//
// Identical to the other copies instruction for instruction: the high bound is applied
// first, then the low bound is tested against the ORIGINAL value, so on an inverted
// range (`lo > hi`) the low bound wins.
// ---------------------------------------------------------------------------
float clampf(float value, float lo, float hi);

// ---------------------------------------------------------------------------
// [exact] A symmetric decibel window, as a GainClamp.
//
//     gain = 10^(|db| / 20)
//     return { lo: gain > 0 ? 1/gain : 1,  hi: gain }
//
// i.e. { 10^(-|db|/20), 10^(+|db|/20) }. A ±6 dB tolerance gives {0.5012, 1.9953}.
//
// IT RETURNS THE STRUCT, NOT A FLOAT. The mangling cannot say so — C++ does not encode
// a non-template function's return type — and the giveaway is at the call site in
// `EffectMain`: `bl dbToGainClamp` is immediately followed by
//
//     stp s0, s1, [sp, #0xb8]      ; [sp+0xb8] is the GainClamp later handed to computeGain
//
// with no write to s1 anywhere in EffectMain. A float-returning helper could not define
// s1; an 8-byte two-float aggregate is returned in exactly s0/s1, so the store needs no
// second call and no scratch. The `mov.16b v1, v0` inside the body exists for the same
// reason — it is what puts `gain` where the second member has to be.
//
// The `> 0` test is the NaN and overflow guard. `exp10f` overflows to +inf for a large
// magnitude, so the low bound becomes 0; a NaN payload would otherwise reach the gain.
// Only the low bound is guarded, so a degenerate input leaves `hi` at 0 or NaN rather
// than at 1 — faithful to the binary, and noted because it looks like an oversight.
// ---------------------------------------------------------------------------
GainClamp dbToGainClamp(float db);

// ---------------------------------------------------------------------------
// [exact] The correction gain: how far to move `current` toward `target`, bounded.
//
//     if (current <= 1e-6f || target <= 1e-6f) return 1.0f;   // nothing to correct
//     const float t = clampf(amount, 0.0f, 1.0f);              // strength
//     const float g = t * (target / current - 1.0f) + 1.0f;    // lerp(1, ratio, t)
//     return clampf(g, clamp.lo, clamp.hi);
//
// ARGUMENT ORDER IS `current` FIRST, `target` SECOND, and the binary says so twice over.
// The body divides `s1 / s0`, and at each of EffectMain's four call sites the registers
// are loaded
//
//     mov.16b v0, v12              ; the per-frame measurement
//     mov.16b v1, v13              ; the result of temporalTarget(...)
//
// so the second argument is the smoothed target. Getting this backwards inverts the
// correction and is invisible to a self-check written from the same misreading, which is
// why the call sites are cited here rather than paraphrased.
//
// The 1e-6 floor keeps a black frame from producing a division by zero: clamping the
// resulting inf/nan after the fact would not be enough, so the guard is on the INPUTS.
// ---------------------------------------------------------------------------
float computeGain(float current, float target, float amount, const GainClamp& clamp);

// ---------------------------------------------------------------------------
// [exact] The centred sub-rectangle of a frame, as a fraction of its size.
//
//     f  = clampf(frac, 0, 1)
//     rw = lroundf(f * width),  rh = lroundf(f * height)     // fcvtas, ties away
//     dx = width - rw,          dy = height - rh
//     x0 = max(dx / 2, 0),      y0 = max(dy / 2, 0)
//     x1 = min(dx / 2 + rw, width), y1 = min(dy / 2 + rh, height)
//
// The clamp on `frac` means frac 1 walks the whole frame. At the other end the rectangle
// does NOT collapse to the top-left pixel, which is what a first reading assumed: frac 0
// gives `minX == maxX == width/2` and `minY == maxY == height/2`, a ZERO-AREA rectangle
// at the CENTRE of the frame. A self-check written from the disassembly rather than from
// that assumption is what caught it. `lroundf` rather than a truncating cast matters too:
// a 0.5-scaled odd dimension lands one pixel differently between the two.
//
// `max(dx / 2, 0)` is not the same expression as `dx < 0 ? 0 : dx / 2` — for a negative
// ODD dx the truncating division gives a negative result where the conditional gives 0 —
// but the two agree everywhere here, because `dx < 0` can only arise from a negative
// `rw`, which `lroundf` of a clamped non-negative fraction against a non-negative width
// cannot produce. The form above is the one that reproduces the bundle's `add`/`asr`
// divide-then-`bic` sequence instruction for instruction.
// ---------------------------------------------------------------------------
RegionBounds centerRegion(int width, int height, float frac);

// ---------------------------------------------------------------------------
// [exact] Arithmetic mean, accumulated in DOUBLE.
//
// The accumulator is `double` and only the RESULT is narrowed to float — the body is
// a `fcvtl`/`fadd d0` chain, not an `fadd s0`. For a per-frame brightness figure over
// thousands of pixels that is a real difference, not a stylistic one: an all-float sum
// loses low bits as it grows and the recovered code does not.
// ---------------------------------------------------------------------------
float meanOf(const std::vector<float>& values);

// ---------------------------------------------------------------------------
// [exact] Median of a copy.
//
// Takes the vector BY VALUE, and the ABI confirms it: the callee sorts the vector it
// was handed (`std::sort` runs on `x0`'s own buffer) rather than a scratch copy. The
// even-length case is the MEAN OF THE TWO MIDDLE ELEMENTS, computed as
// `(a + b) * 0.5f` — a multiply, not a divide, and the two are not interchangeable
// for every denormal.
//
// An empty input returns 0, not a NaN.
// ---------------------------------------------------------------------------
float medianOf(std::vector<float> values);

// ---------------------------------------------------------------------------
// [exact] Mean of the middle, after discarding `trim` of the data from BOTH ends.
//
//     trim is clamped to [0, 0.49]  -- 0.49, not 0.5, so the window can never empty
//     cnt = (size_t)(t * n)
//     k   = min(cnt, (n - 1) / 2)   -- so n - 2k is always at least 1
//
// That min is why `n > 2*k` is a tautology and the whole-array fallback branch in the
// binary is unreachable: the compiler emitted it because it could not prove the bound,
// and this transcription keeps the same guard so the instruction stream lines up.
//
// Copies and sorts `data`; the caller's buffer is untouched.
// ---------------------------------------------------------------------------
float trimmedMean(const float* data, size_t count, float trim);

// [exact] Convenience overload over a vector. Emitted as a five-instruction TAIL CALL
// (`ldp` the begin/end pair, `asr` the byte length to an element count, `b`), so it
// shares the pointer form's code exactly.
float trimmedMean(const std::vector<float>& values, float trim);

// ---------------------------------------------------------------------------
// [exact] Rolling median with a half-width of `w` pixels.
//
// Element i is the median of the window [max(i - w, 0), min(i + w, n - 1)], INCLUSIVE
// of both ends, so the window holds `min(i+w, n-1) - max(i-w, 0) + 1` samples and is
// clamped rather than wrapped at the edges. A negative `w` is treated as zero, which
// makes the filter the identity — the guard is `max(w, 0)` on the width only.
//
// Each window is memcpy'd into a scratch vector and fully sorted, so this is O(n · w
// log w) and not a sliding-window median. That is what the binary does; it is not an
// optimisation opportunity I have taken, because taking it would change the output for
// ties and break the codegen comparison.
// ---------------------------------------------------------------------------
std::vector<float> medianFilter(const std::vector<float>& values, int w);

// ---------------------------------------------------------------------------
// [exact] The temporally smoothed brightness target the effect corrects toward.
//
//     if (w <= 0) return meanOf(values);
//     return meanOf(medianFilter(values, w));
//
// Two stages of smoothing composed: a median filter removes isolated spikes (a flash,
// a dropped frame) without smearing the ones that persist, and the mean that follows
// produces the scalar target. Note the median runs on the whole signal before the
// mean, so the mean is over already-de-spiked samples.
//
// `meanOf` supplies the empty-input answer (0.0f) on both paths, which is why there is
// no explicit emptiness check in the body.
// ---------------------------------------------------------------------------
float temporalTarget(const std::vector<float>& values, int w);

}  // namespace aff
