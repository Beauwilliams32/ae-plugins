// pathscribe.h — reconstructed from the compiled PathScribe bundle.
//
// Every declaration below is copied from the demangled Mach-O symbol table of
// PathScribe.plugin, not from source: the original C++ was destroyed on 2026-10-13
// with no remote and no backup.
//
// THE SHARED LAYER IS A COPY, AND THAT IS THE FINDING. Ten of this bundle's eighteen
// symbols are machine-code-identical to SpectraKeys' — `tools/dsp-duplication.mjs`
// compares every carrier and reports no drift across
// SpectraKeys, PathScribe, BeatSnap and BeatGridRuler. So the analysis pipeline
// recovered from SpectraKeys serves four effects, and what is transcribed here is that
// pipeline under this bundle's namespace.
//
// INCOMPLETE — the analysis layer is here and the drawing layer is not:
//   recovered: clampf, isPowerOfTwo, linearToDb, dbToLinear, hannWindow,
//              hannCoherentGain, fftRadix2, downmixToMono, analyse,
//              Spectrogram::~Spectrogram, spanCoverage, binEdgeHz, smoothStep
//   outstanding: barLevels (112), smoothedLevelsAtFrame (179), waveformColumns (80),
//              rasterizeBars (173), rasterizeRibbon (147)
//
// The recovered types are SpectraKeys' too, since the identical `analyse` and
// `~Spectrogram` can only compile to the same instructions from the same layout.
// test/pathscribe.test.mjs re-checks that claim by divergence parity.

#pragma once
#include <cstddef>
#include <vector>

namespace pathscribe {

struct AnalysisConfig {
  double sampleRate;  // [recovered] offset 0, in Hz
  int fftSize;        // [recovered] offset 8, must be a power of two and >= 2
  double hopRateHz;   // [recovered] offset 0x10; hops per second (see above)
};

// [inferred] A short-time magnitude spectrogram: one magnitude row per frame.
// Spectrogram maps a frequency to a bin index, which
// requires knowing the FFT size and sample rate.
class Spectrogram {
 public:
  // PathScribe's bundle publishes only the destructor for this class, so no
  // hzToBin is declared here even though SpectraKeys' copy has one: nothing in
  // this bundle's symbol table proves PathScribe had it.
  ~Spectrogram();  // [exact] _ZN10pathscribe11SpectrogramD1Ev

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
// [exact] Recovered declarations. The bodies live in pathscribe.cpp, each citing the
// instructions of THIS bundle.
// ---------------------------------------------------------------------------

float clampf(float value, float lo, float hi);
bool isPowerOfTwo(int n);
float linearToDb(float linear);
float dbToLinear(float db);
std::vector<float> hannWindow(int n);
float hannCoherentGain();
void fftRadix2(std::vector<float>& re, std::vector<float>& im);
std::vector<float> downmixToMono(const float* interleaved, size_t frames, int channels);
Spectrogram analyse(const float* mono, size_t frames, const AnalysisConfig& config);

// ---------------------------------------------------------------------------
// [exact] PathScribe's own layer — the drawing geometry.
// ---------------------------------------------------------------------------

// [exact] An EXPONENTIAL approach from `from` toward `to` with time constant `span`,
// despite the name: there is no polynomial and no progress curve. The sign of the move
// picks the span (`spanRising` when `to >= from`), and a NON-POSITIVE `x`, or a span
// not above 1e-5f, returns **`to`** rather than `from`.
float smoothStep(float from, float to, float x, float spanRising, float spanFalling);

// [exact] Geometric band edge: `max(minHz, 1) * pow(max(maxHz, that) / that, index /
// count)`. Equal RATIOS apart, so the bands look even across the octaves; the exponent
// divides by `count` rather than `count - 1`, so edge `count` lands on `maxHz` exactly.
// A `count` below 1 returns `minHz` unchanged.
double binEdgeHz(int index, int count, double minHz, double maxHz);

// [exact] The overlap of the frame `[frameIndex, frameIndex + 1)` with the interval
// `[min(edge0, edge1), max(edge0, edge1)]`, as a fraction of one frame, clamped to
// [0, 1].
//
// The last two parameters are two EDGES, not a span and a start: the body takes their
// min and max, so their order does not matter, and a caller passing "start" and
// "start + span" gets the right answer either way. A first reading of this function
// assumed the second parameter was a length and the third a start, which is what the
// names in that reading suggested; the instruction stream says otherwise, and the
// wrong reading produced a `max` where the bundle has a `min`.
float spanCoverage(float frameIndex, float edge0, float edge1);

/**
 * [exact] A peak-hold waveform: `columns` values, each the sample of LARGEST MAGNITUDE in its own
 * slice of the track, clamped to [-1, 1]. 80 instructions, read in full (row AE-05, 2026-10-27).
 *
 *   out.resize(columns, 0)
 *   span = (double)end / columns                      ; samples per column, fractional
 *   base = start - (end >> 1)                         ; the window is centred on `start`
 *   for col i:
 *       a  = floor(span * i);  b = floor(span * (i + 1))
 *       lo = base + a;         hi = (b > a) ? base + b : lo + 1     ; always at least one sample
 *       peak = 0
 *       for j in lo .. hi-1: if j in range and |samples[j]| > |peak|: peak = samples[j]
 *       out[i] = clamp(peak, -1, 1)
 *
 * Note it keeps the SIGNED sample with the largest magnitude (not the magnitude), the range is
 * half-open, and `columns < 1`, a null/empty track or `end < 1` all return the zero-filled array.
 */
std::vector<float> waveformColumns(const float* samples, size_t count, long start, long end, int columns);

/**
 * [exact] The spectrum strip's bar reducer: `barCount` levels in [0, 1], each the loudest sample in
 * its frequency slice, converted to a level over a 60 dB range. 112 instructions, read in full
 * (row AE-05, 2026-10-27).
 *
 *   out.resize(barCount, 0);   if (barCount < 1 || duration <= 0 || values.empty()) return out
 *   lo = max(min(a, b), 1.0);  hi = max(a, b);  ratio = hi / lo
 *   for bar i:  e    = pow(ratio, i / barCount)
 *               idx  = round(hi * e / duration)      ; fcvtas, NOT truncation
 *               begin = clamp(idx, 0, count - 1);  end = max(begin, min(round at i+1, count - 1))
 *               peak = max(values[begin .. end])
 *               level = clamp((20*log10(max(peak, 1e-9)) + 60) / 60, 0, 1)
 *
 * The frequency axis is LOGARITHMIC: `pow(ratio, i/barCount)` is what makes equal columns cover
 * increasingly wide bands. `duration` converts the frequency back to a sample index, so a caller
 * passing the wrong duration compresses or stretches the whole strip rather than erroring. The
 * 1e-9 floor exists so `log10` never sees zero — it renders as the bottom of the strip.
 */
std::vector<float> barLevels(const std::vector<float>& values, double duration, int barCount,
                             double a, double b);

}  // namespace pathscribe
