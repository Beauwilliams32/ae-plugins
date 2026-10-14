// GlitchRender.h — the GlitchKit render core, with no host dependency.
//
// WHY THIS FILE EXISTS. The five modes used to live inside GlobalSetup's translation
// unit, tangled with AE parameter access, where they could not be executed without a
// running host. That made the plugin's headline promise — "Same seed, same frame,
// same image" — an untested claim.
//
// Split out like this, the engine takes plain buffers and plain numbers, so a test can
// run it on synthetic frames and actually check determinism, the per-mode behaviour,
// and that the 8-bit and 16-bit paths agree. It still uses PF_Pixel8/PF_Pixel16 from
// the AE SDK headers, which are a compile-time dependency and not a host one.
//
// The modes themselves are NEW code (the shipped engine is glitch::apply<T>, 1157 and
// 1228 instructions, unrecovered). The RANDOMNESS is not: every hash here comes from
// the RECOVERED glitch::hashKey and glitch::patternFrame, verified
// instruction-for-instruction against the shipped binary by tools/codegen-diff.mjs.

#pragma once

#include "AE_Effect.h"

namespace glitchkit {

// Parameter values, already normalised out of the AE param defs.
struct FrameParams {
  int   mode = 1;
  float amount = 0.5f;        // 0..1
  int   blockSize = 32;       // pixels
  float seed = 1.0f;
  float speed = 1.0f;
  float channelShift = 20.0f; // pixels
  int   scanlineHeight = 4;
  bool  driveByLuma = false;
  float spread = 1.0f;        // 0..1
};

// Where a frame lives. Deliberately not PF_EffectWorld: a test must be able to hand
// this a std::vector.
struct FrameView {
  void*  data = nullptr;
  long   width = 0;
  long   height = 0;
  long   rowBytes = 0;
};

// The seed for one frame. Exposed so a test can assert the same seed/frame pair gives
// the same value, independently of the render.
unsigned frameSeedFor(const FrameParams& p, double timeSeconds);

// Render one frame, reading `src` and writing `dst`. 8-bit when `deep` is false,
// 16-bit when true. Both paths run the same mode code over a different channel width,
// so a test can render the same content at both depths and compare.
void renderFrame(const FrameParams& p, unsigned frameSeed,
                 const FrameView& src, const FrameView& dst, bool deep);

}  // namespace glitchkit
