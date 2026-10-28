// shake.h — reconstructed from the compiled ShakeCapture bundle.
//
// Every declaration below is copied from the demangled Mach-O symbol table of
// ShakeCapture.plugin, not from source: the original C++ was destroyed on 2026-10-13 with
// no remote and no backup.
//
// INCOMPLETE — the statistics helpers are here and the tracker is not.
//   recovered: clampf (shared), median
//   outstanding: movingAverage (155), highPass (105), accumulate (135),
//              normalizeUnitAmplitude (125), sampleLooped (92), resampleProfile (50),
//              extractJitter (36), ShakeProfile::~ShakeProfile (16),
//              trackTranslation (960)
//
// `trackTranslation` is the largest function in this bundle and the effect's whole point:
// a 960-instruction block-matching search over frames. Everything around it takes
// `ShakeProfile` or `Vec2`, whose layouts are not recovered yet, which is why only the two
// functions that need no such type are here.

#pragma once
#include <cstddef>
#include <vector>

namespace shake {

// ---------------------------------------------------------------------------
// [exact] Shared with the fleet — the fleet's `clampf`, one of five verified-identical
// carriers.
// ---------------------------------------------------------------------------

float clampf(float value, float lo, float hi);

// ---------------------------------------------------------------------------
// [exact] Statistics.
// ---------------------------------------------------------------------------

/**
 * [exact] The median of `values`, taken BY VALUE so it can reorder its own copy with
 * `std::nth_element` — partial selection, not a full sort.
 *
 *     empty          -> 0.0f
 *     odd count      -> data[count / 2]
 *     even count     -> (data[count / 2 - 1] + data[count / 2]) * 0.5f
 *
 * The even case AVERAGES the two middle values, which is the opposite of what
 * `beatsnap::medianOfWindow` does with an even window — that one returns the upper middle
 * and says so. Two functions with the same purpose in the same fleet, disagreeing on the
 * even case, is worth knowing before replacing one with the other.
 */
float median(std::vector<float> values);

// ---------------------------------------------------------------------------
// [exact] The filter chain, recovered 2026-10-27 (row AE-05).
// ---------------------------------------------------------------------------

/**
 * [exact] A centred moving average, with the window CLAMPED at the ends and each output
 * divided by the number of samples that actually landed in its window.
 *
 *     half   = (window > 1 ? window : 1) / 2       ; a window of 1 or less yields half = 0
 *     begin  = max(i - half, 0)
 *     end    = min(i + half, n - 1)
 *     out[i] = sum<double>(v[begin..end]) / (end - begin + 1)
 *
 * The sum is accumulated in DOUBLE (the shipped code widens every sample with `fcvtl`), and the
 * output length always equals the input's, including for an empty input. The divisor is the real
 * count, not `window`, which is why the ends are not darker.
 */
std::vector<float> movingAverage(const std::vector<float>& values, int window);

/**
 * [exact] The residual after removing `movingAverage(values, window)` sample by sample:
 *
 *     smooth = movingAverage(values, window)
 *     out[i] = values[i] - smooth[i]
 *
 * The output vector is allocated and zero-filled first (an explicit `bzero` in the listing), so a
 * short smooth result cannot leave uninitialised output behind.
 */
std::vector<float> highPass(const std::vector<float>& values, int window);

/**
 * [inferred] The whole shake track: two parallel float series.
 *
 * The LAYOUT is recovered, the field names are not. `~ShakeProfile` frees a buffer at +0x0 (after
 * writing its end pointer to +0x8) and another at +0x18 (end at +0x20), i.e. two `std::vector<float>`
 * members 24 bytes apart — and `extractJitter` calls `highPass` on `profile + 0x00` and on
 * `profile + 0x18`, which is what those two members are for.
 */
struct ShakeProfile {
  std::vector<float> horizontal;   // offset 0x00
  std::vector<float> vertical;     // offset 0x18
};

static_assert(sizeof(ShakeProfile) == 48, "two std::vector<float> members, 0x00 and 0x18");

/**
 * [exact] The jitter of a track: the high-passed horizontal series and the high-passed vertical
 * one, in that order.
 *
 *     return { highPass(profile.horizontal, window), highPass(profile.vertical, window) };
 *
 * That is the whole function (36 instructions, most of them the two result stores), and it returns a
 * `ShakeProfile` by value through the sret slot.
 */
ShakeProfile extractJitter(const ShakeProfile& profile, int window);

// ---------------------------------------------------------------------------
// [inferred] The track point. Only the name survives; the layout is pinned by every
// function that walks a `vector<Vec2>`: the vectors are counted with `asr x8, x8, #3`, so a
// Vec2 is 8 bytes, and `accumulate` reads `+0x0` and `+0x4` as the two channels.
// ---------------------------------------------------------------------------
struct Vec2 {
  float x;
  float y;
};

static_assert(sizeof(Vec2) == 8, "a vector<Vec2> is counted by dividing its byte size by 8");

/**
 * [exact] The camera path: cumulative sums of the per-frame deltas, WITH a leading zero.
 *
 *     out.horizontal.resize(count + 1, 0);  out.vertical.resize(count + 1, 0);
 *     for i in 0..count-1:
 *         out.horizontal[i + 1] = out.horizontal[i] + path[i].x
 *         out.vertical[i + 1]   = out.vertical[i]   + path[i].y
 *
 * So an N-frame track yields N+1 samples and `out[0]` is the origin, which is why the two
 * vectors are one longer than the input. Both resizes are visible in the listing as calls to
 * `vector<float>::resize(n + 1, 0.0f)`.
 */
ShakeProfile accumulate(const std::vector<Vec2>& path);

/**
 * [exact] Scale a profile in place so its LARGEST absolute sample — across both channels — is
 * exactly 1.0.
 *
 *     peak = max(|horizontal[i]|, |vertical[j]|) over every sample of both channels
 *     if (peak <= 1e-12f) return;                 // 0x2b8cbccc, i.e. "nothing to normalise"
 *     scale = 1.0f / peak;
 *     every sample of both channels *= scale
 *
 * The guard matters: an all-zero profile (a perfectly still camera) would otherwise divide by
 * zero and fill the track with NaNs. The threshold is 1e-12, not 1e-5 — the immediate decodes
 * to 9.99999996e-13.
 */
void normalizeUnitAmplitude(ShakeProfile& profile);

/**
 * [inferred names, recovered values] How a sample index that runs off either end of a series is
 * handled. The VALUES are read off two `cmp`/`b` chains — `sampleLooped` tests 3 then 2 then 1 and
 * falls through to 0, and 0 returns 0.0 without reading the series at all.
 */
enum class LoopMode : int {
  None = 0,      ///< no sampling; the function returns 0.0f
  Clamp = 1,     ///< the end samples, but only AT the endpoints: outside the range, 0.0f
  Loop = 2,      ///< wrap with fmod, so the series repeats
  PingPong = 3,  ///< reflect: fmod over 2*(n-1), then mirror the second half
};

/**
 * [exact] One sample of `values` at fractional position `t`, by LINEAR interpolation between the two
 * neighbouring samples, with the out-of-range behaviour chosen by `mode`.
 *
 * Recovered edge cases, each of which is a `cmp` in the listing rather than an inference:
 *   * an empty series returns 0.0f;
 *   * a ONE-sample series returns values[0] for t == 0 (and for NaN), returns **0.0f** in Clamp mode
 *     for any other t, and values[0] otherwise — the 0.0f is a quirk of the shipped code, kept;
 *   * Clamp: t < 0 -> 0.0f (not values[0]) and t > last -> 0.0f as well (not values[last]);
 *     the end samples are only returned AT the endpoints;
 *   * Loop: fmod(t, n) with the negative result brought back into range, then the index wraps;
 *   * PingPong: fmod(t, 2*(n-1)), then `period - w` for the mirrored half, then the same
 *     truncate-and-lerp as Clamp;
 *   * in every interpolating path, `t <= 0` returns values[0] and `t >= last` returns values[last]
 *     before any index arithmetic happens.
 */
float sampleLooped(const std::vector<float>& values, double t, LoopMode mode);

/**
 * [exact] `count` samples of `values` taken `step` apart: `out[i] = sampleLooped(values, i*step, mode)`.
 *
 * The output is resized to `count` and ZERO-FILLED first, so a `count` below 1 gives an empty
 * vector, and a non-positive `step` or an empty source returns the zero-filled vector without
 * sampling anything.
 */
std::vector<float> resampleProfile(const std::vector<float>& values, double step, int count, LoopMode mode);

}  // namespace shake
