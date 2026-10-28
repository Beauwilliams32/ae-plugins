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

  // --- smoothedLevelsAtFrame (recovered 2026-10-28): the exponential approach toward the
  // band's own value, with the time constant chosen by the comparison.
  std::printf("pathscribe::smoothedLevelsAtFrame\n");
  {
    check(pathscribe::smoothedLevelsAtFrame({}, 0, 1.0f, 1.0f, 1.0f).empty(),
          "no bands gives an empty vector");

    const std::vector<std::vector<float>> one = {{1.0f, 2.0f, 3.0f}};
    const std::vector<float> first = pathscribe::smoothedLevelsAtFrame(one, 0, 1.0f, 1.0f, 1.0f);
    check(first.size() == 3, "the output is sized from the FIRST band");
    // base = 1 in both directions here, so the element is the sample scaled by
    // (1 - exp(-a/base)) = 1 - exp(-1) = 0.6321206.
    checkNear(first[0], 0.6321206f, 1e-6, "band 0 element 0: the approach from zero");
    checkNear(first[1], 1.2642411f, 1e-6, "and it scales with the sample");
    checkNear(first[2], 1.8963617f, 1e-6, "all the way to the third");

    // a <= 0 takes the raw sample: there is no approach left to make.
    const std::vector<float> none = pathscribe::smoothedLevelsAtFrame(one, 0, 0.0f, 1.0f, 1.0f);
    checkNear(none[0], 1.0f, 0.0, "a == 0 takes the raw sample");
    checkNear(none[2], 3.0f, 0.0, "and every element of it");

    // The base is b when the incoming sample is at least the value so far, and c when it
    // is smaller. Two bands make the second case reachable: the first band lifts the
    // value above the second band's sample.
    const std::vector<std::vector<float>> two = {{4.0f}, {1.0f}};
    const std::vector<float> withC = pathscribe::smoothedLevelsAtFrame(two, 1, 1.0f, 1.0f, 0.25f);
    // band 0: dst 0, src 4, base = b = 1  ->  4 * (1 - exp(-1)) = 2.5284822
    // band 1: dst 2.5284822, src 1, src < dst so base = c = 0.25
    //         ->  dst + (1 - exp(-1/0.25)) * (1 - dst) = 1.0279951
    checkNear(withC[0], 1.0279951f, 1e-6,
              "the smaller sample uses c as the base, not b");

    // A base at or below 1e-5 takes the raw sample instead of dividing by it.
    const std::vector<float> tiny = pathscribe::smoothedLevelsAtFrame(one, 0, 1.0f, 0.0f, 1.0f);
    checkNear(tiny[0], 1.0f, 0.0, "a base at or below 1e-5 takes the raw sample");
    checkNear(tiny[2], 3.0f, 0.0, "for every element");

    // The walk stops at the last band however large the frame index is: `last` is clamped
    // to band count - 1, and the loop is inclusive of it.
    const std::vector<float> beyond = pathscribe::smoothedLevelsAtFrame(one, 99, 1.0f, 1.0f, 1.0f);
    check(beyond.size() == 3, "a frame index past the end still sizes the output");
    checkNear(beyond[0], first[0], 0.0, "and stops at the last band");

    // A band shorter than the output is truncated to the band's own length.
    const std::vector<std::vector<float>> ragged = {{1.0f, 2.0f, 3.0f}, {4.0f}};
    const std::vector<float> shortBand = pathscribe::smoothedLevelsAtFrame(ragged, 1, 1.0f, 1.0f, 1.0f);
    check(shortBand.size() == 3, "a short band does not shorten the output");
    checkNear(shortBand[1], first[1], 0.0, "and leaves the elements it does not reach alone");
  }

  // --- rasterizeRibbon (recovered 2026-10-28): the ribbon profile and its thickness.
  std::printf("pathscribe::rasterizeRibbon\n");
  {
    pathscribe::VizConfig cfg;
    cfg.columns = 4;
    cfg.rows = 6;
    cfg.baseline = 0.0f;
    cfg.amplitude = 1.0f;
    cfg.thickness = 1.0f;
    const std::vector<float> levels = {0.5f, -0.5f, 0.25f, -0.25f};

    std::vector<float> bars, ribbon;
    pathscribe::rasterizeRibbon(levels, cfg, bars, ribbon);
    check(bars.size() == 24, "the profile is columns * rows long");
    check(ribbon.size() == 24, "and so is the ribbon");

    // The profile lands where the level maps to, and the ribbon carries the level's
    // magnitude. These are the pixels that are written; every other pixel stays 0.
    checkNear(bars[14], 1.0f, 0.0, "a peak at column 2 is drawn at full height");
    checkNear(ribbon[14], 0.25f, 0.0, "and carries that column's magnitude");
    checkNear(bars[16], 1.0f, 0.0, "column 0's peak is drawn too");
    checkNear(ribbon[16], 0.5f, 0.0, "with its own magnitude");
    checkNear(bars[18], 0.2977531f, 1e-6, "and a sloped column falls off by distance");
    checkNear(ribbon[18], 0.25f, 0.0, "while the ribbon still carries the magnitude");
    checkNear(bars[21], 1.0f, 0.0, "the next column's peak is drawn");
    checkNear(ribbon[21], 0.5f, 0.0, "with its own magnitude");
    checkNear(bars[22], 0.7659177f, 1e-6, "and a sloped column falls off by distance");

    bool untouched = true;
    for (size_t i = 0; i < bars.size(); ++i) {
      if (bars[i] == 0.0f && ribbon[i] == 0.0f) continue;
      if (i == 14 || i == 16 || i == 18 || i == 21 || i == 22) continue;
      untouched = false;
    }
    check(untouched, "and no other pixel is written");

    // A negative amplitude flips the mapping AND becomes the profile's lower bound,
    // so the drawn band never goes below it.
    pathscribe::VizConfig neg = cfg;
    neg.columns = 2;
    neg.rows = 4;
    neg.baseline = 0.5f;
    neg.amplitude = -1.0f;
    neg.thickness = 2.0f;
    std::vector<float> nbars, nribbon;
    pathscribe::rasterizeRibbon(std::vector<float>{0.0f, 1.0f}, neg, nbars, nribbon);
    check(nbars.size() == 8, "the profile is sized from the config");
    checkNear(nbars[2], 1.0f, 0.0, "a negative amplitude still draws a band");
    checkNear(nbars[5], 0.4393398f, 1e-6, "with the same distance falloff");
    checkNear(nribbon[5], 1.0f, 0.0, "and the magnitude of the loud column");
    checkNear(nribbon[7], 1.0f, 0.0, "wherever the loud column landed");

    // The outputs are SIZED before the early exits, so a degenerate config still
    // returns zero-filled arrays of the right length.
    pathscribe::VizConfig none = cfg;
    none.columns = 0;
    none.rows = 6;
    std::vector<float> zbars, zribbon;
    pathscribe::rasterizeRibbon(levels, none, zbars, zribbon);
    check(zbars.empty(), "no columns gives an empty profile");
    check(zribbon.empty(), "and an empty ribbon");

    pathscribe::VizConfig empty = cfg;
    std::vector<float> ebars, eribbon;
    pathscribe::rasterizeRibbon({}, empty, ebars, eribbon);
    check(ebars.size() == 24, "no levels still sizes the profile");
    bool allZero = true;
    for (float v : ebars) if (v != 0.0f) allZero = false;
    for (float v : eribbon) if (v != 0.0f) allZero = false;
    check(allZero, "and leaves it zero filled");
  }

  // --- rasterizeBars (recovered 2026-10-28): solid columns, and the one VizConfig field
  // the ribbon does not read.
  std::printf("pathscribe::rasterizeBars\n");
  {
    pathscribe::VizConfig cfg;
    cfg.columns = 8;
    cfg.rows = 8;
    cfg.baseline = 0.5f;
    cfg.amplitude = 1.0f;
    cfg.thickness = 0.5f;
    cfg.barScale = 0.5f;

    // One level, so one bar, at the full column pitch less the bar's own width.
    std::vector<float> bars, ribbon;
    pathscribe::rasterizeBars({0.5f}, cfg, bars, ribbon);
    check(bars.size() == 64, "the profile is columns * rows long");
    check(ribbon.size() == 64, "and so is the ribbon");
    // The bar is drawn on columns 2..5: `half` is barScale * step * 0.5 = 2, so the
    // bar's left edge is at 2 and its right edge at 6.
    checkNear(bars[2], 1.0f, 0.0, "the bar starts at column 2");
    checkNear(bars[5], 1.0f, 0.0, "and ends at column 5");
    checkNear(bars[6], 0.0f, 0.0, "with nothing on column 6");
    checkNear(bars[1], 0.0f, 0.0, "nor on column 1");
    // It grows UPWARD from the baseline: the baseline is at row 4, the level is half
    // the height, so rows 0..3 are drawn and row 4 is not.
    checkNear(bars[3 * 8 + 2], 1.0f, 0.0, "the bar reaches the top of its height");
    checkNear(bars[4 * 8 + 2], 0.0f, 0.0, "but stops at the baseline");
    // The ribbon carries the LEVEL, not the drawn height.
    checkNear(ribbon[2], 0.5f, 0.0, "and the ribbon carries the level");
    checkNear(ribbon[6], 0.0f, 0.0, "and nothing where the bar is not");

    // MODE 3 is the only mode that behaves differently: it CENTRES the bar on the
    // baseline instead of growing upward from it.
    pathscribe::VizConfig centred = cfg;
    centred.mode = 3;
    std::vector<float> cbars, cribbon;
    pathscribe::rasterizeBars({0.5f}, centred, cbars, cribbon);
    checkNear(cbars[1 * 8 + 2], 0.0f, 0.0, "mode 3 does not reach the top of the strip");
    checkNear(cbars[2 * 8 + 2], 1.0f, 0.0, "it starts half a level above the baseline");
    checkNear(cbars[5 * 8 + 2], 1.0f, 0.0, "and ends half a level below it");
    checkNear(cbars[6 * 8 + 2], 0.0f, 0.0, "so it is centred, not bottom-anchored");

    // EVERY other mode is the same function: 0, 1 and 2 all grow upward.
    std::vector<float> mbars, mribbon;
    pathscribe::VizConfig mode2 = cfg;
    mode2.mode = 2;
    pathscribe::rasterizeBars({0.5f}, mode2, mbars, mribbon);
    check(mbars == bars && mribbon == ribbon,
          "mode 2 draws exactly what mode 0 draws — only mode 3 differs");

    // PARTIAL COVERAGE. Three levels over eight columns put the bar edges between
    // the column boundaries, so the outer columns of a bar are drawn at a fraction
    // of full height. These are the pixels that a rewrite gets wrong by writing
    // `left`/`right` where the bundle uses their min and max.
    std::vector<float> tbars, tribbon;
    pathscribe::rasterizeBars({0.25f, 0.5f, 0.75f}, cfg, tbars, tribbon);
    checkNear(tbars[2 * 8 + 0], 0.3333333f, 1e-6, "a bar's left edge covers a third of its column");
    checkNear(tbars[2 * 8 + 1], 1.0f, 0.0, "and the next column is fully covered");
    checkNear(tbars[2 * 8 + 2], 0.0f, 0.0, "with nothing beyond the bar's right edge");
    checkNear(tbars[0 * 8 + 3], 0.6666667f, 1e-6, "the middle bar's left edge is partial too");
    checkNear(tbars[0 * 8 + 6], 1.0f, 0.0, "and the tallest bar is fully covered");
    checkNear(tbars[0 * 8 + 7], 0.3333333f, 1e-6, "with a partial column at its right edge");
    checkNear(tribbon[0 * 8 + 7], 0.75f, 0.0, "and the ribbon carries the tallest level there");

    // A NEGATIVE baseline puts the origin at the BOTTOM of the strip.
    pathscribe::VizConfig low = cfg;
    low.baseline = -1.0f;
    std::vector<float> lbars, lribbon;
    pathscribe::rasterizeBars({0.5f}, low, lbars, lribbon);
    checkNear(lbars[3 * 8 + 2], 0.0f, 0.0, "a negative baseline moves the bar down the strip");
    checkNear(lbars[4 * 8 + 2], 1.0f, 0.0, "to the bottom half");

    // A baseline above 1 collapses the origin to the top, where the bar has no room.
    pathscribe::VizConfig high = cfg;
    high.baseline = 2.0f;
    std::vector<float> hbars, hribbon;
    pathscribe::rasterizeBars({0.5f}, high, hbars, hribbon);
    bool nothingDrawn = true;
    for (float v : hbars) if (v != 0.0f) nothingDrawn = false;
    for (float v : hribbon) if (v != 0.0f) nothingDrawn = false;
    check(nothingDrawn, "a baseline above 1 draws nothing at all");

    // A NEGATIVE barScale is clamped to ZERO, not to a small width, so the bars are
    // drawn at the FULL column pitch — the opposite of what the name suggests.
    pathscribe::VizConfig wide = cfg;
    wide.barScale = -1.0f;
    std::vector<float> wbars, wribbon;
    pathscribe::rasterizeBars({0.5f}, wide, wbars, wribbon);
    checkNear(wbars[0], 1.0f, 0.0, "a negative barScale draws at the full column pitch");
    checkNear(wbars[7], 1.0f, 0.0, "right up to the last column");
    // And a barScale of 0 does the same thing, because the width is zero either way.
    pathscribe::VizConfig zero = cfg;
    zero.barScale = 0.0f;
    std::vector<float> zbars2, zribbon2;
    pathscribe::rasterizeBars({0.5f}, zero, zbars2, zribbon2);
    check(zbars2 == wbars, "and so does a barScale of zero");

    // The amplitude is clamped to [0, 4]: a negative amplitude draws nothing at all.
    pathscribe::VizConfig neg = cfg;
    neg.amplitude = -1.0f;
    std::vector<float> nbars2, nribbon2;
    pathscribe::rasterizeBars({0.5f}, neg, nbars2, nribbon2);
    bool ampNothing = true;
    for (float v : nbars2) if (v != 0.0f) ampNothing = false;
    for (float v : nribbon2) if (v != 0.0f) ampNothing = false;
    check(ampNothing, "a negative amplitude draws nothing");

    // The level is clamped to [0, 1]: a negative level is skipped, and one above 1
    // is drawn as if it were 1.
    std::vector<float> negbars, negribbon;
    pathscribe::rasterizeBars({-0.5f}, cfg, negbars, negribbon);
    bool levelNothing = true;
    for (float v : negbars) if (v != 0.0f) levelNothing = false;
    for (float v : negribbon) if (v != 0.0f) levelNothing = false;
    check(levelNothing, "a negative level is skipped");
    std::vector<float> highbars, highribbon;
    pathscribe::rasterizeBars({2.0f}, cfg, highbars, highribbon);
    std::vector<float> onebars, oneribbon;
    pathscribe::rasterizeBars({1.0f}, cfg, onebars, oneribbon);
    check(highbars == onebars && highribbon == oneribbon,
          "a level above 1 is clamped to 1");

    // The outputs are SIZED before the early exits, so a degenerate config still
    // returns zero-filled arrays of the right length.
    pathscribe::VizConfig none = cfg;
    none.columns = 0;
    std::vector<float> zbars, zribbon;
    pathscribe::rasterizeBars({0.5f}, none, zbars, zribbon);
    check(zbars.empty(), "no columns gives an empty profile");
    check(zribbon.empty(), "and an empty ribbon");

    std::vector<float> ebars, eribbon;
    pathscribe::rasterizeBars({}, cfg, ebars, eribbon);
    check(ebars.size() == 64, "no levels still sizes the profile");
    bool allZero = true;
    for (float v : ebars) if (v != 0.0f) allZero = false;
    for (float v : eribbon) if (v != 0.0f) allZero = false;
    check(allZero, "and leaves it zero filled");
  }

  if (failures == 0) std::printf("ALL RECOVERED PATHSCRIBE MATHS VERIFIED (%d checks)\n", passes);
  else std::printf("%d FAILURES out of %d checks\n", failures, passes + failures);
  return failures == 0 ? 0 : 1;
}
