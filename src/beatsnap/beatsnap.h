// beatsnap.h — reconstructed from the compiled BeatSnap bundle.
//
// Every declaration below is copied from the demangled Mach-O symbol table of
// BeatSnap.plugin (bundle id com.williamsmedia.aftereffects.BeatSnap, version
// 1.0.0), not from source: the original C++ was destroyed on 2026-10-13 with no
// remote and no backup.
//
// INCOMPLETE — 7 of this bundle's 11 authored functions are here, and the four that
// are outstanding are named by `tools/codegen-diff.mjs beatsnap`. (The header
// deliberately does not spell out the completeness marker token: saying "not <token>"
// contains it, and the guard in test/codegen.test.mjs reads it as a literal, so the
// negative phrasing claimed completeness by accident.) test/beatsnap.test.mjs asserts
// that the recovered set, the outstanding set and the differ's tally all agree.
//
// THE FIVE RECOVERED HERE ARE COPIES, AND THAT IS THE POINT. BeatSnap's
// `hannWindow`, `fftRadix2`, `downmixToMono`, `isPowerOfTwo` and `clampf` are
// machine-code-identical to the `spectra::` ones — `tools/dsp-duplication.mjs`
// compares every copy across every installed bundle and reports no drift — so the
// transcription is shared and only the namespace differs. That is a property of
// the shipped fleet, not a shortcut: the same five functions were compiled into
// several effects, which is why one recovered implementation serves them all.
//
// SIGNATURE FIDELITY: all five are [exact] — taken verbatim from symbol mangling.
// The structs BeatSnap's remaining functions take (`OnsetConfig`, `FluxTrack`,
// `RenderConfig`, `PermTable`-style state) have NOT been recovered, so nothing
// here pretends to them.

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
//
// `OnsetConfig`'s layout is only partly recovered: the three fields these two
// read are established from the call sites in `detectOnsets`, and nothing else is
// claimed. They are named for their measured effect, not for a name that survives
// nowhere in the binary.
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

}  // namespace beatsnap
