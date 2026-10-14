// Self-check for the recovered beatgrid:: namespace.
//
// The return types are pinned here, not just the values: mangling carries parameter
// types but never the return type, so `phaseSawtooth` returning a float where every one
// of its arguments is a double is a claim that only the instruction stream and a
// compile-time check can establish.

#include "beatgrid.h"

#include <cmath>
#include <cstdio>
#include <type_traits>

namespace {

int failures = 0;
int passes = 0;

void check(bool ok, const char* why) {
  if (ok) { ++passes; std::printf("  PASS %s\n", why); }
  else    { ++failures; std::printf("  FAIL %s\n", why); }
}

void checkNear(double got, double want, double tol, const char* why) {
  if (std::fabs(got - want) <= tol) { ++passes; std::printf("  PASS %s\n", why); }
  else { ++failures; std::printf("  FAIL %s  (got %.9g want %.9g)\n", why, got, want); }
}

}  // namespace

// ---------------------------------------------------------------------------
// Return types, which the symbol table does not give.
//
// `phaseSawtooth` ends in `fcvt s0, d0` and `phaseInLayerTime` ends in an integer-to-FP
// move into v0: the first narrows to float, the second does not. `frameTimeSeconds`
// returns in d0, and `subdivisionFactor` in d0 as well.
// ---------------------------------------------------------------------------

static_assert(std::is_same_v<decltype(beatgrid::phaseSawtooth(120.0, 0.0, beatgrid::Subdivision::Beat, 0.0)), float>,
              "phaseSawtooth returns a FLOAT despite taking four doubles");
static_assert(std::is_same_v<decltype(beatgrid::phaseInLayerTime(120.0, 0.0, 0.0, 0.0)), double>,
              "phaseInLayerTime returns a double");
static_assert(std::is_same_v<decltype(beatgrid::subdivisionFactor(beatgrid::Subdivision::Beat)), double>,
              "subdivisionFactor returns a double");
static_assert(std::is_same_v<decltype(std::declval<const beatgrid::OnsetEnvelope&>().frameTimeSeconds(0)), double>,
              "frameTimeSeconds returns a double");

