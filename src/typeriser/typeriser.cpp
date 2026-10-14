// typeriser.cpp — recovered implementations for the TypeRiser karaoke timing core.
//
// Only RECOVERED functions appear here. The TypeRiser bundle exports 12 authored
// functions in this namespace, and ALL 12 are below. TypeRiser is the first
// effect in the fleet recovered end to end.
//
// The SRT path is complete end to end: parseSrt -> parseTimingLine ->
// parseTimestamp -> trim, with splitWords and cueIndexAt alongside it.

#include "typeriser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

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

// ---------------------------------------------------------------------------
// ease(Easing kind, float t)
//
// __ZN9typeriser4easeENS_6EasingEf, 61 instructions, recovered in full. A small
// easing library, and every branch is a recognisable standard curve.
//
// PROLOGUE — t is clamped to [0,1] before anything else:
//     fmov s1, #1.0 / fcmp / fcsel s1, s1, s0, gt   ; min(t, 1)
//     fcmp s0, #0.0 / movi.2d v0, #0
//     fcsel s0, s0, s1, mi                          ; (t < 0) ? 0 : min(t, 1)
//   The movi zeroes v0 so that s0 is a 0.0 operand for the low branch — the same
//   idiom as wordProgress, and again NOT the raw-negative-value behaviour that
//   spectra::rawBandLevel genuinely has.
//
// DISPATCH (0x1b48..0x1b80) is a comparison chain, and it reveals the enum:
//     cmp w0,#3 / b.gt  -> the >3 group
//       cmp w0,#2 / b.eq -> 0x1ba4  EaseOutQuad
//       cmp w0,#3 / b.ne -> default (return t)
//                            so BOTH 0 and 1 return t unchanged
//     cmp w0,#4 / b.eq   -> 0x1bb4  EaseOutBack
//     cmp w0,#5 / b.ne   -> default (return t)
//
// THE CURVES, each reproduced with its instructions:
//
//   kind 2, EaseOutQuad (0x1ba4):
//     fmov s1,#1.0 / fsub s0,s1,s0      ; 1 - t
//     fmsub s0, s0, s0, s1              ; 1 - (1-t)^2
//   FMSUB computes Sa - Sn*Sm, so this is 1 minus the square, not the square
//   minus 1. Getting FMSUB's operand order backwards inverts the curve.
//
//   kind 3, EaseOutCubic (0x1b60):
//     fsub  s0, s1, s0                  ; 1 - t
//     fnmul s2, s0, s0                  ; -(1-t)^2
//     fmadd s0, s2, s0, s1              ; 1 - (1-t)^3
//
//   kind 4, EaseOutBack (0x1bb4):
//     fadd s0, s0, #-1.0                ; t - 1
//     c3 = 0x402ce6b0 = 2.70158         ; c1 + 1
//     c1 = 0x3fd9cd60 = 1.70158
//     ... -> 1 + c3*(t-1)^3 + c1*(t-1)^2
//   Those two immediates decode to exactly the standard EaseOutBack constants.
//
//   kind 5, EaseInOutCubic (0x1b84):
//     t < 0.5 : 4t^3                    ; via three fmuls, no powf
//     t >= 0.5: 1 - 0.5*(2-2t)^3        ; via _powf, the only call in the function
//   The two halves are computed differently — the lower by multiplication, the
//   upper by powf — which is a quirk of how it was written, not a behavioural
//   difference.
//
// DEFAULT: anything else (including negative values) returns the clamped t, i.e.
// linear. So an unrecognised easing degrades gracefully rather than misbehaving.
// ---------------------------------------------------------------------------
float ease(Easing kind, float t) {
  const float hi = t > 1.0f ? 1.0f : t;
  const float x = t < 0.0f ? 0.0f : hi;  // clamped to [0,1]

  switch (kind) {
    case Easing::EaseOutQuad:  // 2
      return 1.0f - (1.0f - x) * (1.0f - x);

    case Easing::EaseOutCubic: {  // 3
      const float u = 1.0f - x;
      return 1.0f - u * u * u;
    }

    case Easing::EaseOutBack: {  // 4
      constexpr float c1 = 1.70158f;
      constexpr float c3 = 2.70158f;  // c1 + 1, as the immediates confirm
      const float u = x - 1.0f;
      return 1.0f + c3 * u * u * u + c1 * u * u;
    }

    case Easing::EaseInOutCubic:  // 5
      if (x < 0.5f) return 4.0f * x * x * x;
      return 1.0f - 0.5f * std::pow(2.0f - 2.0f * x, 3.0f);

    case Easing::LinearA:  // 0
    case Easing::LinearB:  // 1
    default:
      return x;
  }
}


