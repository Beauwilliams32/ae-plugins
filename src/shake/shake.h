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

}  // namespace shake
