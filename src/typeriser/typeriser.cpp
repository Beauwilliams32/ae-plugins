// typeriser.cpp — recovered implementations for the TypeRiser karaoke timing core.
//
// Only RECOVERED functions appear here. parseSrt (512 instructions) is
// identified but not transcribed, and is absent rather than stubbed; every
// helper it delegates to (trim, splitWords, parseTimestamp, parseTimingLine,
// cueIndexAt) is now recovered in full.

#include "typeriser.h"

#include <cctype>
#include <cmath>

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


}  // namespace typeriser