// ---------------------------------------------------------------------------
// trim(const std::string&)
//
// __ZN9typeriserL4trimE..., internal linkage (the L in the mangling), so it is
// only reachable from inside this translation unit. Recovered shape:
//
//     ldrb  w8, [x0, #0x17]        ; std::string's size byte
//     sxtb  w9, w8                 ; sign-extend: negative means a heap string
//     ldr   x10, [x0, #0x8]        ; heap data pointer
//     csel  x21, x10, x8, lt       ; pick data pointer or the SSO buffer
//     cbz   x21 <empty fast path>
//     adrp  x22, __DefaultRuneLocale
//     ...
//     ldr   w8, [x8, #0x3c]        ; the rune's ctype flags
//     and   w0, w8, #0x4000        ; _CTYPE_S == whitespace
//
// The 0x4000 bit in macOS's _DefaultRuneLocale is _CTYPE_S, i.e. isspace. So this
// is an ordinary whitespace trim, not something bespoke. The sign-extension dance
// is libc++'s small-string optimisation, not parser logic.
// ---------------------------------------------------------------------------
std::string trim(const std::string& s) {
  size_t begin = 0;
  size_t end = s.size();
  while (begin < end) {
    const unsigned char ch = static_cast<unsigned char>(s[begin]);
    if (!std::isspace(ch)) break;
    ++begin;
  }
  while (end > begin) {
    const unsigned char ch = static_cast<unsigned char>(s[end - 1]);
    if (!std::isspace(ch)) break;
    --end;
  }
  return s.substr(begin, end - begin);
}

// ---------------------------------------------------------------------------
// parseTimestamp(const std::string&)
//
// __ZN9typeriser14parseTimestampE..., 103 instructions, recovered in full. A
// hand-rolled parser — no strtol, no sscanf, no locale — that returns
// MILLISECONDS or -1.
//
// STRUCTURE (0x924..0x9e0):
//     bl  trim(s)                    ; the input is trimmed first
//     zero 4 int64 accumulators at sp+0x20..0x38
//     ... walk the characters ...
//       digit path (0x978..0x9a4):
//         sub  w16, w15, #0x30 / cmp #9 / b.hi <not a digit>
//         and  x15, x15, #0xf
//         ldr  x16, [x13, w11, sxtw #3]   ; acc[field]
//         madd x15, x16, x14, x15         ; acc = acc*10 + digit   (w14 == 10)
//         str  x15, [x13, w11, sxtw #3]
//         cmp  w9, #0x3 / b.gt <finish>   ; a 5th digit in one field aborts
//       separator path (0x9a8..0x9dc):
//         ':'          -> advance to the next field
//         ',' or '.'   -> jump straight to field 3 (the fraction)
//   and every transition requires the field to have had at least one digit
//   (`cbz w9 <finish>`).
//
//   The accumulator is stored back to the stack on EVERY digit, so the fields are
//   built incrementally rather than collected into a buffer first.
//
// VALIDATION AND COMBINATION (0xa20..0xa94):
//     mov x19, #-1                        ; the default result
//     cmp w11, #0x3 / b.ne <ret -1>       ; must have reached the fraction field
//     cbz w9 <ret -1>                     ; ...with at least one digit in it
//     ldr x10, [sp,#0x28] / cmp #0x3b / b.gt <ret -1>   ; minutes <= 59
//     ldr x11, [sp,#0x30] / cmp #0x3b / b.gt <ret -1>   ; seconds <= 59
//     ldr x12, [sp,#0x38]                 ; fraction digits as an integer
//       1 digit -> *100    (0x64)
//       2 digits-> *10     (add x9, x12, x12, lsl #2 / lsl #1)
//       3 digits-> *1
//       4+      -> -1
//     ldr x9, [sp,#0x20]                  ; hours
//     madd x9, x9, 60, x10                ; hours*60 + minutes
//     madd x9, x9, 60, x11                ; *60 + seconds
//     madd x19, x9, 1000, x12             ; *1000 + milliseconds
//
// ACCEPTANCE RULES, which are the useful part of this recovery:
//   * hours are NOT range-checked — only minutes and seconds must be <= 59.
//   * minutes and seconds may be one digit, since only the COUNT is checked.
//   * the fraction is REQUIRED, and 1..3 digits. A bare "00:00:01" is rejected,
//     as is "00:00:01,1234".
//   * at most 4 digits are accepted in any field before the parse aborts, so a
//     5-digit hour would be rejected too.
//   * any failure returns -1, and -1 is also a legitimate "no timestamp", so
//     callers must treat it as an error sentinel rather than a position.
// ---------------------------------------------------------------------------
long long parseTimestamp(const std::string& raw) {
  const std::string s = trim(raw);

  long long acc[4] = {0, 0, 0, 0};
  int field = 0;
  int digits = 0;

  for (size_t i = 0; i < s.size(); ++i) {
    const char ch = s[i];
    if (ch >= '0' && ch <= '9') {
      acc[field] = acc[field] * 10 + (ch - '0');
      if (digits > 3) return -1;  // a 5th digit: cmp w9,#3 / b.gt
      ++digits;
      continue;
    }
    if (ch == ':' && field <= 1) {
      if (digits == 0) return -1;  // a field with no digits
      digits = 0;
      ++field;
      continue;
    }
    if ((ch == ',' || ch == '.') && field == 2 && digits != 0) {
      digits = 0;
      field = 3;
      continue;
    }
    return -1;
  }

  if (field != 3) return -1;
  if (digits == 0) return -1;

  const long long hours = acc[0];
  const long long minutes = acc[1];
  const long long seconds = acc[2];
  long long millis = acc[3];

  if (minutes > 59) return -1;
  if (seconds > 59) return -1;

  switch (digits) {
    case 1: millis *= 100; break;
    case 2: millis *= 10; break;
    case 3: break;
    default: return -1;
  }

  return ((hours * 60 + minutes) * 60 + seconds) * 1000 + millis;
}


