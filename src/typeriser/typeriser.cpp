// typeriser.cpp — recovered implementations for the TypeRiser karaoke timing core.
//
// Only RECOVERED functions appear here. parseSrt (512 instructions), splitWords,
// parseTimestamp and ease are identified but not transcribed, and are absent
// rather than stubbed.

#include "typeriser.h"

namespace typeriser {

// ---------------------------------------------------------------------------
// clampf(float value, float lo, float hi)
//
// __ZN9typeriser6clampfEfff:
//     fcmp  s0, s2 / fcsel s2, s2, s0, gt
//     fcmp  s0, s1 / fcsel s0, s1, s2, mi
//     ret
//
// Byte-for-byte the same as spectra::clampf and lutblend::clampf. Three separate
// namespaces in three separate bundles converged on this exact branchless form,
// which is strong evidence all three transcriptions are correct — and a test
// asserts they still agree so an edit to one is caught.
// ---------------------------------------------------------------------------
float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

// ---------------------------------------------------------------------------
// karaokeWordAt(long long start, long long end, long long t, int wordCount)
//
// __ZN9typeriser13karaokeWordAtExxxi, 15 instructions, recovered in full:
//
//     cmp  x2, x1 / b.ge <ret -1>        ; t >= end  -> -1  (half-open interval)
//     subs x8, x2, x0 / b.lt <ret -1>    ; t <  start -> -1
//     subs w9, w3, #0x1 / b.lt <ret -1>  ; wordCount < 1 -> -1
//     sub  x10, x1, x0                   ; duration = end - start
//     mov  w11, w3
//     mul  x8, x8, x11                   ; (t - start) * wordCount
//     sdiv x8, x8, x10                   ; / duration
//     cmp  w8, w9
//     csel w0, w8, w9, lt                ; min(index, wordCount - 1)
//
// Two details worth keeping. The interval is HALF-OPEN: exactly at `end` the
// answer is already -1, which keeps the last word from lingering for one extra
// frame. And the clamp is to wordCount-1, so a t that lands numerically on the
// end still cannot index past the last word — the multiplication can reach
// wordCount exactly at the top of the range, so the clamp is load-bearing rather
// than defensive.
//
// sdiv truncates toward zero; since the numerator is non-negative here that is a
// floor, so the word only advances once t has actually entered it.
// ---------------------------------------------------------------------------
int karaokeWordAt(long long start, long long end, long long t, int wordCount) {
  if (t >= end) return -1;
  if (t < start) return -1;
  if (wordCount < 1) return -1;

  const long long duration = end - start;
  if (duration <= 0) return -1;

  const long long index = ((t - start) * static_cast<long long>(wordCount)) / duration;
  const int last = wordCount - 1;
  return index < last ? static_cast<int>(index) : last;
}

// ---------------------------------------------------------------------------
// wordProgress(long long start, long long end, int wordIndex,
//              float wordDuration, float animDuration)
//
// __ZN9typeriser12wordProgressExxiff, 23 instructions, recovered in full:
//
//     fcvt  d0, s0                       ; (double)wordDuration
//     scvtf d2, x0                       ; (double)start
//     scvtf d3, w2                       ; (double)wordIndex
//     fmadd d0, d3, d0, d2               ; wordIndex * wordDuration + start
//     scvtf d2, x1                       ; (double)end
//     fsub  d0, d2, d0                   ; end - wordStart
//     fcmp  s1, #0.0 / b.ls <step>       ; animDuration <= 0 -> no division
//     fcvt d1, s1
//     fdiv d0, d0, d1                    ; / animDuration
//     fcvt s0, d0
//     fmov s1, #1.0
//     fcmp s0, s1 / fcsel s1, s1, s0, gt ; min(x, 1)
//     fcmp s0, #0.0
//     movi.2d v0, #0
//     fcsel s0, s0, s1, mi               ; (x < 0) ? 0 : min(x, 1)
//     ret
//
//   <step> (0x1c6c):
//     fcmp d0, #0.0
//     movi.2d v0, #0
//     fmov s1, #1.0
//     fcsel s0, s1, s0, ge               ; (remaining >= 0) ? 1.0 : 0.0
//
// Note the movi.2d before each clamping fcsel. Read alone, `fcsel s0, s0, s1, mi`
// looks like it returns the raw negative value — the same shape that is a genuine
// edge case in spectra::rawBandLevel. Here it is NOT, because the movi has just
// zeroed v0, and s0 is v0's low lane: the "low" operand is 0.0. Getting this
// backwards would make the two cases look identical when they are not.
//
// The result is therefore a proper 0..1 clamp, and the non-positive-animDuration
// path degrades to a step function rather than dividing by zero.
// ---------------------------------------------------------------------------
float wordProgress(long long start, long long end, int wordIndex,
                   float wordDuration, float animDuration) {
  const double wordStart = static_cast<double>(start) +
                           static_cast<double>(wordIndex) * static_cast<double>(wordDuration);
  const double remaining = static_cast<double>(end) - wordStart;

  if (!(animDuration > 0.0f)) {
    return remaining >= 0.0 ? 1.0f : 0.0f;
  }

  const float x = static_cast<float>(remaining / static_cast<double>(animDuration));
  const float hi = x > 1.0f ? 1.0f : x;
  return x < 0.0f ? 0.0f : hi;
}

// ---------------------------------------------------------------------------
// cueIndexAt(const std::vector<Cue>& cues, long long t)
//
// __ZN9typeriser10cueIndexAtERKNSt3__16vectorINS_3CueENS0_9allocatorIS2_EEEEx,
// 48 instructions, recovered in full.
//
// PHASE 1 — bound the search (0x1a70..0x1a94):
//     ldp  x9, x8, [x0]          ; begin, end
//     cmp  x9, x8 / b.eq <ret -1>; empty -> -1
//     sub  x8, x8, x9
//     lsr  x8, x8, #4
//     mov  w10, #0xaaab / movk w10, #0xaaaa, lsl #16
//     mul  w8, w8, w10           ; (bytes >> 4) * 0xAAAAaaab == bytes / 48
//     subs w10, w8, #1 / b.lt <ret -1>
//   The magic multiply is the usual reciprocal division; it also CONFIRMS the
//   Cue stride is 48 bytes, which the index arithmetic below relies on.
//
// PHASE 2 — binary search on Cue::start (0x1aa4..0x1acc):
//     w13 = (lo + hi) / 2
//     x14 = begin + w13 * 48
//     x14 = [x14 + 8]                     ; cue.start
//     cmp x14, x1
//     csel w10, w15, w10, gt              ; start > t  -> hi = mid - 1
//     csel w8,  w8,  w13, gt              ; otherwise keep best
//     csinc w11, w11, w13, gt             ; start > t  -> lo unchanged, else mid + 1
//   `best` starts at -1 and only moves when start <= t, so it ends as the last cue
//   whose start is not after t.
//
// PHASE 3 — a bounded backward walk (0x1ad0..0x1b1c). This is the part that
//   matters: sorting by `start` alone cannot tell whether an EARLIER cue has a
//   longer `end` that still covers t, so up to 16 preceding cues are checked:
//     tbnz w8, #0x1f <ret -1>             ; best < 0 -> -1
//     x9 = begin + best*48 + 0x10
//     loop: x11 = [x9 - 8]                ; cue.start  (@0x8)
//           cmp x11, x1 / b.gt <next>     ; start > t -> keep walking back
//           x11 = [x9]                    ; cue.end    (@0x10)
//           cmp x1, x11 / b.lt <return best>   ; t < end -> this cue covers t
//           cmp w10, #0xf / b.hi <ret -1> ; at most 16 candidates
//           x9 -= 0x30 ; ++w10 ...
//   The bound of 16 is a real limit, not a formality: a long cue that starts 17
//   or more cues before t would not be found.
// ---------------------------------------------------------------------------
int cueIndexAt(const std::vector<Cue>& cues, long long t) {
  if (cues.empty()) return -1;
  const int count = static_cast<int>(cues.size());

  int lo = 0;
  int hi = count - 1;
  int best = -1;
  while (lo <= hi) {
    const int mid = (lo + hi) >> 1;
    if (cues[static_cast<size_t>(mid)].start > t) {
      hi = mid - 1;
    } else {
      best = mid;
      lo = mid + 1;
    }
  }

  if (best < 0) return -1;

  int examined = 0;
  for (int i = best; i >= 0; --i) {
    const Cue& c = cues[static_cast<size_t>(i)];
    if (c.start <= t && t < c.end) return i;
    ++examined;
    if (examined > 15) break;
  }
  return -1;
}

}  // namespace typeriser
