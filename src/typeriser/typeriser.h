// typeriser.h — reconstructed from the compiled TypeRiser bundle.
//
// TypeRiser is the third effect with its own domain namespace, after spectra::
// (audio analysis) and lutblend:: (LUT colour). It implements an SRT-subtitle
// karaoke engine: parse cues, then derive per-word animation state from a time.
//
// Recovered from the demangled symbol table and disassembly of TypeRiser.plugin.
// The original C++ was destroyed on 2026-10-13 with no remote and no backup.
//
// Signatures are [exact]; types and layouts marked [recovered] were pinned by the
// code that reads them.

#pragma once
#include <cstddef>
#include <vector>

namespace typeriser {

// ---------------------------------------------------------------------------
// [RECOVERED LAYOUT] Cue — pinned by cueIndexAt(), which reads two fields:
//
//     umaddl x14, w13, w12, x9   ; x12 = 0x30 == 48, the stride
//     ldr    x14, [x14, #0x8]    ; start, in the same units as the time argument
//     add    x9, x9, #0x10
//     ldur   x11, [x9, #-0x8]    ; start again (@0x8)
//     ldr    x11, [x9]           ; end (@0x10)
//
// So a Cue is 48 bytes and holds a half-open [start, end) interval at 0x8 and
// 0x10. The remaining bytes (0 at the base, 0x18..0x30) are held in the same
// allocation but are not read by this function, so their purpose is NOT
// recovered and is deliberately not guessed at here.
// ---------------------------------------------------------------------------
struct Cue {
  long long start = 0;   // [recovered] offset 0x8
  long long end = 0;     // [recovered] offset 0x10
};

// ---------------------------------------------------------------------------
// [exact] Scalar helper.
//
// Third identical copy of this idiom in the fleet, after spectra::clampf and
// lutblend::clampf — same three instructions, same fcsel ordering. Three
// namespaces, three bundles, one implementation.
// ---------------------------------------------------------------------------
float clampf(float value, float lo, float hi);

// ---------------------------------------------------------------------------
// [exact] Which karaoke word is active at time t, or -1 if none.
//
// Returns -1 when t is outside [start, end), when wordCount < 1, or when the
// cue has zero duration.
// ---------------------------------------------------------------------------
int karaokeWordAt(long long start, long long end, long long t, int wordCount);

// ---------------------------------------------------------------------------
// [exact] A 0..1 progress value for word `wordIndex` of a cue.
//
// `wordDuration` spaces the words out; `animDuration` normalises the result.
// When animDuration is not positive the function degrades to a step: 1.0 once the
// word's start has passed, 0.0 before it, with no division.
// ---------------------------------------------------------------------------
float wordProgress(long long start, long long end, int wordIndex,
                   float wordDuration, float animDuration);

// ---------------------------------------------------------------------------
// [exact] Index of the cue covering time t, or -1.
//
// Binary search on `start`, then a bounded backward walk (at most 16 cues) to
// find one whose [start, end) actually contains t. The walk exists because
// sorting on `start` alone cannot rule out an earlier cue with a longer end.
// ---------------------------------------------------------------------------
int cueIndexAt(const std::vector<Cue>& cues, long long t);

}  // namespace typeriser