// ---------------------------------------------------------------------------
// parseTimingLine(const std::string&, long long*, long long*) -> bool
//
// __ZN9typeriserL15parseTimingLineE..., 215 instructions, recovered in full.
// Internal linkage (the L in the mangled name), so it is a helper for parseSrt
// that never crosses the bundle boundary. It is the last piece of the SRT path
// that was still unread.
//
// STRUCTURE, read off the arm64 in this order:
//     x21/x22 = data pointer / size      ; the usual SSO csel dance
//     cmp  x22, #0x3 / b.lt <ret false>  ; fewer than 3 bytes cannot hold "-->"
//     x23 = data + size                  ; end, hoisted out of the loop
//     w24 = 0x2d2d ("--"), w25 = 0x3e ('>')
//
//   the find loop (0x1498..0x14d8), which is line.find("-->") inlined:
//     memchr(p, '-', (end - p) - 2)      ; only positions that can still start
//     cbz  x0 <ret false>                ;   a 3-byte match are searched
//     ldrh w8, [x0] / cmp w8, w24        ; "--"
//     ldrb w9, [x0, #0x2] / ccmp w9, w25 ; ">"
//     b.eq <found>
//     add  x0, x0, #1 / sub x8, x23, x0 / cmp x8, #3 / b.ge <loop>
//
//   Note the memchr's count is recomputed as (end - p) - 2 on every iteration,
//   so the search window shrinks as p advances. libc++'s find(const char*)
//   lowers to exactly this shape, which is why the source reads as a plain find.
//
//   after a match at 0x1500:
//     x27 = arrow - data                 ; the offset, i.e. pos
//     cmn x27, #0x1 / b.eq <ret false>   ; the npos guard
//     x25 = 0x7ffffffffffffff6           ; libc++ basic_string::max_size()
//     x22 = min(size, x27)               ; == pos, the substr(0, pos) length
//     cmp x22, x25 / b.hi <throw_length_error>
//     ... SSO-vs-heap for the left substring at sp+0x20 ...
//     memmove(left, data, pos) / strb wzr, [left + pos]
//     bl parseTimestamp(sp+0x20)         ; x21 = start
//     x26 = x27 + 3                      ; skip past "-->"
//
//   the right side (0x15b0..0x1638) mirrors that but RELOADED from the input:
//     ldrsb x9, [line, #0x17] / tbnz x9, #0x3f <heap>   ; sign bit picks the
//     cmp   x26, x9 / b.ls <ok> / b <throw_out_of_range> ; size source again
//     x22 = size - x26                   ; the substr(pos + 3) length
//     memmove(right, data + x26, x22)    ; built at sp+0x8
//
//   then the right side is post-processed, and this is the part that matters:
//     bl trim(sp+0x8)                    ; sret into sp+0x20 — TRIM only here
//     ldrsb w27, [sp+0x37] / ldp x22, x9, [sp+0x20]
//     csel x23, x22, sp+0x20, lt         ; trim result data pointer
//     csel x24, x9, x8, lt               ; trim result length
//     memchr(x23, ' ', x24)              ; first space in the trimmed text
//     cbz  x0 <use whole string>
//     x24 = min(x24, found - data)       ; cut at it, dropping the rest
//     ... SSO-vs-heap, memmove into sp+0x8, moved up to sp+0x20 ...
//     bl parseTimestamp(sp+0x20)         ; x8 = end
//
//   the accept test (0x171c..0x1738), which is one fused branch:
//     orr  x9, x21, x0                   ; start | end
//     cmn  x9, #0x1                      ; Z=1 iff x9 == -1
//     ccmp x0, x21, #0x4, gt             ; compare only when x9 != -1
//     cset w0, gt                        ; the return value
//     b.le <done>                        ; and the same flags gate the stores
//     str  x21, [x20] / str x8, [x19]    ; *outStart / *outEnd
//
// The `orr` trick is the interesting bit: parseTimestamp signals failure with
// -1, i.e. all bits set, so OR-ing the two results gives -1 when EITHER failed.
// ccmp is written with the #0x4 (Z=1) fallback so that when the guard fails the
// flags are forced to "not greater than", which makes both the cset and the
// b.le come out false. That is exactly:
//
//     if (start != -1 && end != -1 && end > start) { write; return true; }
//     return false;
//
// Two asymmetries are load-bearing and would be easy to "clean up" wrongly:
//   * The LEFT side is NOT trimmed and NOT cut at a space — it goes straight
//     into parseTimestamp. Only the right side is trimmed and space-cut. Since
//     parseTimestamp trims internally, the missing trim on the left is
//     unobservable; the missing space-cut is not, but a space in the left field
//     would make parseTimestamp fail anyway, so the two paths agree.
//   * end > start is STRICT, so a zero-length cue is rejected and the caller
//     never sees a start == end interval. Anything that wants to handle
//     degenerate cues has to do it outside this function.
// ---------------------------------------------------------------------------
bool parseTimingLine(const std::string& line, long long* outStart, long long* outEnd) {
  if (line.size() < 3) return false;

  const size_t pos = line.find("-->");
  if (pos == std::string::npos) return false;

  const long long start = parseTimestamp(line.substr(0, pos));

  std::string rest = trim(line.substr(pos + 3));
  const size_t space = rest.find(' ');
  if (space != std::string::npos) rest = rest.substr(0, space);

  const long long end = parseTimestamp(rest);

  if (start != -1 && end != -1 && end > start) {
    *outStart = start;
    *outEnd = end;
    return true;
  }
  return false;
}


