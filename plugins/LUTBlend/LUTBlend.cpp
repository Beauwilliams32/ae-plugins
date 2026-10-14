// LUTBlend.cpp — host integration for the recovered LUTBlend DSP.
//
// PROVENANCE, because this file is a MIXTURE and that has to be obvious:
//
//   * The PARAMETER TABLE (ids, types, names, bounds, the Drive choice string)
//     and the version/flag constants are RECOVERED from the shipped bundle — see
//     LUTBlendParams.h and ../../src/lutblend/RECOVERY-NOTES.md.
//   * The DSP this file calls (lutblend::clampf, lerpf, luma709, sampleLUT,
//     makeIdentity, applyLUTBlend, CubeLUT) is RECOVERED source, living in
//     ../../src/lutblend/.
//   * The code BELOW is NOT recovered. The original EffectMain body is lost. What
//     is here is newly written host integration that reproduces the recovered
//     contract: the same parameter set in the same order, the same dispatch, and
//     a render path that calls the recovered DSP.
//
// So this is a reimplementation of the surrounding plugin, not a transcription of
// it. Where behaviour is recovered it is cited; where it is new it is marked.
//
// KNOWN GAP: LUT file loading is not wired up. The original plugin has a
// LUT-discovery layer (LB_ScanLutFolder, LB_LutFileList, LB_LutFolderPath,
// LB_BuildPopupMenu, LB_LutForPopupValue) and caches parsed LUTs in a
// std::map<std::string, std::shared_ptr<const CubeLUT>>. None of that is
// recovered, so both popups are populated with a single placeholder entry and the
// render path falls back to the identity LUT — which is exactly what the shipped
// plugin does when a LUT is missing or fails to parse (__ZL11identityLUTv). The
// effect therefore loads, registers, and renders, but is a no-op until a LUT can
// be selected.

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

#include <cmath>
#include <cstring>

#include "lutblend.h"
#include "LUTBlendParams.h"

namespace {

// Each popup gets one placeholder choice until the LUT layer is recovered.
// A PF_ADD_POPUP with zero choices is not legal, so a single entry is required.
const char* kPlaceholderChoices = "None";

lutblend::CubeLUT gIdentity;                 // built once at GLOBAL_SETUP
const lutblend::CubeLUT* gCurrentA = nullptr;
const lutblend::CubeLUT* gCurrentB = nullptr;

// The wording is RECOVERED verbatim from the bundle: the ABOUT format string is
// "%s v%d.%d\r\r%s\r\rDrop .cube files into ~%s and they appear in the LUT A /
// LUT B menus. Blend between them with the slider, or let the image's luma drive
// the mix." with the description "Blend two .cube LUTs by slider or by the
// image's own luma." The version digits are supplied as 1.0 because the recovered
// eVER word 0x00080001 is the build/stage encoding and cannot be read back as a
// human version number without the original PF_VERSION arguments.
PF_Err About(PF_InData* in_data, PF_OutData* out_data) {
  AEGP_SuiteHandler suites(in_data->pica_basicP);

  suites.ANSICallbacksSuite1()->sprintf(
      out_data->return_msg,
      "%s v%d.%d\r\r%s\r\rDrop .cube files into ~%s and they appear in the "
      "LUT A / LUT B menus. Blend between them with the slider, or let the "
      "image's luma drive the mix.",
      LUTBLEND_NAME, 1, 0,
      "Blend two .cube LUTs by slider or by the image's own luma.",
      LUTBLEND_LUT_FOLDER_SUFFIX);
  return PF_Err_NONE;
}

PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data) {
  PF_Err err = PF_Err_NONE;
  (void)in_data;

  // The raw recovered eVER word is assigned directly rather than rebuilt through
  // PF_VERSION(major, minor, bug, stage, build). AE copies my_version into the
  // PiPL's eVER atom, and the shipped atom is exactly 0x00080001, so assigning
  // the recovered word reproduces the shipped value with no guesswork about which
  // version digits produced it.
  out_data->my_version = static_cast<PFVersionInfo>(LUTBLEND_VERSION);

  out_data->out_flags  = PF_OutFlag_DEEP_COLOR_AWARE |
                         PF_OutFlag_USE_OUTPUT_EXTENT;

  out_data->out_flags2 = PF_OutFlag2_FLOAT_COLOR_AWARE |
                         PF_OutFlag2_SUPPORTS_SMART_RENDER;

  lutblend::makeIdentity(&gIdentity, 2, true);
  gCurrentA = &gIdentity;
  gCurrentB = &gIdentity;

  return err;
}

