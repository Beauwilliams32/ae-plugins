// noisefield.h — reconstructed from the compiled NoiseField bundle.
//
// Every declaration below is copied from the demangled Mach-O symbol table of
// NoiseField.plugin (bundle id com.williamsmedia.aftereffects.NoiseField), not from
// source: the original C++ was destroyed on 2026-10-13 with no remote and no backup.
//
// INCOMPLETE — the noise core is here and the effect's own layers are not.
//
// RECOVERED HERE: `clampf`, `clampi`, `fbmAmplitudeSum`, `kGrad3`, `PermTable::build`,
// `noise3`, `fbm3`.
//
// NOT RECOVERED:
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
// The permutation, and the noise built on it.
// ---------------------------------------------------------------------------

/**
 * [inferred] A permutation of 0..255, written TWICE: 256 bytes then the same 256
 * again.
 *
 * The duplication is not padding. `noise3` indexes the table with sums of three
 * bytes, so an index can reach 255 + 255 = 510, and the second copy is what makes
 * that legal without a modulo. The layout is pinned by `build`, which stores its
 * 256-byte working copy at `+0x000` AND at `+0x100`, and by `noise3`, which reads
 * `p[AA]` and `p[AA + 1]` for the two z-slices — the second of those is
 * `p[(p[A] + Z) + 1]`, an index that only stays in range because of the copy.
 */
struct PermTable {
  unsigned char p[512];

  /**
   * [exact] Fills the table for a seed: a byte ramp 0..255, shuffled by
   * Fisher-Yates driven by xorshift32.
   *
   *   tmp[256] = {0, 1, 2, ... 255}          (copied from __TEXT,__const: a literal
   *                                           byte ramp, which is where the identity
   *                                           permutation comes from)
   *   if (seed == 0) seed = 0x9e3779b9       (the golden-ratio constant, so seed 0 is
   *                                           still a usable seed rather than a
   *                                           degenerate one)
   *   for (i = 255; i > 0; --i) {
   *     seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
   *     j = seed % (i + 1);                  (compiled as udiv + msub)
   *     swap(tmp[i], tmp[j]);
   *   }
   *   for (i = 0; i < 512; ++i) p[i] = tmp[i & 255];
   *
   * Note which way the shuffle runs and that the seed advances ONCE PER ITERATION,
   * not once per table: the sequence is deterministic in the seed and independent of
   * anything else, which is what makes the same seed give the same noise field.
   */
  void build(unsigned int seed);
};

static_assert(sizeof(PermTable) == 512, "the permutation is stored twice");

/**
 * [exact] Perlin's improved gradient noise at (x, y, z), in roughly [-0.9, 0.9].
 *
 * The structure is the canonical one and every part of it is read off the
 * instructions: the integer cell is `floor` and masked to 0xff; the fractions are
 * `x - floor(x)`; the fade is the quintic `t*t*t*(t*(t*6 - 15) + 10)`; the eight
 * corner hashes are `p[AA]`, `p[BA]`, `p[AB]`, `p[BB]` and the same four `+ 1` for
 * the upper z-slice; each hash selects one of the twelve edge gradients with
 * `h % 12` (compiled as `(h * 0xab) >> 11`); and the corners are combined by
 * trilinear interpolation in x, then y, then z.
 *
 * The last instruction scales the result by **0.9f** (0x3f666666). That is the
 * effect's own tuning and not part of the textbook algorithm — easy to lose, and the
 * self-check pins it against a value taken from the vanilla algorithm.
 */
float noise3(const PermTable& table, float x, float y, float z);

/**
 * [exact] Fractal Brownian motion: `octaves` samples of `noise3`, each at a frequency
 * multiplied by `lacunarity` and weighted by an amplitude multiplied by `gain`, then
 * divided by the amplitude sum.
 *
 * The octave count is clamped to at least one, exactly as `fbmAmplitudeSum` clamps it,
 * and the normalisation is that function's arithmetic — the compiler inlined it, so
 * this transcription calls it, which is bit-identical. Note the argument split: the
 * SIXTH parameter (the first float after the count) is the frequency multiplier and
 * the SEVENTH is the amplitude gain, which is the opposite of what the names might
 * suggest if read in the other order. Both are recovered from the two `fmul`s at the
 * bottom of the loop.
 */
float fbm3(const PermTable& table, float x, float y, float z, int octaves, float gain,
           float lacunarity);

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
