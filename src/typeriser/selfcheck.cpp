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
