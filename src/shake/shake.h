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

}  // namespace shake
