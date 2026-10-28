// Self-check for the recovered beatsnap:: namespace.
//
// Every assertion is anchored to something the disassembly establishes, not to
// what the maths "ought" to do. The five functions here are the fleet's shared
// DSP, so where a property is shared with the `spectra::` copies the check says so
// — the two were recovered independently and must agree.

#include "beatsnap.h"

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

void checkNear(float got, float want, float tol, const char* why) {
  if (std::fabs(got - want) <= tol) { ++passes; std::printf("  PASS %s\n", why); }
  else { ++failures; std::printf("  FAIL %s  (got %.9g want %.9g)\n", why, got, want); }
}

/** Naive O(N^2) DFT, the oracle the radix-2 result is compared against. */
void naiveDft(const std::vector<float>& re, const std::vector<float>& im,
              std::vector<float>& outRe, std::vector<float>& outIm) {
  const int n = static_cast<int>(re.size());
  outRe.assign(static_cast<size_t>(n), 0.0f);
  outIm.assign(static_cast<size_t>(n), 0.0f);
  for (int k = 0; k < n; ++k) {
    double sr = 0.0, si = 0.0;
    for (int t = 0; t < n; ++t) {
      const double angle = -2.0 * 3.14159265358979323846 * static_cast<double>(k)
                         * static_cast<double>(t) / static_cast<double>(n);
      sr += static_cast<double>(re[static_cast<size_t>(t)]) * std::cos(angle)
          - static_cast<double>(im[static_cast<size_t>(t)]) * std::sin(angle);
      si += static_cast<double>(re[static_cast<size_t>(t)]) * std::sin(angle)
          + static_cast<double>(im[static_cast<size_t>(t)]) * std::cos(angle);
    }
    outRe[static_cast<size_t>(k)] = static_cast<float>(sr);
    outIm[static_cast<size_t>(k)] = static_cast<float>(si);
  }
}

}  // namespace

