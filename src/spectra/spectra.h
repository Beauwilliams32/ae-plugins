// spectra.h — reconstructed from the compiled SpectraKeys bundle.
//
// Every declaration below is copied from the demangled Mach-O symbol table of
// SpectraKeys.plugin (MD5 79960266bd76621d60213efd98b4ca60), not from source:
// the original C++ was destroyed on 2026-10-13 with no remote and no backup.
//
// SIGNATURE FIDELITY: declarations marked [exact] were recovered verbatim from
// symbol mangling and are not guesses. The enclosing types were NOT recovered
// as definitions — only their names appear in symbols — so the struct/class
// layouts below are [inferred] from how the functions use them and must be
// treated as a hypothesis until checked against the effect at runtime.

#pragma once
#include <cstddef>
#include <vector>

namespace spectra {

// ---------------------------------------------------------------------------
// Recovered value types.
//
// Only the names of these types survive in the binary. Layouts are inferred.
// ---------------------------------------------------------------------------

// [inferred] A frequency band: lower and upper edge in Hz, plus an enable flag.
// SK_ReadBandSpec(PF_ParamDef**, int, BandSpec*) and the authored parameter
// strings ("Band %s Enabled", "%s Low Freq (Hz)", "%s High Freq (Hz)") pin the
// three fields and their order.
struct BandSpec {
  float lowHz;
  float highHz;
  bool enabled;
};

// [inferred] FFT analysis settings. The parameter strings expose "FFT Size",
// "Display Mode", "Floor (dB)", "Ceiling (dB)" and "Falloff Time (s)".
struct AnalysisConfig {
  int fftSize;      // must be a power of two — isPowerOfTwo() guards it
  int sampleRate;
  float floorDb;
  float ceilingDb;
  float falloffSeconds;
  int displayMode;
};

// [inferred] A short-time magnitude spectrogram: one magnitude row per frame.
// Spectrogram::hzToBin(float) const maps a frequency to a bin index, which
// requires knowing the FFT size and sample rate.
class Spectrogram {
 public:
  // [exact] recovered from _ZNK7spectra11Spectrogram8hzToBinEf
  int hzToBin(float hz) const;
  ~Spectrogram();  // [exact] _ZN7spectra11SpectrogramD1Ev

  std::vector<std::vector<float>> frames;
  int fftSize = 0;
  int sampleRate = 0;
};

// ---------------------------------------------------------------------------
// [exact] Scalar helpers. Signatures recovered verbatim; bodies recovered from
// disassembly and reproduced below in spectra.cpp with the instructions cited.
// ---------------------------------------------------------------------------

float clampf(float value, float lo, float hi);
bool isPowerOfTwo(int n);
float linearToDb(float linear);
float dbToLinear(float db);

// ---------------------------------------------------------------------------
// [exact] Windowing and transform.
// ---------------------------------------------------------------------------

std::vector<float> hannWindow(int n);
float hannCoherentGain();

// In-place radix-2 FFT. Returns without touching anything unless both vectors
// are the same power-of-two length of at least 2.
void fftRadix2(std::vector<float>& re, std::vector<float>& im);

// ---------------------------------------------------------------------------
// [exact] Analysis pipeline.
// ---------------------------------------------------------------------------

// Mixes `channels` interleaved samples down to `frames` mono samples.
std::vector<float> downmixToMono(const float* interleaved, size_t frames, int channels);

Spectrogram analyse(const float* mono, size_t frames, const AnalysisConfig& config);

std::vector<float> analyseBands(const float* mono,
                                size_t frames,
                                const AnalysisConfig& config,
                                const std::vector<BandSpec>& bands);

float rawBandLevel(const std::vector<float>& spectrum,
                   const Spectrogram& spectrogram,
                   const BandSpec& band);

float bandValues(const Spectrogram& spectrogram, const BandSpec& band, double time);

}  // namespace spectra
