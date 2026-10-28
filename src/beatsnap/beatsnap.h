// beatsnap.h — reconstructed from the compiled BeatSnap bundle.
//
// Every declaration below is copied from the demangled Mach-O symbol table of
// BeatSnap.plugin (bundle id com.williamsmedia.aftereffects.BeatSnap, version
// 1.0.0), not from source: the original C++ was destroyed on 2026-10-13 with no
// remote and no backup.
//
// All ELEVEN of this bundle's authored functions are transcribed here.
//
// NAMESPACE-COMPLETE
//
// The five shared-DSP functions are COPIES, and that is the point. BeatSnap's
// `hannWindow`, `fftRadix2`, `downmixToMono`, `isPowerOfTwo` and `clampf` are
// machine-code-identical to the `spectra::` ones — `tools/dsp-duplication.mjs`
// compares every copy across every installed bundle and reports no drift — so the
// transcription is shared and only the namespace differs. That is a property of
// the shipped fleet, not a shortcut: the same five functions were compiled into
// several effects, which is why one recovered implementation serves them all.
//
// The other six are BeatSnap's own onset detection, and every one of them was
// recovered from this bundle's own instructions: medianOfWindow,
// adaptiveThreshold, computeFlux, pickOnsets, detectOnsets and renderValues.
// None of them is shared with any other bundle, so there was nothing to copy —
// each was read out of BeatSnap.plugin's own listing. It is the listing of all
// six that pinned the three structs below.
//
// SIGNATURE FIDELITY. `[exact]` means the signature is verbatim from the symbol
// mangling. `[partial]` means the signature is verbatim but the BODY is a
// behavioural recovery rather than an instruction-identical one: the semantics
// are pinned by the listing and by the self-check assertions, but the recovered
// source compiles to a different instruction stream. The two are not the same
// claim and are not interchangeable: an `[exact]` body was read out of the
// listing instruction by instruction, an `[partial]` one was read out of the
// listing's CONTROL FLOW and its constants.
//
// THE STRUCTS. Only their layouts are recovered — the names are not in the
// binary. Every offset below is a load or a store in this bundle's own code;
// none of them is a guess about order. Where the listing cannot see past a
// field, that is said rather than filled in.

#pragma once
#include <cstddef>
#include <vector>