// ---------------------------------------------------------------------------
// splitWords(const std::string&) -> std::vector<std::string>
//
// __ZN9typeriser10splitWordsE..., 123 instructions, recovered in full. A
// whitespace splitter, and most of the instruction count is vector growth and
// libc++ small-string handling rather than splitting logic.
//
// STRUCTURE:
//     mov  x19, x8 / zero it        ; the result vector
//     zero a local string at sp+8   ; the current-word buffer
//     ... decode the input's data pointer and length (the SSO csel dance again) ...
//     cbz  x22 <done>               ; an empty input returns an empty vector
//     x23 = __DefaultRuneLocale
//
//   loop, one character at a time:
//     ldrsb x20, [x21]
//     tbnz  x20, #0x3f <maskrune>   ; a byte >= 0x80 goes to __maskrune
//     add   x8, x23, x20, lsl #2
//     ldr   w8, [x8, #0x3c]         ; ctype flags for an ASCII byte, looked up
//     and   w0, w8, #0x4000         ; _CTYPE_S == whitespace, no call at all
//   <maskrune>: and w0, x20, #0xff / mov w1, #0x4000 / bl ___maskrune
//     cbz   w0 <not-space>
//
//   NOT a space (0x18d8): push the character onto the word buffer and advance.
//   IS a space (0x191c..0x1988): if the buffer is non-empty, append it to the
//     result and CLEAR the buffer; if it is empty, do nothing. That `cbz x9` on
//     the buffer length is what collapses runs of whitespace into a single break
//     and keeps leading/trailing whitespace from producing empty words.
//
//   After the loop (0x199c..0x1a30) the SAME flush runs once more, so the final
//   word is emitted. Without that trailing flush a sentence with no trailing
//   whitespace would silently lose its last word — the most likely way to get
//   this wrong.
//
// Three details worth keeping:
//   * ASCII is tested with a table lookup, not a call; only bytes >= 0x80 reach
//     __maskrune. So the common path allocates nothing and calls nothing.
//   * Empty runs are skipped rather than emitted, so "a  b" gives two words, not
//     three, and " a " gives one.
//   * The result vector is returned by value through the hidden first argument
//     (x8), which is how we know the return type is a vector rather than a
//     single string.
// ---------------------------------------------------------------------------
std::vector<std::string> splitWords(const std::string& s) {
  std::vector<std::string> out;
  std::string word;

  for (size_t i = 0; i < s.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(s[i]);
    if (!std::isspace(ch)) {
      word.push_back(static_cast<char>(ch));
      continue;
    }
    if (!word.empty()) {
      out.push_back(word);
      word.clear();
    }
  }

  if (!word.empty()) out.push_back(word);
  return out;
}


