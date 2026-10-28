// glitch.h — recovered from the compiled GlitchKit bundle.
//
// GlitchKit is a deterministic, seeded glitch effect: five modes (Block Displace,
// RGB Channel Shift, Scanline Jitter, Bad Signal, Datamosh Smear) sharing one
// PRNG, so the same seed and frame always produce the same image.
//
// Recovered from the demangled symbol table and disassembly of GlitchKit.plugin.
// The original C++ was destroyed on 2026-10-13 with no remote and no backup.
//
// STATE — FIVE of the SIX `glitch::` symbols are recovered. ONE is outstanding.
//
//   glitch::clampf                  5 instructions    INSTRUCTION-IDENTICAL
//   glitch::clampi                  5 instructions    INSTRUCTION-IDENTICAL
//   glitch::hashKey                34 instructions    INSTRUCTION-IDENTICAL
//   glitch::patternFrame            7 instructions    INSTRUCTION-IDENTICAL
//   glitch::apply<unsigned char>  1157 instructions    BEHAVIOURAL RECOVERY
//
// OUTSTANDING: `glitch::apply<unsigned short>` — 1228 instructions in the shipped
// binary, listed in /tmp/ae/glitch_us.txt. It is NOT recovered at this commit: the
// symbol is not instantiated, so the differ reports it as not implemented and this
// namespace is NOT complete.
//
// The four scalar helpers reproduce the bundle's instructions EXACTLY — 5, 5, 34 and 7
// instructions, all four identical. The recovered `apply<unsigned char>` is a
// BEHAVIOURAL RECOVERY: its semantics are transcribed from the listing and verified
// by differential testing against the shipped bundle itself — the shipped `apply` is
// dlopen'd, run on the same frames, and its output buffer is compared byte for byte —
// but the instruction stream is a re-expression rather than a copy, so the differ
// reports it as DIFFERING and not identical. Treat the BEHAVIOUR as the recovered
// fact for `apply<unsigned char>`, and see src/glitch/RECOVERY-NOTES.md for the
// reading of each mode body, the addresses it was read from, and what is identical
// versus behavioural. The differ's tally for this namespace is 4 identical, 1
// differing, 1 not implemented.
//
// No After Effects host has ever loaded a rebuilt GlitchKit bundle. Nothing in this
// header is a host-acceptance claim.
//
// The same View<T> + apply<T> shape appears in NoiseField (noisefield::apply<T>)
// and ChromaKeyAssist (chromakey::renderView<T>), so it is a fleet-wide idiom for
// satisfying DEEP_COLOR_AWARE rather than a GlitchKit peculiarity.
//
// Listing read: /tmp/ae/glitchkit.asm, the arm64 disassembly of
//   /Applications/Adobe After Effects 2026/Plug-ins/Williams Media/GlitchKit.plugin
// The 8-bit body is `__ZN6glitch5applyIhEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEjf`
// at 0x738..0x1948 (1157 instructions, listed in /tmp/ae/glitch_uc.txt) — that is the
// listing the RECOVERED 8-bit `apply` was read from.
//
// The 16-bit body is `__ZN6glitch5applyItEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEjf`
// at 0x1a04..0x2d30 (1228 instructions, listed in /tmp/ae/glitch_us.txt). Its listing
// has been read too, and every structural fact below that describes "the 16-bit build"
// is read from it — but that symbol is NOT recovered at this commit: it is not
// instantiated, so the differ reports it as not implemented.
//
// Addresses quoted in the comments below are the 8-bit body's unless stated otherwise,
// and every 16-bit address is given as such.

#pragma once

