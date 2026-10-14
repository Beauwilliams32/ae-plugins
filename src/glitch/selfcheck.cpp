// Self-check for the recovered glitch:: namespace.
//
// Like the other namespaces: every assertion below is anchored to something the
// disassembly actually establishes, not to what the maths "ought" to do.

#include "glitch.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;
int passes = 0;

void check(bool ok, const char* why) {
  if (ok) { ++passes; std::printf("  PASS %s\n", why); }
  else    { ++failures; std::printf("  FAIL %s\n", why); }
}

void checkEqI(int got, int want, const char* why) {
  if (got == want) { ++passes; std::printf("  PASS %s\n", why); }
  else { ++failures; std::printf("  FAIL %s  (got %d want %d)\n", why, got, want); }
}

void checkEqU(unsigned got, unsigned want, const char* why) {
  if (got == want) { ++passes; std::printf("  PASS %s\n", why); }
  else { ++failures; std::printf("  FAIL %s  (got %u want %u)\n", why, got, want); }
}

}  // namespace

int main() {
  std::printf("glitch::clampf / clampi\n");
  {
    check(glitch::clampf(5.0f, 0.0f, 10.0f) == 5.0f, "in range passes through");
    check(glitch::clampf(-1.0f, 0.0f, 10.0f) == 0.0f, "below lo clamps to lo");
    check(glitch::clampf(11.0f, 0.0f, 10.0f) == 10.0f, "above hi clamps to hi");
    // The low bound is tested LAST, so it wins on an inverted range. This is the
    // detail shared with the other three clampf copies in the fleet.
    check(glitch::clampf(5.0f, 10.0f, 0.0f) == 10.0f,
          "an INVERTED range yields lo, because lo is tested last");

    checkEqI(glitch::clampi(5, 0, 10), 5, "clampi in range");
    checkEqI(glitch::clampi(-1, 0, 10), 0, "clampi below lo");
    checkEqI(glitch::clampi(11, 0, 10), 10, "clampi above hi");
    checkEqI(glitch::clampi(5, 10, 0), 10, "clampi also yields lo on an inverted range");
    // Integer extremes must not overflow or wrap.
    checkEqI(glitch::clampi(2147483647, 0, 100), 100, "clampi handles INT_MAX");
    checkEqI(glitch::clampi(-2147483647 - 1, 0, 100), 0, "clampi handles INT_MIN");
  }

  std::printf("glitch::patternFrame\n");
  {
    // seed * time, truncated. The multiply is in double, so a large seed with a
    // fractional time does not round the way a float multiply would.
    checkEqU(glitch::patternFrame(1000u, 0.5f), 500u, "seed * time, truncated");
    checkEqU(glitch::patternFrame(1000u, 2.999f), 2999u, "truncation, not rounding");
    checkEqU(glitch::patternFrame(7u, 1.0f), 7u, "one frame in");

    // The guard is a STRICT > 0.
    checkEqU(glitch::patternFrame(1000u, 0.0f), 0u, "time 0 yields 0, not the seed");
    checkEqU(glitch::patternFrame(1000u, -1.0f), 0u, "negative time yields 0");
    checkEqU(glitch::patternFrame(0u, 5.0f), 0u, "seed 0 yields 0 at any time");

    // Double, not float: 16777217 cannot be represented in float, so a float
    // multiply would round it and pick a different frame.
    const unsigned big = glitch::patternFrame(16777217u, 1.0f);
    checkEqU(big, 16777217u, "the multiply really is in double (16777217 survives)");

    // Monotone in time for a fixed seed.
    bool mono = true;
    unsigned prev = 0;
    for (int i = 0; i <= 40; ++i) {
      const unsigned f = glitch::patternFrame(500u, static_cast<float>(i) * 0.7f);
      if (f < prev) mono = false;
      prev = f;
    }
    check(mono, "patternFrame is non-decreasing in time");
  }

  std::printf("glitch::hashKey\n");
  {
    // Determinism is the effect's headline promise.
    checkEqU(glitch::hashKey(1u, 2u, 3u), glitch::hashKey(1u, 2u, 3u),
             "same inputs give the same hash");
    check(glitch::hashKey(1u, 2u, 3u) != glitch::hashKey(3u, 2u, 1u),
          "argument order matters");

    // Avalanche: a one-bit change in any input should change roughly half the
    // output bits, and must not leave the output unchanged.
    auto popcount = [](unsigned v) { int n = 0; while (v) { n += v & 1u; v >>= 1; } return n; };
    int minFlips = 32, totalFlips = 0, cases = 0;
    for (unsigned a = 0; a < 64; ++a) {
      for (unsigned b = 0; b < 4; ++b) {
        const unsigned base = glitch::hashKey(a, b, 0u);
        for (int bit = 0; bit < 32; bit += 7) {
          const unsigned alt = glitch::hashKey(a ^ (1u << bit), b, 0u);
          const int flips = popcount(base ^ alt);
          if (flips < minFlips) minFlips = flips;
          totalFlips += flips;
          ++cases;
        }
      }
    }
    check(cases > 0, "the avalanche sweep ran");
    check(minFlips > 0, "every single-bit input change changes the output");
    const double avgBits = static_cast<double>(totalFlips) / static_cast<double>(cases);
    check(avgBits > 8.0 && avgBits < 24.0,
          "single-bit changes flip a plausible number of output bits");

    // The `csinc ... ne` tail: the result is never left at 0 by the guard's
    // "equal" branch. Scanning a spread of inputs, no output is 0 unless the
    // mixed value genuinely was.
    bool anyZero = false;
    for (unsigned a = 0; a < 2000; ++a) {
      if (glitch::hashKey(a, a * 7u, a * 13u) == 0u) anyZero = true;
    }
    check(!anyZero, "the guard keeps the result away from a run of zeros");

    // Determinism across a wider sweep.
    bool stable = true;
    for (unsigned a = 0; a < 500; ++a) {
      if (glitch::hashKey(a, 1u, 2u) != glitch::hashKey(a, 1u, 2u)) stable = false;
    }
    check(stable, "hashKey is stable across 500 inputs");
  }

  std::printf("\n%s\n", failures == 0
                ? "ALL RECOVERED GLITCH MATHS VERIFIED"
                : "TRANSCRIPTION ERRORS PRESENT");
  return failures == 0 ? 0 : 1;
}