namespace beatsnap {

// ---------------------------------------------------------------------------
// Shared DSP, recovered and verified instruction-identical.
// ---------------------------------------------------------------------------

// [exact] Three-way clamp. The LOW bound is tested last, so an inverted range
// yields `lo` — a detail shared with every other copy in the fleet.
float clampf(float value, float lo, float hi);

// [exact] Strict `> 0` guard: 0 and every negative value are not powers of two.
bool isPowerOfTwo(int n);

// [exact] Periodic (not symmetric) Hann window of `n` points: the denominator is
// `n`, not `n - 1`, so the window does not repeat its endpoints. `n == 1` is
// special-cased to 1.0f; `n < 1` yields an empty vector.
std::vector<float> hannWindow(int n);

// [exact] In-place radix-2 complex FFT on a power-of-two length. Returns without
// touching the input when `re.size() < 2`, when `im` is a different length, or
// when the length is not a power of two — it does NOT resize or pad. The
// transform is the forward convention (negative exponent), and the twiddles are
// built by recurrence from `sin`/`cos` of the per-stage angle.
void fftRadix2(std::vector<float>& re, std::vector<float>& im);

// ---------------------------------------------------------------------------
// Onset detection, recovered from this bundle's own functions.
// ---------------------------------------------------------------------------

// [exact] Median of a window of `values` centred on `centre`, `radius` samples to
// each side, clamped to the vector's bounds.
//
// Two details are worth stating because both are easy to "fix" into being wrong:
// the window is COPIED and sorted (so the input is not disturbed), and the result
// is `sorted[count / 2]` — for an EVEN count that is the upper of the two middle
// values, NOT their average. An empty input, or a window that clamps to nothing
// (radius < 0 wide enough to invert the range), returns 0.0f.
float medianOfWindow(const std::vector<float>& values, int centre, int radius);

// [exact] Per-sample onset threshold over a flux track.
//
// `window` is the half-width handed to `medianOfWindow`. `scale` is a control
// whose direction is worth naming: the multiplier starts at 5.0 and falls to 1.4
// as `scale` rises from 0 to 100, so a HIGHER `scale` means a LOWER threshold and
// more onsets. Above 100 it saturates; a negative `scale` skips the ramp entirely
// and uses 5.0.
//
// Every threshold is then floored at 5% of the loudest sample in the whole track,
// which is what stops a quiet passage from turning every noise floor wobble into
// an onset. The floor is computed once, from the track maximum, and the maximum
// starts at ZERO rather than at the first sample — so an all-negative track floors
// at 0.
std::vector<float> adaptiveThreshold(const std::vector<float>& flux, int window, float scale);

// [exact] Interleaved to mono by AVERAGING the channels, not summing them, so the
// result is the same level regardless of channel count. Null input or
// `channels < 1` yields an empty vector. The sum is DIVIDED by the channel count
// converted to float — `ucvtf` then `fdiv` in the bundle, which is not the same
// thing as multiplying by a float reciprocal: the two differ by an ulp, and a
// first pass at this header claimed the reciprocal form. The self-check pins the
// distinction rather than the value.
std::vector<float> downmixToMono(const float* interleaved, size_t frames, int channels);

// ---------------------------------------------------------------------------
// [inferred names, recovered layout] BeatSnap's onset-detection parameters.
//
//   ldr  d8, [x2]            +0x00  sampleRate    (a DOUBLE; the guard is `> 0`)
//   ldr  w28, [x2, #0x8]     +0x08  fftSize       (an int; must be a power of two >= 4)
//   ldp  s1, s0, [x22, #0xc] +0x0c  bandLoHz, +0x10 bandHiHz
//                                                    (floats; the pair is min/max'd
//                                                     before it is used at all)
//   ldr  s0, [x20, #0x14]    +0x14  thresholdScale (a float)
//   ldr  s0, [x20, #0x18]    +0x18  pickScale      (a float)
//   ldr  w1, [x20, #0x1c]    +0x1c  medianWindow  (an int)
//
// The four floats at +0x14..+0x1c are read by `detectOnsets` only, and it reads
// them in this order: medianWindow -> thresholdScale -> pickScale.
// ---------------------------------------------------------------------------
struct OnsetConfig {
  double sampleRate = 0.0;    // +0x00
  int fftSize = 0;            // +0x08
  float bandLoHz = 0.0f;      // +0x0c
  float bandHiHz = 0.0f;      // +0x10
  float thresholdScale = 0.0f;// +0x14
  float pickScale = 0.0f;     // +0x18
  int medianWindow = 0;       // +0x1c
};

static_assert(offsetof(OnsetConfig, sampleRate) == 0x00, "`ldr d8, [x2]`");
static_assert(offsetof(OnsetConfig, fftSize) == 0x08, "`ldr w28, [x2, #0x8]`");
static_assert(offsetof(OnsetConfig, bandLoHz) == 0x0c, "the low half of `ldp s1, s0, [x22, #0xc]`");
static_assert(offsetof(OnsetConfig, bandHiHz) == 0x10, "the high half of the same pair");
static_assert(offsetof(OnsetConfig, thresholdScale) == 0x14, "`ldr s0, [x20, #0x14]`");
static_assert(offsetof(OnsetConfig, pickScale) == 0x18, "`ldr s0, [x20, #0x18]`");
static_assert(offsetof(OnsetConfig, medianWindow) == 0x1c, "`ldr w1, [x20, #0x1c]`");
static_assert(sizeof(OnsetConfig) == 0x20, "0x20 bytes: a double, an int, six more");

// ---------------------------------------------------------------------------
// [inferred names, recovered layout] The spectral-flux track `computeFlux`
// returns, and the only thing `pickOnsets` reads out of it besides the flux
// itself.
//
//   ldp x9, x8, [x0]         +0x00  the flux vector's begin/end (a std::vector<float>)
//   ldp d1, d2, [x21, #0x18] +0x18  hopSeconds, +0x20 windowSeconds (doubles)
//   stp d0, d1, [x8, #0x18]           the two doubles are stored together
//
// The vector is the FIRST member: the listing's zeroing prologue is
// `str xzr, [x8, #0x20]` / `movi.2d v0, #0` / `stp q0, q0, [x8]`, which covers
// 0x00..0x28 in one go, and `computeFlux` calls `vector<float>::assign` with the
// sret pointer ITSELF as the vector's `this` — so the flux vector lives at +0x00
// and the two doubles follow it.
// ---------------------------------------------------------------------------
struct FluxTrack {
  std::vector<float> flux;      // +0x00
  double hopSeconds = 0.0;      // +0x18
  double windowSeconds = 0.0;   // +0x20
};

static_assert(offsetof(FluxTrack, flux) == 0x00, "the vector is the first member");
static_assert(offsetof(FluxTrack, hopSeconds) == 0x18, "`ldp d1, d2, [x21, #0x18]`");
static_assert(offsetof(FluxTrack, windowSeconds) == 0x20, "the high half of the same pair");
static_assert(sizeof(FluxTrack) == 0x28, "a 24-byte vector plus two doubles");

// ---------------------------------------------------------------------------
// [inferred names, recovered layout] How `renderValues` turns onset times into
// a rendered value track.
//
//   ldr  w8, [x22]           +0x00  mode      (an int; 1 means "discrete", else a curve)
//   ldr  d8, [x22, #0x8]     +0x08  timeScale (a DOUBLE; the guard is `> 0`)
//   ldp  s2, s3, [x22,#0x10] +0x10  attackMs, +0x14 decayMs (floats, in milliseconds)
//   ldp  s12, s1, [x22,#0x18]+0x18  fillValue, +0x1c peakValue (floats)
//
// +0x10 and +0x14 are a PAIR in the listing and are min/max'd together? No —
// they are not: +0x10 is the ramp width in ms and +0x14 is the decay in ms, and
// the two are used in different places (the ramp divides by +0x10/1000, the decay
// sets a Gaussian width). They are loaded together only because the compiler
// pairs the two adjacent floats.
//
// The peak value is read two ways — `ldr s0, [x22, #0x1c]` on the discrete path
// and `ldp s12, s1, [x22, #0x18]` on the curve path — and it is the SAME field.
// ---------------------------------------------------------------------------
struct RenderConfig {
  int mode = 0;             // +0x00
  double timeScale = 0.0;   // +0x08
  float attackMs = 0.0f;    // +0x10
  float decayMs = 0.0f;     // +0x14
  float fillValue = 0.0f;   // +0x18
  float peakValue = 0.0f;   // +0x1c
};

static_assert(offsetof(RenderConfig, mode) == 0x00, "`ldr w8, [x22]`");
static_assert(offsetof(RenderConfig, timeScale) == 0x08, "`ldr d8, [x22, #0x8]`");
static_assert(offsetof(RenderConfig, attackMs) == 0x10, "the low half of `ldp s2, s3, [x22, #0x10]`");
static_assert(offsetof(RenderConfig, decayMs) == 0x14, "the high half of the same pair");
static_assert(offsetof(RenderConfig, fillValue) == 0x18, "the low half of `ldp s12, s1, [x22, #0x18]`");
static_assert(offsetof(RenderConfig, peakValue) == 0x1c, "the high half of the same pair");
static_assert(sizeof(RenderConfig) == 0x20, "0x20 bytes: an int, a double, six more");

// ---------------------------------------------------------------------------
// BeatSnap's own onset detection. Each body below cites the listing it was read
// from; each is [partial] or [exact] as marked, and the mark says how far the
// recovery goes.
// ---------------------------------------------------------------------------

// [partial] The spectral-flux track for a block of samples: window -> FFT ->
// magnitudes -> the positive difference between consecutive magnitude frames,
// summed over a band of bins.
//
// 416 instructions, read in full from the bundle (row AE-03, 2026-10-28). The
// CONTROL FLOW and every constant are recovered; the recovered source compiles
// to a different instruction stream because the bundle's window loop and
// magnitude loop were vectorised by the compiler that built it and this
// repository's differ builds with different vectorisation settings. The
// arithmetic is pinned by the self-check.
FluxTrack computeFlux(const float* samples, size_t n, const OnsetConfig& config);

// [partial] The onset times of a flux track: local peaks that also clear their
// per-sample threshold, merged so that two onsets closer together than the
// scale's tolerance collapse into the first of them.
//
// 136 instructions, read in full. The recovered source compiles to a different
// instruction stream for the same reason as `computeFlux`.
std::vector<double> pickOnsets(const FluxTrack& track, const std::vector<float>& thresholds,
                               float scale);

// [exact] The whole detection chain: flux -> thresholds -> onsets. 47
// instructions, and the recovered source reproduces all of them.
std::vector<double> detectOnsets(const float* samples, size_t n, const OnsetConfig& config);

// [partial] Renders onset times into `count` values: either one impulse per
// onset (mode 1) or an attack/decay curve per onset.
//
// 190 instructions, read in full. The recovered source compiles to a different
// instruction stream for the same reason as `computeFlux`.
std::vector<float> renderValues(const std::vector<double>& times, int count,
                                 const RenderConfig& config);

}  // namespace beatsnap
