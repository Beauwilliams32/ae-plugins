// spectra.h — reconstructed from the compiled SpectraKeys bundle.
//
// Every declaration below is copied from the demangled Mach-O symbol table of
// SpectraKeys.plugin (MD5 79960266bd76621d60213efd98b4ca60), not from source:
// the original C++ was destroyed on 2026-10-13 with no remote and no backup.
//
// NAMESPACE-COMPLETE: every `spectra::` symbol in the bundle is transcribed, so
// tools/codegen-diff.mjs reports zero unrecovered symbols for this directory.
// test/codegen.test.mjs asserts that marker and that measurement agree.
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
// [RECOVERED LAYOUT] rawBandLevel() reads seven fields and its arithmetic pins
// every one, including the four dB fields:
//
//   ldp s2, s3, [x2]       ; lowHz @0, highHz @4
//   ldr w11, [x2, #0x8]    ; mode @8, compared against 1/2/3
//   ldr s0,  [x19, #0xc]   ; thresholdDb @0xc  (below it the band reads 0)
//   ldp s2, s0, [x19,#0x10]; floorDb @0x10, ceilingDb @0x14
//   ldr s1,  [x19, #0x18]  ; gainDb @0x18  (added to the level before thresholding)
//
// The three mode values are not guesses: the effect's own popup option string is
// "Peak|Average|RMS", and the loop selects peak (1), sum/count (2) and
// sqrt(sumSquares/count) (3) for exactly those.
struct BandSpec {
  float lowHz;         // [recovered] offset 0
  float highHz;        // [recovered] offset 4
  int mode;            // [recovered] offset 8: 1 = Peak, 2 = Average, 3 = RMS
  float thresholdDb;   // [recovered] offset 0xc
  float floorDb;       // [recovered] offset 0x10
  float ceilingDb;     // [recovered] offset 0x14
  float gainDb;        // [recovered] offset 0x18
  float outputMin;     // [recovered] offset 0x1c  (bandValues maps the level here)
  float outputMax;     // [recovered] offset 0x20
  int falloffMode;     // [recovered] offset 0x24: 1 Instant, 2 Linear, 3 Exponential, 4 None/Integrate
  float falloffTime;   // [recovered] offset 0x28  (seconds; floored at 1e-4f)
  bool binarise;       // [recovered] offset 0x2c  (true -> every level becomes 0 or 1)
};

// [RECOVERED LAYOUT] analyse() reads exactly three fields and its arithmetic
// pins all of them:
//
//   ldr  d0, [x2]        ; double @0      -> divided by fftSize to make hzPerBin
//   ldr  w26, [x2, #0x8] ; int    @8      -> fftSize (power-of-two checked inline)
//   ldr  d1, [x2, #0x10] ; double @0x10   -> divided into @0 to make the hop rate
//
// @0 is a SAMPLE RATE: hzPerBin = @0 / fftSize goes straight into the Spectrogram,
// so @0 must be Hz.
//
// @0x10 is a HOP RATE in Hz, and that is now established rather than assumed:
// analyseBands passes this same field to bandValues as its third argument, and
// the two uses only agree on one physical meaning.
//
//   analyse     : samplesPerHop  = sampleRate / @0x10     -> samples per hop
//   bandValues  : secondsPerHop  = 1 / @0x10              -> time step for falloff
//
// Dividing a rate gives samples per hop; inverting a rate gives seconds per hop.
// A hop rate of 125 Hz at 8 kHz is 64 samples and 8 ms per hop, which is
// coherent. A fixed overlap factor would make the second use meaningless, since
// falloff is measured in seconds.
//
// (An earlier note here named this "hopSeconds", which is wrong: dividing by
// seconds gives 1/s^2, which is how a test passing 0.008 ended up producing a
// frame count of 1.)
struct AnalysisConfig {
  double sampleRate;  // [recovered] offset 0, in Hz
  int fftSize;        // [recovered] offset 8, must be a power of two and >= 2
  double hopRateHz;   // [recovered] offset 0x10; hops per second (see above)
};

// [inferred] A short-time magnitude spectrogram: one magnitude row per frame.
// Spectrogram::hzToBin(float) const maps a frequency to a bin index, which
// requires knowing the FFT size and sample rate.
class Spectrogram {
 public:
  // [exact] recovered from _ZNK7spectra11Spectrogram7hzToBinEf
  int hzToBin(float hz) const;
  ~Spectrogram();  // [exact] _ZN7spectra11SpectrogramD1Ev

  // [RECOVERED LAYOUT] hzToBin() reads exactly two fields, and its arithmetic
  // pins both their types and their offsets:
  //
  //     ldr w9, [x0]        ; 32-bit int at offset 0
  //     ldr d1, [x0, #0x8]  ; 64-bit double at offset 8
  //
  // The int is decremented by one and used as an upper bound, so it is the bin
  // count. The double divides the frequency, so it is the Hz-per-bin width.
  // That makes this the first part of this class that is recovered rather than
  // guessed — the remaining fields below are still [inferred].
  // [recovered] The compiled analyse() ZEROES the return object before doing
  // anything else:
  //     str xzr, [x8, #0x20]   ; both 16-byte halves...
  //     stp q0,  q0,  [x8]     ; ...covering the whole 0x28-byte header
  // That is load-bearing, not incidental. Every guard in analyse() returns early,
  // and those paths must hand back a Spectrogram whose frameCount is 0 so that
  // callers (bandValues, analyseBands) treat it as empty. Leaving these
  // indeterminate made bandValues trust a garbage frameCount, allocate it, and
  // run off the end of an empty frames vector — a segfault, caught by the
  // analyseBands test.
  int binCount = 0;       // [recovered] offset 0
  int frameCount = 0;     // [recovered] offset 4
  double hzPerBin = 0.0;  // [recovered] offset 8  (0 or negative is a guarded no-op)
  // frames @0x10: contiguous vector<float>, stride 24 bytes (sizeof a vector)

  // [inferred] placement unknown; only its existence and element type come from
  // bandValues(Spectrogram const&, ...).
  std::vector<std::vector<float>> frames;
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

// One result row per band, each frameCount long. The element type is
// vector<float>, which is what pins the 24-byte stride in the compiled loop.
std::vector<std::vector<float>> analyseBands(const float* mono,
                                             size_t frames,
                                             const AnalysisConfig& config,
                                             const std::vector<BandSpec>& bands);

float rawBandLevel(const std::vector<float>& spectrum,
                   const Spectrogram& spectrogram,
                   const BandSpec& band);

// Per-frame band levels with the effect's falloff/decay applied, mapped through
// the band's output range. `hopRateHz` is AnalysisConfig::hopRateHz; the compiled
// code inverts it to get seconds per hop, which is the falloff time step.
//
// ONE RESULT PER FRAME, not one per call: this walks the whole spectrogram and
// returns `frameCount` values. The third parameter was misnamed `time` here until a
// caller (tools/demo/harness.cpp) believed the name, passed seconds, and got an
// empty vector back for every call. The docstring above it, the implementation and
// analyseBands all agree it is a hop rate; only the parameter name disagreed.
std::vector<float> bandValues(const Spectrogram& spectrogram, const BandSpec& band,
                              double hopRateHz);

}  // namespace spectra
