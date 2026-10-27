// Self-check for the recovered pathscribe:: namespace.
//
// Two jobs here. The first is PathScribe's OWN three helpers, where every assertion is
// anchored to an instruction. The second is a spot check on the SHARED pipeline: that
// code is verified where it was recovered (spectra's self-check) and its transcription
// into this namespace is verified by divergence parity in test/pathscribe.test.mjs, so
// what is checked here is only that this copy actually behaves — a handful of
// properties that would collapse if the copy were mangled.

#include "pathscribe.h"

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

void checkNear(double got, double want, double tol, const char* why) {
  if (std::fabs(got - want) <= tol) { ++passes; std::printf("  PASS %s\n", why); }
  else { ++failures; std::printf("  FAIL %s  (got %.9g want %.9g)\n", why, got, want); }
}

}  // namespace

int main() {
  std::printf("pathscribe::spanCoverage\n");
  {
    // The frame is [frameIndex, frameIndex + 1). The span is the interval between the
    // last two arguments, in EITHER order.
    checkNear(pathscribe::spanCoverage(3.0f, 3.0f, 4.0f), 1.0, 1e-6, "an interval exactly as wide as the frame covers it");
    checkNear(pathscribe::spanCoverage(3.0f, 4.0f, 3.0f), 1.0, 1e-6, "and the same interval given the other way round");
    checkNear(pathscribe::spanCoverage(3.0f, 2.5f, 4.5f), 1.0, 1e-6, "a wider one covers it too, clamped at 1");
    checkNear(pathscribe::spanCoverage(3.0f, 3.0f, 3.5f), 0.5, 1e-6, "half the frame is covered half way");
    checkNear(pathscribe::spanCoverage(3.0f, 3.25f, 3.5f), 0.25, 1e-6, "a quarter, wholly inside the frame");
    checkNear(pathscribe::spanCoverage(3.0f, 2.5f, 3.25f), 0.25, 1e-6, "and a quarter straddling the frame's start");
    checkNear(pathscribe::spanCoverage(3.0f, 3.75f, 5.0f), 0.25, 1e-6, "and one straddling its end");
    checkNear(pathscribe::spanCoverage(3.0f, 4.0f, 4.5f), 0.0, 1e-6, "an interval at the next frame does not overlap");
    checkNear(pathscribe::spanCoverage(3.0f, 2.0f, 2.5f), 0.0, 1e-6, "nor one that ended before this frame");

    // THE MIN CHECK. The right-hand edge is min(the interval's end, frame + 1), not
    // max: here the interval runs 0.5..0.75 inside frame 0, so the overlap is 0.25. A
    // transcription that took the maximum answers 0.5, and it is what the first version
    // of this file did -- the differ caught it at 1 of 16 positions.
    checkNear(pathscribe::spanCoverage(0.0f, 0.5f, 0.75f), 0.25, 1e-6,
              "the right edge is the MINIMUM: 0.5..0.75 inside frame 0 overlaps by 0.25");

    // A degenerate interval covers nothing, and can never return a negative fraction.
    checkNear(pathscribe::spanCoverage(3.0f, 3.5f, 3.5f), 0.0, 1e-6, "a zero-width interval covers nothing");
    check(pathscribe::spanCoverage(3.0f, 5.0f, 1.0f) >= 0.0f, "and the result is never negative");
  }

  std::printf("pathscribe::smoothStep\n");
  {
    // An exponential approach: from + (to - from) * (1 - exp(-x / span)).
    const double want = 0.0 + (1.0 - 0.0) * (1.0 - std::exp(-1.0 / 2.0));
    checkNear(pathscribe::smoothStep(0.0f, 1.0f, 1.0f, 2.0f, 2.0f), want, 1e-6,
              "x = 1 with span 2 is 1 - exp(-0.5) of the way across");

    // The direction chooses the span: rising uses the fourth parameter, falling the fifth.
    const double rising = 1.0 - std::exp(-1.0 / 4.0);
    const double falling = 1.0 - std::exp(-1.0 / 8.0);
    checkNear(pathscribe::smoothStep(0.0f, 1.0f, 1.0f, 4.0f, 8.0f), rising, 1e-6,
              "rising from 0 to 1 uses the FOURTH parameter as the span");
    checkNear(pathscribe::smoothStep(1.0f, 0.0f, 1.0f, 4.0f, 8.0f), 1.0 - falling, 1e-6,
              "falling from 1 to 0 uses the FIFTH");

    // THE ODD ONE, and the reason this function is not what its name suggests: a
    // non-positive x returns `to`, the DESTINATION, not `from`.
    checkNear(pathscribe::smoothStep(0.0f, 1.0f, 0.0f, 2.0f, 2.0f), 1.0, 0.0,
              "x = 0 returns `to`, not `from` — the guard is a bare `ret` of the second value");
    checkNear(pathscribe::smoothStep(0.0f, 1.0f, -5.0f, 2.0f, 2.0f), 1.0, 0.0, "and so does a negative x");
    // A span at or below 1e-5f takes the same path, since the divide would be useless.
    checkNear(pathscribe::smoothStep(0.0f, 1.0f, 1.0f, 1e-5f, 1e-5f), 1.0, 0.0,
              "a span of exactly 1e-5f (0x3727c5ac) takes the guard, not the curve");
    checkNear(pathscribe::smoothStep(0.0f, 1.0f, 1.0f, 1e-4f, 1e-4f),
              1.0 - std::exp(-1.0 / 1e-4), 1e-9, "a span just above it does not");

    // It approaches the destination and never overshoots it.
    checkNear(pathscribe::smoothStep(0.0f, 1.0f, 1000.0f, 1.0f, 1.0f), 1.0, 1e-6, "a long x arrives at `to`");
    bool monotone = true;
    double previous = -1.0;
    for (int i = 1; i <= 200; ++i) {
      const double v = pathscribe::smoothStep(0.0f, 1.0f, 0.05f * i, 1.0f, 1.0f);
      if (v < previous) monotone = false;
      previous = v;
    }
    check(monotone, "the approach is monotone in x");
    check(pathscribe::smoothStep(0.0f, 1.0f, 5.0f, 1.0f, 1.0f) <= 1.0f, "and never exceeds `to`");
  }

  std::printf("pathscribe::binEdgeHz\n");
  {
    const double lo = pathscribe::binEdgeHz(0, 8, 20.0, 20000.0);
    const double hi = pathscribe::binEdgeHz(8, 8, 20.0, 20000.0);
    checkNear(lo, 20.0, 1e-9, "edge 0 is minHz");
    checkNear(hi, 20000.0, 1e-6, "edge `count` is maxHz exactly (the exponent divides by count)");

    // GEOMETRIC: consecutive edges are a constant RATIO apart, which is the point of a
    // log-spaced analysis. An arithmetic spacing would fail this immediately.
    const double ratio = pathscribe::binEdgeHz(1, 8, 20.0, 20000.0) / pathscribe::binEdgeHz(0, 8, 20.0, 20000.0);
    bool geometric = true;
    for (int i = 1; i < 8; ++i) {
      const double r = pathscribe::binEdgeHz(i + 1, 8, 20.0, 20000.0)
                     / pathscribe::binEdgeHz(i, 8, 20.0, 20000.0);
      if (std::fabs(r - ratio) > 1e-9) geometric = false;
    }
    check(geometric, "consecutive edges are a constant ratio apart, not a constant difference");

    // The 1 Hz floor, so the ratio is never taken from zero.
    checkNear(pathscribe::binEdgeHz(0, 4, 0.0, 16.0), 1.0, 1e-9, "a minHz of 0 is floored to 1 Hz");
    checkNear(pathscribe::binEdgeHz(0, 4, -5.0, 16.0), 1.0, 1e-9, "and so is a negative one");
    checkNear(pathscribe::binEdgeHz(4, 4, 0.0, 16.0), 16.0, 1e-6, "with the top edge still reaching maxHz");
    // maxHz below the floor collapses to it.
    checkNear(pathscribe::binEdgeHz(3, 3, 4.0, 0.5), 4.0, 1e-9, "a maxHz below the floor collapses to the floor");

    // count < 1 returns the incoming minHz UNCHANGED: the branch target is a bare
    // `ret`, so the untouched third argument is what comes back.
    checkNear(pathscribe::binEdgeHz(0, 0, 123.0, 456.0), 123.0, 0.0, "count 0 returns minHz unchanged");
    checkNear(pathscribe::binEdgeHz(5, -3, 77.0, 456.0), 77.0, 0.0, "and so does a negative count");
  }

  std::printf("the shared pipeline, read through this namespace's copy\n");
  {
    // These four lines are what the copy would break first if the namespace rename or
    // the body extraction went wrong. The functions themselves are verified in
    // spectra's self-check and by divergence parity.
    check(pathscribe::clampf(5.0f, 10.0f, 0.0f) == 10.0f, "clampf keeps the fleet's inverted-range behaviour");
    check(pathscribe::isPowerOfTwo(1024) && !pathscribe::isPowerOfTwo(0), "isPowerOfTwo guards on > 0");
    checkNear(pathscribe::dbToLinear(pathscribe::linearToDb(0.25f)), 0.25, 1e-4, "linearToDb round-trips through dbToLinear");
    checkNear(pathscribe::hannCoherentGain(), 0.5, 1e-6, "hannCoherentGain is 0.5");

    const std::vector<float> w = pathscribe::hannWindow(64);
    double sum = 0.0;
    for (float v : w) sum += v;
    checkNear(sum / 64.0, 0.5, 1e-4, "and the periodic window's mean agrees with it");

    std::vector<float> re{1.0f, 0.0f, 0.0f, 0.0f};
    std::vector<float> im{0.0f, 0.0f, 0.0f, 0.0f};
    pathscribe::fftRadix2(re, im);
    checkNear(re[0], 1.0, 1e-6, "the FFT of a unit impulse is flat");
    check(im[1] == 0.0f && im[2] == 0.0f && im[3] == 0.0f, "with no imaginary part");

    // The analysis pipeline end to end: 1 second of a full-scale 1 kHz tone at 48 kHz
    // must put its energy in the bin for 1 kHz.
    const int rate = 48000;
    std::vector<float> tone(static_cast<size_t>(rate));
    for (int i = 0; i < rate; ++i) tone[static_cast<size_t>(i)] = std::sin(2.0 * 3.14159265358979323846 * 1000.0 * i / rate);
    pathscribe::AnalysisConfig config;
    config.sampleRate = static_cast<double>(rate);
    config.fftSize = 1024;
    config.hopRateHz = 20.0;
    const pathscribe::Spectrogram spec = pathscribe::analyse(tone.data(), tone.size(), config);
    check(spec.frameCount > 0, "analyse produces frames");
    // binCount is fftSize / 2, not fftSize: the recovered prologue stores
    // `fftSize / 2` into the Spectrogram's first field.
    check(spec.binCount == 512, "with binCount = fftSize / 2");

    int peak = 0;
    double peakValue = 0.0;
    for (int b = 0; b < spec.binCount; ++b) {
      const double v = spec.frames.empty() ? 0.0 : spec.frames[0][static_cast<size_t>(b)];
      if (v > peakValue) { peakValue = v; peak = b; }
    }
    const double hz = peak * spec.hzPerBin;
    char label[96];
    std::snprintf(label, sizeof label, "and the 1 kHz tone peaks at %.0f Hz (bin %d)", hz, peak);
    check(hz > 900.0 && hz < 1100.0, label);
  }

  // --- waveformColumns (recovered 2026-10-27): peak-hold per column, clamped to [-1, 1].
  std::printf("pathscribe::waveformColumns\n");
  {
    const float track[8] = {0.0f, 0.5f, -0.9f, 0.2f, 1.5f, -2.0f, 0.1f, 0.0f};
    check(pathscribe::waveformColumns(track, 8, 0, 8, 0).empty(), "columns < 1 gives an empty vector");
    const std::vector<float> nullTrack = pathscribe::waveformColumns(nullptr, 8, 0, 8, 4);
    check(nullTrack.size() == 4 && nullTrack[0] == 0.0f, "a null track gives the zero-filled array");
    const std::vector<float> noEnd = pathscribe::waveformColumns(track, 8, 0, 0, 4);
    check(noEnd.size() == 4 && noEnd[3] == 0.0f, "end < 1 likewise");

    // `start` is the window's CENTRE, not its first sample: base = start - (end >> 1). So a
    // centred read of an 8-sample track passes start = 4, and only then does column c inspect
    // sample c. (Passing start = 0 shifts every column four samples left, which is the behaviour
    // the first version of this check got wrong.)
    const std::vector<float> shifted = pathscribe::waveformColumns(track, 8, 0, 8, 8);
    checkNear(shifted[0], 0.0f, 1e-6f, "start = 0 inspects samples before the track and clamps to nothing");
    checkNear(shifted[5], 0.5f, 1e-6f, "so column 5 lands on sample 1 (base = -4)");

    // One column per sample: each column is that sample, clamped to [-1, 1] — the 1.5 and -2.0
    // are clipped, and the SIGN is kept (the reducer compares magnitudes but stores the sample).
    const std::vector<float> perSample = pathscribe::waveformColumns(track, 8, 4, 8, 8);
    check(perSample.size() == 8, "one column per sample when columns == end");
    checkNear(perSample[1], 0.5f, 1e-6f, "an in-range sample passes through");
    checkNear(perSample[2], -0.9f, 1e-6f, "including its sign");
    checkNear(perSample[4], 1.0f, 1e-6f, "a sample above 1 clips to 1");
    checkNear(perSample[5], -1.0f, 1e-6f, "and one below -1 clips to -1");

    // Two columns over the same track: each is the largest-magnitude sample of its half, so the
    // -0.9 in the first half and the -2.0 (clipped) in the second win over their neighbours.
    const std::vector<float> halves = pathscribe::waveformColumns(track, 8, 4, 8, 2);
    checkNear(halves[0], -0.9f, 1e-6f, "the first half's peak is the largest magnitude, signed");
    checkNear(halves[1], -1.0f, 1e-6f, "the second half's peak clips");

    // Every answer is inside [-1, 1] and the output length is always `columns`.
    bool bounded = true, sized = true;
    for (int columns = 1; columns <= 16; ++columns) {
      const std::vector<float> out = pathscribe::waveformColumns(track, 8, 4, 8, columns);
      if (out.size() != static_cast<size_t>(columns)) sized = false;
      for (float v : out) if (!(v >= -1.0f && v <= 1.0f)) bounded = false;
    }
    check(sized, "the output is always exactly `columns` long");
    check(bounded, "and never leaves [-1, 1], whatever the column count");
  }

  // --- barLevels (recovered 2026-10-27): log-spaced peak-hold bars over a 60 dB range.
  std::printf("pathscribe::barLevels\n");
  {
    std::vector<float> track(64, 0.0f);
    for (size_t i = 0; i < track.size(); ++i) track[i] = 0.001f * static_cast<float>(i + 1);
    check(pathscribe::barLevels(track, 1.0, 0, 20.0, 20000.0).empty(), "barCount < 1 gives an empty vector");
    const std::vector<float> noDuration = pathscribe::barLevels(track, 0.0, 8, 20.0, 20000.0);
    check(noDuration.size() == 8 && noDuration[0] == 0.0f, "a non-positive duration gives the zero array");
    check(pathscribe::barLevels({}, 1.0, 8, 20.0, 20000.0).size() == 8, "an empty track still sizes the output");

    // The frequency pair is order-independent: swapping the arguments must not change the answer.
    const std::vector<float> ascending = pathscribe::barLevels(track, 1.0, 6, 20.0, 20000.0);
    const std::vector<float> descending = pathscribe::barLevels(track, 1.0, 6, 20000.0, 20.0);
    check(ascending == descending, "swapping the two frequency arguments changes nothing");

    // A 60 dB conversion: a full-scale (1.0) peak is 0 dB, which is the top of the strip.
    std::vector<float> hot(track.size(), 0.03f);
    hot[0] = 1.0f;
    const std::vector<float> hotBars = pathscribe::barLevels(hot, 1.0, 4, 20.0, 20000.0);
    check(hotBars.size() == 4, "one level per bar");
    bool inRange = true;
    for (float v : hotBars) if (!(v >= 0.0f && v <= 1.0f)) inRange = false;
    check(inRange, "every level is inside [0, 1]");

    // Silence renders at the BOTTOM of the strip, not NaN: the 1e-9 floor is what makes that true.
    std::vector<float> silence(track.size(), 0.0f);
    const std::vector<float> quiet = pathscribe::barLevels(silence, 1.0, 4, 20.0, 20000.0);
    bool allZero = true, finite = true;
    for (float v : quiet) {
      if (v != 0.0f) allZero = false;
      if (!(v == v)) finite = false;
    }
    check(allZero, "an all-silent track renders every bar at 0");
    check(finite, "and produces no NaN (the log10 floor)");

    // The axis is logarithmic: doubling the frequency range does not double a bar's index, so the
    // level of a monotonically rising track is monotone too.
    bool monotone = true;
    for (size_t i = 1; i < hotBars.size(); ++i) if (hotBars[i] + 1e-6f < hotBars[i - 1]) monotone = false;
    check(monotone, "a rising track gives non-decreasing bars");
  }

  if (failures == 0) std::printf("ALL RECOVERED PATHSCRIBE MATHS VERIFIED (%d checks)\n", passes);
  else std::printf("%d FAILURES out of %d checks\n", failures, passes + failures);
  return failures == 0 ? 0 : 1;
}
