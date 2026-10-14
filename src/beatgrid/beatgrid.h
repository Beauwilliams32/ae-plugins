// beatgrid.h — reconstructed from the compiled BeatGridRuler bundle.
//
// Every declaration below is copied from the demangled Mach-O symbol table of
// BeatGridRuler.plugin, not from source: the original C++ was destroyed on 2026-10-13
// with no remote and no backup.
//
// INCOMPLETE — the beat maths is here and the analysis and the host integration are not.
//   recovered: clampf, isPowerOfTwo, downmixToMono (shared),
//              subdivisionFactor, phaseSawtooth, phaseInLayerTime,
//              OnsetEnvelope::frameTimeSeconds
//   outstanding: computeOnsetEnvelope (436), estimateTempoBpm (578),
//              estimatePhaseSeconds (138), generateBeatGrid (94), analyzeBeatGrid (57)
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
 * [inferred] An onset envelope: a per-frame measure of where the music's attacks are.
 *
 * ONLY THE THREE FIELDS `frameTimeSeconds` reads are declared. Everything else is left
 * as opaque bytes rather than invented, and the three offsets are pinned by
 * `static_assert` so a layout claim cannot drift from the check.
 */
struct OnsetEnvelope {
  unsigned char unknown[0x18];  // [inferred] nothing recovered here reads this range
  double hopInterval;           // +0x18  [recovered] the divisor; <= 0 is a guarded no-op
  int startFrameHalf;           // +0x20  [recovered] read as a 32-bit int, scaled by 1/2
  short step;                   // +0x24  [recovered] read as a 16-bit SIGNED value

  // [exact] The time in seconds of `frame` within this envelope.
  //
  //     if (hopInterval <= 0.0) return 0.0;
  //     return (frame * step + startFrameHalf / 2.0) / hopInterval;
  //
  // The `startFrameHalf` field is divided by two on the way in — the compiler emits
  // `scvtf d3, w8, #0x1`, a fixed-point convert with one fractional bit — so that field
  // counts HALF frames. What it counts half-frames OF is not established: nothing
  // recovered so far writes it, and the name is a description of the arithmetic rather
  // than a recovered name.
  double frameTimeSeconds(int frame) const;
};

static_assert(offsetof(OnsetEnvelope, hopInterval) == 0x18, "hopInterval is read at +0x18");
static_assert(offsetof(OnsetEnvelope, startFrameHalf) == 0x20, "startFrameHalf is read at +0x20");
static_assert(offsetof(OnsetEnvelope, step) == 0x24, "step is read at +0x24, sign-extended");

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

}  // namespace beatgrid
