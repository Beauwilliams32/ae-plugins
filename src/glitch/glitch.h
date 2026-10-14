// glitch.h — recovered from the compiled GlitchKit bundle.
//
// GlitchKit is a deterministic, seeded glitch effect: five modes (Block Displace,
// RGB Channel Shift, Scanline Jitter, Bad Signal, Datamosh Smear) sharing one
// PRNG, so the same seed and frame always produce the same image.
//
// Recovered from the demangled symbol table and disassembly of GlitchKit.plugin.
// The original C++ was destroyed on 2026-10-13 with no remote and no backup.
//
// SCOPE. This header covers the four scalar helpers that ARE recovered. The
// bundle's actual render engine is
//
//     void glitch::apply<T>(const View<const T>& in, View<T>& out,
//                           const Params&, unsigned seed, float time)
//
// instantiated for T = unsigned char and T = unsigned short — 1157 and 1228
// instructions respectively — plus the `View<T>` and `Params` types it takes.
// Those are NOT recovered. They are the substance of the effect; the four
// functions below are what the engine builds its patterns from.
//
// The same View<T> + apply<T> shape appears in NoiseField (noisefield::apply<T>)
// and ChromaKeyAssist (chromakey::renderView<T>), so it is a fleet-wide idiom for
// satisfying DEEP_COLOR_AWARE rather than a GlitchKit peculiarity.

#pragma once

namespace glitch {

// ---------------------------------------------------------------------------
// [exact] Scalar clamps.
//
// clampf is the FOURTH identical copy of this idiom in the fleet, after
// spectra::, lutblend:: and typeriser::. clampi is its integer twin, with the
// same branchless `csel` shape. Four namespaces, four bundles, one form.
// ---------------------------------------------------------------------------
float clampf(float value, float lo, float hi);
int clampi(int value, int lo, int hi);

// ---------------------------------------------------------------------------
// [exact] The PRNG. Mixes three 32-bit inputs into one.
//
// Two stages of the same pattern: an XOR-shift-and-multiply avalanche fed by a
// strength-reduced multiply (`madd` plus a shifted add, which is how the compiler
// emitted the original without a multiply-looking shape). Constants recovered
// exactly: 0x85ebca6b, 0x7af29ac0, 0x9e3779b9, 0xc2b2ae35, 0xacab8d40,
// 0x27d4eb2f, 0x7feb352d, 0x846ca68b.
//
// The final `csinc w0, w10, wzr, ne` after `cmp w9, w8` is a conditional
// increment, i.e. the function returns 1 rather than the mixed value when two
// intermediate stages happen to agree. That is transcribed as written; what it
// was FOR is not recoverable, and it is not guessed at here.
// ---------------------------------------------------------------------------
unsigned int hashKey(unsigned int a, unsigned int b, unsigned int c);

// ---------------------------------------------------------------------------
// [exact] Which pattern frame a given time falls in.
//
//     d1 = (double)seed * (double)time      ; ucvtf / fcvt / fmul, in DOUBLE
//     w8 = (unsigned)trunc(d1)              ; fcvtzu
//     return (time > 0.0f) ? w8 : 0         ; fcmp + csel w0, w8, wzr, hi
//
// Note the arithmetic is done in double and only the result is truncated, and
// that the guard is a strict `> 0` — time exactly 0 gives 0, not the seed.
// ---------------------------------------------------------------------------
unsigned int patternFrame(unsigned int seed, float time);

}  // namespace glitch
