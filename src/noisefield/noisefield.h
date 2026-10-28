// noisefield.h — reconstructed from the compiled NoiseField bundle.
//
// Every declaration below is copied from the demangled Mach-O symbol table of
// NoiseField.plugin (bundle id com.williamsmedia.aftereffects.NoiseField), not from
// source: the original C++ was destroyed on 2026-10-13 with no remote and no backup.
//
// NAMESPACE-COMPLETE: every `noisefield::` symbol in the bundle is transcribed, so this
// header is a complete description of the namespace.
//
// `node tools/codegen-diff.mjs noisefield` reports 3 identical, 7 differing, 0 not
// implemented — every symbol is present, and the seven that are not instruction-
// identical are behavioural recoveries whose behaviour is measured against the shipped
// bundle (see src/noisefield/differential.cpp and src/noisefield/RECOVERY-NOTES.md).
//
// RECOVERED HERE, and how far:
//   * `clampf`, `clampi`, `fbmAmplitudeSum`  — INSTRUCTION-IDENTICAL to the bundle.
//   * `kGrad3`                                 — verified against the bundle's own bytes.
//   * `noise3`, `fbm3`                        — same instruction COUNT as the bundle
//                                                 (183 and 51), different register
//                                                 schedule; verified against independently
//                                                 written oracles.
//   * `PermTable::build`                      — 63 instructions here against the
//                                                 bundle's 109: clang expands the
//                                                 256-byte copy differently. Verified
//                                                 against a shuffle oracle.
//   * `fieldAt`, `curlField`                  — BEHAVIOURAL recoveries: 226 and 163
//                                                 instructions, the same counts as the
//                                                 bundle, but a different register
//                                                 schedule. The semantics are pinned by
//                                                 the listing and by a bit-for-bit
//                                                 differential against the shipped
//                                                 bundle; the instruction streams are
//                                                 not identical.
//   * `apply<unsigned char>`, `apply<unsigned short>` — BEHAVIOURAL recoveries: 363 and
//                                                 367 instructions here against the
//                                                 bundle's 408 and 412. The semantics are
//                                                 pinned by the listing and by a
//                                                 bit-for-bit differential against the
//                                                 shipped bundle (3,096,000 render
//                                                 vectors, 0 differing bytes); the
//                                                 instruction streams are not identical.
//
// No After Effects host has ever loaded a rebuilt bundle. Nothing here is a claim of
// host acceptance.
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
// The parameter block.
//
// [exact] Read out of `fieldAt`'s and `apply`'s instructions. The offsets are not
// guesses: each field is loaded at a fixed displacement and used in a way that names
// it. `+0x00` is compared against 2 and 3 to select a mode; `+0x04` is divided into
// the sample coordinate and floored at 0.001f; `+0x0c` is passed as the third noise
// coordinate; `+0x10` is compared against 1 to clamp an octave count; `+0x14` and
// `+0x18` are loaded as a PAIR (`ldp s3, s12, [x1, #0x14]`) and used as the frequency
// multiplier and the amplitude gain respectively; `+0x1c` seeds the permutation;
// `+0x20` and `+0x24` are loaded as a PAIR (`ldp w8, w26, [x21, #0x20]`) and select
// the displacement and edge behaviours.
struct Params {
  int mode;              // +0x00 — 0/1 curl, 2 advect, 3 gradient
  float scale;           // +0x04 — sample coordinate divisor, floored at 0.001f
  float amount;          // +0x08 — displacement scale; ZERO means "copy, do not displace"
  float evolution;       // +0x0c — the third noise coordinate
  int octaves;           // +0x10 — octave count, clamped to at least one
  float freqMult;        // +0x14 — frequency multiplier between octaves
  float gain;            // +0x18 — amplitude gain between octaves
  unsigned int seed;     // +0x1c — permutation seed
  int dispMode;          // +0x20 — 2 suppresses the Y displacement, 3 the X one
  int edgeMode;          // +0x24 — 0/1 clamp, 2 wrap, 3 "outside is transparent"
};