#include <cstddef>

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
// BOTH instantiations read the input view at all four offsets and the output view at
// two of them (the data pointer and the stride). That is what makes this a layout
// rather than a guess: the 8-bit and the 16-bit build agree on it, and they agree
// because they are the SAME source at two channel widths.
//
// THE 16-BIT BUILD'S PAIRED LOAD MATTERS, and it is easy to miss. Its prologue is
// `ldp w9, w28, [x0, #0x8]` at 0x1a24 — ONE paired load that fetches the width
// (+0x08) AND the height (+0x0c) together. A scanner that only looks for an explicit
// `[reg, #offset]` sees "+0x8" and never learns that +0x0c is read as well, which is
// how an earlier draft of this header wrongly claimed the 16-bit build read the input
// "at only 0x0 and 0x8". It does not.
//
// The 16-bit build then reads the two strides through the SPILLED view pointers rather
// than through x0 and x1 directly, because it spills both arguments at the top of the
// prologue (`stp x1, x0, [sp, #0x18]` at 0x1a20 — the output view in the first slot,
// the input view in the second). So:
//
//     ldr  x8, [sp, #0x20]     ; 0x1c8c  the input view pointer again
//     ldr  x0, [x8, #0x10]     ; 0x1c94  the input's rowBytes
//     ...
//     ldr  x8, [sp, #0x18]     ; 0x1c98  the output view pointer again
//     ldr  x13, [x8]           ; 0x1c9c  the output's data
//     ldr  x2, [x8, #0x10]     ; 0x1ca0  the output's rowBytes
//
// and LATER, in the same body, the INPUT view is reloaded from its own spill
// slot (NOT the output's):
//
//     ldr  x8, [sp, #0x20]     ; 0x28e4  the input view pointer again
//     ldr  x0, [x8]            ; 0x28e8  the input's data
//     ldr  x1, [x8, #0x10]     ; 0x28f0  the input's rowBytes
//
// and the pair is reloaded together at 0x2ba4 — `ldp x14, x8, [sp, #0x18]` gives
// x14 = the OUTPUT view (the low slot) and x8 = the INPUT view (the high slot).
//
// which is why the offsets are the same four and the same two as the 8-bit build's,
// and why test/glitch.test.mjs follows the spill rather than assuming x0 and x1 keep
// their argument roles.
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
// [RECOVERED LAYOUT] Params — all NINE fields `apply<T>` reads.
//
//     +0x00  int    mode          the one-based mode selector, tested against 1..5
//     +0x04  float  amount        capped at 1; an early return when it is <= 0
//     +0x08  int    blockSize     clamped to 2..4096; Block Displace's block edge
//     +0x0c  int    hashInput     one of the hashKey inputs
//     +0x10  float  rate          the TIME fed to patternFrame, guarded by `> 0`
//     +0x14  float  shift         the RGB channel shift distance in pixels
//     +0x18  int    scanline      the band height for Scanline Jitter, clamped 1..4096
//     +0x1c  BYTE  luma           read with ldrb: bit 0 is "drive by luminance"
//     +0x20  float  spread        capped at 1; an early return when it is <= 0
//
// NINE fields, and the set is complete rather than partial.
//
// "Capped at 1", not "clamped to 0..1": the lower test is made against the
// ORIGINAL value rather than against the capped one, so a NEGATIVE amount is
// left negative rather than lifted to 0 — it takes the early copy-and-return
// instead. The observable difference from a real 0..1 clamp is nil, because
// both a negative value and a zero value take the same early return, but the
// two are not the same instruction sequence and the header should not claim
// the clamp is stronger than it is.
//
// x2 does NOT stay the `&Params` pointer for the whole body of the 8-bit build. It is
// repurposed as a cursor in the COPY paths and in the mode-4 body — `lsl x2, x15, #2`
// at 0x88c (mode 5's inline frame copy), `ldr x2, [x1]` at 0x9dc (the copy taken when
// the amount or the spread is <= 0) with `strb w17, [x2], #1` at 0xa24, `mov x2, #0x0`
// at 0xeb8 (mode 4) and `ldr x2, [x1]` at 0x17c0 (the catch-all copy). The 16-bit
// build repurposes x2 as well, but differently: as a byte-scaled stride (`lsl x2,
// x2, #1` at 0x1cec on the OUTPUT stride and `lsl x2, x8, #1` at 0x2bf8 on the
// INPUT stride) and as a loop counter (`mov x2, #0x0` at 0x1b6c, 0x1ebc, 0x21c4
// and 0x28a4).
//
// What the listing DOES establish is the offset set: across the WHOLE body of BOTH
// instantiations, the only offsets ever read through x2 are exactly these nine, one per
// field, and each of the nine reads happens on its own dispatch branch — before that
// branch's body repurposes x2. The nine reads are therefore NOT a prologue; they are
// spread through the body, and the addresses below show exactly where each one is.
//
// The 8-bit listing contains exactly these nine offsets and no other:
//
//     ldr  s1, [x2, #0x4]   ; 0x764  amount
//     ldr  s4, [x2, #0x20]  ; 0x780  spread
//     ldrb w8, [x2, #0x1c]  ; 0x7a4  luma
//     ldr  s3, [x2, #0x10]  ; 0x7e8  rate
//     ldr  w10, [x2, #0xc]  ; 0x7ec  hashInput
//     ldr  w6, [x2]         ; 0x7f0  mode
//     ldr  s3, [x2, #0x14]  ; 0xacc  shift   (mode 2's body)
//     ldr  w9, [x2, #0x18]  ; 0xc80  scanline (mode 3's body)
//     ldr  w10, [x2, #0x8]  ; 0x1248 blockSize (mode 1's body)
//
// and the 16-bit listing the same nine, at:
//
//     ldr  s1, [x2, #0x4]   ; 0x1a54 amount
//     ldr  s4, [x2, #0x20]  ; 0x1a70 spread
//     ldrb w10, [x2, #0x1c] ; 0x1a94 luma
//     ldr  s3, [x2, #0x10]  ; 0x1ad8 rate
//     ldr  w13, [x2, #0xc]  ; 0x1adc hashInput
//     ldr  w11, [x2]        ; 0x1ae0 mode
//     ldr  s3, [x2, #0x14]  ; 0x1db4 shift
//     ldr  w8, [x2, #0x18]  ; 0x1f7c scanline
//     ldr  w11, [x2, #0x8]  ; 0x2554 blockSize
//
// The field NAMES beyond `mode` are not in the binary. They are named here after what
// the code does with them, and the meanings are pinned by the self-check.
//
// The nine reads really do all happen before their own branch's body touches x2,
// in BOTH builds. Mode 1's body opens with its own read (`ldr w10, [x2, #0x8]` at
// 0x1248 in the 8-bit listing, `ldr w11, [x2, #0x8]` at 0x2554 in the 16-bit one),
// mode 2's with the shift (`ldr s3, [x2, #0x14]` at 0xacc and 0x1db4) and mode 3's
// with the scanline (`ldr w9, [x2, #0x18]` at 0xc80 and 0x1f7c). The two bodies that
// clobber x2 on entry — mode 4 at 0xeb8 and 0x21c4, and mode 5's copy — read no
// Params field of their own, so there is nothing for them to read afterwards.
// ---------------------------------------------------------------------------
struct Params {
  int mode = 0;
  float amount = 0.0f;
  int blockSize = 0;
  int hashInput = 0;
  float rate = 0.0f;
  float shift = 0.0f;
  int scanline = 0;
  unsigned char luma = 0;
  float spread = 0.0f;
};

