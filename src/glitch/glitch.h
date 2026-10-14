// glitch.h — recovered from the compiled GlitchKit bundle.
//
// GlitchKit is a deterministic, seeded glitch effect: five modes (Block Displace,
// RGB Channel Shift, Scanline Jitter, Bad Signal, Datamosh Smear) sharing one
// PRNG, so the same seed and frame always produce the same image.
//
// Recovered from the demangled symbol table and disassembly of GlitchKit.plugin.
// The original C++ was destroyed on 2026-10-13 with no remote and no backup.
//
// SCOPE. The four scalar helpers are recovered in full. The bundle's render engine is
//
//     void glitch::apply<T>(const View<const T>& in, View<T>& out,
//                           const Params&, unsigned seed, float time)
//
// instantiated for T = unsigned char and T = unsigned short — 1157 and 1228
// instructions respectively — and its five mode BODIES are NOT recovered. What IS
// recovered of it is the part that can be read without transcribing them: the two types
// it takes, the mode field, and the shape of the dispatch. See below.
//
// The same View<T> + apply<T> shape appears in NoiseField (noisefield::apply<T>)
// and ChromaKeyAssist (chromakey::renderView<T>), so it is a fleet-wide idiom for
// satisfying DEEP_COLOR_AWARE rather than a GlitchKit peculiarity.

#pragma once

namespace glitch {

// ---------------------------------------------------------------------------
// [RECOVERED LAYOUT] View<T> — { T* data; int width; int height; long rowBytes; },
// 24 bytes.
//
//     ldr    x16, [x0]        ; +0x00  data
//     ldp    w15, w16, [x0, #0x8]   ; +0x08 width, +0x0c height, read as a PAIR
//     ldrsw  x15, [x0, #0x8]  ; and separately sign-extended, so both are `int`
//     ldrsw  x21, [x0, #0xc]
//     ldr    x22, [x0, #0x10] ; +0x10  rowBytes, 64-bit
//
// Both instantiations walk the input view at all four offsets, which is what makes this a
// layout rather than a guess: the 8-bit and 16-bit builds read `[view]`, `[view,#0x8]`,
// `[view,#0xc]` and `[view,#0x10]` identically and differ only in the channel width the
// data pointer is interpreted with.
//
// THE TWO INSTANTIATIONS DO NOT READ THE SAME SUBSET, and the layout is pinned by the
// 8-bit one. `apply<unsigned char>` walks the input at 0x0, 0x8, 0xc and 0x10 and the
// output at 0x0 and 0x10; `apply<unsigned short>` reads the input at only 0x0 and 0x8 and
// the output at only 0x0. Same source, same struct — the access sets differ because the
// two builds schedule the loops differently, which is why test/glitch.test.mjs asserts
// that neither view is touched OUTSIDE the layout rather than that both read all of it.
//
// What is a recovered BEHAVIOUR and not an artefact: the output view's WIDTH AND HEIGHT
// ARE NEVER READ, by either build. The loops are driven entirely by the input's
// dimensions and the output is assumed to match, so a caller handing over a smaller
// output view gets writes at the input's stride rather than a clamp or an error.
//
// The pause at +0x04 is real — width is at +8, not +4 — so the pointer is followed by
// four bytes of padding before the two ints.
// ---------------------------------------------------------------------------
template <typename T>
struct View {
  T* data = nullptr;      // +0x00
  int width = 0;          // +0x08
  int height = 0;         // +0x0c
  long rowBytes = 0;      // +0x10
};

static_assert(sizeof(View<unsigned char>) == 24,
              "View<T> is a pointer, two ints and a long, with padding after the pointer");

// ---------------------------------------------------------------------------
// [PARTLY RECOVERED] Params — the six fields `apply<T>` reads, and only those.
//
//     +0x00  int    mode          compared against 1, 2, 3, 4 and 5
//     +0x04  float  (clamped 0..1) and an early return when it is <= 0
//     +0x0c  int                   one of the hashKey inputs
//     +0x10  float                 multiplies the seed, guarded by `> 0`
//     +0x1c  BYTE                  read with ldrb, so a flag rather than a float
//     +0x20  float  (clamped 0..1) and an early return when it is <= 0
//
// THE OFFSETS ARE ESTABLISHED FROM THE PROLOGUE ALONE, and that is a deliberate limit.
// `apply<T>` overwrites x2 with an OUTPUT CURSOR once the mode dispatch is taken, so
// every `[x2, #N]` after that point is reading pixels, not parameters. The set above is
// the one both instantiations read BEFORE the reuse, in the same order on the same
// offsets. What sits at +0x08, +0x14 and +0x18 is therefore NOT claimed — those offsets
// appear in the disassembly, but past the point where x2 stops being `&Params`.
//
// The field NAMES beyond `mode` are not in the binary. They are described by what the
// prologue does with them, which is why two of them say nothing more than "clamped 0..1".
//
// NOT DEFINED HERE, deliberately. `apply<T>` takes this by const reference, so the
// offsets are usable without a definition — and writing one would mean inventing field
// names and a total size for a struct whose remaining bytes have not been read.
// ---------------------------------------------------------------------------
struct Params;

// ---------------------------------------------------------------------------
// [RECOVERED] The mode dispatch — five bodies, one-based, matching the popup.
//
//     cmp w6, #0x2 / b.le 0xab8    ; mode <= 2
//     cmp w6, #0x3 / b.eq 0xc7c
//     cmp w6, #0x4 / b.eq 0xeb8
//     cmp w6, #0x5 / b.ne 0x1784
//     ...
//     0xab8: cmp w6, #0x1 / b.eq 0x1248
//            cmp w6, #0x2 / b.ne 0x1784
//
// so the mode is ONE-BASED with five values and a catch-all:
//
//     1 -> 0x1248    2 -> 0xac8     3 -> 0xc7c    4 -> 0xeb8    5 -> 0x888
//     anything else (including 0) -> 0x1784
//
// That matches the recovered popup, whose five choices are "Block Displace|RGB Channel
// Shift|Scanline Jitter|Bad Signal|Datamosh Smear" — a `PF_ADD_POPUP` is one-based, so
// choice N is value N and no remapping is needed. It is worth stating because the
// ALTERNATIVE is so easy to assume: a zero-based enum here would make every mode off by
// one, and the popup's own text is what rules it out.
//
// WHAT THIS DOES NOT ESTABLISH: which body is which mode. The addresses above are
// ordered 1 -> 0x1248, 2 -> 0xac8, 3 -> 0xc7c, 4 -> 0xeb8, 5 -> 0x888, which is NOT
// address order, so the bodies cannot be matched to the popup names by position. Until
// the bodies are read, `plugins/GlitchKit/GlitchRender.cpp`'s mapping from name to
// behaviour is new code consistent with the popup's ORDER and unverified against the
// engine. That is recorded rather than glossed, because it is the one place in the two
// built plugins where a shipped behaviour rests on an ordering assumption.
// ---------------------------------------------------------------------------

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
