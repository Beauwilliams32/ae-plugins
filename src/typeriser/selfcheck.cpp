// selfcheck.cpp — verification for the recovered typeriser timing core.

#include "typeriser.h"

#include <cmath>
#include <cstdio>
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

  std::printf("\n%s\n", failures == 0 ? "ALL RECOVERED TYPERISER MATHS VERIFIED"
                                      : "TRANSCRIPTION ERRORS PRESENT");
  return failures == 0 ? 0 : 1;
}