static_assert(sizeof(Params) == 0x24, "Params is nine fields ending at +0x24");
static_assert(offsetof(Params, mode) == 0x00, "Params.mode at +0x00");
static_assert(offsetof(Params, amount) == 0x04, "Params.amount at +0x04");
static_assert(offsetof(Params, blockSize) == 0x08, "Params.blockSize at +0x08");
static_assert(offsetof(Params, hashInput) == 0x0c, "Params.hashInput at +0x0c");
static_assert(offsetof(Params, rate) == 0x10, "Params.rate at +0x10");
static_assert(offsetof(Params, shift) == 0x14, "Params.shift at +0x14");
static_assert(offsetof(Params, scanline) == 0x18, "Params.scanline at +0x18");
static_assert(offsetof(Params, luma) == 0x1c, "Params.luma at +0x1c");
static_assert(offsetof(Params, spread) == 0x20, "Params.spread at +0x20");

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
// WHICH BODY IS WHICH MODE is established from what each body does rather than from its
// address:
//
//     1 -> 0x1248  Block Displace     — tiles the frame in blockSize squares and
//                                       displaces each block's pixels
//     2 -> 0xac8   RGB Channel Shift  — reads R from the left of the pixel and B from
//                                       the right, A and G straight through
//     3 -> 0xc7c   Scanline Jitter    — tiles the frame in `scanline`-tall bands and
//                                       slides each band's pixels sideways
//     4 -> 0xeb8   Bad Signal         — a rolling band of quantised, displaced pixels
//     5 -> 0x888   Datamosh Smear     — copies the frame, then smears pixels rightward
//
// Each of those readings is anchored to the instructions quoted in
// src/glitch/RECOVERY-NOTES.md.
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
//
// `apply<T>` calls it ONCE for the frame's top hash and then once per block, band or
// row inside the mode bodies. What salts those calls is the THIRD argument: the top
// hash uses the mode itself, and each mode's own hashes use `mode * 0x101` — 0x101
// for Block Displace, 0x202 for RGB Channel Shift, 0x303 for Scanline Jitter, 0x404
// for Bad Signal and 0x505 for Datamosh Smear.
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

