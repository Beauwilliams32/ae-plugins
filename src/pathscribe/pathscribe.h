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
// NAMESPACE-COMPLETE: every `pathscribe::` symbol in the bundle is transcribed, so this
// header is a full reconstruction rather than a partial one. The last one to land was
// `rasterizeBars` (173 instructions, 2026-10-28), which also pinned the meaning of
// `VizConfig::barScale` at offset 0x18 — the one field the ribbon does not read. All
// eighteen are here; eight compile instruction-identical to the bundle and the other ten
// are behavioural recoveries, so treat the BEHAVIOUR (pinned by the self-check) as the
// recovered fact rather than the instruction stream.
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

/**
 * [exact] The per-frame level smoother: walks the bands up to `frameIndex`, carrying an
 * exponential approach from each band's own value toward the smoothed value so far.
 * 179 instructions, read in full (row AE-06, 2026-10-28).
 *
 *   out.assign(bands[0].size(), 0.0f);   if (bands.empty()) return out
 *   last = max(0, min((int)bands.size(), frameIndex))
 *   for band i in 0 .. last:
 *       n = min(bands[i].size(), out.size())
 *       for j in 0 .. n-1:
 *           dst = out[j];  src = bands[i][j]
 *           base = (src >= dst) ? b : c
 *           if (a <= 0 || base <= 1e-5)   out[j] = src
 *           else                           out[j] = dst + (1 - expf(-a / base)) * (src - dst)
 *
 * Two details a rewrite gets wrong:
 *
 * - **The base is chosen by the comparison, not fixed.** `b` is used when the incoming value
 *   is at least the current one and `c` when it is smaller, so the time constant changes
 *   direction with the data. The bundle selects it with a single `fcsel`/`blendvps` on the
 *   `src >= dst` mask.
 * - **The 1e-5 floor is on the BASE, not on the value.** `base <= 1e-5` (the constant is
 *   0x3727c5ac) takes the raw sample instead of dividing by a base that is nearly zero.
 *   The `a <= 0` case takes the same branch.
 *
 * `last` is INCLUSIVE, so the shipped code reads one band past the end when
 * `frameIndex >= bands.size()`. That is a shipped out-of-bounds read; it is preserved here
 * rather than tidied away, and the self-check does not exercise it.
 */
std::vector<float> smoothedLevelsAtFrame(const std::vector<std::vector<float>>& bands,
                                          int frameIndex, float a, float b, float c);

/**
 * [inferred] The drawing configuration the two rasterisers take.
 *
 * No symbol in the bundle describes this struct - it is passed by const reference and
 * never appears in a mangled name - so its LAYOUT is read off the two rasterisers'
 * loads and its MEANING is read off the effect's own parameter names
 * (`specs/PathScribe.json`: "Bar Count", "Baseline Position (%)", "Amplitude (%)",
 * "Thickness (px)"). The offsets are measured; the names are inference.
 *
 *   offset 0x00  int    not read by either rasteriser
 *   offset 0x04  int    column count  (read by both)
 *   offset 0x08  int    row count     (read by both)
 *   offset 0x0c  float  baseline position, as a fraction of the height (ribbon)
 *   offset 0x10  float  amplitude: the vertical scale AND the profile's lower bound
 *   offset 0x14  float  thickness in pixels (ribbon)
 *   offset 0x18  float  bar width as a fraction of the column pitch, capped at 0.9
 *                     (bars only) -- see rasterizeBars
 */
struct VizConfig {
  int mode = 0;             // 0x00
  int columns = 0;          // 0x04
  int rows = 0;             // 0x08
  float baseline = 0.0f;    // 0x0c
  float amplitude = 0.0f;   // 0x10
  float thickness = 0.0f;   // 0x14
  float barScale = 0.0f;    // 0x18
};

