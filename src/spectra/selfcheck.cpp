// selfcheck.cpp — compile-and-run verification of the recovered spectra maths.
//
// The recovered implementations in spectra.cpp were transcribed from arm64
// disassembly by hand, so they need checking rather than trusting. This harness
// links against them and asserts their mathematical properties, then
// cross-checks the two functions that constrain each other.
//
// It is deliberately dependency-free so it can run anywhere clang++ exists.
// A non-zero exit means the transcription is wrong.

#include "spectra.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <algorithm>

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
  std::printf("clampf\n");
  checkNear(spectra::clampf(5.0f, 0.0f, 10.0f), 5.0f, 0.0f, "mid value passes through");
  checkNear(spectra::clampf(-3.0f, 0.0f, 10.0f), 0.0f, 0.0f, "below lo clamps to lo");
  checkNear(spectra::clampf(99.0f, 0.0f, 10.0f), 10.0f, 0.0f, "above hi clamps to hi");
  checkNear(spectra::clampf(0.0f, 0.0f, 10.0f), 0.0f, 0.0f, "boundary lo is inclusive");
  checkNear(spectra::clampf(10.0f, 0.0f, 10.0f), 10.0f, 0.0f, "boundary hi is inclusive");
  // The disassembly compares lo FIRST in program order after hi, but the two
  // fcsel results compose so that lo wins on conflict; verify that reading.
  checkNear(spectra::clampf(-1.0f, 2.0f, 1.0f), 2.0f, 0.0f, "inverted range: lo wins (matches fcsel order)");

  std::printf("isPowerOfTwo\n");
  check(spectra::isPowerOfTwo(1), "1 is a power of two (matches csel, n>=1)");
  check(spectra::isPowerOfTwo(2) && spectra::isPowerOfTwo(4) && spectra::isPowerOfTwo(1024),
        "2, 4, 1024 are powers of two");
  check(!spectra::isPowerOfTwo(0), "0 is not");
  check(!spectra::isPowerOfTwo(-4), "negatives are not");
  check(!spectra::isPowerOfTwo(3) && !spectra::isPowerOfTwo(6), "3 and 6 are not");

  std::printf("linearToDb / dbToLinear\n");
  checkNear(spectra::linearToDb(1.0f), 0.0f, 1e-5f, "0 dB at unity");
  checkNear(spectra::linearToDb(0.5f), -6.0206f, 1e-3f, "half amplitude is about -6.02 dB");
  checkNear(spectra::linearToDb(10.0f), 20.0f, 1e-4f, "10x amplitude is 20 dB (factor 20, not 10)");
  checkNear(spectra::dbToLinear(0.0f), 1.0f, 1e-6f, "0 dB is unity");
  checkNear(spectra::dbToLinear(-6.0206f), 0.5f, 1e-3f, "inverse of the above");
  // The 1e-9f floor is the log(0) guard; without it this is -inf.
  check(std::isfinite(spectra::linearToDb(0.0f)), "0.0 does not produce -inf (1e-9f guard holds)");
  check(spectra::linearToDb(0.0f) < -170.0f, "and floors at about -180 dB");
  check(std::isfinite(spectra::linearToDb(-1.0f)), "negative input is guarded too");

  std::printf("hannWindow\n");
  const std::vector<float> w8 = spectra::hannWindow(8);
  check(w8.size() == 8, "n=8 returns 8 samples");
  checkNear(w8[0], 0.0f, 1e-7f, "PERIODIC window: first sample is 0 (cos(0)=1)");
  check(w8[4] > 0.99f && w8[4] <= 1.0f, "centre sample peaks at about 1");
  check(w8[3] > w8[2] && w8[2] > w8[1], "rises monotonically to the centre");
  check(w8[5] < w8[4] && w8[6] < w8[5], "falls monotonically after the centre");

  const std::vector<float> w1 = spectra::hannWindow(1);
  checkNear(w1[0], 1.0f, 0.0f, "n=1 is special-cased to 1.0 (not 0.0)");
  check(spectra::hannWindow(0).empty() || spectra::hannWindow(0).size() == 0, "n=0 returns empty");
  check(spectra::hannWindow(-5).empty(), "negative n returns empty");

  std::printf("cross-check: Hann mean vs hannCoherentGain\n");
  // These two were recovered independently — one from a loop, one from a bare
  // `fmov s0, #0.5`. The mean of a periodic Hann window is exactly 0.5, so if
  // both transcriptions are right they must agree. This is the strongest check
  // available without running the original binary.
  for (int n : {2, 8, 64, 1024, 4096}) {
    const std::vector<float> w = spectra::hannWindow(n);
    double sum = 0.0;
    for (float v : w) sum += v;
    const double mean = sum / static_cast<double>(w.size());
    char label[96];
    std::snprintf(label, sizeof label, "mean of hannWindow(%d) == hannCoherentGain()", n);
    checkNear(static_cast<float>(mean), spectra::hannCoherentGain(), 1e-4f, label);
  }

  std::printf("Spectrogram::hzToBin\n");
  {
    // 1024 bins covering 0..22050 Hz => 21.533203125 Hz per bin.
    spectra::Spectrogram s;
    s.binCount = 1024;
    s.hzPerBin = 22050.0 / 1024.0;

    check(s.hzToBin(0.0f) == 0, "DC maps to bin 0");
    check(s.hzToBin(21.0f) == 1, "just under one bin width rounds to bin 1");
    check(s.hzToBin(1000.0f) == 46, "1 kHz lands on the expected bin");

    // The guard: a bin width that was never set must not divide.
    spectra::Spectrogram unset;
    unset.binCount = 1024;
    unset.hzPerBin = 0.0;
    check(unset.hzToBin(1000.0f) == 0, "hzPerBin == 0 returns 0 rather than inf/NaN");
    unset.hzPerBin = -5.0;
    check(unset.hzToBin(1000.0f) == 0, "negative hzPerBin also returns 0 (b.ls, not b.eq)");

    // Clamping, both ends.
    spectra::Spectrogram small;
    small.binCount = 8;
    small.hzPerBin = 1000.0;
    check(small.hzToBin(99999.0f) == 7, "clamps high to binCount - 1");
    check(small.hzToBin(-500.0f) == 0, "clamps negative to 0 (branchless bic)");
    check(small.hzToBin(-0.4f) == 0, "a value rounding to -0 is clamped to 0");

    // fcvtas rounds ties away from zero, so a half-bin goes up.
    spectra::Spectrogram tie;
    tie.binCount = 64;
    tie.hzPerBin = 100.0;
    check(tie.hzToBin(50.0f) == 1, "exact half-bin rounds away from zero (fcvtas, not banker's)");
    check(tie.hzToBin(150.0f) == 2, "and again one bin up");
  }

  std::printf("fftRadix2 (verified against a naive DFT)\n");
  {
    // Naive O(n^2) DFT with the same forward convention the recovered code uses
    // (angle -2*pi*k*n/N). This is the only check that can catch a sign,
    // bit-reversal or indexing error; the property checks alone cannot.
    auto naiveDft = [](std::vector<float> re, std::vector<float> im,
                       std::vector<double>& outRe, std::vector<double>& outIm) {
      const size_t n = re.size();
      outRe.assign(n, 0.0);
      outIm.assign(n, 0.0);
      for (size_t k = 0; k < n; ++k) {
        double sr = 0.0, si = 0.0;
        for (size_t t = 0; t < n; ++t) {
          const double ang = -2.0 * M_PI * static_cast<double>(k * t) / static_cast<double>(n);
          const double c = std::cos(ang), s = std::sin(ang);
          sr += static_cast<double>(re[t]) * c - static_cast<double>(im[t]) * s;
          si += static_cast<double>(re[t]) * s + static_cast<double>(im[t]) * c;
        }
        outRe[k] = sr; outIm[k] = si;
      }
    };

    // 1. An impulse transforms to a flat spectrum of 1.0.
    {
      std::vector<float> re(8, 0.0f), im(8, 0.0f);
      re[0] = 1.0f;
      spectra::fftRadix2(re, im);
      bool flat = true;
      for (size_t k = 0; k < 8; ++k) {
        if (std::fabs(re[k] - 1.0f) > 1e-5f || std::fabs(im[k]) > 1e-5f) flat = false;
      }
      check(flat, "impulse -> flat unit spectrum (catches a broken bit-reversal)");
    }

    // 2. A sinusoid at exactly bin 2 peaks at bin 2, and its mirror.
    {
      const size_t n = 32;
      std::vector<float> re(n), im(n, 0.0f);
      for (size_t t = 0; t < n; ++t)
        re[t] = std::sin(2.0 * M_PI * 2.0 * static_cast<double>(t) / static_cast<double>(n));
      spectra::fftRadix2(re, im);
      auto mag = [&](size_t k) { return std::sqrt(re[k] * re[k] + im[k] * im[k]); };
      check(mag(2) > 10.0f, "a bin-2 sinusoid peaks at bin 2");
      check(mag(30) > 10.0f, "and at its mirror bin N-2 (real input, conjugate symmetry)");
      check(mag(7) < 1e-3f, "and is ~0 elsewhere");
    }

    // 3. Agreement with a naive DFT across sizes, including DC and Nyquist.
    for (size_t n : {2u, 4u, 8u, 16u, 64u}) {
      // Deterministic pseudo-random input, so a failure is reproducible.
      uint32_t seed = 0x1234567u;
      auto next = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return (static_cast<float>(seed >> 8) / 8388608.0f) - 1.0f;  // ~[-1, 1)
      };
      std::vector<float> re(n), im(n);
      for (size_t i = 0; i < n; ++i) { re[i] = next(); im[i] = next(); }

      std::vector<float> re2 = re, im2 = im;
      spectra::fftRadix2(re2, im2);

      std::vector<double> dr, di;
      naiveDft(re, im, dr, di);

      double worst = 0.0;
      for (size_t k = 0; k < n; ++k) {
        worst = std::max(worst, std::fabs(re2[k] - dr[k]));
        worst = std::max(worst, std::fabs(im2[k] - di[k]));
      }
      char label[112];
      std::snprintf(label, sizeof label,
                    "n=%zu matches the naive DFT (worst abs error %.2e; recurrence drift tolerated)",
                    n, worst);
      // The twiddle recurrence accumulates error, so the tolerance is a
      // property of the recovered algorithm rather than of this transcription.
      check(worst < 1e-2, label);
    }

    // 4. The guard: silently does nothing rather than misbehaving.
    {
      std::vector<float> re(6, 1.0f), im(6, 0.0f);  // 6 is not a power of two
      const std::vector<float> before = re;
      spectra::fftRadix2(re, im);
      check(re == before, "non-power-of-two length is left untouched");

      std::vector<float> r2(8, 1.0f), i2(4, 0.0f);  // size mismatch
      const std::vector<float> r2before = r2;
      spectra::fftRadix2(r2, i2);
      check(r2 == r2before, "mismatched re/im sizes are left untouched");

      std::vector<float> r3(1, 1.0f), i3(1, 0.0f);  // n < 2
      const std::vector<float> r3before = r3;
      spectra::fftRadix2(r3, i3);
      check(r3 == r3before, "n < 2 is left untouched");
    }
  }


  std::printf("rawBandLevel (band -> animatable level)\n");
  {
    spectra::Spectrogram sp;
    sp.binCount = 16;
    sp.hzPerBin = 100.0;              // bins at 0,100,...1500 Hz
    std::vector<float> spectrum(16, 0.0f);
    spectrum[3] = 0.5f;               // the loud bin
    spectrum[4] = 0.25f;

    auto band = [&](int mode, float lowHz, float highHz) {
      spectra::BandSpec b{};
      b.lowHz = lowHz; b.highHz = highHz; b.mode = mode;
      b.thresholdDb = -200.0f;        // effectively no gate
      b.floorDb = -100.0f;
      b.ceilingDb = 0.0f;
      b.gainDb = 0.0f;
      return b;
    };

    // Peak mode reports the strongest bin in the band, not the sum. The band
    // holds a 0.5 peak, and 0.5 amplitude is -6.0206 dB, so through a [-100, 0]
    // window that lands at (100 - 6.0206)/100, NOT at full scale.
    const float peak = spectra::rawBandLevel(spectrum, sp, band(1, 0.0f, 1600.0f));
    checkNear(peak, (20.0f * std::log10(0.5f) + 100.0f) / 100.0f, 1e-4f,
              "Peak mode: 0.5 peak = -6.02 dB -> 0.9398 of the window");

    // Average mode over the same band: (0.5 + 0.25) / 16 = 0.0469, i.e. -26.6 dB.
    const float avg = spectra::rawBandLevel(spectrum, sp, band(2, 0.0f, 1600.0f));
    checkNear(avg, (20.0f * std::log10((0.5f + 0.25f) / 16.0f) + 100.0f) / 100.0f, 1e-4f,
              "Average mode: mean of the band, not the peak");

    // RMS mode sits between peak and average for this sparse spectrum.
    const float rms = spectra::rawBandLevel(spectrum, sp, band(3, 0.0f, 1600.0f));
    check(rms > avg && rms < peak, "RMS mode sits between average and peak");

    // A reversed band (high < low) is swapped, not rejected.
    const float reversed = spectra::rawBandLevel(spectrum, sp, band(1, 1600.0f, 0.0f));
    checkNear(reversed, peak, 0.0f, "a reversed band is swapped, giving the same level as the forward one");

    // The threshold is a gate.
    spectra::BandSpec gated = band(1, 0.0f, 1600.0f);
    gated.thresholdDb = 50.0f;        // nothing can reach +50 dB
    checkNear(spectra::rawBandLevel(spectrum, sp, gated), 0.0f, 0.0f, "above the threshold, the band reads 0");

    // A degenerate range reports 0 rather than dividing by zero.
    spectra::BandSpec flat = band(1, 0.0f, 1600.0f);
    flat.ceilingDb = flat.floorDb;
    checkNear(spectra::rawBandLevel(spectrum, sp, flat), 0.0f, 0.0f, "ceiling == floor returns 0, not inf/NaN");

    // THE OBSERVED EDGE CASE, asserted so it cannot regress silently and so the
    // suggested fix is obviously a behaviour change rather than a tidy-up.
    // Subtract 30 dB via negative gain so the level lands below floorDb while
    // still passing the threshold test (threshold below floor).
    spectra::BandSpec raw = band(1, 0.0f, 1600.0f);
    raw.gainDb = -200.0f;             // enough to fall far below floorDb
    raw.thresholdDb = -1000.0f;       // deliberately BELOW floorDb, so no early gate
    raw.floorDb = -100.0f;
    const float below = spectra::rawBandLevel(spectrum, sp, raw);
    check(below < 0.0f,
          "gain pushing the level below floorDb yields a NEGATIVE value (observed; clampf would give 0)");

    // Empty spectrum and zero bins are guarded.
    checkNear(spectra::rawBandLevel(std::vector<float>(), sp, band(1, 0.0f, 1600.0f)), 0.0f, 0.0f,
              "empty spectrum returns 0");
  }

  std::printf("\n%s\n", failures == 0 ? "ALL RECOVERED MATHS VERIFIED" : "TRANSCRIPTION ERRORS PRESENT");
  return failures == 0 ? 0 : 1;
}
