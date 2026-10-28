// beatgrid.h — reconstructed from the compiled BeatGridRuler bundle.
//
// Every declaration below is copied from the demangled Mach-O symbol table of
// BeatGridRuler.plugin, not from source: the original C++ was destroyed on 2026-10-13
// with no remote and no backup.
//
// INCOMPLETE — the beat maths and the tempo analysis are here; the host integration is not.
//   recovered: clampf, isPowerOfTwo, downmixToMono (shared),
//              subdivisionFactor, phaseSawtooth, phaseInLayerTime,
//              OnsetEnvelope::frameTimeSeconds
//   outstanding: computeOnsetEnvelope (436), estimateTempoBpm (578), analyzeBeatGrid (57)
//   excluded: (anonymous namespace)::WriteMarkerGrid (811) — it takes
//              AEGP_SuiteHandler& and _AEGPp_Stream**, which are Adobe SDK types, so it
//              stays out of `src/` the way every other host integration does.
//
// THREE OF THE EIGHT ARE SHARED. `tools/dsp-duplication.mjs` reports this bundle as a
// carrier of `clampf`, `isPowerOfTwo` and `downmixToMono`, so those are the fleet's
// code under this namespace, checked by divergence parity.

#pragma once
#include <cstddef>
#include <vector>

namespace beatgrid {

// ---------------------------------------------------------------------------
// [inferred] Types. Only their names survive in the binary; the layouts below come from
// the offsets the recovered functions read, and nothing else is claimed.
// ---------------------------------------------------------------------------

/**
 * [inferred] A beat subdivision. The enumerator NAMES are not in the binary, so the
 * values are named for what `subdivisionFactor` maps them to:
 *
 *   value 2 -> 0.5     value 3 -> 0.25     value 4 -> 0.125     value 5 -> 4.0
 *   anything else, including 0 and negatives -> 1.0
 *
 * So 2, 3 and 4 halve in turn and 5 is four times the beat rather than a fraction of
 * it. Passing the enum as an int is what the mangling shows; the values themselves come
 * from the discriminant `value - 2` in `subdivisionFactor`.
 */
enum class Subdivision : int {
  Beat = 0,
  Half = 2,
  Quarter = 3,
  Eighth = 4,
  FourBeats = 5,
};

/**
 * [inferred names, recovered layout] An onset envelope: a per-frame measure of where the
 * music's attacks are.
 *
 * The layout was NOT readable from `frameTimeSeconds` alone — that function only ever
 * touched three fields and the first 0x18 bytes were opaque. `computeOnsetEnvelope` is
 * the function that BUILDS the envelope, and its stores pin every offset:
 *
 *   str  q1, q1, [x8]              ; the 0x30-byte result is zeroed first
 *   str  d0, [x8, #0x18]           ; +0x18 the sample rate, as a double
 *   stp  w9, w3, [x8, #0x20]       ; +0x20 the window size, +0x24 the hop size
 *   str  d0, [x8, #0x28]           ; +0x28 the frame rate, sampleRate / hopSize
 *   vector<float>::assign(numFrames, 0.0f)  ; +0x00 the values themselves
 *
 * The NAMES are inferred from what the code does with them — nothing in the binary
 * spells them — but the OFFSETS and the TYPES are read out of the stores above, and
 * each one is pinned by a `static_assert` whose message quotes the instruction that
 * establishes it.
 */
struct OnsetEnvelope {
  std::vector<float> values;  // +0x00 [recovered] one onset strength per analysis frame
  double sampleRate;          // +0x18 [recovered] the audio sample rate, in Hz
  int windowSize;             // +0x20 [recovered] the FFT window size in samples
  int32_t hopSize;            // +0x24 [recovered] the analysis hop in samples
  double framesPerSecond;     // +0x28 [recovered] sampleRate / hopSize