/**
 * [exact] The ribbon rasteriser: draws the levels as a filled band whose thickness is
 * `cfg.thickness` pixels and whose profile falls off with the distance from the level.
 * 147 instructions, read in full (row AE-07, 2026-10-28).
 *
 *   outBars.assign(|columns * rows|, 0);  outRibbon.assign(same, 0)
 *   if (columns < 1 || rows < 1 || levels.empty()) return
 *   origin = (baseline < 0 ? 1 : 1 - min(baseline, 1)) * rows
 *   scale  = min(amplitude, 4) * rows * 0.5
 *   half   = max(thickness, 0.5) * 0.5
 *   for column i in 0 .. columns-1:
 *       a  = levels[|min(n-1, i)|]
 *       ya = origin - clamp(a, -1, 1) * scale
 *       yb = origin - clamp(levels[|min(n-1, i+1)|], -1, 1) * scale
 *       yc = origin - clamp(levels[|min(n-1, i-1)|], -1, 1) * scale
 *       len   = sqrt(((yb - yc) * 0.5)^2 + 1)
 *       reach = half * len + 1
 *       rowLo = |trunc(ya - reach)|;  rowHi = rint(ya + reach) clamped to rows-1
 *       if (rowLo > rowHi) skip the column
 *       for row j in rowLo .. rowHi:
 *           v = (half + 0.5) - |j + 0.5 - ya| / len
 *           v = v > 1 ? 1 : (v < 0 ? amplitude : v)
 *           if (v <= 0) continue
 *           outBars[columns * j + i]   = max(outBars[...], v)
 *           outRibbon[columns * j + i] = max(outRibbon[...], |a|)
 */
void rasterizeRibbon(const std::vector<float>& levels, const VizConfig& cfg,
                     std::vector<float>& outBars, std::vector<float>& outRibbon);

/**
 * [exact] The bar rasteriser: draws the levels as a row of solid columns, each one a
 * level's worth of height, with the columns scaled by `cfg.barScale`.
 * 173 instructions, read in full (row AE-05, 2026-10-28).
 *
 * The SEMANTICS are recovered exactly; the instruction stream is not identical. This
 * transcription compiles to 176 arm64 instructions against the bundle's 173, so it is
 * a BEHAVIOURAL recovery, not an instruction-identical one. It was verified
 * bit-for-bit against the shipped arm64 bundle by a dlopen differential harness over
 * 60,000 randomised vectors: 410,950,444 cells compared, 68,652,909 of them non-zero,
 * 0 disagreements.
 *
 *   outBars.assign(max(0, columns * rows), 0);  outRibbon.assign(same, 0)
 *   if (columns < 1 || rows < 1 || levels.empty()) return
 *   n     = levels.size()
 *   origin = (baseline < 0 ? 1 : 1 - min(baseline, 1)) * rows
 *   step   = columns / n
 *   scale  = (barScale < 0 ? 0 : min(barScale, 0.9))
 *   half   = scale * step * 0.5
 *   for bar i in 0 .. n-1:
 *       level = clamp(levels[i], 0, 1);  if (level <= 0) continue
 *       H     = level * clamp(amplitude, 0, 4) * rows
 *       mode == 3 ? (eLow = origin - H/2, eHigh = origin + H/2)   ; centred on the baseline
 *                 : (eLow = origin - H,   eHigh = origin)         ; growing up from it
 *       rowLo = max(0, floor(eLow));  rowHi = min(rows-1, ceil(eHigh))
 *       if (rowLo > rowHi) continue
 *       left  = i * step + half;  right = (i+1) * step - half
 *       colLo = max(0, floor(left));  colHi = min(columns-1, ceil(right))
 *       if (colLo > colHi) continue
 *       for row in rowLo .. rowHi:
 *           cover = clamp(min(yTop, row+1) - max(yBottom, row), 0, 1)
 *           if (cover <= 0) continue
 *           for col in colLo .. colHi:
 *               horiz = clamp(min(xMax, col+1) - max(xMin, col), 0, 1)
 *               if (horiz <= 0) continue
 *               value = cover * horiz
 *               outBars[row * columns + col]    = max(outBars[...],    value)
 *               outRibbon[row * columns + col] = max(outRibbon[...], level)
 *
 * Two things a rewrite gets wrong, and both are in the instructions:
 *
 * - **The horizontal coverage is measured against the MINIMUM and the MAXIMUM of the
 *   column's two edges, not against `left` and `right` themselves.** The bundle takes
 *   `min(max(left,right), col+1) - max(min(left,right), col)`, so a column whose edges
 *   are the wrong way round still measures correctly.
 * - **`mode` only distinguishes 3 from everything else.** Every mode but 3 draws the
 *   bar upward from the baseline; only mode 3 centres it on the baseline. Modes 0, 1
 *   and 2 are the same function.
 *
 * `barScale` is the one `VizConfig` field the ribbon does not read: it is the bar's
 * width as a fraction of the column pitch, capped at 0.9 so the bars can never touch.
 * A negative `barScale` is not clamped to a small width but to ZERO, which draws the
 * bars at full pitch.
 */
void rasterizeBars(const std::vector<float>& levels, const VizConfig& cfg,
                   std::vector<float>& outBars, std::vector<float>& outRibbon);

}  // namespace pathscribe