// ---------------------------------------------------------------------------
// [BEHAVIOURAL RECOVERY] The render engine.
//
//     void glitch::apply<T>(const View<const T>& in, View<T>& out,
//                           const Params& params, unsigned seed, float maxValue)
//
// Listing read: /tmp/ae/glitch_uc.txt — the arm64 listing of
//   __ZN6glitch5applyIhEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEjf
// at 0x738..0x1948, 1157 instructions.
//
// This is a BEHAVIOURAL RECOVERY: the semantics are transcribed from that listing,
// and the behaviour is verified against the shipped bundle by dlopen differential —
// the bundle's own `apply` is run on the same frames with the same parameters into a
// sentinel-filled buffer, and the two buffers are compared byte for byte, including
// the padding past the last row. The instruction STREAM is not a copy of the shipped
// one, so the differ reports it as DIFFERING. That difference is recorded, not
// hidden.
//
// The 16-bit twin `glitch::apply<unsigned short>` — the same source at a wider
// channel depth, listed in /tmp/ae/glitch_us.txt at 0x1a04..0x2d30, 1228
// instructions — is OUTSTANDING at this commit: the symbol is not instantiated, so
// the differ reports it as not implemented and this namespace is NOT complete.
//
// The fifth argument is NOT a time: `apply<T>` feeds Params.rate (+0x10) to
// patternFrame, and the float it receives is the PIXEL RANGE — 255 for 8-bit,
// 65535 for 16-bit — because it divides luminance by it and scales the Bad Signal
// quantiser by it.
//
// ONE PIXEL IS FOUR ELEMENTS of T — A, R, G, B — and View<T>::rowBytes is a stride
// in ELEMENTS of T, not in bytes. The 16-bit build multiplies it by two when it
// forms a byte address (`lsl x0, x0, #1` at 0x1ce4 on the INPUT stride and
// `lsl x2, x2, #1` at 0x1cec on the OUTPUT stride; again at 0x28f4 on the input
// stride and 0x2c00 on the output stride, in the 16-bit listing). So for a
// tightly packed frame rowBytes is width * 4 for BOTH depths; reading it as
// width * 4 * sizeof(T) puts the 16-bit build half a row out of step.
//
// The body is transcribed from the listing. Its shape:
//
//   * an EMPTY frame — a width or a height below 1 — leaves the output
//     completely untouched (0x738..0x744);
//   * amount and spread are capped at 1, and if EITHER is <= 0 the frame is
//     copied and returned WITHOUT the spread blend (0x794/0x79c -> 0x9a0);
//   * a per-frame hash is mixed from hashInput, the pattern frame and the mode;
//   * one of five mode bodies runs (see the dispatch above);
//   * finally the spread blend mixes the mode's result with the original input.
//
// The output view's width and height are never read: the loops run off the input's
// dimensions, so a caller handing over a smaller output view gets writes at the
// input's stride rather than a clamp or an error.
//
// See src/glitch/RECOVERY-NOTES.md for the per-mode readings and their addresses.
// ---------------------------------------------------------------------------
template <typename T>
void apply(const View<const T>& in, View<T>& out, const Params& params,
           unsigned seed, float maxValue);

}  // namespace glitch
