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
  // Unbuffered: if a check crashes, the redirected output must still show how
  // far it got. Block buffering hid the crash site once already.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
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


  std::printf("bandValues (falloff envelope + output mapping)\n");
  {
    // 3 frames of 4 bins: a loud frame then two silent ones, so the envelope
    // has something to decay from.
    spectra::Spectrogram sp;
    sp.binCount = 4;
    sp.frameCount = 3;
    sp.hzPerBin = 100.0;
    sp.frames.assign(3, std::vector<float>(4, 0.0f));
    sp.frames[0][0] = 1.0f;

    auto mk = [&]() {
      spectra::BandSpec b{};
      b.lowHz = 0.0f; b.highHz = 400.0f; b.mode = 2;   // Average over all 4 bins
      b.thresholdDb = -100.0f;                          // silence reads 0, not negative
      b.floorDb = -100.0f; b.ceilingDb = 0.0f; b.gainDb = 0.0f;
      b.outputMin = 0.0f; b.outputMax = 1.0f;
      b.falloffMode = 1; b.falloffTime = 1.0f; b.binarise = false;
      return b;
    };

    // Frame 0 averages to 0.25 -> -12.04 dB -> 0.8796 of a [-100, 0] window.
    const float expect0 = (20.0f * std::log10(0.25f) + 100.0f) / 100.0f;

    // Instant: every frame reports its own level, no decay carried over.
    {
      const std::vector<float> v = spectra::bandValues(sp, mk(), 1.0);
      check(v.size() == 3, "one output per frame");
      checkNear(v[0], expect0, 1e-4f, "Instant: loud frame mapped through the window");
      checkNear(v[1], 0.0f, 1e-6f, "Instant: the silent frame drops straight to the threshold");
      checkNear(v[2], 0.0f, 1e-6f, "Instant: and stays there");
    }

    // Exponential: value = level + (prev - level) * decay, decay = exp(-(1/time)/falloff).
    {
      spectra::BandSpec b = mk();
      b.falloffMode = 3; b.falloffTime = 1.0f;
      const std::vector<float> v = spectra::bandValues(sp, b, 1.0);
      const float decay = static_cast<float>(std::exp(-1.0));
      checkNear(v[1], expect0 * decay, 1e-4f, "Exponential: frame 1 decays from the peak by exp(-1)");
      checkNear(v[2], expect0 * decay * decay, 1e-4f, "Exponential: frame 2 decays again");
      check(v[1] > v[2] && v[2] > 0.0f, "Exponential decay is gradual, not a cliff");
    }

    // Linear: a constant step per frame, floored at the current level.
    {
      spectra::BandSpec b = mk();
      b.falloffMode = 2; b.falloffTime = 10.0f;   // step = (1/1)/10 = 0.1
      const std::vector<float> v = spectra::bandValues(sp, b, 1.0);
      checkNear(v[1], expect0 - 0.1f, 1e-4f, "Linear: frame 1 drops by exactly one step");
      checkNear(v[2], expect0 - 0.2f, 1e-4f, "Linear: frame 2 drops by two steps");
    }

    // None (Integrate): accumulates and clamps into [0, 1].
    {
      spectra::BandSpec b = mk();
      b.falloffMode = 4;
      const std::vector<float> v = spectra::bandValues(sp, b, 1.0);
      check(v[0] > 0.0f, "Integrate: accumulates the first frame");
      check(v[1] >= v[0] && v[1] <= 1.0f, "Integrate: keeps rising and clamps at 1");
      check(v[2] >= v[1] && v[2] <= 1.0f, "Integrate: still within range on the third frame");
    }

    // Output mapping is a lerp into [outputMin, outputMax].
    {
      spectra::BandSpec b = mk();
      b.outputMin = 100.0f; b.outputMax = 200.0f;
      const std::vector<float> v = spectra::bandValues(sp, b, 1.0);
      checkNear(v[0], 100.0f + expect0 * 100.0f, 1e-3f, "output range remaps the envelope (lerp to 100..200)");
      checkNear(v[1], 100.0f, 1e-3f, "a thresholded frame sits at outputMin");
    }

    // The binarise flag collapses any level to 0 or 1 before the envelope.
    {
      spectra::BandSpec b = mk();
      b.binarise = true;
      const std::vector<float> v = spectra::bandValues(sp, b, 1.0);
      checkNear(v[0], 1.0f, 1e-6f, "binarise: the loud frame becomes exactly 1");
    }

    // Guards.
    {
      check(spectra::bandValues(sp, mk(), 0.0).empty(), "time == 0 returns empty (no frame rate)");
      check(spectra::bandValues(sp, mk(), -1.0).empty(), "negative time returns empty");
      spectra::Spectrogram none;
      none.binCount = 4; none.frameCount = 0; none.hzPerBin = 100.0;
      check(spectra::bandValues(none, mk(), 1.0).empty(), "frameCount 0 returns empty");
    }
  }


  std::printf("analyse (STFT: centred frames, zero padding, magnitude)\n");
  {
    spectra::AnalysisConfig cfg{};
    cfg.sampleRate = 8000.0;   // 8 kHz keeps the arithmetic easy to read
    cfg.fftSize = 64;
    cfg.hopRateHz = 125.0;    // samplesPerHop = 8000/125 = 64

    // 512 samples at 8 kHz with 64 samples per hop gives exactly 8 frames.
    const size_t N = 512;
    std::vector<float> mono(N, 0.0f);

    // Pure tone at exactly bin 4: frequency = bin * sampleRate / fftSize.
    const double binHz = 4.0 * 8000.0 / 64.0;   // 500 Hz
    for (size_t t = 0; t < N; ++t)
      mono[t] = static_cast<float>(std::sin(2.0 * M_PI * binHz * static_cast<double>(t) / 8000.0));

    const spectra::Spectrogram sp = spectra::analyse(mono.data(), N, cfg);

    check(sp.binCount == 32, "binCount is fftSize/2 (real-packed spectrum)");
    check(sp.frameCount == 8, "frameCount is ceil(frames / samplesPerHop)");
    checkNear(static_cast<float>(sp.hzPerBin), 8000.0f / 64.0f, 1e-6f, "hzPerBin = sampleRate / fftSize");
    check(sp.frames.size() == 8, "one spectrum row per frame");
    if (sp.frames.size() < 8) { std::printf("  (skipping dependent checks: too few frames)\n"); }
    check(sp.frames[4].size() == 32, "each row has binCount entries");

    // A bin-4 tone must dominate bin 4 in a middle frame.
    const std::vector<float>& mid = sp.frames.at(4);
    size_t peakBin = 0;
    for (size_t k = 1; k < mid.size(); ++k) if (mid[k] > mid[peakBin]) peakBin = k;
    check(peakBin == 4, "a 500 Hz tone peaks in the bin it belongs to");

    // Amplitude scale, and the window's signature. For a bin-centred unit sine the
    // Hann window gives A*N/4 at the peak bin, and the 4/fftSize scale divides
    // that back to exactly A. Hann also puts first sidelobes at half the peak and
    // nulls beyond, so 0.5 / 1.0 / 0.5 / 0.0 is a fingerprint of a correct
    // windowed DFT — a wrong window, scale or bin mapping breaks the pattern.
    checkNear(mid[4], 1.0f, 1e-3f, "a bin-centred unit sine normalises to exactly 1.0");
    checkNear(mid[3], 0.5f, 1e-3f, "Hann first sidelobe sits at half the peak");
    checkNear(mid[5], 0.5f, 1e-3f, "and symmetrically on the other side");
    checkNear(mid[6], 0.0f, 1e-4f, "and the next bin out is a null");

    // Zero padding: the first and last frames are centred on positions that run
    // off both ends of the signal, so they must still exist rather than be skipped.
    bool firstRowFinite = true, lastRowFinite = true;
    for (float v : sp.frames.front()) if (!std::isfinite(v)) firstRowFinite = false;
    for (float v : sp.frames.back()) if (!std::isfinite(v)) lastRowFinite = false;
    check(firstRowFinite && lastRowFinite, "edge frames are zero-padded, not dropped or NaN");

    // A signal shorter than one window still yields at least one frame.
    {
      std::vector<float> tiny(8, 1.0f);
      const spectra::Spectrogram s2 = spectra::analyse(tiny.data(), tiny.size(), cfg);
      check(s2.frameCount >= 1, "a signal shorter than the window still yields a frame");
    }

    // Silence is silent, and finite.
    {
      std::vector<float> silence(512, 0.0f);
      const spectra::Spectrogram s3 = spectra::analyse(silence.data(), silence.size(), cfg);
      bool allZero = true;
      for (const auto& row : s3.frames) for (float v : row) if (v != 0.0f) allZero = false;
      check(allZero, "silence produces exactly zero magnitudes (no NaN from the 1e-9 guard path)");
    }

    // The guards mirror the compiled ones.
    check(spectra::analyse(nullptr, 512, cfg).frames.empty(), "null input returns empty");
    check(spectra::analyse(mono.data(), 0, cfg).frames.empty(), "zero frames returns empty");
    {
      spectra::AnalysisConfig bad = cfg; bad.fftSize = 48;   // not a power of two
      check(spectra::analyse(mono.data(), N, bad).frames.empty(), "non-power-of-two fftSize returns empty");
      spectra::AnalysisConfig bad2 = cfg; bad2.sampleRate = 0.0;
      check(spectra::analyse(mono.data(), N, bad2).frames.empty(), "zero sampleRate returns empty");
      spectra::AnalysisConfig bad3 = cfg; bad3.hopRateHz = 0.0;
      check(spectra::analyse(mono.data(), N, bad3).frames.empty(), "zero hop returns empty");
    }
  }


  std::printf("analyseBands (one transform, many bands)\n");
  {
    spectra::AnalysisConfig cfg{};
    cfg.sampleRate = 8000.0; cfg.fftSize = 64; cfg.hopRateHz = 125.0;  // 64 samples/hop, 8 ms

    const size_t N = 512;                       // 8 frames
    std::vector<float> mono(N, 0.0f);
    const double binHz = 4.0 * 8000.0 / 64.0;   // 500 Hz, exactly bin 4
    for (size_t t = 0; t < N; ++t)
      mono[t] = static_cast<float>(std::sin(2.0 * M_PI * binHz * static_cast<double>(t) / 8000.0));

    auto mkband = [](float lo, float hi) {
      spectra::BandSpec b{};
      b.lowHz = lo; b.highHz = hi; b.mode = 1;          // Peak
      b.thresholdDb = -100.0f; b.floorDb = -100.0f; b.ceilingDb = 0.0f; b.gainDb = 0.0f;
      b.outputMin = 0.0f; b.outputMax = 1.0f;
      b.falloffMode = 1; b.falloffTime = 1.0f; b.binarise = false;
      return b;
    };

    // One band around the tone, one in a silent region.
    std::vector<spectra::BandSpec> bands{ mkband(400.0f, 600.0f), mkband(3000.0f, 3400.0f) };
    const auto out = spectra::analyseBands(mono.data(), N, cfg, bands);

    check(out.size() == 2, "one result row per band");
    check(out[0].size() == 8, "each row is frameCount long");
    check(out[1].size() == 8, "and so is the second");

    // The band spanning the tone must be loud; the band in silence must be at the
    // floor. This is the property that matters: bands are independent.
    bool toneLoud = true, silenceQuiet = true;
    for (float v : out[0]) if (v < 0.5f) toneLoud = false;
    for (float v : out[1]) if (v > 1e-6f) silenceQuiet = false;
    check(toneLoud, "the band containing the tone reads loud in every frame");
    check(silenceQuiet, "the band in silence reads zero in every frame");

    // Consistency: analyseBands must agree with analyse + bandValues called by
    // hand. This is what catches a wrong hopRate being forwarded.
    {
      const spectra::Spectrogram sp = spectra::analyse(mono.data(), N, cfg);
      const std::vector<float> manual = spectra::bandValues(sp, bands[0], cfg.hopRateHz);
      bool same = manual.size() == out[0].size();
      if (same) for (size_t i = 0; i < manual.size(); ++i) if (manual[i] != out[0][i]) same = false;
      check(same, "matches analyse() + bandValues() called separately (hop rate forwarded correctly)");
    }

    // Empty band list yields an empty result rather than crashing.
    {
      const std::vector<spectra::BandSpec> none;
      check(spectra::analyseBands(mono.data(), N, cfg, none).empty(), "no bands yields an empty result");
    }

    // An invalid config makes analyse() return its zeroed Spectrogram, whose
    // frameCount is 0. analyseBands does NOT skip bands in that case — the
    // compiled loop calls bandValues for every band regardless — and bandValues
    // returns an empty vector when frameCount is 0. So the result is one EMPTY
    // row per band, not an empty result. That distinction is also what the
    // zero-initialisation of Spectrogram protects: without it this path read an
    // indeterminate frameCount and crashed.
    {
      spectra::AnalysisConfig bad = cfg; bad.fftSize = 48;   // not a power of two
      const auto rows = spectra::analyseBands(mono.data(), N, bad, bands);
      check(rows.size() == bands.size(), "an invalid config still yields one row per band");
      bool allEmpty = true;
      for (const auto& r : rows) if (!r.empty()) allEmpty = false;
      check(allEmpty, "and every such row is empty, because frameCount is 0");
    }
  }

  std::printf("\n%s\n", failures == 0 ? "ALL RECOVERED MATHS VERIFIED" : "TRANSCRIPTION ERRORS PRESENT");
  return failures == 0 ? 0 : 1;
}
