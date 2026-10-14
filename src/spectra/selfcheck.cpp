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
#include <cstdlib>

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

  std::printf("\n%s\n", failures == 0 ? "ALL RECOVERED MATHS VERIFIED" : "TRANSCRIPTION ERRORS PRESENT");
  return failures == 0 ? 0 : 1;
}