// ---------------------------------------------------------------------------
// parseSrt(const std::string&) -> std::vector<Cue>
//
// __ZN9typeriser8parseSrtE..., 512 instructions, the largest function in the
// TypeRiser namespace and the last one recovered. It is the top of the SRT path:
// everything else on that path (trim, parseTimestamp, parseTimingLine) is a
// helper it calls.
//
// The composition is legible from the call graph alone: 6 x trim, 2 x
// parseTimingLine, 1 x atoi, a vector<string> accumulating LINES, a vector<Cue>
// result, and one __stable_sort over it.
//
// STAGE 1 — strip a UTF-8 BOM (0xc94..0xd0c)
//     ldursb w9, [x29, #-0x59]     ; the string's size/SSO byte
//     and    w8, w9, #0xff
//     tbnz   w9, #0x1f <heap>      ; sign bit picks heap vs SSO, as everywhere
//     cmp    w8, #0x3 / b.lo <skip>
//     ... s[0] == 0xef && s[1] == 0xbb && s[2] == 0xbf ...
//     bl basic_string::erase(0, 3)
//   The heap variant compares size > 2 rather than >= 3 — the same test written
//   the other way round. Note the input is first COPIED into a local string
//   (__init_copy_ctor_external at 0xc90), so the caller's string is not mutated
//   and the parameter stays effectively const.
//
// STAGE 2 — split into lines (0xd10..0xf10)
//   Two locals are zeroed up front: a vector<string> of lines and a string
//   holding the line under construction. The loop walks the input ONE BYTE AT A
//   TIME and dispatches on the byte:
//
//     cmp w8, #0xd / b.eq <CR>     ; 0x0d
//     cmp w8, #0xa / b.ne <append> ; 0x0a
//
//   LF (0xd94): flush the line, CLEAR the buffer, advance by one.
//   CR (0xdbc): flush the line, CLEAR the buffer, and then — the part worth
//     reading twice — peek at the NEXT byte:
//         0xe50  x8 = i + 1
//         0xe68  cmp x8, size / b.hs <advance>   ; nothing to look at
//         0xe7c  w9 = data[x8]
//         0xe80  cmp w9, #0xa
//         0xe84  csel x20, x8, x20, eq           ; i = i+1 ONLY if it is an LF
//     so a CRLF pair is consumed as ONE break and a bare CR is a break on its
//     own. Several Mac-era SRT files use bare CR, which is why this is here.
//   Anything else (0xd48): push_back onto the line buffer and advance.
//
//   A flush copies the buffer into the vector and clears it, EXCEPT on the final
//   flush after the loop (0xeac..0xf00), where the heap buffer is freed instead
//   because it is the last use. That final flush is UNCONDITIONAL, so input that
//   already ends in a newline produces one extra, empty line. It is harmless (an
//   empty line is skipped below) but it is real, and it is why an empty input
//   still yields a one-element vector rather than an empty one.
//
// STAGE 3 — walk the lines into cues (0xf18..0x1208)
//     x21 = line count, from (bytes / 24) via the usual magic multiply
//   The outer loop (0xf60) begins by testing trim(lines[i]).empty() and, if so,
//   simply advancing (0xf84/0xf88). So BLANK LINES ARE SKIPPED, not treated as
//   block delimiters; the structure is positional, not delimiter-driven.
//
//   For a non-empty line it zeroes a 48-byte Cue and two long longs, then tries
//   two shapes:
//
//     (a) 0xfcc  parseTimingLine(trim(lines[i]), &start, &end)
//         If that succeeds there is NO ordinal line: ++i and ok = true (0xfd4).
//
//     (b) otherwise the line must be the ordinal (0xfe0):
//             bl atoi(trim(lines[i]).c_str())
//             str w0, [sp, #0x58]        ; Cue::index, offset 0x0
//         and the NEXT line is tried as the timing line:
//             0x1020 parseTimingLine(trim(lines[i + 1]), &start, &end)
//             0x1040 csel x25, x8, x28, ne   ; i += ok ? 2 : 1
//         If there is no next line (0x1048) then ok = false and i += 1.
//
//   Note the out-parameter order: x1 = sp+0x50 and x2 = sp+0x48, and later
//   `ldp x9, x8, [sp, #0x48]` feeds Cue::start from 0x50 and Cue::end from 0x48.
//   That is parseTimingLine(line, &start, &end) — the same order as its own
//   signature, just allocated in the opposite stack order.
//
//   Then the TEXT loop (0x1050..0x1114) consumes consecutive lines until one
//   trims to empty or the input runs out:
//         0x10c0 cbz <accumulator length> <skip>   ; only join when non-empty
//         0x10c8 push_back('\n')
//         0x10fc append(trim(lines[i]))
//   So each line is individually trimmed and joined with '\n'. The accumulator
//   is NOT trimmed at the end — only tested.
//
//   Finally (0x1118..0x1204) the cue is pushed only if BOTH hold:
//         0x1118 cbz w20 <skip>          ; a timing line was actually found
//         0x1120 trim(accumulator) -> length
//         0x113c cbz x20 <skip>          ; and the text is not empty after trim
//   A block with a valid timing line but no text is therefore DROPPED, and the
//   trimmed length is used only for that test — cue.text keeps the untrimmed
//   accumulator (string::operator= at 0x1150).
//
// STAGE 4 — sort (0x120c..0x1298)
//   The public std::stable_sort is inlined here, which is why the buffer
//   allocation is visible: operator new(count * 48, nothrow) at 0x1244, halving
//   and retrying on failure (0x124c..0x1258) — libc++'s standard behaviour when
//   it cannot get a full-size merge buffer.
//
//   The COMPARATOR is not here: it is inlined into __stable_sort_move, a
//   separate function. Reading it there (0x2e40):
//         ldr  x9,  [x8, #0x8]     ; a->start
//         ldr  x10, [x21, #0x8]    ; b->start
//         cmp  x9,  x10
//         b.ge <take from b>
//   i.e. the first run is consumed only while a->start < b->start. A signed
//   64-bit compare on offset 0x8 alone, so: std::stable_sort by Cue::start
//   ascending. Sorting on start alone cannot resolve overlaps — which is exactly
//   why cueIndexAt() in this same namespace does a bounded backward walk after
//   its binary search instead of trusting neighbour order.
//
// Two consequences worth stating, because both are easy to get wrong:
//   * Every failure mode is non-fatal. A malformed timing line makes ok false,
//     which skips that block; the loop still advances past it. parseSrt has no
//     error return and never throws on bad input.
//   * `i += ok ? 2 : 1` in the ordinal path means a block whose timing line is
//     bad still consumes its ordinal line and then re-examines the following
//     line from scratch. Recovery is per-line, not per-block.
// ---------------------------------------------------------------------------
std::vector<Cue> parseSrt(const std::string& input) {
  // STAGE 1. The input is copied before the BOM is removed, so the caller's
  // string is untouched.
  std::string s = input;
  if (s.size() >= 3 &&
      static_cast<unsigned char>(s[0]) == 0xEF &&
      static_cast<unsigned char>(s[1]) == 0xBB &&
      static_cast<unsigned char>(s[2]) == 0xBF) {
    s.erase(0, 3);
  }

  // STAGE 2.
  std::vector<std::string> lines;
  std::string line;
  for (size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (c == '\r') {
      lines.push_back(line);
      line.clear();
      // A CRLF pair is one break; a bare CR is a break on its own.
      if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
      continue;
    }
    if (c == '\n') {
      lines.push_back(line);
      line.clear();
      continue;
    }
    line.push_back(c);
  }
  // Unconditional, so a trailing newline yields one extra empty line.
  lines.push_back(line);

  // STAGE 3.
  std::vector<Cue> cues;
  size_t i = 0;
  while (i < lines.size()) {
    if (trim(lines[i]).empty()) {  // blank lines are skipped, not delimiters
      ++i;
      continue;
    }

    Cue cue;
    long long start = 0;
    long long end = 0;
    bool ok;
    const std::string first = trim(lines[i]);

    if (parseTimingLine(first, &start, &end)) {
      // No ordinal line in this block.
      ++i;
      ok = true;
    } else {
      cue.index = std::atoi(first.c_str());
      if (i + 1 < lines.size()) {
        ok = parseTimingLine(trim(lines[i + 1]), &start, &end);
        i += ok ? 2 : 1;
      } else {
        ok = false;
        ++i;
      }
    }

    std::string text;
    while (i < lines.size()) {
      if (trim(lines[i]).empty()) break;
      if (!text.empty()) text.push_back('\n');
      text += trim(lines[i]);
      ++i;
    }

    if (!ok) continue;
    if (trim(text).empty()) continue;

    cue.start = start;
    cue.end = end;
    cue.text = text;
    cues.push_back(cue);
  }

  // STAGE 4.
  std::stable_sort(cues.begin(), cues.end(),
                   [](const Cue& a, const Cue& b) { return a.start < b.start; });
  return cues;
}

