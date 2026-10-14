// GlitchKit.cpp — host integration for the recovered GlitchKit DSP.
//
// PROVENANCE, because this file is a MIXTURE and that has to stay obvious:
//
//   * The PARAMETER TABLE (ids, types, names, bounds, the five mode names) and the
//     version/flag constants are RECOVERED — see GlitchKitParams.h and PARAMETERS.md.
//   * `glitch::hashKey`, `patternFrame`, `clampf` and `clampi` are RECOVERED source
//     in ../../src/glitch/, verified INSTRUCTION-IDENTICAL to the shipped binary by
//     tools/codegen-diff.mjs. All four functions match exactly, including all 34
//     instructions and all 16 constants of hashKey.
//   * The code BELOW is NOT recovered. The shipped render engine is
//     `glitch::apply<T>(const View<const T>&, View<T>&, const Params&, unsigned, float)`
//     — 1157 and 1228 instructions for the unsigned char and unsigned short
//     instantiations — plus the `View<T>` and `Params` types. None of that is
//     recovered, so the five modes here are newly written.
//
// WHY THAT STILL MATTERS. The plugin's own description is "Five deterministic, seeded
// glitch modes. Same seed, same frame, same image." That promise rests entirely on
// hashKey and patternFrame, and those ARE the recovered functions. A rebuild driven by
// them is seeded from the same generator as the original, so the patterns are
// characteristic of this plugin rather than of something invented alongside it. The
// per-mode pixel arithmetic is new; the randomness is not.
//
// NOT VERIFIED: no After Effects host has loaded this bundle. See README.md.

#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectVers.h"
#include "String_Utils.h"
#include "AE_GeneralPlug.h"
#include "AEGP_SuiteHandler.h"

#include "glitch.h"
#include "GlitchRender.h"
#include "GlitchKitParams.h"

namespace {

// The per-pixel engine lives in GlitchRender.cpp so it can be executed without a
// host. See GlitchRender.h.

// A 16-bit path is REQUIRED, not optional: the recovered eGLO sets
// PF_OutFlag_DEEP_COLOR_AWARE, so AE will supply 16-bit worlds. There is no float
// path because the recovered eGL2 does not claim PF_OutFlag2_FLOAT_COLOR_AWARE.

PF_Err About(PF_InData* in_data, PF_OutData* out_data) {
  AEGP_SuiteHandler suites(in_data->pica_basicP);
  // Wording RECOVERED verbatim from the bundle's string table.
  suites.ANSICallbacksSuite1()->sprintf(
      out_data->return_msg,
      "%s v%d.%d\r\r%s",
      GLITCHKIT_NAME, 1, 0,
      "Five deterministic, seeded glitch modes. Same seed, same frame, same image.");
  return PF_Err_NONE;
}

PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data) {
  (void)in_data;
  out_data->my_version = static_cast<PFVersionInfo>(GLITCHKIT_VERSION);
  out_data->out_flags  = GLITCHKIT_GLOBAL_OUT_FLAGS;
  out_data->out_flags2 = GLITCHKIT_GLOBAL_OUT_FLAGS2;
  return PF_Err_NONE;
}

PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data) {
  PF_Err err = PF_Err_NONE;
  PF_ParamDef def;

  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP(GLITCHKIT_P_MODE, 5, GLITCHKIT_M_BLOCK_DISPLACE,
               GLITCHKIT_MODE_CHOICES, GLITCHKIT_MODE_DISK_ID);

  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX(GLITCHKIT_P_AMOUNT, GLITCHKIT_AMOUNT_MIN, GLITCHKIT_AMOUNT_MAX,
                       GLITCHKIT_AMOUNT_MIN, GLITCHKIT_AMOUNT_MAX, 50.0,
                       PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT,
                       PF_FSliderFlag_NONE, GLITCHKIT_AMOUNT_DISK_ID);

  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX(GLITCHKIT_P_BLOCK, GLITCHKIT_BLOCK_MIN, GLITCHKIT_BLOCK_MAX,
                       GLITCHKIT_BLOCK_MIN, GLITCHKIT_BLOCK_MAX, GLITCHKIT_BLOCK_MAX,
                       PF_Precision_INTEGER, PF_ValueDisplayFlag_NONE,
                       PF_FSliderFlag_NONE, GLITCHKIT_BLOCK_DISK_ID);

  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX(GLITCHKIT_P_SEED, GLITCHKIT_SEED_MIN, GLITCHKIT_SEED_MAX,
                       GLITCHKIT_SEED_MIN, GLITCHKIT_SEED_MAX, 1.0,
                       PF_Precision_INTEGER, PF_ValueDisplayFlag_NONE,
                       PF_FSliderFlag_NONE, GLITCHKIT_SEED_DISK_ID);

  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX(GLITCHKIT_P_SPEED, GLITCHKIT_SPEED_MIN, GLITCHKIT_SPEED_MAX,
                       GLITCHKIT_SPEED_MIN, GLITCHKIT_SPEED_MAX, 1.0,
                       PF_Precision_HUNDREDTHS, PF_ValueDisplayFlag_NONE,
                       PF_FSliderFlag_NONE, GLITCHKIT_SPEED_DISK_ID);

  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX(GLITCHKIT_P_SHIFT, GLITCHKIT_SHIFT_MIN, GLITCHKIT_SHIFT_MAX,
                       GLITCHKIT_SHIFT_MIN, GLITCHKIT_SHIFT_MAX, 20.0,
                       PF_Precision_INTEGER, PF_ValueDisplayFlag_NONE,
                       PF_FSliderFlag_NONE, GLITCHKIT_SHIFT_DISK_ID);

  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX(GLITCHKIT_P_SCANLINE, GLITCHKIT_SCANLINE_MIN, GLITCHKIT_SCANLINE_MAX,
                       GLITCHKIT_SCANLINE_MIN, GLITCHKIT_SCANLINE_MAX, GLITCHKIT_SCANLINE_MAX,
                       PF_Precision_INTEGER, PF_ValueDisplayFlag_NONE,
                       PF_FSliderFlag_NONE, GLITCHKIT_SCANLINE_DISK_ID);

  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOX(GLITCHKIT_P_LUMA, GLITCHKIT_P_LUMA, FALSE, PF_ParamFlag_NONE,
                  GLITCHKIT_LUMA_DISK_ID);

  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX(GLITCHKIT_P_SPREAD, GLITCHKIT_SPREAD_MIN, GLITCHKIT_SPREAD_MAX,
                       GLITCHKIT_SPREAD_MIN, GLITCHKIT_SPREAD_MAX, 100.0,
                       PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT,
                       PF_FSliderFlag_NONE, GLITCHKIT_SPREAD_DISK_ID);

  out_data->num_params = GLITCHKIT_NUM_PARAMS;
  return err;
}

