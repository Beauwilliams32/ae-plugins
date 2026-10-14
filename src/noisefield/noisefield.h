// noisefield.h — reconstructed from the compiled NoiseField bundle.
//
// Every declaration below is copied from the demangled Mach-O symbol table of
// NoiseField.plugin (bundle id com.williamsmedia.aftereffects.NoiseField), not from
// source: the original C++ was destroyed on 2026-10-13 with no remote and no backup.
//
// INCOMPLETE — three functions and the gradient table. What is here is the part that
// needs nothing else: two fleet idioms, the fractal-amplitude normalisation, and the
// gradient set the noise is built from. The noise itself is not transcribed yet, and
// inventing it would be worse than leaving it out.
//
// RECOVERED HERE: `clampf`, `clampi`, `fbmAmplitudeSum`, and `kGrad3`.
//
// NOT RECOVERED:
//   PermTable::build(unsigned)         (109) — shuffles the permutation
//   noise3(const PermTable&, float, float, float)  (183) — the gradient noise
//   fbm3(..., int, float, float)        (51) — the octave sum over noise3
//   fieldAt(const PermTable&, float, float, const Params&) (226)
//   curlField(...)                     (163)
//   apply<unsigned char>               (408) — the per-pixel loops
//   apply<unsigned short>              (412)
//
// `kGrad3` IS THE NOISE'S DEFINITION, which is why it is recovered before the code
// that consumes it: `noise3` hashing into a different gradient set would be a
// different effect, and the table is what pins it.

#pragma once
#include <cstddef>

namespace noisefield {

// ---------------------------------------------------------------------------
// Shared with the rest of the fleet.
// ---------------------------------------------------------------------------

// [exact] Three-way clamp, floats. The LOW bound is tested last, so an inverted range
// yields `lo` — the fleet-wide idiom, one of fifteen copies.
float clampf(float value, float lo, float hi);

// [exact] Three-way clamp, ints. Same structure, `csel` rather than `fcsel`, and the
// low bound tested last for the same reason.
int clampi(int value, int lo, int hi);

// ---------------------------------------------------------------------------
// Fractal Brownian motion.
// ---------------------------------------------------------------------------

// [exact] The sum the octave amplitudes are divided by:
//
//   n   = octaves > 1 ? octaves : 1        (the count is clamped to at least one)
//   sum = 0; amp = 1
//   repeat n: sum += amp; amp *= gain
//   return sum > 1e-6f ? sum : 1.0f
//
// So it is `1 + gain + gain^2 + ...`, with 1e-6f (0x358637bd) as a divide-by-zero
// guard. Note that the guard is reachable and the clamp is not: the first term is
// always 1, so the sum can only fall to zero from a NEGATIVE gain, and a
// non-positive octave count produces the same sum as one octave once the guard is
// applied — see RECOVERY-NOTES.md.
float fbmAmplitudeSum(int octaves, float gain);

// ---------------------------------------------------------------------------
// The gradient set.
// ---------------------------------------------------------------------------

/**
 * [exact] The twelve edge gradients of Perlin's improved noise, in the canonical
 * order, stored as integer triples:
 *
 *   (1,1,0) (-1,1,0) (1,-1,0) (-1,-1,0) (1,0,1) (-1,0,1)
 *   (1,0,-1) (-1,0,-1) (0,1,1) (0,-1,1) (0,1,-1) (0,-1,-1)
 *
 * Each entry's squared length is 2, and the set is exactly the twelve edge midpoints
 * of a cube. This was read out of the bundle's `__TEXT,__const` as 36 32-bit words
 * and matches the canonical table as a set AND in order.
 *
 * LINKAGE DEVIATION, on purpose: in the shipped bundle this symbol is `s` — local,
 * inside an anonymous namespace — so nothing outside the translation unit can see it.
 * It is declared here so the self-check can assert its contents, which is the only
 * way a table can be verified rather than trusted. The deviation is real and is
 * recorded rather than glossed: a caller linking against this header sees a symbol
 * the original did not export.
 */
extern const int kGrad3[12][3];

}  // namespace noisefield