// The six parameters, recovered types and order. See LUTBlendParams.h.
PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data) {
  PF_Err err = PF_Err_NONE;
  PF_ParamDef def;

  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP(LUTBLEND_PARAM_LUT_A,
               1,           // num choices
               1,           // default
               kPlaceholderChoices,
               LUTBLEND_LUT_A_DISK_ID);

  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP(LUTBLEND_PARAM_LUT_B,
               1,
               1,
               kPlaceholderChoices,
               LUTBLEND_LUT_B_DISK_ID);

  // Recovered: valid_max = 100, slider_max = 100, precision = 1.
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX(LUTBLEND_PARAM_BLEND,
                       LUTBLEND_BLEND_MIN,
                       LUTBLEND_BLEND_MAX,
                       LUTBLEND_BLEND_MIN,
                       LUTBLEND_BLEND_MAX,
                       LUTBLEND_BLEND_DEFAULT,
                       PF_Precision_TENTHS,     // precision 1: RECOVERED
                       PF_ValueDisplayFlag_PERCENT,
                       PF_FSliderFlag_NONE,
                       LUTBLEND_BLEND_DISK_ID);

  // Recovered: name "Drive", choices "Slider|Global Luma|Per-pixel Luma".
  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP(LUTBLEND_PARAM_DRIVE,
               3,
               LUTBLEND_DRIVE_SLIDER,
               LUTBLEND_DRIVE_CHOICES,
               LUTBLEND_DRIVE_DISK_ID);

  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX(LUTBLEND_PARAM_STRENGTH,
                       LUTBLEND_STRENGTH_MIN,
                       LUTBLEND_STRENGTH_MAX,
                       LUTBLEND_STRENGTH_MIN,
                       LUTBLEND_STRENGTH_MAX,
                       LUTBLEND_STRENGTH_DEFAULT,
                       PF_Precision_TENTHS,     // precision 1: RECOVERED
                       PF_ValueDisplayFlag_PERCENT,
                       PF_FSliderFlag_NONE,
                       LUTBLEND_STRENGTH_DISK_ID);

  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOX(LUTBLEND_PARAM_PRESERVE_LUMA,
                  LUTBLEND_PARAM_PRESERVE_LUMA,
                  FALSE,
                  PF_ParamFlag_NONE,
                  LUTBLEND_PRESERVE_LUMA_DISK_ID);

  out_data->num_params = LUTBLEND_NUM_PARAMS;
  return err;
}

// Per-pixel work. Calls the RECOVERED lutblend::applyLUTBlend.
//
// The Drive popup selects where `blend` comes from: the slider, the image's mean
// luma, or each pixel's own luma. applyLUTBlend itself takes a plain float and
// knows nothing about modes — that resolution happened in the original
// EffectMain, which is lost, so the three cases here are newly written to match
// the recovered popup choices.
struct PixelRefcon {
  const lutblend::CubeLUT* a;
  const lutblend::CubeLUT* b;
  float blend;        // 0..1, already resolved from the Drive mode
  float strength;     // 0..1
  bool  preserveLuma;
  float meanLuma;     // 0..1, used by the Global Luma mode
  int   drive;
};

PF_Err BlendPixel8(void* refcon, A_long xL, A_long yL,
                   PF_Pixel8* inP, PF_Pixel8* outP) {
  PF_Err err = PF_Err_NONE;
  (void)xL;
  (void)yL;
  PixelRefcon* rc = reinterpret_cast<PixelRefcon*>(refcon);
  if (!rc) return PF_Err_INTERNAL_STRUCT_DAMAGED;

  const float rgb[3] = {
    inP->red   / 255.0f,
    inP->green / 255.0f,
    inP->blue  / 255.0f
  };

  float blend = rc->blend;
  if (rc->drive == LUTBLEND_DRIVE_GLOBAL_LUMA) {
    blend = rc->meanLuma;
  } else if (rc->drive == LUTBLEND_DRIVE_PER_PIXEL_LUMA) {
    // The image's own luma drives the mix, per pixel.
    blend = lutblend::luma709(rgb[0], rgb[1], rgb[2]);
  }

  float out[3];
  lutblend::applyLUTBlend(*rc->a, *rc->b, blend, rc->strength,
                          rc->preserveLuma, rgb, out);

  outP->alpha = inP->alpha;
  outP->red   = static_cast<A_u_char>(lutblend::clampf(out[0], 0.0f, 1.0f) * 255.0f + 0.5f);
  outP->green = static_cast<A_u_char>(lutblend::clampf(out[1], 0.0f, 1.0f) * 255.0f + 0.5f);
  outP->blue  = static_cast<A_u_char>(lutblend::clampf(out[2], 0.0f, 1.0f) * 255.0f + 0.5f);
  return err;
}

PF_Err Render(PF_InData* in_data, PF_OutData* out_data,
              PF_ParamDef* params[], PF_LayerDef* output) {
  PF_Err err = PF_Err_NONE;
  (void)out_data;
  AEGP_SuiteHandler suites(in_data->pica_basicP);

  // Recovered param IDs; see LUTBLEND_* in LUTBlendParams.h.
  const double blendPct    = params[LUTBLEND_BLEND]->u.fs_d.value;    // 0..100
  const double strengthPct = params[LUTBLEND_STRENGTH]->u.fs_d.value; // 0..100
  const int    drive       = params[LUTBLEND_DRIVE]->u.pd.value;
  const bool   preserveLuma = params[LUTBLEND_PRESERVE_LUMA]->u.bd.value != 0;

  // The recovered DSP works in 0..1. clampf is the recovered helper, so the
  // normalisation uses it rather than a hand-rolled min/max.
  PixelRefcon rc;
  rc.a            = gCurrentA ? gCurrentA : &gIdentity;
  rc.b            = gCurrentB ? gCurrentB : &gIdentity;
  rc.blend        = lutblend::clampf(static_cast<float>(blendPct) / 100.0f, 0.0f, 1.0f);
  rc.strength     = lutblend::clampf(static_cast<float>(strengthPct) / 100.0f, 0.0f, 1.0f);
  rc.preserveLuma = preserveLuma;
  rc.drive        = drive;
  rc.meanLuma     = 0.5f;   // TODO: computed from the input frame; see README

  ERR(suites.Iterate8Suite1()->iterate(in_data, 0, output->height,
                                       &params[0]->u.ld,
                                       NULL, &rc, BlendPixel8, output));
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
      case PF_Cmd_ABOUT:
        err = About(in_data, out_data);
        break;
      case PF_Cmd_GLOBAL_SETUP:
        err = GlobalSetup(in_data, out_data);
        break;
      case PF_Cmd_PARAMS_SETUP:
        err = ParamsSetup(in_data, out_data);
        break;
      case PF_Cmd_RENDER:
        err = Render(in_data, out_data, params, output);
        break;
      default:
        break;
    }
  } catch (PF_Err& thrown) {
    err = thrown;
  }
  return err;
}