PF_Err Render(PF_InData* in_data, PF_OutData* out_data,
              PF_ParamDef* params[], PF_LayerDef* output) {
  PF_Err err = PF_Err_NONE;
  AEGP_SuiteHandler suites(in_data->pica_basicP);
  (void)out_data;

  glitchkit::FrameParams p;
  p.mode          = params[GLITCHKIT_MODE]->u.pd.value;
  p.amount        = glitch::clampf(static_cast<float>(params[GLITCHKIT_AMOUNT]->u.fs_d.value) / 100.0f, 0.0f, 1.0f);
  p.blockSize     = static_cast<int>(params[GLITCHKIT_BLOCK_SIZE]->u.fs_d.value);
  p.seed          = static_cast<float>(params[GLITCHKIT_SEED]->u.fs_d.value);
  p.speed         = static_cast<float>(params[GLITCHKIT_SPEED]->u.fs_d.value);
  p.channelShift  = static_cast<float>(params[GLITCHKIT_CHANNEL_SHIFT]->u.fs_d.value);
  p.scanlineHeight= static_cast<int>(params[GLITCHKIT_SCANLINE_HEIGHT]->u.fs_d.value);
  p.driveByLuma   = params[GLITCHKIT_DRIVE_BY_LUMA]->u.bd.value != 0;
  p.spread        = glitch::clampf(static_cast<float>(params[GLITCHKIT_SPREAD]->u.fs_d.value) / 100.0f, 0.0f, 1.0f);

  // The frame seed comes from the RECOVERED patternFrame, which is what makes "same
  // seed, same frame, same image" true rather than aspirational. Time is the frame
  // time scaled by Speed, so Speed changes how fast the pattern evolves rather than
  // what it is.
  const double seconds = static_cast<double>(in_data->current_time) /
                         static_cast<double>(in_data->time_scale);
  const unsigned seed = glitchkit::frameSeedFor(p, seconds);

  glitchkit::FrameView srcv, dstv;
  srcv.data = params[0]->u.ld.data;
  srcv.width = params[0]->u.ld.width;
  srcv.height = params[0]->u.ld.height;
  srcv.rowBytes = params[0]->u.ld.rowbytes;
  dstv.data = output->data;
  dstv.width = output->width;
  dstv.height = output->height;
  dstv.rowBytes = output->rowbytes;

  // A 16-bit path is REQUIRED: the recovered eGLO sets DEEP_COLOR_AWARE. There is no
  // float path because eGL2 does not claim FLOAT_COLOR_AWARE.
  glitchkit::renderFrame(p, seed, srcv, dstv, PF_WORLD_IS_DEEP(&params[0]->u.ld) != 0);

  return err;
}

}  // namespace

extern "C" DllExport
PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
                  PF_ParamDef* params[], PF_LayerDef* output, void* extra) {
  PF_Err err = PF_Err_NONE;
  (void)extra;
  try {
    switch (cmd) {
      case PF_Cmd_ABOUT:        err = About(in_data, out_data); break;
      case PF_Cmd_GLOBAL_SETUP: err = GlobalSetup(in_data, out_data); break;
      case PF_Cmd_PARAMS_SETUP: err = ParamsSetup(in_data, out_data); break;
      case PF_Cmd_RENDER:       err = Render(in_data, out_data, params, output); break;
      default: break;
    }
  } catch (PF_Err& thrown) {
    err = thrown;
  }
  return err;
}
