// lutblend.h — reconstructed from the compiled LUTBlend bundle.
//
// LUTBlend vendors a self-contained .cube LUT engine in its own `lutblend::`
// namespace, separate from the `spectra::` audio module the other effects share.
// Everything here is recovered from the demangled symbol table and disassembly of
// LUTBlend.plugin, not from source: the original C++ was destroyed on 2026-10-13
// with no remote and no backup.
//
// Signatures are [exact] — they come from symbol mangling. Types marked
// [recovered] had their layout pinned by the code that reads them.

#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace lutblend {

// ---------------------------------------------------------------------------
// [RECOVERED LAYOUT] CubeLUT — pinned by CubeLUT::valid(), which reads every
// field it has:
//
//     ldrb w9, [x0]            ; is3D
//     ldr  w8, [x0, #0x4]      ; size
//     ldp  x10, x9, [x0, #0x20]; data.begin, data.end
//     ldr  s0, [x0, #0x14] / ldr s1, [x0, #0x8]     ; max vs min, channel 0
//     ... the same at 0x18/0xc and 0x1c/0x10 for channels 1 and 2
//
// The min/max triples sit before data, each as three consecutive floats, and
// data is a std::vector<float> at 0x20.
// ---------------------------------------------------------------------------
struct CubeLUT {
  bool is3D = true;         // [recovered] offset 0  — false means a 1D LUT
  int size = 0;             // [recovered] offset 4  — entries per axis
  float minR = 0.0f;        // [recovered] offset 0x8
  float minG = 0.0f;        // [recovered] offset 0xc
  float minB = 0.0f;        // [recovered] offset 0x10
  float maxR = 1.0f;        // [recovered] offset 0x14
  float maxG = 1.0f;        // [recovered] offset 0x18
  float maxB = 1.0f;        // [recovered] offset 0x1c
  std::vector<float> data;  // [recovered] offset 0x20 — RGB triples

  // [exact] _ZNK8lutblend7CubeLUT5validEv
  bool valid() const;
};

// The default member initialisers above are RECOVERED, not chosen. parseCube
// builds into a default-constructed local CubeLUT, and the prologue writes the
// whole struct field by field (0xd70..0xd98):
//
//     mov  w8, #0x1 / sturb w8, [x29, #-0xc8]   ; is3D  = true
//     stur wzr,      [x29, #-0xc4]              ; size  = 0
//     stur xzr,      [x29, #-0xc0]              ; minR = minG = 0
//     stur wzr,      [x29, #-0xb8]              ; minB  = 0
//     fmov.2s v0, #1.0 / stur d0, [x29, #-0xb4] ; maxR = maxG = 1.0
//     mov  w8, #0x3f800000 / stur w8, [x29,#-0xac] ; maxB = 1.0
//     stp  xzr, xzr, [x29, #-0xa0] / stur xzr, [x29, #-0xa8] ; data empty
//
// Those offsets land exactly on the layout above, which is how we know the
// defaults are is3D = true, min = 0 and max = 1 rather than anything else.
// Earlier revisions of this header defaulted max to 0.0f, which is wrong: a
// default-constructed CubeLUT covers the [0,1] domain, and max = 0 would make
// every default-constructed LUT degenerate under valid().
static_assert(sizeof(CubeLUT) == 0x38, "CubeLUT must stay 56 bytes");
static_assert(offsetof(CubeLUT, data) == 0x20, "parseCube writes data at 0x20");

// ---------------------------------------------------------------------------
// [exact] Scalar helpers.
// ---------------------------------------------------------------------------

// Same idiom as spectra::clampf, which is a useful cross-check: two independently
// authored modules in this fleet produced the identical branchless form.
float clampf(float value, float lo, float hi);

// lerpf(a, b, t) = a + (b - a) * t. Note the argument order: the third argument
// is the mix factor, not an upper bound.
float lerpf(float a, float b, float t);

// BT.709 luma: 0.2126 R + 0.7152 G + 0.0722 B. The three immediates decode to
// exactly those coefficients and sum to 1.0.
float luma709(float r, float g, float b);

// ---------------------------------------------------------------------------
// Sample the LUT at a normalised input, writing three channels to out[0..2].
//
// [recovered] Handles both LUT shapes, selected by CubeLUT::is3D:
//   * 3D  -> trilinear interpolation across the eight surrounding lattice points
//   * 1D  -> three independent per-channel curves, each indexed by its own
//            coordinate rather than by one shared axis
// ---------------------------------------------------------------------------
void sampleLUT(const CubeLUT& lut, float r, float g, float b, float* out);

// ---------------------------------------------------------------------------
// [exact] Overwrite `lut` with the identity transform.
//
// Sets the range to [0,1] on all three axes and fills `data` with the identity
// ramp in .cube order — B outermost, G middle, R innermost, each triple stored
// (r, g, b) — so entry (b, g, r) is exactly (r, g, b) normalised by size - 1.
// That is a SECOND, independent confirmation of the .cube ordering already
// pinned by sampleLUT: this function writes the data and sampleLUT reads it, and
// they agree.
//
// The vector is cleared and then reserved to exactly 3 * size^3 floats (3 * size
// for a 1D LUT), so capacity is reused across calls rather than reallocated.
//
// NOTE: size == 1 divides by zero here (the normaliser is size - 1, which is 0),
// so every entry becomes NaN. CubeLUT::valid() rejects size < 2, so a caller that
// checks that first never reaches this. It is preserved rather than "fixed",
// because changing it would change behaviour the shipped plugin has.
// ---------------------------------------------------------------------------
void makeIdentity(CubeLUT* lut, int size, bool is3D);

// ---------------------------------------------------------------------------
// [exact] Sample both LUTs at one RGB point, blend them, then blend the result
// back against the original pixel.
//
//   mixed = lerp(sampleLUT(a, rgb), sampleLUT(b, rgb), clamp(blend, 0, 1))
//   out   = lerp(rgb, mixed, clamp(intensity, 0, 1))
//
// When `preserveLuma` is set, `mixed` is first rescaled so its Rec.709 luma
// matches the input's, which keeps a grade from changing overall brightness.
// The rescale is skipped when the mixed luma is <= 1e-5, which is what keeps it
// from dividing by zero on a black pixel.
//
// NOTE: the luma is computed with the same constants AND the same evaluation
// order as luma709() in this namespace, instruction for instruction, rather than
// by calling it. This implementation calls luma709() precisely because that is
// bit-identical — see the .cpp.
// ---------------------------------------------------------------------------
void applyLUTBlend(const CubeLUT& a, const CubeLUT& b, float blend, float intensity,
                   bool preserveLuma, const float* rgb, float* out);

}  // namespace lutblend
