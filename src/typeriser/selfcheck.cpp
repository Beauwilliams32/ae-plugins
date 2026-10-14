// selfcheck.cpp — verification for the recovered typeriser timing core.

#include "typeriser.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "  PASS" : "  FAIL", what);
  if (!ok) ++failures;
}
void checkNear(float got, float want, float tol, const char* what) {
  const bool ok = std::fabs(got - want) <= tol;
  std::printf("%s %s  (got %.9g want %.9g)\n", ok ? "  PASS" : "  FAIL", what, got, want);
  if (!ok) ++failures;
}
}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  std::printf("typeriser::clampf\n");
  checkNear(typeriser::clampf(5.0f, 0.0f, 10.0f), 5.0f, 0.0f, "mid passes through");
  checkNear(typeriser::clampf(-3.0f, 0.0f, 10.0f), 0.0f, 0.0f, "below lo clamps to lo");
  checkNear(typeriser::clampf(99.0f, 0.0f, 10.0f), 10.0f, 0.0f, "above hi clamps to hi");

  std::printf("typeriser::karaokeWordAt\n");
  // A 1000-unit cue with 4 words: each word owns 250 units.
  check(typeriser::karaokeWordAt(0, 1000, 0, 4) == 0, "t at the start is word 0");
  check(typeriser::karaokeWordAt(0, 1000, 249, 4) == 0, "just before the boundary is still word 0");
  check(typeriser::karaokeWordAt(0, 1000, 250, 4) == 1, "the boundary advances to word 1");
  check(typeriser::karaokeWordAt(0, 1000, 500, 4) == 2, "midpoint is word 2");
  check(typeriser::karaokeWordAt(0, 1000, 999, 4) == 3, "just before the end is the last word");
  // Half-open: exactly at end is already out of range.
  check(typeriser::karaokeWordAt(0, 1000, 1000, 4) == -1, "t == end is -1 (half-open interval)");
  check(typeriser::karaokeWordAt(0, 1000, 1001, 4) == -1, "past the end is -1");
  check(typeriser::karaokeWordAt(0, 1000, -1, 4) == -1, "before the start is -1");
  check(typeriser::karaokeWordAt(0, 1000, 500, 0) == -1, "zero words is -1");
  check(typeriser::karaokeWordAt(0, 1000, 100, 1) == 0, "a one-word cue is always word 0");
  // A non-zero start, and the clamp doing real work.
  check(typeriser::karaokeWordAt(500, 1500, 500, 4) == 0, "non-zero start: t at start is word 0");
  check(typeriser::karaokeWordAt(500, 1500, 1499, 4) == 3, "non-zero start: last word near the end");
  // The multiplication reaches wordCount exactly at the top of the range, so the
  // min() clamp must catch it.
  check(typeriser::karaokeWordAt(0, 4, 3, 4) == 3, "index can reach wordCount-1 without overrunning");
  check(typeriser::karaokeWordAt(0, 4, 2, 4) == 2, "and stays in range one step earlier");

  std::printf("typeriser::wordProgress\n");
  // wordDuration 0 makes every word start at `start`, so the remaining span is
  // the whole cue and progress is driven by animDuration alone.
  checkNear(typeriser::wordProgress(0, 100, 0, 0.0f, 100.0f), 1.0f, 1e-6f, "full span over animDuration is 1");
  checkNear(typeriser::wordProgress(0, 100, 0, 0.0f, 200.0f), 0.5f, 1e-6f, "twice the duration is 0.5");
  checkNear(typeriser::wordProgress(0, 100, 0, 0.0f, 400.0f), 0.25f, 1e-6f, "four times is 0.25");
  // Clamped at both ends.
  checkNear(typeriser::wordProgress(0, 100, 0, 0.0f, 50.0f), 1.0f, 1e-6f, "above 1 clamps to 1");
  checkNear(typeriser::wordProgress(0, 100, 0, 0.0f, 1000.0f), 0.1f, 1e-6f, "small values are not clamped");
  // A later word starts later, so less of the cue remains.
  checkNear(typeriser::wordProgress(0, 100, 2, 25.0f, 100.0f), 0.5f, 1e-6f,
            "word 2 at 25 units each starts at 50, leaving 50 of 100");
  checkNear(typeriser::wordProgress(0, 100, 4, 25.0f, 100.0f), 0.0f, 1e-6f,
            "a word starting at the end has no remaining span");
  checkNear(typeriser::wordProgress(0, 100, 5, 25.0f, 100.0f), 0.0f, 1e-6f,
            "a word starting past the end clamps to 0, not negative");
  // Non-positive animDuration degrades to a step, with no division.
  checkNear(typeriser::wordProgress(0, 100, 0, 0.0f, 0.0f), 1.0f, 1e-6f,
            "animDuration 0 with remaining >= 0 steps to 1");
  checkNear(typeriser::wordProgress(0, 100, 9, 25.0f, 0.0f), 0.0f, 1e-6f,
            "animDuration 0 past the end steps to 0 (no divide by zero, no NaN)");
  check(std::isfinite(typeriser::wordProgress(0, 100, 0, 0.0f, -5.0f)),
        "negative animDuration is finite (treated as the step path)");

  std::printf("typeriser::cueIndexAt\n");
  {
    auto mk = [](long long s, long long e) { typeriser::Cue c; c.start = s; c.end = e; return c; };
    const std::vector<typeriser::Cue> cues{ mk(0, 100), mk(200, 300), mk(400, 900) };
    check(typeriser::cueIndexAt(cues, 0) == 0, "t at a cue start finds it");
    check(typeriser::cueIndexAt(cues, 99) == 0, "t inside the first cue");
    check(typeriser::cueIndexAt(cues, 100) == -1, "half-open: t == end finds nothing");
    check(typeriser::cueIndexAt(cues, 200) == 1, "t at the second cue's start");
    check(typeriser::cueIndexAt(cues, 500) == 2, "t inside the third cue");
    check(typeriser::cueIndexAt(cues, 150) == -1, "a gap between cues finds nothing");
    check(typeriser::cueIndexAt(cues, 5000) == -1, "t past every cue finds nothing");
    check(typeriser::cueIndexAt(cues, -1) == -1, "t before every cue finds nothing");
    const std::vector<typeriser::Cue> none;
    check(typeriser::cueIndexAt(none, 0) == -1, "no cues finds nothing");

    // The backward walk exists for exactly this case: an earlier cue with a long
    // end that still covers t, which a search on `start` alone would miss.
    const std::vector<typeriser::Cue> overlap{ mk(0, 1000), mk(10, 20), mk(30, 40) };
    check(typeriser::cueIndexAt(overlap, 500) == 0,
          "a long earlier cue is found after passing several shorter ones");
    check(typeriser::cueIndexAt(overlap, 35) == 2, "and a later contained cue still wins when it covers t");
  }


  std::printf("typeriser::ease\n");
  {
    using E = typeriser::Easing;
    // Every curve must be anchored at both ends, or the animation starts or ends
    // somewhere other than where it should.
    struct { E k; const char* n; } cases[] = {
      {E::LinearA, "LinearA(0)"}, {E::LinearB, "LinearB(1)"},
      {E::EaseOutQuad, "EaseOutQuad(2)"}, {E::EaseOutCubic, "EaseOutCubic(3)"},
      {E::EaseOutBack, "EaseOutBack(4)"}, {E::EaseInOutCubic, "EaseInOutCubic(5)"},
      {static_cast<E>(99), "unknown(99)"},
    };
    for (auto& c2 : cases) {
      char label[96];
      std::snprintf(label, sizeof label, "%s anchors at 0", c2.n);
      checkNear(typeriser::ease(c2.k, 0.0f), 0.0f, 1e-4f, label);
      std::snprintf(label, sizeof label, "%s anchors at 1", c2.n);
      checkNear(typeriser::ease(c2.k, 1.0f), 1.0f, 1e-4f, label);
    }

    // t is clamped before the curve is applied, so out-of-range input cannot
    // extrapolate the easing.
    checkNear(typeriser::ease(E::EaseOutQuad, -5.0f), 0.0f, 1e-5f, "negative t clamps to 0");
    checkNear(typeriser::ease(E::EaseOutQuad, 5.0f), 1.0f, 1e-5f, "t above 1 clamps to 1");

    // Linear is the identity, and 0 and 1 are indistinguishable in the binary.
    checkNear(typeriser::ease(E::LinearA, 0.25f), 0.25f, 1e-6f, "LinearA passes t through");
    checkNear(typeriser::ease(E::LinearB, 0.25f), 0.25f, 1e-6f,
              "LinearB is identical to LinearA (not separable in the compiled switch)");
    checkNear(typeriser::ease(static_cast<E>(99), 0.25f), 0.25f, 1e-6f,
              "an unknown easing degrades to linear rather than misbehaving");

    // Ease-out curves must be ahead of linear in the middle of the range.
    check(typeriser::ease(E::EaseOutQuad, 0.5f) > 0.5f, "EaseOutQuad is ahead of linear at t=0.5");
    checkNear(typeriser::ease(E::EaseOutQuad, 0.5f), 0.75f, 1e-5f, "EaseOutQuad(0.5) = 1 - 0.5^2 = 0.75");
    checkNear(typeriser::ease(E::EaseOutCubic, 0.5f), 0.875f, 1e-5f, "EaseOutCubic(0.5) = 1 - 0.5^3 = 0.875");
    check(typeriser::ease(E::EaseOutCubic, 0.5f) > typeriser::ease(E::EaseOutQuad, 0.5f),
          "cubic overshoots further than quad, as a stronger ease-out should");

    // EaseOutBack must actually overshoot past 1 before settling, which is the
    // whole point of the curve. If FMSUB/FMADD operand order were wrong this
    // would not happen.
    {
      float peak = 0.0f;
      for (int i = 0; i <= 100; ++i) {
        const float v = typeriser::ease(E::EaseOutBack, static_cast<float>(i) / 100.0f);
        if (v > peak) peak = v;
      }
      check(peak > 1.0f, "EaseOutBack overshoots past 1 somewhere in (0,1)");
      check(peak < 1.15f, "and the overshoot is modest (about 1.10 for c1 = 1.70158)");
    }

    // EaseInOutCubic is symmetric about the midpoint and slow at both ends.
    checkNear(typeriser::ease(E::EaseInOutCubic, 0.5f), 0.5f, 1e-5f, "EaseInOutCubic is 0.5 at t=0.5");
    check(typeriser::ease(E::EaseInOutCubic, 0.25f) < 0.25f, "it starts slower than linear (eases in)");
    check(typeriser::ease(E::EaseInOutCubic, 0.75f) > 0.75f, "and finishes faster (eases out)");
    // Symmetry: f(t) + f(1-t) == 1 for a symmetric ease-in-out.
    for (float t : {0.1f, 0.3f, 0.42f}) {
      const float a = typeriser::ease(E::EaseInOutCubic, t);
      const float b = typeriser::ease(E::EaseInOutCubic, 1.0f - t);
      char label[96];
      std::snprintf(label, sizeof label, "EaseInOutCubic symmetric at t=%.2f (f(t)+f(1-t)=1)", t);
      checkNear(a + b, 1.0f, 1e-4f, label);
    }
    // Both halves of the piecewise join continuously at 0.5.
    const float lo = typeriser::ease(E::EaseInOutCubic, 0.4999f);
    const float hi = typeriser::ease(E::EaseInOutCubic, 0.5001f);
    check(std::fabs(hi - lo) < 1e-3f, "the two halves of EaseInOutCubic join without a step");
  }


  std::printf("typeriser::trim\n");
  check(typeriser::trim("  hi  ") == "hi", "strips leading and trailing spaces");
  check(typeriser::trim("\t\n 00:00:01,000 \r\n") == "00:00:01,000", "strips tabs, CR and LF too");
  check(typeriser::trim("x") == "x", "leaves a single character alone");
  check(typeriser::trim("") == "", "empty stays empty");
  check(typeriser::trim("   ") == "", "all-whitespace becomes empty");
  check(typeriser::trim("a b") == "a b", "inner whitespace is preserved");

  std::printf("typeriser::parseTimestamp\n");
  check(typeriser::parseTimestamp("00:00:00,000") == 0, "zero");
  check(typeriser::parseTimestamp("00:00:01,500") == 1500, "one and a half seconds");
  check(typeriser::parseTimestamp("00:00:01,050") == 1050, "leading zero in the fraction");
  check(typeriser::parseTimestamp("00:00:01,005") == 1005, "two leading zeros");
  check(typeriser::parseTimestamp("00:00:59,999") == 59999, "last representable second");
  check(typeriser::parseTimestamp("00:01:00,000") == 60000, "one minute");
  check(typeriser::parseTimestamp("01:00:00,000") == 3600000, "one hour");
  check(typeriser::parseTimestamp("01:02:03,004") == 3723004, "h:m:s:ms combine correctly");
  check(typeriser::parseTimestamp("99:59:59,999") == (99LL*3600+59*60+59)*1000+999,
        "large two-digit hours are accepted (hours are NOT range-checked)");
  check(typeriser::parseTimestamp("100:00:00,000") == 100LL*3600000,
        "three-digit hours are accepted too");

  // Fractions are right-padded conceptually: 1 digit is tenths, 2 hundredths.
  check(typeriser::parseTimestamp("00:00:00,1") == 100, "1 fraction digit is tenths -> 100 ms");
  check(typeriser::parseTimestamp("00:00:00,12") == 120, "2 fraction digits -> 120 ms");
  check(typeriser::parseTimestamp("00:00:00,123") == 123, "3 fraction digits -> 123 ms");

  // The '.' separator is accepted as well as ','.
  check(typeriser::parseTimestamp("00:00:01.500") == 1500, "a dot also separates the fraction");

  // Whitespace is trimmed first, which is why real SRT lines parse.
  check(typeriser::parseTimestamp("  00:00:02,000  ") == 2000, "surrounding whitespace is trimmed");

  // Rejections, each pinned to a rule read off the compiled code.
  check(typeriser::parseTimestamp("") == -1, "empty is rejected");
  check(typeriser::parseTimestamp("00:00:01") == -1, "a REQUIRED fraction: no fraction is rejected");
  check(typeriser::parseTimestamp("00:00:01,") == -1, "a fraction separator with no digits is rejected");
  check(typeriser::parseTimestamp("00:00:01,1234") == -1, "4 fraction digits are rejected");
  check(typeriser::parseTimestamp("00:60:00,000") == -1, "minutes > 59 are rejected");
  check(typeriser::parseTimestamp("00:00:60,000") == -1, "seconds > 59 are rejected");
  check(typeriser::parseTimestamp("00:00:00,0000") == -1, "and 4 fraction digits stay rejected");
  check(typeriser::parseTimestamp("not a time") == -1, "garbage is rejected");
  check(typeriser::parseTimestamp("00-00-01,000") == -1, "the wrong separator is rejected");
  check(typeriser::parseTimestamp("00:00:0a,000") == -1, "a letter inside a field is rejected");
  // Hours being unchecked is a real asymmetry, not an oversight in the test.
  check(typeriser::parseTimestamp("99999:00:00,000") == -1,
        "but a 5-digit field aborts the parse (at most 4 digits per field)");

  // A typical SRT block line, which is the actual use case.
  check(typeriser::parseTimestamp("00:01:23,456") == 83456, "a realistic SRT timestamp parses");


  std::printf("typeriser::parseTimingLine\n");
  {
    long long a = 0, b = 0;

    check(typeriser::parseTimingLine("00:00:01,000 --> 00:00:02,500", &a, &b), "a plain timing line parses");
    check(a == 1000 && b == 2500, "both sides land in the outputs");

    // SRT allows a coordinate suffix after the end time; the space-cut drops it.
    a = b = 0;
    check(typeriser::parseTimingLine("00:00:01,000 --> 00:00:02,500 X1:1 X2:2 Y1:3 Y2:4", &a, &b),
          "a coordinate suffix does not stop the parse");
    check(a == 1000 && b == 2500, "the suffix is discarded at the first space");

    // The right side is trimmed, so padding around the end time is fine.
    a = b = 0;
    check(typeriser::parseTimingLine("00:00:01,000 -->    00:00:02,500   ", &a, &b),
          "the right side tolerates surrounding whitespace");
    check(a == 1000 && b == 2500, "trimmed whitespace does not leak into the value");

    // A single arrow is not enough; the LEFT side is cut at the FIRST "-->".
    a = b = 0;
    check(typeriser::parseTimingLine("00:00:01,000-->00:00:02,500", &a, &b), "spaces around the arrow are optional");
    check(a == 1000 && b == 2500, "a tight arrow still splits correctly");

    // end > start is STRICT, so degenerate and reversed intervals are refused.
    a = -7; b = -7;
    check(!typeriser::parseTimingLine("00:00:02,000 --> 00:00:02,000", &a, &b),
          "a zero-length interval is rejected (end > start is strict)");
    check(a == -7 && b == -7, "and the outputs are left UNTOUCHED on failure");

    check(!typeriser::parseTimingLine("00:00:05,000 --> 00:00:01,000", &a, &b), "a reversed interval is rejected");
    check(a == -7 && b == -7, "still untouched after a reversed interval");

    // A -1 on EITHER side fails: the orr/cmn/ccmp idiom ORs the two results.
    check(!typeriser::parseTimingLine("not a time --> 00:00:02,000", &a, &b), "a bad start fails the line");
    check(!typeriser::parseTimingLine("00:00:01,000 --> not a time", &a, &b), "a bad end fails the line");
    check(!typeriser::parseTimingLine("00:00:01,000 --> 00:00:0", &a, &b), "a truncated end fails the line");

    // Structural rejections.
    check(!typeriser::parseTimingLine("", &a, &b), "empty input is rejected");
    check(!typeriser::parseTimingLine("-->", &a, &b), "a bare arrow has no timestamps");
    check(!typeriser::parseTimingLine("00:00:01,000", &a, &b), "a line with no arrow is rejected");
    check(!typeriser::parseTimingLine("00:00:01,000 -> 00:00:02,000", &a, &b), "a 2-character arrow is not enough");
    check(!typeriser::parseTimingLine("00:00:01,000 -- 00:00:02,000", &a, &b), "a truncated arrow is not enough");

    // The left side is NOT cut at a space, so a prefixed start fails rather than
    // silently parsing the first token. This is the asymmetry with the right.
    check(!typeriser::parseTimingLine("X1:1 00:00:01,000 --> 00:00:02,000", &a, &b),
          "a prefix on the LEFT is not trimmed away (only the right side is space-cut)");
  }


  std::printf("typeriser::parseSrt\n");
  {
    // A well-formed two-cue document, LF, with ordinals.
    const std::string two =
        "1\n00:00:01,000 --> 00:00:02,500\nHello\n\n"
        "2\n00:00:03,000 --> 00:00:04,000\nWorld\n";
    auto cues = typeriser::parseSrt(two);
    check(cues.size() == 2, "a two-cue document yields two cues");
    check(cues[0].index == 1 && cues[0].start == 1000 && cues[0].end == 2500,
          "the first cue carries its ordinal and its interval");
    check(cues[0].text == "Hello", "the first cue carries its text");
    check(cues[1].index == 2 && cues[1].start == 3000 && cues[1].text == "World",
          "the second cue parses independently");

    // Cue layout: 48 bytes, with index at 0 and text at 0x18.
    check(sizeof(typeriser::Cue) == 48, "Cue stays the 48-byte stride cueIndexAt uses");
    check(offsetof(typeriser::Cue, start) == 0x8 && offsetof(typeriser::Cue, end) == 0x10,
          "start/end stay at the offsets cueIndexAt reads");
    check(offsetof(typeriser::Cue, text) == 0x18,
          "text stays at the offset parseSrt's operator= writes");

    // Line endings. The CR branch consumes a following LF, so CRLF is ONE break.
    auto crlf = typeriser::parseSrt("1\r\n00:00:01,000 --> 00:00:02,500\r\nHi\r\n\r\n");
    check(crlf.size() == 1 && crlf[0].text == "Hi", "CRLF yields one break, not two");

    auto cr = typeriser::parseSrt("1\r00:00:01,000 --> 00:00:02,500\rHi\r\r");
    check(cr.size() == 1 && cr[0].text == "Hi", "a bare CR also terminates a line");

    // Mixed endings in one document.
    auto mixed = typeriser::parseSrt(
        "1\r\n00:00:01,000 --> 00:00:02,000\r\nA\r\n\r\n"
        "2\n00:00:03,000 --> 00:00:04,000\nB\n");
    check(mixed.size() == 2 && mixed[0].text == "A" && mixed[1].text == "B",
          "LF and CRLF can be mixed in one document");

    // UTF-8 BOM is stripped before anything else.
    auto bom = typeriser::parseSrt("\xEF\xBB\xBF" "1\n00:00:01,000 --> 00:00:02,000\nB\n");
    check(bom.size() == 1 && bom[0].index == 1,
          "a leading UTF-8 BOM does not corrupt the ordinal line");

    // The ordinal line is optional; a bare timing line still parses, with index 0.
    auto noidx = typeriser::parseSrt("00:00:01,000 --> 00:00:02,000\nNo index\n");
    check(noidx.size() == 1 && noidx[0].index == 0 && noidx[0].start == 1000,
          "a missing ordinal line leaves index at 0");

    // Multi-line text is joined with '\n', each line trimmed individually.
    auto multi = typeriser::parseSrt(
        "1\n00:00:01,000 --> 00:00:02,500\n  Line one  \n\tLine two\n\n");
    check(multi.size() == 1 && multi[0].text == "Line one\nLine two",
          "text lines are trimmed and joined with a single newline");

    // A trailing newline produces one extra EMPTY line, which must not become a cue.
    check(multi.size() == 1, "the extra trailing empty line does not create a cue");

    // A block with a timing line but no text is DROPPED.
    auto notext = typeriser::parseSrt(
        "1\n00:00:01,000 --> 00:00:02,500\n\n2\n00:00:03,000 --> 00:00:04,000\nKept\n");
    check(notext.size() == 1 && notext[0].index == 2,
          "a cue with empty text is dropped entirely");

    // Whitespace-only text counts as empty too (the test is on the TRIMMED text).
    auto blank = typeriser::parseSrt("1\n00:00:01,000 --> 00:00:02,500\n   \n");
    check(blank.empty(), "whitespace-only text is treated as empty");

    // Output is STABLE-sorted by start; equal starts keep document order.
    auto sorted = typeriser::parseSrt(
        "1\n00:00:09,000 --> 00:00:10,000\nLate\n\n"
        "2\n00:00:01,000 --> 00:00:02,000\nEarly\n");
    check(sorted.size() == 2 && sorted[0].text == "Early" && sorted[1].text == "Late",
          "cues come back sorted by start regardless of document order");

    auto tied = typeriser::parseSrt(
        "7\n00:00:01,000 --> 00:00:02,000\nFirst\n\n"
        "8\n00:00:01,000 --> 00:00:03,000\nSecond\n");
    check(tied.size() == 2 && tied[0].index == 7 && tied[1].index == 8,
          "cues sharing a start keep document order (stable, not just sorted)");

    // Malformed blocks are skipped, and parsing RECOVERS at the next line.
    auto bad = typeriser::parseSrt(
        "1\nGARBAGE\nText\n\n2\n00:00:03,000 --> 00:00:04,000\nGood\n");
    check(bad.size() == 1 && bad[0].index == 2 && bad[0].text == "Good",
          "a malformed block is skipped and the next block still parses");

    // A block with an ordinal but no timing line at all.
    auto notiming = typeriser::parseSrt("1\nJust text, no times\n");
    check(notiming.empty(), "an ordinal with no timing line yields no cue");

    // Degenerate inputs never throw and are all empty.
    check(typeriser::parseSrt("").empty(), "empty input yields no cues");
    check(typeriser::parseSrt("\n\n\n").empty(), "blank lines alone yield no cues");
    check(typeriser::parseSrt("-->").empty(), "a bare arrow yields no cues");

    // Input without a trailing newline keeps its last line.
    auto tail = typeriser::parseSrt("1\n00:00:01,000 --> 00:00:02,000\nTail");
    check(tail.size() == 1 && tail[0].text == "Tail",
          "the final line is flushed even with no trailing newline");

    // The caller's string is not mutated (parseSrt copies before stripping).
    std::string original = "\xEF\xBB\xBF" "1\n00:00:01,000 --> 00:00:02,000\nX\n";
    const std::string before = original;
    (void)typeriser::parseSrt(original);
    check(original == before, "the input string is left untouched (including its BOM)");

    // A realistic multi-cue document, exercising the whole path at once.
    auto real = typeriser::parseSrt(
        "1\n00:00:00,500 --> 00:00:02,000\nIt was the best of times\n\n"
        "2\n00:00:02,100 --> 00:00:04,000\nit was the worst of times\n\n"
        "3\n00:00:04,200 --> 00:00:06,000\nit was the age of wisdom\n");
    check(real.size() == 3, "a realistic three-cue document parses completely");
    check(real[0].start == 500 && real[2].end == 6000, "its outer bounds are correct");
    check(real[1].text == "it was the worst of times", "its middle text is intact");
  }


  std::printf("typeriser::wrapWords\n");
  {
    // A naive reference written the way the DOC describes the rule, so the
    // compiled version is checked against prose rather than against itself.
    auto naive = [](const std::vector<float>& w, float sep, float maxw) {
      std::vector<int> out(w.size(), 0);
      if (w.empty()) return out;
      float running = w[0];
      int line = 0;
      for (size_t i = 1; i < w.size(); ++i) {
        float next = sep + running + w[i];
        if (next > maxw) { running = w[i]; ++line; }
        else { running = next; }
        out[i] = line;
      }
      return out;
    };
    auto same = [](const std::vector<int>& a, const std::vector<int>& b) {
      return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
    };

    // Exact cases.
    check(typeriser::wrapWords({}, 1.f, 10.f).empty(), "no words -> no lines");
    check(same(typeriser::wrapWords({5.f}, 1.f, 10.f), {0}), "one word is always line 0");

    auto two = typeriser::wrapWords({6.f, 6.f}, 1.f, 10.f);
    check(same(two, {0, 1}), "6 + sep 1 + 6 = 13 > 10, so the second word wraps");

    auto tight = typeriser::wrapWords({5.f, 4.f}, 1.f, 10.f);
    check(same(tight, {0, 0}), "5 + sep 1 + 4 = 10 is NOT > 10, so it stays (strict >)");

    auto over = typeriser::wrapWords({5.f, 4.5f}, 1.f, 10.f);
    check(same(over, {0, 1}), "10.5 > 10 does wrap — the boundary case either side");

    auto three = typeriser::wrapWords({3.f, 3.f, 3.f}, 1.f, 7.f);
    check(same(three, {0, 0, 1}), "3+1+3=7 fits, then 7+1+3=11 wraps");

    // The reset drops the separator: after a break the first word on the new
    // line is measured ALONE. 8 + sep 1 + 3 would be 12 > 10, but 3 fits.
    auto reset = typeriser::wrapWords({8.f, 3.f, 3.f}, 1.f, 10.f);
    check(same(reset, {0, 1, 1}), "after a break the new line measures the word alone");

    // A zero separator degenerates to a plain cumulative sum.
    auto nosep = typeriser::wrapWords({4.f, 4.f, 4.f}, 0.f, 10.f);
    check(same(nosep, {0, 0, 1}), "separator 0: 4+4 fits in 10, +4 does not");

    // Property sweep against the naive reference.
    bool all_agree = true;
    size_t cases = 0;
    for (float sep : {0.f, 0.5f, 1.f, 3.f}) {
      for (float maxw : {4.f, 10.f, 25.f, 100.f}) {
        for (size_t n = 0; n <= 24; ++n) {
          std::vector<float> w;
          for (size_t i = 0; i < n; ++i)
            w.push_back(1.f + static_cast<float>((i * 7 + n * 3) % 9));
          ++cases;
          if (!same(typeriser::wrapWords(w, sep, maxw), naive(w, sep, maxw)))
            all_agree = false;
        }
      }
    }
    check(all_agree, "wrapWords agrees with the naive rule across 400 input shapes");
    check(cases == 400, "the property sweep actually ran 400 cases");

    // Monotonicity: line indices never go backwards.
    bool monotone = true;
    for (size_t n = 1; n <= 40; ++n) {
      std::vector<float> w(n, 2.f);
      auto got = typeriser::wrapWords(w, 1.f, 9.f);
      for (size_t i = 1; i < got.size(); ++i)
        if (got[i] < got[i - 1]) monotone = false;
    }
    check(monotone, "line indices are non-decreasing");
  }


  std::printf("typeriser::wordStyle\n");
  {
    using typeriser::Anim;
    using typeriser::Easing;
    const float D = 40.0f;  // an arbitrary offsetDistance

    // Helper: run at progress p with no stagger, animDuration 1000.
    auto at = [&](Anim a, Easing ez, float p) {
      return typeriser::wordStyle(a, ez, 0, 1000,
                                  static_cast<long long>(p * 1000.0f),
                                  0, 1, 0.f, 1000.f, D);
    };

    // ---- Anim 0 is the identity, and so is anything outside 1..4.
    auto none = at(Anim::None, Easing::EaseOutQuad, 0.25f);
    check(none.alpha == 1.f && none.scale == 1.f && none.offset == 0.f &&
          none.highlight == 0,
          "Anim::None is the identity regardless of easing");
    auto bogus = at(static_cast<Anim>(99), Easing::EaseOutBack, 0.25f);
    check(bogus.alpha == 1.f && bogus.scale == 1.f,
          "an out-of-range Anim is also the identity");

    // ---- Pop (1): scale 0.3 -> 1.0 while alpha ramps at 2x.
    auto pop0 = at(Anim::Pop, Easing::LinearA, 0.f);
    check(pop0.alpha == 0.f && pop0.scale == 0.3f, "Pop starts at alpha 0, scale 0.3");
    auto pop5 = at(Anim::Pop, Easing::LinearA, 0.5f);
    check(pop5.alpha == 1.f && std::fabs(pop5.scale - 0.65f) < 1e-6f,
          "Pop at half progress: alpha already 1 (2x), scale 0.65");
    auto pop1 = at(Anim::Pop, Easing::LinearA, 1.f);
    check(pop1.alpha == 1.f && std::fabs(pop1.scale - 1.f) < 1e-6f,
          "Pop ends at alpha 1, scale 1.0");

    // ---- Fade (2): alpha only.
    auto fade5 = at(Anim::Fade, Easing::LinearA, 0.5f);
    check(fade5.alpha == 0.5f && fade5.scale == 1.f && fade5.offset == 0.f,
          "Fade moves alpha only");

    // ---- Rise (3): alpha at 1.5x, offset decaying to 0.
    auto rise0 = at(Anim::Rise, Easing::LinearA, 0.f);
    check(rise0.alpha == 0.f && std::fabs(rise0.offset - D) < 1e-5f,
          "Rise starts fully offset and transparent");
    auto rise5 = at(Anim::Rise, Easing::LinearA, 0.5f);
    check(std::fabs(rise5.alpha - 0.75f) < 1e-6f,
          "Rise alpha ramps at 1.5x");
    auto rise1 = at(Anim::Rise, Easing::LinearA, 1.f);
    check(rise1.alpha == 1.f && std::fabs(rise1.offset) < 1e-5f,
          "Rise lands at full alpha and zero offset");
    auto riseLate = at(Anim::Rise, Easing::LinearA, 0.9f);
    check(riseLate.alpha == 1.f, "Rise alpha saturates at 1.0 well before the end");

    // ---- Karaoke (4): no easing; the word under the playhead is found by time.
    auto kara = typeriser::wordStyle(Anim::Karaoke, Easing::EaseOutQuad,
                                     0, 1000, 500, 2, 4, 0.f, 1000.f, D);
    check(kara.alpha == 1.f && kara.scale == 1.08f && kara.highlight == 1,
          "the karaoke word under the playhead is scaled up and flagged");
    check(kara.offset == D, "karaoke passes offsetDistance through unchanged");

    auto karaOther = typeriser::wordStyle(Anim::Karaoke, Easing::EaseOutQuad,
                                          0, 1000, 500, 1, 4, 0.f, 1000.f, D);
    check(karaOther.scale == 1.f && karaOther.highlight == 0,
          "a karaoke word NOT under the playhead is left alone");

    // now = 500 over 1000 ms with 4 words -> idx = 500*4/1000 = 2.
    check(kara.highlight == 1 && karaOther.highlight == 0,
          "the karaoke index is derived from elapsed time, not word order");

    auto karaStart = typeriser::wordStyle(Anim::Karaoke, Easing::LinearA,
                                          0, 1000, 0, 0, 4, 0.f, 1000.f, D);
    check(karaStart.highlight == 1, "at t=start the FIRST word is active");

    auto karaEnd = typeriser::wordStyle(Anim::Karaoke, Easing::LinearA,
                                        0, 1000, 1000, 3, 4, 0.f, 1000.f, D);
    check(karaEnd.highlight == 0 && karaEnd.scale == 1.f,
          "at t=end no word is active (the guard is a strict now < end)");

    auto karaPast = typeriser::wordStyle(Anim::Karaoke, Easing::LinearA,
                                         0, 1000, 5000, 0, 4, 0.f, 1000.f, D);
    check(karaPast.highlight == 0, "past the cue, no word is active");

    // The index clamps to wordCount - 1 rather than running past the last word.
    auto kara999 = typeriser::wordStyle(Anim::Karaoke, Easing::LinearA,
                                        0, 1000, 999, 3, 4, 0.f, 1000.f, D);
    check(kara999.highlight == 1, "the karaoke index clamps to the last word");

    // wordCount 0 must not divide by zero or index anything.
    auto karaZero = typeriser::wordStyle(Anim::Karaoke, Easing::LinearA,
                                         0, 1000, 500, 0, 0, 0.f, 1000.f, D);
    check(karaZero.highlight == 0, "wordCount 0 activates nothing and does not trap");

    // ---- Degenerate animDuration.
    auto degPos = typeriser::wordStyle(Anim::Fade, Easing::LinearA,
                                       0, 1000, 500, 0, 1, 0.f, 0.f, D);
    check(degPos.alpha == 1.f, "animDuration 0 with time elapsed -> fully on");
    auto degNeg = typeriser::wordStyle(Anim::Fade, Easing::LinearA,
                                       0, 1000, -500, 0, 1, 0.f, 0.f, D);
    check(degNeg.alpha == 0.f, "animDuration 0 before the word starts -> fully off");

    // ---- Progress clamps low, so a word before its own stagger time is off.
    auto early = typeriser::wordStyle(Anim::Pop, Easing::LinearA,
                                      0, 1000, 100, 3, 6, 500.f, 1000.f, D);
    check(early.alpha == 0.f && early.scale == 0.3f,
          "a word before its stagger offset has not started");

    // ---- Stagger shifts the word's own start.
    auto stag = typeriser::wordStyle(Anim::Fade, Easing::LinearA,
                                     0, 10000, 1100, 2, 6, 500.f, 1000.f, D);
    // starts at 2*500 = 1000, so progress = (1100-1000)/1000 = 0.1
    check(std::fabs(stag.alpha - 0.1f) < 1e-6f, "wordStagger shifts the per-word start");

    // ---- The easing curves here duplicate ease()'s maths. Comparing them is a
    // cross-check of two independently recovered implementations: ease() came
    // out of its own switch, and these curves are written out inline in
    // wordStyle. Agreement means both transcriptions are right.
    //
    // The comparison has to account for ONE real difference: wordStyle clamps
    // its ALPHA fields to <= 1.0 (0x1e90..0x1ea0 is `fcmp`/`fcsel :gt`/`fcsel
    // :mi` on the Anim 2 path, and every other Anim path clamps the same way),
    // while ease() returns the raw curve. That only matters for EaseOutBack,
    // which overshoots past 1.0 on its way back down.
    const Easing curves[] = {Easing::LinearA, Easing::EaseOutQuad, Easing::EaseOutCubic,
                             Easing::EaseOutBack, Easing::EaseInOutCubic};
    bool curves_agree = true;
    for (Easing ez : curves) {
      for (int k = 0; k <= 20; ++k) {
        const float p = static_cast<float>(k) / 20.0f;
        const float raw = typeriser::ease(ez, p);
        const float want = raw > 1.0f ? 1.0f : raw;   // the alpha clamp
        const float got = typeriser::wordStyle(Anim::Fade, ez, 0, 1000,
                                               static_cast<long long>(p * 1000.0f),
                                               0, 1, 0.f, 1000.f, D).alpha;
        if (std::fabs(got - want) > 1e-4f) curves_agree = false;
      }
    }
    check(curves_agree,
          "wordStyle's inline curves match ease(), modulo its alpha clamp");

    // ---- EaseOutBack overshoots to 1.0993 at t=0.6. That overshoot is INVISIBLE
    // in alpha (clamped), so its only visible effect is through Pop's SCALE,
    // which is not clamped. This is the one place the overshoot survives.
    check(std::fabs(typeriser::ease(Easing::EaseOutBack, 0.6f) - 1.09935164f) < 1e-5f,
          "EaseOutBack overshoots past 1.0");
    auto backFade = at(Anim::Fade, Easing::EaseOutBack, 0.6f);
    check(backFade.alpha == 1.f, "the overshoot is clamped out of alpha");
    auto backPop = at(Anim::Pop, Easing::EaseOutBack, 0.6f);
    check(backPop.scale > 1.f,
          "but Pop's scale carries the overshoot, so the pop really overshoots");

    // ---- WordStyle layout: 16 bytes, three floats then an int.
    check(sizeof(typeriser::WordStyle) == 16,
          "WordStyle is the 16 bytes the caller reads out of x0:x1");
  }


  std::printf("typeriser::splitWords\n");
  {
    auto eq = [](const std::vector<std::string>& got, std::initializer_list<const char*> want) {
      if (got.size() != want.size()) return false;
      size_t i = 0;
      for (const char* w : want) if (got[i++] != w) return false;
      return true;
    };

    check(eq(typeriser::splitWords("hello world"), {"hello", "world"}), "two words");
    check(eq(typeriser::splitWords("one"), {"one"}), "a single word with no trailing space is KEPT (final flush)");
    // Empty runs collapse rather than producing empty words.
    check(eq(typeriser::splitWords("a  b"), {"a", "b"}), "a run of spaces yields no empty word");
    check(eq(typeriser::splitWords("a\t\nb"), {"a", "b"}), "tabs and newlines also separate");
    check(eq(typeriser::splitWords("  a  "), {"a"}), "leading and trailing whitespace produce no empty words");
    check(eq(typeriser::splitWords("   "), {}), "all whitespace yields nothing");
    check(eq(typeriser::splitWords(""), {}), "empty input yields nothing");
    // A realistic SRT cue line.
    check(eq(typeriser::splitWords("It was the best of times"),
             {"It", "was", "the", "best", "of", "times"}),
          "a realistic caption line splits into six words");
    // Punctuation is NOT a separator: this splits on whitespace only.
    check(eq(typeriser::splitWords("well-known, isn't"), {"well-known,", "isn't"}),
          "punctuation stays attached (whitespace-only splitting)");
    // Non-ASCII goes through the __maskrune path; an accented byte is not a space.
    check(eq(typeriser::splitWords("caf\xc3\xa9 noir"), {"caf\xc3\xa9", "noir"}),
          "a non-ASCII byte is not treated as whitespace");
  }

  std::printf("\n%s\n", failures == 0 ? "ALL RECOVERED TYPERISER MATHS VERIFIED"
                                      : "TRANSCRIPTION ERRORS PRESENT");
  return failures == 0 ? 0 : 1;
}
