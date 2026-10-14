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
#include <string>
#include <vector>

namespace typeriser {

// ---------------------------------------------------------------------------
// [RECOVERED LAYOUT] Cue — 48 bytes. Every field below is pinned by code that
// reads or writes it, not inferred from the size.
//
// cueIndexAt() reads the interval:
//     umaddl x14, w13, w12, x9   ; x12 = 0x30 == 48, the stride
//     ldr    x14, [x14, #0x8]    ; start, in the same units as the time argument
//     add    x9, x9, #0x10
//     ldur   x11, [x9, #-0x8]    ; start again (@0x8)
//     ldr    x11, [x9]           ; end (@0x10)
//
// parseSrt() writes the other two:
//     str  w0, [sp, #0x58]                 ; atoi() -> offset 0x0, an int
//     add  x0, x23, #0x18                  ; &cue.text
//     bl   basic_string::operator=         ; std::string at offset 0x18
//
// 4 (index) + 4 (padding) + 8 (start) + 8 (end) + 24 (string) == 48, which
// matches the 0x30 stride cueIndexAt uses. So a Cue is exactly:
//
//     int index; long long start; long long end; std::string text;
//
// `start`/`end` are in MILLISECONDS, the unit parseTimestamp returns, and the
// interval is half-open: [start, end).
//
// Earlier revisions of this header described offsets 0x0 and 0x18..0x30 as
// "not recovered and deliberately not guessed at". parseSrt recovered both, and
// the guess-free version turned out to be this.
// ---------------------------------------------------------------------------
struct Cue {
  int index = 0;         // [recovered] offset 0x0 — the SRT ordinal, via atoi
  long long start = 0;   // [recovered] offset 0x8 — milliseconds
  long long end = 0;     // [recovered] offset 0x10 — milliseconds, exclusive
  std::string text;      // [recovered] offset 0x18 — lines joined with '\n'
};

static_assert(sizeof(Cue) == 48, "Cue must stay the 0x30 stride cueIndexAt assumes");
static_assert(offsetof(Cue, start) == 0x8, "cueIndexAt reads start at 0x8");
static_assert(offsetof(Cue, end) == 0x10, "cueIndexAt reads end at 0x10");
static_assert(offsetof(Cue, text) == 0x18, "parseSrt writes text at 0x18");

// ---------------------------------------------------------------------------
// [exact] Scalar helper.
//
// Third identical copy of this idiom in the fleet, after spectra::clampf and
// lutblend::clampf — same three instructions, same fcsel ordering. Three
// namespaces, three bundles, one implementation.
// ---------------------------------------------------------------------------
float clampf(float value, float lo, float hi);

// ---------------------------------------------------------------------------
// [RECOVERED] Easing — the selector values come from ease()'s switch. Two of the
// seven cases are indistinguishable in the compiled code: the value 0 and the
// value 1 both fall through to "return t unchanged", so whether they were
// originally separate names (e.g. "None" and "Linear") cannot be recovered from
// this function alone. They are named here for what they DO.
// ---------------------------------------------------------------------------
enum class Easing : int {
  LinearA = 0,        // [recovered] returns t
  LinearB = 1,        // [recovered] returns t — identical to LinearA in the binary
  EaseOutQuad = 2,    // [recovered] 1 - (1-t)^2
  EaseOutCubic = 3,   // [recovered] 1 - (1-t)^3
  EaseOutBack = 4,    // [recovered] standard overshoot, c1 = 1.70158
  EaseInOutCubic = 5, // [recovered] 4t^3 below 0.5, mirrored above
  // Any other value also returns t unchanged.
};

// ---------------------------------------------------------------------------
// [recovered] Strip leading and trailing whitespace. Internal linkage in the
// binary (never exported), and implemented with ctype's isspace via
// __DefaultRuneLocale and the _CTYPE_S bit (0x4000).
// ---------------------------------------------------------------------------
std::string trim(const std::string& s);

// ---------------------------------------------------------------------------
// [exact] Split on whitespace, dropping empty runs.
//
// Uses ctype whitespace (_CTYPE_S) with a __maskrune fallback for non-ASCII
// bytes, so it behaves like isspace in the current locale. See the .cpp for the
// three structural details worth knowing.
// ---------------------------------------------------------------------------
std::vector<std::string> splitWords(const std::string& s);

// ---------------------------------------------------------------------------
// [exact] Parse an SRT timestamp into MILLISECONDS, or -1 if malformed.
//
// Accepts H+:MM:SS then ,mmm or .mmm with one to three fraction digits. Minutes
// and seconds must be <= 59; hours are not range-checked. See the .cpp for the
// full set of acceptance rules, all of which were read off the compiled code.
// ---------------------------------------------------------------------------
long long parseTimestamp(const std::string& s);

// ---------------------------------------------------------------------------
// [exact] Parse one SRT timing line ("START --> END") into two millisecond
// positions. Internal linkage in the binary — it never leaves the bundle.
//
// Returns true only when both sides parse AND end > start; a half-open or
// reversed interval is rejected and the outputs are left UNTOUCHED. The left
// side is parsed raw (no trimming); the right side is trimmed and then cut at
// the first space, which is how SRT's optional "X1:.. X2:.. Y1:.. Y2:.."
// coordinate suffix is discarded. See the .cpp for the full derivation.
// ---------------------------------------------------------------------------
bool parseTimingLine(const std::string& line, long long* outStart, long long* outEnd);

// ---------------------------------------------------------------------------
// [exact] Apply an easing curve to a normalised t.
// ---------------------------------------------------------------------------
float ease(Easing kind, float t);

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

// ---------------------------------------------------------------------------
// [exact] Parse a whole SRT document into cues, sorted by start time.
//
// Tolerant by construction rather than by accident: it strips a UTF-8 BOM,
// accepts LF, CRLF and bare CR, lets the ordinal line be absent, and drops
// blocks that carry no text. Malformed blocks are SKIPPED, not fatal — the
// return value is always a (possibly empty) vector, never an error.
//
// The result is stable-sorted by `start`, so cues that share a start time keep
// their document order. See the .cpp for the full derivation, including the two
// behaviours most likely to surprise: a trailing newline yields a final empty
// line, and a cue whose text is empty is dropped.
// ---------------------------------------------------------------------------
std::vector<Cue> parseSrt(const std::string& text);

}  // namespace typeriser