// ---------------------------------------------------------------------------
// A view of a raster.
//
// [exact] `data` at +0x00, `width` at +0x08, `height` at +0x0c, `stride` at +0x10.
//
// The stride is a 64-bit quantity and it is counted in ELEMENTS of T, not bytes:
// `madd x8, x9, x22, x8` multiplies it by the row index in 64-bit arithmetic, and the
// 16-bit instantiation scales it by two before adding it to the byte pointer.
template <class T>
struct View {
  T* data;
  int width;
  int height;
  long long stride;
};

// [exact] A two-component float, returned in s0/s1.
struct Vec2 {
  float x;
  float y;
};

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
 * effect's own tuning and not part of the textbook algorithm — easy to lose, and
 * the self-check pins it against a value taken from the vanilla algorithm.
 */
float noise3(const PermTable& table, float x, float y, float z);

/**
 * [exact] Fractal Brownian motion: `octaves` samples of `noise3`, each at a frequency
 * multiplied by `freqMult` and weighted by an amplitude multiplied by `ampGain`, then
 * divided by the amplitude sum.
 *
 * The octave count is clamped to at least one, exactly as `fbmAmplitudeSum` clamps it,
 * and the normalisation is that function's arithmetic — the compiler inlined it, so
 * this transcription calls it, which is bit-identical.
 *
 * THE ARGUMENT ORDER, which is the thing most easily got wrong: the SIXTH parameter
 * (the first float after the count) is the FREQUENCY multiplier and the SEVENTH is
 * the AMPLITUDE gain. That is settled by the two `fmul`s at the bottom of the loop —
 * `fmul s15, s9, s15` scales the frequency by the sixth argument and `fmul s14, s8,
 * s14` scales the amplitude by the seventh — and by the normalisation, which is built
 * from the SEVENTH. `fieldAt` and `curlField` corroborate it independently: they
 * inline the same loop and multiply the frequency by `Params::freqMult` (+0x14) and
 * the amplitude by `Params::gain` (+0x18).
 */
float fbm3(const PermTable& table, float x, float y, float z, int octaves, float freqMult,
           float ampGain);

// ---------------------------------------------------------------------------
// The field.
// ---------------------------------------------------------------------------

/**
 * [behavioural] The displacement field at a point, in PIXELS of displacement.
 *
 * Read off `noisefield::fieldAt` (226 instructions, arm64 0x0efc..0x1280 in
 * `/tmp/ae/noisefield.asm`):
 *
 *   scale = fmax(params.scale, 0.001f)      // 0x3a83126f, never divides by zero
 *   px = x / scale;  py = y / scale
 *   z  = params.evolution
 *
 *   mode 3 (gradient): four fBm samples at +/-0.01f around the point, each NORMALISED
 *     on its own, and the two differences divided by 0.02f:
 *       a = fbm(px+0.01, py, z); b = fbm(px-0.01, py, z)
 *       c = fbm(px, py+0.01, z); d = fbm(px, py-0.01, z)
 *       return { (a - b) / 0.02f, (c - d) / 0.02f }
 *     BOTH components divide by +0.02f (0x3ca3d70a) — the CURL, which divides its Y
 *     component by -0.02f, does not.
 *
 *   mode 2 (advect): FOUR steps along the curl field, not five:
 *       repeat 4: c = curlField(table, ax, ay, z, params)
 *                 ax = c.x * 0.35f + ax;  ay = c.y * 0.35f + ay
 *       return { ax - px, ay - py }
 *     The step is 0.35f (0x3eb33333) and there are exactly four calls to `curlField`
 *     in the listing (0xf6c, 0xf98, 0xfb8, 0xfd8).
 *
 *   modes 0 and 1: `curlField(table, px, py, z, params)` directly.
 */
Vec2 fieldAt(const PermTable& table, float x, float y, const Params& params);

