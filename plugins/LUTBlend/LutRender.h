// LutRender.h — the LUTBlend render core, with no host dependency.
//
// WHY THIS FILE EXISTS, same reason as GlitchKit's GlitchRender.h: the render path
// used to live inside the host integration, where it could not run without a live
// After Effects. That left every claim about it — which Drive mode reads what, whether
// strength 0 is a genuine no-op, whether the 16-bit path agrees with the 8-bit one —
// untested.
//
// Split out, the engine takes plain buffers, plain numbers and two CubeLUTs, so a test
// can drive it directly. PF_Pixel8/PF_Pixel16 come from the AE SDK HEADERS, which is a
// compile-time dependency and not a host one.
//
// The DSP it calls (lutblend::applyLUTBlend, sampleLUT, luma709, clampf) is RECOVERED
// source in ../../src/lutblend/, verified against the shipped binary. The wiring here
// is new.

#pragma once

#include "AE_Effect.h"

#include "lutblend.h"

namespace lutrender {

// The Drive popup's values, recovered from GlitchKit-style PF_ADD_PARAM decoding of
// LUTBlend's own parameter table. See PARAMETERS.md.
enum Drive {
  kSlider = 1,          // the Blend (%) slider decides the mix
  kGlobalLuma = 2,      // the frame's MEAN luma decides it
  kPerPixelLuma = 3,    // each PIXEL's own luma decides it
};

struct FrameParams {
  int   drive = kSlider;
  float blend = 1.0f;      // 0..1, from the Blend (%) slider
  float strength = 1.0f;   // 0..1, from the Strength (%) slider
  bool  preserveLuma = false;
  const lutblend::CubeLUT* a = nullptr;   // LUT A, or null for identity
  const lutblend::CubeLUT* b = nullptr;   // LUT B, or null for identity
};

struct FrameView {
  void* data = nullptr;
  long  width = 0;
  long  height = 0;
  long  rowBytes = 0;
};

// Blend one pixel. Input and output are 0..1 floats, so this is depth-independent and
// directly testable: the depth callbacks below are thin conversions around it.
void blendPixel(const FrameParams& p, float meanLuma, const float rgb[3], float out[3]);

// The frame's mean luma, by stride subsampling. Depth-aware: the stride is in PIXELS
// but the row offset is in BYTES, so reading a 16-bit frame with 8-bit arithmetic walks
// it at half its real stride and averages the wrong bytes. That bug was written once
// already and is regression-tested in test/lutrender.test.mjs.
//
// Returns 0.5 for an empty frame, matching the render path's fallback.
float meanLuma(const FrameView& src, bool deep, long stride);

// Render one frame. `deep` selects the 16-bit path.
void renderFrame(const FrameParams& p, float meanLumaValue,
                 const FrameView& src, const FrameView& dst, bool deep);

}  // namespace lutrender
