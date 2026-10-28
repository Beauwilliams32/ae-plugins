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
static_assert(std::is_same_v<decltype(beatgrid::computeOnsetEnvelope(nullptr, 0, 0.0, 0, 0)), beatgrid::OnsetEnvelope>,
              "computeOnsetEnvelope returns an OnsetEnvelope BY VALUE (the 0x30-byte result is zeroed at +0x00)");
static_assert(std::is_same_v<decltype(beatgrid::estimateTempoBpm(std::declval<const beatgrid::OnsetEnvelope&>(),
                                                                0.0f, 0.0f, 0.0f)),
                           beatgrid::TempoAnalysis>,
              "estimateTempoBpm returns a TempoAnalysis of three floats (the 12-byte HFA in v0/v1/v2)");
static_assert(std::is_same_v<decltype(beatgrid::analyzeBeatGrid(nullptr, 0, 0.0, 0.0f, 0.0f, 0.0f, 0, 0)),
                           beatgrid::TempoAnalysis>,
              "analyzeBeatGrid returns a TempoAnalysis of three floats (the 12-byte HFA in v0/v1/v2)");

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
    env.sampleRate = 20.0;
    env.windowSize = 8;
    env.hopSize = 2;

    // (frame * hopSize + windowSize / 2) / sampleRate = (6 + 4) / 20
    checkNear(env.frameTimeSeconds(3), 0.5, 1e-12, "frame 3 with hop 2 and window 8 is 0.5 s");
    checkNear(env.frameTimeSeconds(0), 0.2, 1e-12, "frame 0 is the window's HALF: 4/20");

    // THE WINDOW IS HALVED ON THE WAY IN. The compiler emits `scvtf d3, w8, #0x1`, a
    // fixed-point convert with one fractional bit, so the window size is a HALF-window
    // count: a window of 2 contributes one frame, not two.
    beatgrid::OnsetEnvelope half{};
    half.sampleRate = 20.0;
    half.windowSize = 2;
    half.hopSize = 0;
    checkNear(half.frameTimeSeconds(0), 0.05, 1e-12, "a window of 2 contributes HALF a frame (1/20)");

    // A negative hop is carried through with its sign.
    beatgrid::OnsetEnvelope backwards{};
    backwards.sampleRate = 10.0;
    backwards.windowSize = 0;
    backwards.hopSize = -3;
    checkNear(backwards.frameTimeSeconds(4), -1.2, 1e-12, "a negative hop moves backwards in time");

    // The guard, which returns zero rather than dividing.
    beatgrid::OnsetEnvelope zero{};
    zero.sampleRate = 0.0;
    zero.hopSize = 5;
    check(zero.frameTimeSeconds(100) == 0.0, "a sample rate of 0 returns 0");
    zero.sampleRate = -1.0;
    check(zero.frameTimeSeconds(100) == 0.0, "a negative sample rate returns 0");
  }

  std::printf("beatgrid::estimatePhaseSeconds\n");
  {
    // An envelope at 100 frames a second (a 1000 Hz sample rate and a hop of 10) with a
    // single onset in it. The window's half is 50 samples, so frame 20 sits 0.20 s into
    // the envelope but is the CENTRE of the 100-sample window that starts at 0.15 s.
    beatgrid::OnsetEnvelope env{};
    env.sampleRate = 1000.0;
    env.windowSize = 100;
    env.hopSize = 10;
    env.framesPerSecond = 100.0;
    env.values.assign(100, 0.0f);
    env.values[20] = 1.0f;

    // At 120 bpm the beat is half a second and the candidates are half a beat apart, so
    // the onset at 0.25 s is exactly one of them.
    const float phase = beatgrid::estimatePhaseSeconds(env, 120.0f);
    checkNear(phase, 0.25, 1e-5, "the onset at 0.25 s is the phase at 120 bpm");
    check(phase >= 0.0f && phase < 0.5f, "the phase is inside one beat period");

    // The guards.
    beatgrid::OnsetEnvelope empty{};
    check(beatgrid::estimatePhaseSeconds(empty, 120.0f) == 0.0f, "an empty envelope gives 0");
    check(beatgrid::estimatePhaseSeconds(env, 0.0f) == 0.0f, "a bpm of 0 gives 0");
    check(beatgrid::estimatePhaseSeconds(env, -120.0f) == 0.0f, "a negative bpm gives 0");

    // A non-positive frame rate is a guard too: the listing checks +0x28 before it does
    // any arithmetic at all.
    beatgrid::OnsetEnvelope noRate = env;
    noRate.framesPerSecond = 0.0;
    check(beatgrid::estimatePhaseSeconds(noRate, 120.0f) == 0.0f,
          "a frame rate of 0 gives 0");
  }

  std::printf("beatgrid::computeOnsetEnvelope\n");
  {
    // A 1000 Hz signal, a 64-sample window and a 32-sample hop. The listing divides
    // the frame count as (frames - window) / hop + 1, and floors the hop division.
    const int windowSize = 64;
    const int hopSize = 32;
    const size_t frames = 1000;
    std::vector<float> silence(frames, 0.0f);
    const auto quiet = beatgrid::computeOnsetEnvelope(silence.data(), frames, 1000.0, windowSize, hopSize);

    check(quiet.values.size() == (frames - windowSize) / hopSize + 1,
          "the frame count is (frames - window) / hop + 1");
    checkNear(quiet.sampleRate, 1000.0, 0.0, "the sample rate round-trips");
    check(quiet.windowSize == windowSize, "the window size round-trips");
    check(quiet.hopSize == hopSize, "the hop size round-trips");
    checkNear(quiet.framesPerSecond, 1000.0 / hopSize, 1e-12,
              "the frame rate is the sample rate divided by the hop");
    checkNear(quiet.values[0], 0.0, 0.0,
              "the FIRST frame is zero: it has no predecessor to difference against");

    // Silence has no onsets, so every frame after the first is zero too. This pins the
    // flux as a difference against the PREVIOUS frame rather than an absolute level.
    bool allZero = true;
    for (float v : quiet.values) {
      if (v != 0.0f) { allZero = false; break; }
    }
    check(allZero, "a silent signal gives an all-zero envelope");

    // A burst of energy part way through: the frames that overlap it must see a
    // POSITIVE flux.
    std::vector<float> burst(frames, 0.0f);
    for (size_t i = 500; i < 560; ++i) burst[i] = 1.0f;
    const auto loud = beatgrid::computeOnsetEnvelope(burst.data(), frames, 1000.0, windowSize, hopSize);
    check(loud.values.size() == quiet.values.size(), "the burst envelope has the same frame count");
    float peak = 0.0f;
    for (float v : loud.values) if (v > peak) peak = v;
    check(peak > 0.0f, "a burst produces a positive spectral flux");
    check(loud.values[0] == 0.0f, "and the burst envelope still starts at zero");

    // The guards, in the listing's own order: no samples, no frames, a non-positive
    // sample rate, a window below one, a hop below one, a window below two, and a
    // window that is not a power of two. Each returns a ZEROED envelope.
    const auto nullSamples = beatgrid::computeOnsetEnvelope(nullptr, frames, 1000.0, windowSize, hopSize);
    check(nullSamples.values.empty() && nullSamples.sampleRate == 0.0,
          "no samples gives a zeroed envelope");
    const auto noFrames = beatgrid::computeOnsetEnvelope(silence.data(), 0, 1000.0, windowSize, hopSize);
    check(noFrames.values.empty(), "no frames gives a zeroed envelope");
    const auto noRate = beatgrid::computeOnsetEnvelope(silence.data(), frames, 0.0, windowSize, hopSize);
    check(noRate.values.empty() && noRate.sampleRate == 0.0, "a sample rate of 0 gives a zeroed envelope");
    const auto badWindow = beatgrid::computeOnsetEnvelope(silence.data(), frames, 1000.0, 0, hopSize);
    check(badWindow.values.empty(), "a window of 0 gives a zeroed envelope");
    const auto badHop = beatgrid::computeOnsetEnvelope(silence.data(), frames, 1000.0, windowSize, 0);
    check(badHop.values.empty(), "a hop of 0 gives a zeroed envelope");
    const auto tinyWindow = beatgrid::computeOnsetEnvelope(silence.data(), frames, 1000.0, 1, hopSize);
    check(tinyWindow.values.empty(), "a window of 1 gives a zeroed envelope");
    const auto oddWindow = beatgrid::computeOnsetEnvelope(silence.data(), frames, 1000.0, 48, hopSize);
    check(oddWindow.values.empty(), "a window that is not a power of two gives a zeroed envelope");
  }

  std::printf("beatgrid::generateBeatGrid\n");
  {
    const auto Beat = beatgrid::Subdivision::Beat;
    const auto Half = beatgrid::Subdivision::Half;
    const auto Four = beatgrid::Subdivision::FourBeats;

    // 120 bpm is a half-second beat, and the region is two seconds long.
    const auto grid = beatgrid::generateBeatGrid(120.0, 0.0, Beat, 0.0, 2.0);
    check(grid.size() == 4, "four beats in two seconds at 120 bpm");
    checkNear(grid[0].timeSeconds, 0.0, 1e-9, "the first marker is at the origin");
    checkNear(grid[1].timeSeconds, 0.5, 1e-9, "the second marker is half a beat later");
    checkNear(grid[3].timeSeconds, 1.5, 1e-9, "the fourth marker is 1.5 s in");

    // THE MARKER NUMBERS ARE ONE-BASED. The listing adds one to the beat index.
    checkNear(grid[0].beatNumber, 1.0, 1e-9, "the first marker is beat 1");
    checkNear(grid[1].beatNumber, 2.0, 1e-9, "the second marker is beat 2");
    checkNear(grid[3].beatNumber, 4.0, 1e-9, "the fourth marker is beat 4");

    // A half subdivision halves the period, so twice as many markers.
    const auto halves = beatgrid::generateBeatGrid(120.0, 0.0, Half, 0.0, 2.0);
    check(halves.size() == 8, "a half subdivision doubles the marker count");
    checkNear(halves[1].timeSeconds, 0.25, 1e-9, "the half-beat markers are a quarter of a beat apart");

    // Four beats is NOT a fraction: the period is four beats long.
    const auto fours = beatgrid::generateBeatGrid(120.0, 0.0, Four, 0.0, 2.0);
    check(fours.size() == 1, "a four-beat subdivision puts one marker in two seconds");
    checkNear(fours[0].timeSeconds, 0.0, 1e-9, "and that marker is at the origin");

    // The origin offsets the whole grid.
    const auto offset = beatgrid::generateBeatGrid(120.0, 0.25, Beat, 0.0, 2.0);
    check(offset.size() == 4, "an origin of 0.25 s still gives four markers");
    checkNear(offset[0].timeSeconds, 0.25, 1e-9, "the first marker is at the origin");
    checkNear(offset[0].beatNumber, 1.0, 1e-9, "and it is still beat 1");

    // The region bounds the grid: it starts at or after the region's start.
    const auto clipped = beatgrid::generateBeatGrid(120.0, 0.0, Beat, 0.1, 1.0);
    check(clipped.size() == 2, "a region from 0.1 s to 1.1 s holds two markers");
    checkNear(clipped[0].timeSeconds, 0.5, 1e-9, "the first marker is rounded UP to 0.5 s");

    // The guards.
    check(beatgrid::generateBeatGrid(0.0, 0.0, Beat, 0.0, 2.0).empty(), "a bpm of 0 gives no grid");
    check(beatgrid::generateBeatGrid(-120.0, 0.0, Beat, 0.0, 2.0).empty(),
          "a negative bpm gives no grid");
    check(beatgrid::generateBeatGrid(120.0, 0.0, Beat, 0.0, 0.0).empty(),
          "a region of no length gives no grid");
    check(beatgrid::generateBeatGrid(120.0, 0.0, Beat, 0.0, -1.0).empty(),
          "a negative region length gives no grid");
  }

  std::printf("beatgrid::estimateTempoBpm\n");
  {
    // An envelope at 100 frames a second with an onset every 50 frames, which is a
    // 0.5 s beat — 120 bpm. The onsets are impulses, which is the cleanest thing the
    // covariance search can be pointed at.
    beatgrid::OnsetEnvelope env{};
    env.sampleRate = 1000.0;
    env.windowSize = 100;
    env.hopSize = 10;
    env.framesPerSecond = 100.0;
    env.values.assign(500, 0.0f);
    for (size_t i = 0; i < env.values.size(); i += 50) env.values[i] = 1.0f;

    const auto tempo = beatgrid::estimateTempoBpm(env, 60.0f, 180.0f, 0.85f);
    checkNear(tempo.bpm, 120.0, 1.2, "the 0.5 s beat period comes back as 120 bpm");
    check(tempo.confidence > 0.0f && tempo.confidence <= 1.0f,
          "the confidence is a fraction and never exceeds one");
    check(tempo.phaseSeconds >= 0.0f && tempo.phaseSeconds < 0.5f,
          "the phase is inside the 0.5 s beat period");

    // The DEGENERATE range: one bpm, so there is nothing to search for and the listing
    // takes a separate branch that indexes the correlation directly.
    const auto fixed = beatgrid::estimateTempoBpm(env, 120.0f, 120.0f, 0.85f);
    checkNear(fixed.bpm, 120.0, 1e-6, "a range of one bpm returns exactly that bpm");

    // The guards. Every one of them returns all three fields as zero.
    const beatgrid::OnsetEnvelope empty{};
    const auto none = beatgrid::estimateTempoBpm(empty, 60.0f, 180.0f, 0.85f);
    check(none.bpm == 0.0f && none.phaseSeconds == 0.0f && none.confidence == 0.0f,
          "an empty envelope gives all zeroes");

    const auto inverted = beatgrid::estimateTempoBpm(env, 180.0f, 60.0f, 0.85f);
    check(inverted.bpm == 0.0f && inverted.confidence == 0.0f,
          "a range with maxBpm below minBpm gives all zeroes");

    const auto noMax = beatgrid::estimateTempoBpm(env, 60.0f, 0.0f, 0.85f);
    check(noMax.bpm == 0.0f && noMax.confidence == 0.0f,
          "a maxBpm of 0 gives all zeroes");

    const auto noMin = beatgrid::estimateTempoBpm(env, 0.0f, 180.0f, 0.85f);
    check(noMin.bpm == 0.0f && noMin.confidence == 0.0f,
          "a minBpm of 0 gives all zeroes");

    beatgrid::OnsetEnvelope noRate = env;
    noRate.framesPerSecond = 0.0;
    const auto still = beatgrid::estimateTempoBpm(noRate, 60.0f, 180.0f, 0.85f);
    check(still.bpm == 0.0f && still.confidence == 0.0f,
          "a frame rate of 0 gives all zeroes");

    // A FLAT envelope has no variance, and the listing's threshold test only applies
    // when the variance is positive — so a flat signal falls through the guard rather
    // than being rescued by it.
    beatgrid::OnsetEnvelope flat = env;
    flat.values.assign(500, 0.25f);
    const auto level = beatgrid::estimateTempoBpm(flat, 60.0f, 180.0f, 0.85f);
    check(level.bpm == 0.0f, "a flat envelope finds no beat");
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