// ---------------------------------------------------------------------------
// wrapWords(const std::vector<float>&, float, float) -> std::vector<int>
//
// __ZN9typeriser9wrapWordsE..., 52 instructions, recovered in full — the whole
// function fits on one screen, and the arithmetic is five instructions wide.
//
//     mov  x19, x8                 ; the result vector<int>, returned by value
//     ldp  x22, x8, [x0]           ; wordWidths.begin / .end
//     subs x20, x8, x22 / b.eq     ; byte size == count * 4; empty in -> empty out
//     asr  x23, x20, #2            ; count
//     mov.16b v8, v1               ; s8 = the SECOND float argument
//     mov.16b v9, v0               ; s9 = the FIRST float argument
//     bl   operator new / bzero    ; a zero-filled vector<int> of `count` slots
//     ldr  s0, [x22]               ; s0 = wordWidths[0]  -- the running total
//     str  wzr, [x21]              ; result[0] = 0
//     cmp  x20, #0x4 / b.eq <done> ; a single word never breaks
//
//   then, once per remaining word (0x1fdc..0x1ffc):
//     ldr   s1, [x10], #0x4        ; s1 = wordWidths[i]
//     fadd  s0, s9, s0             ; running += the first float
//     fadd  s0, s0, s1             ; running += wordWidths[i]
//     fcmp  s0, s8
//     fcsel s0, s1, s0, gt         ; if (running > second) running = wordWidths[i]
//     cinc  w8, w8, gt             ; if (running > second) ++line
//     str   w8, [x11], #0x4        ; result[i] = line
//
//   The two floats are therefore ADDITIVE and THRESHOLD in that order: the first
//   is charged between adjacent words, the second is the limit. Note the reset
//   assigns `s1` (the word width) and NOT `s9 + s1`, so the separator is not
//   charged at the start of a line — the standard greedy wrap.
//
// Three details worth keeping:
//   * `fcmp` + `fcsel ... gt` is a STRICT greater-than, so a line measuring
//     exactly the limit is kept rather than broken. `>=` would break it.
//   * The result is `std::vector<int>` — confirmed by the out-of-line throw at
//     0x2018 being vector<int>::__throw_length_error, not vector<float>'s.
//   * bzero fills the allocation before the loop writes it. Combined with
//     result[0] = 0, an EMPTY input returns an empty vector and a one-word input
//     returns {0}, never an uninitialised or partially-written vector.
//
// The original parameter NAMES are not recoverable — only the roles. They are
// named for what the arithmetic does with them.
// ---------------------------------------------------------------------------
std::vector<int> wrapWords(const std::vector<float>& wordWidths,
                           float separatorWidth, float maxWidth) {
  std::vector<int> lines(wordWidths.size(), 0);
  if (wordWidths.empty()) return lines;

  float running = wordWidths[0];
  int line = 0;

  for (size_t i = 1; i < wordWidths.size(); ++i) {
    running = separatorWidth + running + wordWidths[i];
    if (running > maxWidth) {
      running = wordWidths[i];
      ++line;
    }
    lines[i] = line;
  }
  return lines;
}