int main() {
  std::printf("beatgrid::subdivisionFactor\n");
  {
    // The four table entries, read out of the literal pool at 0x77e0 rather than guessed.
    checkNear(beatgrid::subdivisionFactor(beatgrid::Subdivision::Half), 0.5, 0.0, "value 2 is half the beat");
    checkNear(beatgrid::subdivisionFactor(beatgrid::Subdivision::Quarter), 0.25, 0.0, "value 3 is a quarter");
    checkNear(beatgrid::subdivisionFactor(beatgrid::Subdivision::Eighth), 0.125, 0.0, "value 4 is an eighth");
    // NOT a fraction: the fourth entry is 4.0. A guess from the first three would make
    // it a sixteenth.
    checkNear(beatgrid::subdivisionFactor(beatgrid::Subdivision::FourBeats), 4.0, 0.0,
              "value 5 is FOUR beats, not a sixteenth — the table's odd one out");

    // The default, reached by everything else. The index is computed as (value - 2)
    // UNSIGNED, so a negative enumerator wraps to a huge index and lands here too.
    checkNear(beatgrid::subdivisionFactor(beatgrid::Subdivision::Beat), 1.0, 0.0, "value 0 is one beat");
    checkNear(beatgrid::subdivisionFactor(static_cast<beatgrid::Subdivision>(1)), 1.0, 0.0, "value 1 is one beat");
    checkNear(beatgrid::subdivisionFactor(static_cast<beatgrid::Subdivision>(6)), 1.0, 0.0, "value 6 falls through");
    checkNear(beatgrid::subdivisionFactor(static_cast<beatgrid::Subdivision>(-1)), 1.0, 0.0,
              "a NEGATIVE enumerator is out of the table's range and takes the default");
  }

  std::printf("beatgrid::phaseSawtooth\n");
  {
    const auto Beat = beatgrid::Subdivision::Beat;
    const auto Half = beatgrid::Subdivision::Half;
    const auto Four = beatgrid::Subdivision::FourBeats;

    // 120 bpm is a 0.5 s beat.
    checkNear(beatgrid::phaseSawtooth(120.0, 0.0, Beat, 0.0), 0.0, 1e-6, "at the start the phase is 0");
    checkNear(beatgrid::phaseSawtooth(120.0, 0.0, Beat, 0.25), 0.5, 1e-6, "half a beat in the phase is 0.5");
    checkNear(beatgrid::phaseSawtooth(120.0, 0.0, Beat, 0.5), 0.0, 1e-6, "one whole beat wraps back to 0");

    // The subdivision scales the BEAT, so half a beat is a 0.25 s period here.
    checkNear(beatgrid::phaseSawtooth(120.0, 0.0, Half, 0.125), 0.5, 1e-6,
              "with a half subdivision the period is 0.25 s, so 0.125 s is half way");
    checkNear(beatgrid::phaseSawtooth(120.0, 0.0, Half, 0.25), 0.0, 1e-6, "and 0.25 s wraps");

    // Four beats: a 2 s period at 120 bpm.
    checkNear(beatgrid::phaseSawtooth(120.0, 0.0, Four, 1.0), 0.5, 1e-6, "four beats at 120 bpm is a 2 s period");

    // A time BEFORE the start wraps UP rather than going negative.
    checkNear(beatgrid::phaseSawtooth(120.0, 1.0, Beat, 0.875), 0.75, 1e-6,
              "a time before the start wraps up: -0.125 of 0.5 is 0.75");
    check(beatgrid::phaseSawtooth(120.0, 1.0, Beat, 0.875) >= 0.0f, "and is never negative");

    // Guards: a non-positive tempo returns zero rather than dividing by it.
    check(beatgrid::phaseSawtooth(0.0, 0.0, Beat, 1.0) == 0.0f, "bpm 0 returns 0");
    check(beatgrid::phaseSawtooth(-60.0, 0.0, Beat, 1.0) == 0.0f, "a negative bpm returns 0");

    // The phase is bounded to [0, 1) whatever the inputs.
    bool bounded = true;
    for (int i = 0; i < 500; ++i) {
      const double t = -3.0 + 0.0131 * i;
      const float p = beatgrid::phaseSawtooth(97.5, 0.37, Half, t);
      if (!(p >= 0.0f && p < 1.0f)) bounded = false;
    }
    check(bounded, "the phase stays inside [0, 1) across a sweep of times");
  }

  std::printf("beatgrid::phaseInLayerTime\n");
  {
    // IT RETURNS SECONDS, NOT A FRACTION. At 120 bpm the beat is 0.5 s, so a quarter of
    // a second into it is 0.25 — not 0.5. This is the opposite convention to
    // phaseSawtooth, which divides by the period; a first version of these two
    // expectations was written as if this one divided too.
    checkNear(beatgrid::phaseInLayerTime(120.0, 0.25, 0.0, 0.0), 0.25, 1e-9,
              "0.25 s into a 0.5 s beat is 0.25 SECONDS, not half");
    checkNear(beatgrid::phaseInLayerTime(120.0, 0.3, 0.2, 0.0), 0.0, 1e-9,
              "the three offsets are summed first: 0.3 + 0.2 is one whole beat, which wraps to 0");
    checkNear(beatgrid::phaseInLayerTime(120.0, 0.0, 0.0, 0.25), 0.25, 1e-9,
              "the fourth argument is SUBTRACTED last: -0.25 of 0.5 wraps to 0.25");
    check(beatgrid::phaseInLayerTime(0.0, 1.0, 1.0, 0.5) == 0.0, "bpm 0 returns 0");
    check(beatgrid::phaseInLayerTime(-120.0, 1.0, 1.0, 0.5) == 0.0, "a negative bpm returns 0");

    bool bounded = true;
    for (int i = 0; i < 500; ++i) {
      const double p = beatgrid::phaseInLayerTime(133.0, 0.11, 0.42, -3.0 + 0.0131 * i);
      if (!(p >= 0.0 && p < 60.0 / 133.0)) bounded = false;
    }
    check(bounded, "the phase stays inside one beat across a sweep");
  }

  std::printf("beatgrid::OnsetEnvelope::frameTimeSeconds\n");
  {
    beatgrid::OnsetEnvelope env{};
    env.hopInterval = 20.0;
    env.startFrameHalf = 4;
    env.step = 2;

    // (frame * step + startHalf / 2) / hopInterval = (6 + 2) / 20
    checkNear(env.frameTimeSeconds(3), 0.4, 1e-12, "frame 3 with step 2 and start 4/2 is 0.4 s");
    checkNear(env.frameTimeSeconds(0), 0.1, 1e-12, "frame 0 is the start alone: 2/20");

    // THE HALF-FRAME SCALE. `startFrameHalf` counts half frames — the compiler emits
    // `scvtf d3, w8, #0x1`, a fixed-point convert with one fractional bit — so setting
    // it to 1 contributes 0.5 frames, not 1.
    beatgrid::OnsetEnvelope half{};
    half.hopInterval = 20.0;
    half.startFrameHalf = 1;
    half.step = 0;
    checkNear(half.frameTimeSeconds(0), 0.025, 1e-12, "a startFrameHalf of 1 contributes HALF a frame (0.5/20)");

    // A negative step is carried through with its sign.
    beatgrid::OnsetEnvelope backwards{};
    backwards.hopInterval = 10.0;
    backwards.startFrameHalf = 0;
    backwards.step = -3;
    checkNear(backwards.frameTimeSeconds(4), -1.2, 1e-12, "a negative step moves backwards in time");

    // The guard, which returns zero rather than dividing.
    beatgrid::OnsetEnvelope zero{};
    zero.hopInterval = 0.0;
    zero.step = 5;
    check(zero.frameTimeSeconds(100) == 0.0, "hopInterval 0 returns 0");
    zero.hopInterval = -1.0;
    check(zero.frameTimeSeconds(100) == 0.0, "a negative hopInterval returns 0");
  }

  std::printf("the shared DSP, read through this namespace's copy\n");
  {
    check(beatgrid::clampf(5.0f, 10.0f, 0.0f) == 10.0f, "clampf keeps the fleet's inverted-range behaviour");
    check(beatgrid::isPowerOfTwo(1024) && !beatgrid::isPowerOfTwo(0), "isPowerOfTwo guards on > 0");
    const float stereo[4] = {1.0f, 3.0f, 1.0f, 3.0f};
    const std::vector<float> mono = beatgrid::downmixToMono(stereo, 2, 2);
    check(mono.size() == 2 && mono[0] == 2.0f, "downmixToMono AVERAGES: stereo {1,3} is 2, not 4");
  }

  if (failures == 0) std::printf("ALL RECOVERED BEATGRID MATHS VERIFIED (%d checks)\n", passes);
  else std::printf("%d FAILURES out of %d checks\n", failures, passes + failures);
  return failures == 0 ? 0 : 1;
}