int main() {
  std::printf("beatsnap::clampf\n");
  {
    check(beatsnap::clampf(5.0f, 0.0f, 10.0f) == 5.0f, "in range passes through");
    check(beatsnap::clampf(-1.0f, 0.0f, 10.0f) == 0.0f, "below lo clamps to lo");
    check(beatsnap::clampf(11.0f, 0.0f, 10.0f) == 10.0f, "above hi clamps to hi");
    // The low bound is tested LAST, so it wins on an inverted range. Shared with
    // every other clampf copy in the fleet.
    check(beatsnap::clampf(5.0f, 10.0f, 0.0f) == 10.0f,
          "an INVERTED range yields lo, because lo is tested last");
  }

  std::printf("beatsnap::isPowerOfTwo\n");
  {
    check(!beatsnap::isPowerOfTwo(0), "zero is not a power of two (strict > 0 guard)");
    check(beatsnap::isPowerOfTwo(1), "one is");
    check(beatsnap::isPowerOfTwo(2), "two is");
    check(!beatsnap::isPowerOfTwo(3), "three is not");
    check(beatsnap::isPowerOfTwo(1024), "1024 is");
    check(!beatsnap::isPowerOfTwo(-4), "a negative is not");
    check(!beatsnap::isPowerOfTwo(INT32_MIN + 1), "a large negative is not");
  }

  std::printf("beatsnap::hannWindow\n");
  {
    const std::vector<float> w8 = beatsnap::hannWindow(8);
    check(w8.size() == 8, "n=8 returns 8 samples");
    // PERIODIC: the denominator is n, not n-1, so the endpoints are not repeated.
    checkNear(w8[0], 0.0f, 1e-7f, "PERIODIC window: first sample is 0 (cos(0)=1)");
    check(w8[4] > 0.99f && w8[4] <= 1.0f, "centre sample peaks at about 1");
    check(w8[3] > w8[2] && w8[2] > w8[1], "rises monotonically to the centre");
    check(w8[5] < w8[4] && w8[6] < w8[5], "falls monotonically after the centre");

    const std::vector<float> w1 = beatsnap::hannWindow(1);
    check(w1.size() == 1, "n=1 returns one sample");
    checkNear(w1[0], 1.0f, 0.0f, "n=1 is special-cased to 1.0 (not 0.0)");
    check(beatsnap::hannWindow(0).empty(), "n=0 returns empty");
    check(beatsnap::hannWindow(-5).empty(), "negative n returns empty");

    // A periodic Hann window's mean is exactly 0.5. SpectraKeys' copy is checked
    // against `spectra::hannCoherentGain`, recovered separately; BeatSnap has no
    // such symbol, so the anchor here is the closed form instead.
    for (int n : {2, 8, 64, 1024, 4096}) {
      const std::vector<float> w = beatsnap::hannWindow(n);
      double sum = 0.0;
      for (float v : w) sum += v;
      const double mean = sum / static_cast<double>(w.size());
      char label[80];
      std::snprintf(label, sizeof label, "mean of hannWindow(%d) is the periodic 0.5", n);
      checkNear(static_cast<float>(mean), 0.5f, 1e-4f, label);
    }
  }

  std::printf("beatsnap::fftRadix2\n");
  {
    // Guards, all three taken from the prologue.
    {
      std::vector<float> re{1.0f};
      std::vector<float> im{0.0f};
      beatsnap::fftRadix2(re, im);
      check(re.size() == 1 && re[0] == 1.0f, "n < 2 returns without touching the input");
    }
    {
      std::vector<float> re{1.0f, 2.0f, 3.0f, 4.0f};
      std::vector<float> im{0.0f, 0.0f, 0.0f};
      beatsnap::fftRadix2(re, im);
      check(re[0] == 1.0f && re[3] == 4.0f, "a length mismatch returns without transforming");
    }
    {
      std::vector<float> re{1.0f, 2.0f, 3.0f};
      std::vector<float> im{0.0f, 0.0f, 0.0f};
      beatsnap::fftRadix2(re, im);
      check(re[1] == 2.0f, "a non-power-of-two length returns without transforming");
    }

    // DC: N samples of 1.0 put N in bin 0 and nothing anywhere else.
    for (int n : {2, 4, 64}) {
      std::vector<float> re(static_cast<size_t>(n), 1.0f);
      std::vector<float> im(static_cast<size_t>(n), 0.0f);
      beatsnap::fftRadix2(re, im);
      char label[80];
      std::snprintf(label, sizeof label, "DC of N=%d lands as N in bin 0", n);
      checkNear(re[0], static_cast<float>(n), 1e-3f, label);
      bool otherBinsZero = true;
      for (int k = 1; k < n; ++k) {
        if (std::fabs(re[static_cast<size_t>(k)]) > 1e-3f ||
            std::fabs(im[static_cast<size_t>(k)]) > 1e-3f) otherBinsZero = false;
      }
      std::snprintf(label, sizeof label, "DC of N=%d leaves every other bin at zero", n);
      check(otherBinsZero, label);
    }

    // A single tone lands in the bin its frequency names.
    {
      const int n = 64;
      std::vector<float> re(static_cast<size_t>(n), 0.0f);
      std::vector<float> im(static_cast<size_t>(n), 0.0f);
      const int bin = 7;
      for (int t = 0; t < n; ++t) {
        re[static_cast<size_t>(t)] = std::cos(2.0 * 3.14159265358979323846 * bin
                                              * static_cast<double>(t) / static_cast<double>(n));
      }
      beatsnap::fftRadix2(re, im);
      float peak = 0.0f;
      int peakBin = -1;
      for (int k = 0; k < n / 2; ++k) {
        const float mag = std::sqrt(re[static_cast<size_t>(k)] * re[static_cast<size_t>(k)]
                                  + im[static_cast<size_t>(k)] * im[static_cast<size_t>(k)]);
        if (mag > peak) { peak = mag; peakBin = k; }
      }
      check(peakBin == bin, "a cosine at bin 7 peaks in bin 7");
      checkNear(peak, static_cast<float>(n) / 2.0f, 0.5f, "a full-scale tone peaks at N/2");
    }

    // The strong check: the whole spectrum against a naive DFT.
    {
      const int n = 64;
      std::vector<float> re(static_cast<size_t>(n), 0.0f);
      std::vector<float> im(static_cast<size_t>(n), 0.0f);
      for (int t = 0; t < n; ++t) {
        // Deliberately not a single tone: a mix, plus a small imaginary part, so a
        // sign error in the twiddle recurrence cannot cancel out.
        re[static_cast<size_t>(t)] = std::sin(0.31f * static_cast<float>(t))
                                   + 0.4f * std::cos(0.07f * static_cast<float>(t));
        im[static_cast<size_t>(t)] = 0.15f * std::sin(0.11f * static_cast<float>(t));
      }
      std::vector<float> refRe, refIm;
      naiveDft(re, im, refRe, refIm);

      std::vector<float> gotRe = re;
      std::vector<float> gotIm = im;
      beatsnap::fftRadix2(gotRe, gotIm);

      float worst = 0.0f;
      for (int k = 0; k < n; ++k) {
        worst = std::max(worst, std::fabs(gotRe[static_cast<size_t>(k)] - refRe[static_cast<size_t>(k)]));
        worst = std::max(worst, std::fabs(gotIm[static_cast<size_t>(k)] - refIm[static_cast<size_t>(k)]));
      }
      checkNear(worst, 0.0f, 1e-3f, "N=64 matches a naive DFT (the twiddle recurrence's drift)");

      // Parseval: the transform's energy equals the input's, times N.
      double energyIn = 0.0, energyOut = 0.0;
      for (int k = 0; k < n; ++k) {
        energyIn += static_cast<double>(re[static_cast<size_t>(k)]) * re[static_cast<size_t>(k)]
                  + static_cast<double>(im[static_cast<size_t>(k)]) * im[static_cast<size_t>(k)];
        energyOut += static_cast<double>(gotRe[static_cast<size_t>(k)]) * gotRe[static_cast<size_t>(k)]
                   + static_cast<double>(gotIm[static_cast<size_t>(k)]) * gotIm[static_cast<size_t>(k)];
      }
      checkNear(static_cast<float>(energyOut / (energyIn * n)), 1.0f, 1e-3f,
                "Parseval: the transform preserves energy (scaled by N)");
    }
  }

  std::printf("beatsnap::downmixToMono\n");
  {
    check(beatsnap::downmixToMono(nullptr, 8, 2).empty(), "a null pointer returns empty");
    check(beatsnap::downmixToMono(nullptr, 8, -3).empty(), "a null pointer with a bad count is empty too");
    {
      const float in[8] = {1.0f, 3.0f, 1.0f, 3.0f, 1.0f, 3.0f, 1.0f, 3.0f};
      check(beatsnap::downmixToMono(in, 4, 0).empty(), "channels == 0 returns empty");
      check(beatsnap::downmixToMono(in, 4, -1).empty(), "a negative channel count returns empty");
      check(beatsnap::downmixToMono(in, 0, 2).empty(), "frames == 0 returns empty");

      // IT AVERAGES. Stereo {1,3} -> 2, not 4. A summing downmix gives 4 here, and
      // the difference is a level error of `channels`, which is what the disassembly
      // settles: the sum is divided by the channel count held as a float.
      const std::vector<float> stereo = beatsnap::downmixToMono(in, 4, 2);
      check(stereo.size() == 4, "four frames produce four samples");
      checkNear(stereo[0], 2.0f, 1e-6f, "stereo {1,3} averages to 2, not 4 (it AVERAGES)");

      const std::vector<float> mono = beatsnap::downmixToMono(in, 8, 1);
      check(mono.size() == 8 && mono[0] == 1.0f && mono[1] == 3.0f, "mono passes through untouched");
    }
    {
      const float six[6] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
      const std::vector<float> out = beatsnap::downmixToMono(six, 1, 6);
      checkNear(out[0], 3.5f, 1e-5f, "a 6-channel frame averages to 3.5");
    }
    {
      // `fdiv`, not a reciprocal multiply. The disassembly is `ucvtf s0, w19` then
      // `fdiv`, and at three channels of 0.1 the two forms differ by one ulp — so
      // this asserts both halves: the value it has, and the value it does not.
      const float tenths[3] = {0.1f, 0.1f, 0.1f};
      const std::vector<float> out = beatsnap::downmixToMono(tenths, 1, 3);
      const float divided = (0.1f * 3.0f) / 3.0f;
      const float reciprocal = (0.1f * 3.0f) * (1.0f / 3.0f);
      checkNear(out[0], divided, 0.0f, "the sum is DIVIDED by the channel count (fdiv, not a reciprocal)");
      check(out[0] != reciprocal,
            "and it is NOT a reciprocal multiply — the two differ by an ulp at this input");
    }
  }


  std::printf("beatsnap::medianOfWindow\n");
  {
    const std::vector<float> odd{9.0f, 1.0f, 5.0f, 3.0f, 7.0f};
    checkNear(beatsnap::medianOfWindow(odd, 2, 2), 5.0f, 0.0f, "the whole vector of 5 sorts to a middle of 5");

    // EVEN count: the upper of the two middle values, NOT their mean. This is the
    // check that separates the recovered function from the obvious implementation.
    const std::vector<float> even{1.0f, 2.0f, 3.0f, 4.0f};
    checkNear(beatsnap::medianOfWindow(even, 1, 2), 3.0f, 0.0f,
              "an EVEN window yields sorted[count/2], the upper middle (3, not 2.5)");

    // Radius 0 is the sample itself.
    checkNear(beatsnap::medianOfWindow(odd, 3, 0), 3.0f, 0.0f, "radius 0 is the sample itself");

    // Clamping at the left edge: centre 0, radius 1 -> only samples 0 and 1, and
    // an even count again means the larger of the two.
    const std::vector<float> edge{5.0f, 1.0f, 9.0f};
    checkNear(beatsnap::medianOfWindow(edge, 0, 1), 5.0f, 0.0f, "the window clamps at the left edge");
    checkNear(beatsnap::medianOfWindow(edge, 2, 1), 9.0f, 0.0f, "and at the right edge");

    // A radius so large the window is the whole vector.
    checkNear(beatsnap::medianOfWindow(odd, 2, 9999), 5.0f, 0.0f, "an over-large radius takes the whole vector");

    // The inverted-window guard: lo > hi returns 0 rather than reading out of range.
    checkNear(beatsnap::medianOfWindow(odd, 2, -1), 0.0f, 0.0f, "a negative radius yields an inverted window and returns 0");
    check(beatsnap::medianOfWindow({}, 0, 2) == 0.0f, "an empty vector returns 0");

    // It COPIES: the caller's vector must be untouched, since a median that
    // reordered its input would corrupt the flux track for the next sample.
    std::vector<float> untouched{3.0f, 1.0f, 2.0f};
    (void)beatsnap::medianOfWindow(untouched, 1, 1);
    check(untouched[0] == 3.0f && untouched[1] == 1.0f && untouched[2] == 2.0f,
          "the input vector is NOT reordered (the window is copied before sorting)");
  }

  std::printf("beatsnap::adaptiveThreshold\n");
  {
    check(beatsnap::adaptiveThreshold({}, 2, 0.0f).empty(), "an empty track yields an empty threshold track");

    // With radius 0 the median is the sample, so the multiplier can be read back
    // out of a single-sample track. Floor is 5% of 10 = 0.5, so it does not bind.
    const std::vector<float> one{10.0f};
    checkNear(beatsnap::adaptiveThreshold(one, 0, 0.0f)[0], 50.0f, 1e-4f,
              "scale 0 gives the flat multiplier 5.0 (10 * 5.0)");
    checkNear(beatsnap::adaptiveThreshold(one, 0, 100.0f)[0], 14.0f, 1e-4f,
              "scale 100 gives the bottom of the ramp, 1.4 (10 * 1.4)");
    checkNear(beatsnap::adaptiveThreshold(one, 0, 50.0f)[0], 32.0f, 1e-3f,
              "scale 50 lands halfway down the ramp, 3.2");
    checkNear(beatsnap::adaptiveThreshold(one, 0, 500.0f)[0], 14.0f, 1e-4f,
              "scale saturates at 100 and stops falling");
    // A negative scale skips the ramp entirely and uses 5.0.
    checkNear(beatsnap::adaptiveThreshold(one, 0, -5.0f)[0], 50.0f, 1e-4f,
              "a NEGATIVE scale takes the flat 5.0 rather than ramping");

    // THE FLOOR. A quiet sample in a track whose loudest sample is 100 is lifted to
    // 5% of it — without the floor these would be 0.
    const std::vector<float> spikes{0.0f, 0.0f, 100.0f};
    const std::vector<float> floored = beatsnap::adaptiveThreshold(spikes, 0, 0.0f);
    check(floored.size() == 3, "the output has one entry per input sample");
    checkNear(floored[0], 5.0f, 1e-4f, "a silent sample is floored at 5% of the track maximum");
    checkNear(floored[1], 5.0f, 1e-4f, "and so is the next one");
    checkNear(floored[2], 500.0f, 1e-3f, "the loud sample itself is thresholded normally");

    // The maximum starts at ZERO, not at the first sample: an all-negative track
    // floors at 0, and a max_element-style floor would give -0.5 here.
    const std::vector<float> negative{-10.0f};
    checkNear(beatsnap::adaptiveThreshold(negative, 0, 0.0f)[0], 0.0f, 0.0f,
              "an all-negative track floors at ZERO, not at its largest sample");

    // The window is the median's half-width, so a wide window smooths the track.
    const std::vector<float> ramp{0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f, 0.0f};
    const std::vector<float> narrow = beatsnap::adaptiveThreshold(ramp, 1, 0.0f);
    const std::vector<float> wide = beatsnap::adaptiveThreshold(ramp, 3, 0.0f);
    check(wide[3] <= narrow[3], "a wider window does not raise the threshold at the spike");
    check(narrow.size() == wide.size() && narrow.size() == 7, "both sizes match the input length");
  }

  std::printf("beatsnap::computeFlux\n");
  {
    beatsnap::OnsetConfig cfg;
    cfg.sampleRate = 48000.0;
    cfg.fftSize = 64;
    cfg.bandLoHz = 20.0f;
    cfg.bandHiHz = 8000.0f;

    const std::vector<float> silence(256, 0.0f);

    // The four guards, in the order the prologue tests them. Each one returns
    // through the shared epilogue with the sret still zeroed, so the track is
    // empty AND both times are zero.
    {
      const beatsnap::FluxTrack t = beatsnap::computeFlux(nullptr, 256, cfg);
      check(t.flux.empty(), "computeFlux: a null sample pointer yields an empty track");
      check(t.hopSeconds == 0.0 && t.windowSeconds == 0.0,
            "computeFlux: a null sample pointer leaves the hop and window times at zero");
    }
    {
      const beatsnap::FluxTrack t = beatsnap::computeFlux(silence.data(), 0, cfg);
      check(t.flux.empty(), "computeFlux: n == 0 yields an empty track");
      check(t.hopSeconds == 0.0 && t.windowSeconds == 0.0,
            "computeFlux: n == 0 leaves the hop and window times at zero");
    }
    {
      beatsnap::OnsetConfig bad = cfg;
      bad.sampleRate = 0.0;
      const beatsnap::FluxTrack t = beatsnap::computeFlux(silence.data(), 256, bad);
      check(t.flux.empty(), "computeFlux: a non-positive sample rate yields an empty track");
    }
    {
      beatsnap::OnsetConfig bad = cfg;
      bad.fftSize = 6;  // not a power of two
      const beatsnap::FluxTrack t = beatsnap::computeFlux(silence.data(), 256, bad);
      check(t.flux.empty(), "computeFlux: a non-power-of-two fft size yields an empty track");
    }
    {
      beatsnap::OnsetConfig bad = cfg;
      bad.fftSize = 2;  // a power of two, but below four
      const beatsnap::FluxTrack t = beatsnap::computeFlux(silence.data(), 256, bad);
      check(t.flux.empty(),
            "computeFlux: fft == 2 yields an empty track (the `fft < 4` arm excludes it)");
    }

    // THE ORDERING. `stp d0, d1, [x8, #0x18]` comes BEFORE `cmp w27, #0x1`, so a
    // signal too short to make a single frame still comes back with its two
    // times filled in. Only the four guards above leave them at zero.
    {
      const std::vector<float> few(32, 0.0f);  // 32 samples, fft is 64
      const beatsnap::FluxTrack t = beatsnap::computeFlux(few.data(), few.size(), cfg);
      check(t.flux.empty(), "computeFlux: a signal shorter than one frame yields no flux");
      check(t.hopSeconds == 16.0 / 48000.0,
            "computeFlux: ... but hopSeconds is STILL set (hopSize/rate = 16/48000)");
      check(t.windowSeconds == 32.0 / 48000.0,
            "computeFlux: ... and windowSeconds is STILL set (fft*0.5/rate = 32/48000)");
    }

    // The frame count: `hops = (n < fft) ? 0 : (n - fft)/hopSize + 1`.
    {
      const std::vector<float> exact(64, 0.0f);
      const beatsnap::FluxTrack t = beatsnap::computeFlux(exact.data(), exact.size(), cfg);
      check(t.flux.size() == 1, "computeFlux: n == fft yields exactly one frame");
      check(t.flux[0] == 0.0f, "computeFlux: the first frame's flux is a literal zero");
    }
    {
      const std::vector<float> two(80, 0.0f);  // fft + hop
      const beatsnap::FluxTrack t = beatsnap::computeFlux(two.data(), two.size(), cfg);
      check(t.flux.size() == 2, "computeFlux: n == fft + hop yields two frames");
      check(t.flux[0] == 0.0f, "computeFlux: ... and the first frame is still a literal zero");
    }

    // A burst, so the flux is not trivially zero everywhere.
    std::vector<float> burst(512, 0.0f);
    for (size_t i = 128; i < 192; ++i) burst[i] = 1.0f;
    {
      const beatsnap::FluxTrack t = beatsnap::computeFlux(burst.data(), burst.size(), cfg);
      check(t.flux.size() == (512 - 64) / 16 + 1,
            "computeFlux: the frame count follows (n - fft)/hopSize + 1");
      check(t.flux[0] == 0.0f, "computeFlux: the first frame's flux is zero even for a loud signal");
      bool nonNegative = true;
      bool somePositive = false;
      for (float f : t.flux) {
        if (f < 0.0f) nonNegative = false;
        if (f > 0.0f) somePositive = true;
      }
      check(nonNegative,
            "computeFlux: the flux sums POSITIVE differences, so it is never negative");
      check(somePositive, "computeFlux: a burst produces a positive flux");
    }

    // The band pair is min/max'd and then SORTED, so an upside-down band is
    // the same band.
    {
      beatsnap::OnsetConfig inverted = cfg;
      inverted.bandLoHz = 8000.0f;
      inverted.bandHiHz = 20.0f;
      const beatsnap::FluxTrack a = beatsnap::computeFlux(burst.data(), burst.size(), cfg);
      const beatsnap::FluxTrack b = beatsnap::computeFlux(burst.data(), burst.size(), inverted);
      check(a.flux == b.flux,
            "computeFlux: an upside-down band gives the identical flux (the pair is sorted)");
    }
  }

  std::printf("beatsnap::pickOnsets\n");
  {
    beatsnap::FluxTrack track;
    track.hopSeconds = 0.25;
    track.windowSeconds = 0.5;

    // The two guards, in the listing's order: a track shorter than three
    // samples, and a threshold track that is not the same length.
    {
      beatsnap::FluxTrack tiny = track;
      tiny.flux = {0.0f, 1.0f};
      const std::vector<float> thr{0.0f, 0.0f};
      check(beatsnap::pickOnsets(tiny, thr, 0.0f).empty(),
            "pickOnsets: a flux track of fewer than three samples yields nothing");
    }
    {
      beatsnap::FluxTrack t = track;
      t.flux = {0.0f, 1.0f, 1.0f, 0.0f};
      const std::vector<float> shortThr{0.0f, 0.0f, 0.0f};
      check(beatsnap::pickOnsets(t, shortThr, 0.0f).empty(),
            "pickOnsets: a threshold track of a different length yields nothing");
    }

    // THE ASYMMETRIC PEAK TEST. The left neighbour is compared with `>=` and the
    // right neighbour with `>`, so a plateau is picked at its LAST sample.
    {
      beatsnap::FluxTrack t = track;
      t.flux = {0.0f, 1.0f, 1.0f, 0.0f};
      const std::vector<float> thr{0.0f, 0.0f, 0.0f, 0.0f};
      const std::vector<double> got = beatsnap::pickOnsets(t, thr, 0.0f);
      check(got.size() == 1, "pickOnsets: a two-sample plateau yields exactly one onset");
      check(got.size() == 1 && got[0] == 2.0 * 0.25 + 0.5,
            "pickOnsets: a plateau is picked at its LAST sample (index 2)");
    }
    {
      // The right-hand test is STRICT: a plateau that ends the track is not a
      // peak, because flux[i] > flux[i+1] fails.
      beatsnap::FluxTrack t = track;
      t.flux = {0.0f, 1.0f, 1.0f};
      const std::vector<float> thr{0.0f, 0.0f, 0.0f};
      check(beatsnap::pickOnsets(t, thr, 0.0f).empty(),
            "pickOnsets: the right-hand neighbour test is strict, so a trailing plateau is not a peak");
    }
    {
      // The threshold test is strict too: flux[i] must be GREATER than the
      // threshold, not equal to it.
      beatsnap::FluxTrack t = track;
      t.flux = {0.0f, 5.0f, 0.0f};
      const std::vector<float> thr{0.0f, 5.0f, 0.0f};
      check(beatsnap::pickOnsets(t, thr, 0.0f).empty(),
            "pickOnsets: a sample exactly equal to its threshold is NOT an onset");
    }

    // Two peaks, and the merge that collapses them.
    {
      beatsnap::FluxTrack t = track;
      t.flux = {0.0f, 5.0f, 0.0f, 5.0f, 0.0f};
      const std::vector<float> thr{0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

      const std::vector<double> both = beatsnap::pickOnsets(t, thr, 500.0f);
      check(both.size() == 2, "pickOnsets: two clear peaks yield two onsets");
      check(both.size() == 2 && both[0] == 0.25 + 0.5 && both[1] == 3.0 * 0.25 + 0.5,
            "pickOnsets: an onset's time is i*hopSeconds + windowSeconds");

      // tol = scale/1000 = 0.5, and the two onsets are exactly 0.5 apart, so
      // `next - last >= tol` KEEPS the second one. Equality is kept.
      check(both.size() == 2,
            "pickOnsets: an onset exactly one tolerance after the last is KEPT (the test is >=)");

      // tol = 1.0 now, so the second onset is closer than the tolerance and
      // collapses onto the FIRST.
      const std::vector<double> merged = beatsnap::pickOnsets(t, thr, 1000.0f);
      check(merged.size() == 1, "pickOnsets: two onsets inside the tolerance collapse into one");
      check(merged.size() == 1 && merged[0] == 0.25 + 0.5,
            "pickOnsets: the merge keeps the FIRST of the collapsed pair");

      // A negative scale makes the tolerance zero, so nothing is merged away.
      const std::vector<double> unmerged = beatsnap::pickOnsets(t, thr, -100.0f);
      check(unmerged.size() == 2,
            "pickOnsets: a negative scale clamps the tolerance to zero, so both onsets survive");
    }
  }

  if (failures == 0) std::printf("ALL RECOVERED BEATSNAP MATHS VERIFIED (%d checks)\n", passes);
  else std::printf("%d FAILURES out of %d checks\n", failures, passes + failures);
  return failures == 0 ? 0 : 1;
}