// ---------------------------------------------------------------------------
// wordStyle(Anim, Easing, long long, long long, long long, int, int, float,
//           float, float) -> WordStyle
//
// __ZN9typeriser9wordStyleENS_4AnimENS_6EasingExxxiifff, 164 instructions,
// recovered in full. The last function in the TypeRiser namespace.
//
// ARGUMENT REGISTERS map straight onto the signature, which is how the parameter
// roles below were identified:
//     w0 anim   w1 easing   x2 start   x3 end   x4 now
//     w5 wordIndex   w6 wordCount
//     s0 wordStagger   s1 animDuration   s2 offsetDistance
//
// PROGRESS (0x1d1c..0x1d7c). Worked in DOUBLE, then narrowed:
//     scvtf d3, x2 / scvtf d4, w5 / fcvt d0, s0
//     fmadd d0, d4, d0, d3          ; wordIndex * wordStagger + start
//     scvtf d3, x4 / fsub d0, d3, d0 ; now - that
//     fcmp  s1, #0.0 / b.ls <degenerate>
//     fcvt  d1, s1 / fdiv d0, d0, d1 / fcvt s0, d0   ; / animDuration
//   then two clamps (0x1d4c..0x1d5c and 0x1d74..0x1d7c) which together are
//   `clamp(progress, 0, 1)`. The degenerate branch (animDuration <= 0) skips the
//   division and yields 1.0 when the span is >= 0, else 0.0.
//
// EASING (0x1d80..0x1e3c). A switch on w1 that does NOT call ease() — it repeats
// the same five curves inline, which is a real duplication in the original:
//     case 2: fnmul/fmsub        -> 1 - (1-t)^2      EaseOutQuad
//     case 3: fnmul + fmadd      -> 1 - (1-t)^3      EaseOutCubic
//     case 4: fmadd x2           -> 1 + 1.70158*(t-1)^2 + 2.70158*(t-1)^3
//     case 5: t < 0.5 ? 4t^3 : 1 - 0.5*powf(2 - 2t, 3)
//     default: t unchanged
//   Case 5's upper half really does go through powf(0x1ee0), and for t >= 0.5 the
//   code spills and restores v2 across the call — which is how we know the third
//   float argument survives into field 2 of the result untouched.
//
// ANIMATION (0x1dc4..0x1f48). `cmp w0, #0x3` appears after every easing case, so
// Anim is switched on SECOND, and Anim 4 is special-cased before everything.
//
//   Anim 4 (0x1cc4..0x1d18): no easing at all. It derives a WORD INDEX from
//     elapsed time instead:
//         idx = -1
//         if (now < end && now - start >= 0 && wordCount - 1 >= 0)
//             idx = min((now - start) * wordCount / (end - start), wordCount - 1)
//     and then tests `idx == wordIndex` ONCE, feeding the same comparison to both
//     a scale (1.08 vs 1.0) and the boolean field. The divide cannot trap: the
//     guard `now < end` together with `now >= start` forces end - start > 0.
//
//   Anim 3 -> clamps 1.5*e and sets field 2 to offsetDistance * (1 - e).
//   Anim 2 -> clamps e, field 1 stays 1.0.
//   Anim 1 -> field 1 becomes 0.7*e + 0.3 and field 0 clamps 2*e: the word
//     scales up from 0.3 while fading in at double rate.
//   Anything else -> {1, 1, 0, 0}: the identity.
//
// One caveat worth recording rather than papering over. Anim 3 is the ONLY path
// that does not write x1, so field 3 is never assigned there. It reads as 0 in
// practice because the caller loads w1 with `ldp w1, w0, [sp, #0x48]`, and a
// 32-bit load into w1 zeroes the upper half of x1 — so the flag is clear on
// entry and stays clear. That is correct-by-accident, not correct-by-design, and
// this transcription makes the intent explicit with a 0.
// ---------------------------------------------------------------------------
WordStyle wordStyle(Anim anim, Easing easing,
                    long long start, long long end, long long now,
                    int wordIndex, int wordCount,
                    float wordStagger, float animDuration, float offsetDistance) {
  WordStyle style;

  // ---- Anim 4: karaoke. No easing; the active word is found by elapsed time.
  if (anim == Anim::Karaoke) {
    long long idx = -1;
    if (now < end && now - start >= 0 && wordCount - 1 >= 0) {
      idx = ((now - start) * wordCount) / (end - start);
      if (idx > wordCount - 1) idx = wordCount - 1;
    }

    const bool hit = (static_cast<int>(idx) == wordIndex);
    style.alpha = 1.0f;
    style.scale = hit ? 1.08f : 1.0f;
    style.offset = offsetDistance;
    style.highlight = hit ? 1 : 0;  // see the caveat above; this path does set it
    return style;
  }

  // ---- Progress, in double, then clamped to 0..1.
  const double span =
      static_cast<double>(now) -
      (static_cast<double>(wordIndex) * static_cast<double>(wordStagger) +
       static_cast<double>(start));

  float t;
  if (animDuration > 0.0f) {
    t = static_cast<float>(span / static_cast<double>(animDuration));
    if (t > 1.0f) t = 1.0f;
  } else {
    t = (span >= 0.0) ? 1.0f : 0.0f;
  }
  if (t < 0.0f) t = 0.0f;

  // ---- Easing, applied to the progress. Same curves as ease(), written out.
  float e;
  switch (easing) {
    case Easing::EaseOutQuad:
      e = 1.0f - (1.0f - t) * (1.0f - t);
      break;
    case Easing::EaseOutCubic: {
      const float u = 1.0f - t;
      e = 1.0f - u * u * u;
      break;
    }
    case Easing::EaseOutBack: {
      const float u = t - 1.0f;
      e = 1.0f + 1.70158f * u * u + 2.70158f * u * u * u;
      break;
    }
    case Easing::EaseInOutCubic:
      if (t < 0.5f) {
        e = 4.0f * t * t * t;
      } else {
        // The original really calls powf here rather than multiplying; keeping
        // that preserves its exact rounding.
        e = 1.0f - 0.5f * std::pow(2.0f - 2.0f * t, 3.0f);
      }
      break;
    default:
      e = t;
      break;
  }

  // ---- Anim, switched on second.
  switch (anim) {
    case Anim::Rise: {
      style.alpha = e * 1.5f;
      if (style.alpha > 1.0f) style.alpha = 1.0f;
      style.scale = 1.0f;
      style.offset = offsetDistance * (1.0f - e);
      style.highlight = 0;
      break;
    }
    case Anim::Fade:
      style.alpha = e;
      if (style.alpha > 1.0f) style.alpha = 1.0f;
      style.scale = 1.0f;
      style.offset = 0.0f;
      style.highlight = 0;
      break;
    case Anim::Pop: {
      style.alpha = e + e;
      if (style.alpha > 1.0f) style.alpha = 1.0f;
      style.scale = e * 0.7f + 0.3f;
      style.offset = 0.0f;
      style.highlight = 0;
      break;
    }
    default:
      // Anim::None, or any value outside 1..4: the identity.
      style.alpha = 1.0f;
      style.scale = 1.0f;
      style.offset = 0.0f;
      style.highlight = 0;
      break;
  }
  return style;
}


}  // namespace typeriser