// ---------------------------------------------------------------------------
// The renderer.
// ---------------------------------------------------------------------------

/**
 * [behavioural] Displaces `src` into `dst`, one pixel at a time.
 *
 * Read off `noisefield::apply<unsigned char>` (408 instructions, arm64 0x06e8..0x0d48)
 * and `noisefield::apply<unsigned short>` (412 instructions, arm64 0x1284..0x18f4) in
 * `/tmp/ae/noisefield.asm`; the two differ only in the width of the pixel.
 *
 *   * A pixel is FOUR channels (the load and store offsets run 0..3 for 8-bit and
 *     0,2,4,6 for 16-bit), and the channel values are read UNSIGNED (`ldrb`/`ldrh`
 *     followed by `ucvtf`).
 *
 *   * `src.width < 1 || src.height < 1` returns without touching `dst`.
 *
 *   * `params.amount == 0.0f` takes a COPY path: per row, a `memmove` of
 *     `width * 4 * sizeof(T)` bytes from `src.data + y*src.stride` to
 *     `dst.data + y*dst.stride`. The fourth argument (`time`) is DEAD — the
 *     instruction stream overwrites `s0` with `params.amount` before it is read.
 *
 *   * Otherwise the permutation is built from `params.seed` and every pixel is
 *     displaced:
 *         (fx, fy) = fieldAt(table, (float)x, (float)y, params)
 *         dx = fx * params.amount;   dy = fy * params.amount
 *         if (params.dispMode == 2) dy = 0;      // suppress the Y displacement
 *         if (params.dispMode == 3) dx = 0;      // suppress the X displacement
 *         sx = (float)x + dx;   sy = (float)y + dy
 *     and `sx`/`sy` are forced to ZERO when the corresponding extent is below 2 —
 *     a special case the listing makes explicit (`movi.2d v10, #0` for a width of 1).
 *
 *   * The edge behaviour, selected by `params.edgeMode`:
 *         0, 1  clamp to [0, extent-1]
 *         2     wrap:  f = fmodf(v, extent); if (f < 0) f += extent; then clamp to
 *               [0, extent-1] — so the wrap is followed by the SAME upper clamp
 *         3     no clamp: a sample outside [0, extent-1] is "outside" and the pixel
 *               is written as four zero bytes (8 for 16-bit) instead
 *     The X and Y tests are symmetric: both reject a coordinate that is below zero
 *     as well as one above the top.
 *
 *   * The sample is taken bilinearly between the four neighbours
 *     `i0 = clampi(floor(sx), 0, width-1)`, `i1 = i0+1` (wrapped to 0 in edge mode 2,
 *     otherwise clamped to width-1) and likewise for `j0`/`j1`, with
 *         top = fma(fxx, p01 - p00, p00)
 *         bot = fma(fxx, p11 - p10, p10)
 *         out = fma(fyy, bot - top, top)
 *
 *   * The result is converted with `fcvtas` — round to nearest, ties away from zero,
 *     saturating to a 64-bit integer — and the LOW byte (or halfword) is stored, so
 *     the STORE wraps rather than clamps: the instruction sequence would turn 300
 *     into 44 in 8-bit and 70000 into 4464 in 16-bit. That wrap is NOT reachable
 *     through `apply` itself, because the sample is a convex combination of the
 *     source values and so never leaves the source's range; it is a property of
 *     the conversion, not an observable behaviour of the renderer.
 */
template <class T>
void apply(const View<const T>& src, View<T>& dst, const Params& params, float time);

// The two instantiations the bundle ships. Both are named explicitly, because the
// mangled names are the only thing that ties them to the bundle's symbols.
extern template void apply<unsigned char>(const View<const unsigned char>&,
                                           View<unsigned char>&, const Params&, float);
extern template void apply<unsigned short>(const View<const unsigned short>&,
                                            View<unsigned short>&, const Params&, float);

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