  // [exact] The time in seconds of `frame` within this envelope.
  //
  //     if (sampleRate <= 0.0) return 0.0;
  //     return (frame * hopSize + windowSize / 2.0) / sampleRate;
  //
  // It is the CENTRE time of the frame, not its start: the window is half a window
  // long on either side of the centre, and the shipped code folds that half into the
  // convert — `scvtf d3, w8, #0x1`, a fixed-point convert with one fractional bit —
  // so `windowSize` is halved on the way in.
  double frameTimeSeconds(int frame) const;
};

static_assert(offsetof(OnsetEnvelope, values) == 0x00,
              "the vector `ldp x20, x8, [x0]` reads at +0x00");
static_assert(offsetof(OnsetEnvelope, sampleRate) == 0x18,
              "`str d0, [x8, #0x18]` stores the sample rate");
static_assert(offsetof(OnsetEnvelope, windowSize) == 0x20,
              "`stp w9, w3, [x8, #0x20]` stores the window size");
static_assert(offsetof(OnsetEnvelope, hopSize) == 0x24,
              "the second word of `stp w9, w3, [x8, #0x20]` stores the hop size");
static_assert(offsetof(OnsetEnvelope, framesPerSecond) == 0x28,
              "`str d0, [x8, #0x28]` stores sampleRate / hopSize");
static_assert(sizeof(OnsetEnvelope) == 0x30,
              "the result is zeroed as 0x30 bytes: `stp q1, q1, [x8, #0x10]` + `str q1, [x8]`");

/**
 * [inferred names, recovered layout] One marker on the beat grid.
 *
 * Sixteen bytes: `generateBeatGrid` pushes them with `stp d12, d0, [sp]`, two doubles,
 * and the vector's element type is `beatgrid::BeatMarker` in the mangling. The first
 * double is the marker's TIME and the second its BEAT NUMBER — `generateBeatGrid`
 * stores the marker time in the first lane and the running beat index in the second.
 */
struct BeatMarker {
  double timeSeconds = 0.0;  // +0x00
  double beatNumber = 0.0;   // +0x08
};

static_assert(sizeof(BeatMarker) == 16, "two doubles — `stp d12, d0, [sp]`");

/**
 * [inferred names, recovered layout] What the tempo analysis reports.
 *
 * THREE FLOATS, returned in three separate 128-bit registers: `analyzeBeatGrid` ends
 * with `mov.16b v0, v8` / `v1, v9` / `v2, v10`, each holding a single float in its low
 * lane. That is the AArch64 return convention for a homogeneous float aggregate of
 * three members, so this is a 12-byte struct of three floats — not three doubles.
 *
 * The ORDER is read out of `analyzeBeatGrid`: the tempo branch puts the bpm in v8, the
 * phase in v9 and the confidence in v10, and the fixed-bpm branch does the same.
 */
struct TempoAnalysis {
  float bpm = 0.0f;          // +0x00
  float phaseSeconds = 0.0f; // +0x04
  float confidence = 0.0f;   // +0x08
};

static_assert(sizeof(TempoAnalysis) == 12, "three floats, each in its own return register");

// ---------------------------------------------------------------------------
// [exact] The shared DSP, recovered from SpectraKeys and identical in this bundle.
// ---------------------------------------------------------------------------

float clampf(float value, float lo, float hi);
bool isPowerOfTwo(int n);
std::vector<float> downmixToMono(const float* interleaved, size_t frames, int channels);

// ---------------------------------------------------------------------------
// [exact] The beat maths.
// ---------------------------------------------------------------------------

/** [exact] The factor a subdivision scales the beat interval by. See the enum above. */
double subdivisionFactor(Subdivision subdivision);

/**
 * [exact] The sawtooth phase of `time` within its subdivision of the beat, in [0, 1).
 *
 *     guard:   bpm <= 0                    -> 0
 *     period = (60 / bpm) * subdivisionFactor(subdivision)
 *     guard:   period <= 0                 -> 0
 *     phase  = fmod(time - startTime, period), wrapped up if negative
 *     return   (float)(phase / period)
 *
 * The beat interval is 60/bpm seconds — `60.0` appears as the double literal
 * 0x404e000000000000 — and the subdivision scales THAT, so the period is shorter for a
 * finer subdivision. The result is a `float` even though every input is a `double`:
 * the last instruction is `fcvt s0, d0`.
 */
float phaseSawtooth(double bpm, double startTime, Subdivision subdivision, double time);

/**
 * [exact] The position of `time` within the beat, measured IN SECONDS from the beat's
 * start and so running over [0, 60/bpm), with NO subdivision:
 *
 *     guard:   bpm <= 0                    -> 0
 *     period = 60 / bpm
 *     phase  = fmod(a + b - c, period), wrapped up if negative
 *     return   phase < period ? phase : phase - period
 *
 * The last comparison cannot fire — `fmod` never returns its divisor — but it is
 * transcribed because it is in the binary. Unlike `phaseSawtooth`, which divides by its
 * period and returns a fraction, this returns a DOUBLE measured in SECONDS, and it takes
 * no subdivision. The three offsets are summed and the subtraction applied last, which
 * is the order the instructions use and matters for the wrap.
 */
double phaseInLayerTime(double bpm, double a, double b, double c);

// ---------------------------------------------------------------------------
// The tempo analysis. The signatures are verbatim from the symbol mangling; the bodies
// are BEHAVIOURAL recoveries — the semantics are pinned by the listing and by the
// self-check, but the instruction streams differ (see RECOVERY-NOTES.md).
// ---------------------------------------------------------------------------

/**
 * [partial] The phase of the beat, in SECONDS, within one beat period. 138 instructions,
 * read in full (/tmp/ae/beatgrid.asm lines 1405–1543, `estimatePhaseSeconds`).
 *
 *     guard:   the envelope is empty            -> 0
 *     guard:   bpm <= 0                         -> 0
 *     guard:   the frame rate is not positive   -> 0
 *     period  = 60 / bpm
 *     half    = (sampleRate > 0) ? (windowSize / 2) / sampleRate : 0
 *
 * It scores 512 candidate offsets, half a beat apart, and each candidate is the sum of
 * the envelope sampled every `period` seconds — linearly interpolated between frames,
 * and offset by the half-window so the score reads the window CENTRE rather than its
 * start. The winner is then refined by the curvature of its two neighbours, which
 * recovers a sub-half-beat offset, and the result is wrapped into [0, period).
 */
float estimatePhaseSeconds(const OnsetEnvelope& envelope, float bpm);

/**
 * [partial] The markers of a beat grid across a region. 94 instructions, read in full
 * (/tmp/ae/beatgrid.asm lines 1611–1705, `generateBeatGrid`).
 *
 *     guard:   bpm <= 0                        -> an empty grid
 *     guard:   regionLength <= 0                -> an empty grid
 *     period  = (60 / bpm) * subdivisionFactor(subdivision)
 *     guard:   period <= 0                      -> an empty grid
 *
 * The grid starts at the first marker at or after `regionStart` — rounded UP from the
 * origin on the subdivision period — and steps by `period` until it passes
 * `regionStart + regionLength`. The marker number is the beat index measured from the
 * origin, plus one.
 */
std::vector<BeatMarker> generateBeatGrid(double bpm, double origin, Subdivision subdivision,
                                          double regionStart, double regionLength);

/**
 * [partial] The spectral-flux onset envelope of a mono signal. 436 instructions, read in
 * full (/tmp/ae/beatgrid.asm lines 216–652, `computeOnsetEnvelope`).
 *
 * A Hann-windowed radix-2 FFT over `windowSize` samples, stepped by `hopSize`, and the
 * half-spectrum magnitudes are compared frame to frame: the flux of a frame is the sum
 * of the POSITIVE differences. The first frame has no predecessor and so is zero.
 *
 *     guard:   no samples, no frames, a non-positive sample rate,
 *              a window or hop below one, a window below two,
 *              or a window that is not a power of two -> an empty envelope
 */
OnsetEnvelope computeOnsetEnvelope(const float* samples, size_t frames, double sampleRate,
                                    int windowSize, int hopSize);

/**
 * [partial] The tempo of an onset envelope, searched over a bpm range. 578 instructions,
 * read in full (/tmp/ae/beatgrid.asm lines 826–1404, `estimateTempoBpm`).
 *
 * The envelope is smoothed twice by a centred box filter, and then correlated against
 * itself at every lag the bpm range allows; the strongest local maximum of that
 * autocorrelation is the beat period.
 *
 *     guard:   the envelope is empty            -> all zeroes
 *     guard:   the bpm range is empty or non-positive -> all zeroes
 */
TempoAnalysis estimateTempoBpm(const OnsetEnvelope& envelope, float minBpm, float maxBpm,
                                float unusedThreshold);

/**
 * [partial] The entry point: envelope, then either a fixed-tempo phase or a tempo search.
 * 57 instructions, read in full (/tmp/ae/beatgrid.asm lines 1544–1601, `analyzeBeatGrid`).
 *
 *     guard:   the envelope comes back empty    -> all zeroes
 *     if (fixedBpm > 0) -> { fixedBpm, the phase at that tempo, 1.0 }
 *     else              -> estimateTempoBpm(envelope, minBpm, maxBpm, 0.85f)
 */
TempoAnalysis analyzeBeatGrid(const float* samples, size_t frames, double sampleRate,
                               float minBpm, float maxBpm, float fixedBpm, int windowSize,
                               int hopSize);

}  // namespace beatgrid
