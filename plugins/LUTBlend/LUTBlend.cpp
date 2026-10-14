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
// LUT LOADING: the shipped plugin's discovery layer (LB_ScanLutFolder,
// LB_LutFileList, LB_LutFolderPath, LB_BuildPopupMenu, LB_LutForPopupValue) is NOT
// recovered, so LutLibrary.cpp reimplements it as new code against the recovered
// folder path and the recovered .cube grammar. Parsed LUTs are cached, mirroring
// the shipped plugin's std::map<std::string, std::shared_ptr<const CubeLUT>>.
// A missing or unparseable LUT falls back to the identity LUT, which is what the
// shipped plugin does (__ZL11identityLUTv).

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
#include "LutLibrary.h"
#include "LutRender.h"
#include "LUTBlendParams.h"

namespace {

// Each popup gets one placeholder choice until the LUT layer is recovered.
// A PF_ADD_POPUP with zero choices is not legal, so a single entry is required.
lutblend::CubeLUT gIdentity;                 // built once at GLOBAL_SETUP
std::vector<std::string> gLutFiles;          // scanned once, in the same order the
std::string gLutChoices;                     // popups are built from
lutlib::Library gLibrary;                    // parsed-LUT cache

// Popup choice index -> file path. Index 0 is always the placeholder, so a popup
// value of 1 maps to gLutFiles[0].
const std::string* lutForPopupValue(int v) {
  const int idx = v - 1;
  if (idx < 0 || static_cast<size_t>(idx) >= gLutFiles.size()) return nullptr;
  return &gLutFiles[static_cast<size_t>(idx)];
}

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

  // The RECOVERED words, assigned verbatim. An earlier revision of this file
  // built them from named flags instead and got three of them wrong:
  // PF_OutFlag_USE_OUTPUT_EXTENT, PF_OutFlag2_FLOAT_COLOR_AWARE and
  // PF_OutFlag2_SUPPORTS_SMART_RENDER were all invented, and
  // PF_OutFlag2_SUPPORTS_THREADED_RENDERING was missing. That is not cosmetic —
  // FLOAT_COLOR_AWARE would have had AE hand over 32-bpc float data for a render
  // path that reads 8-bit pixels. See LUTBlendParams.h for the decoding.
  out_data->out_flags  = LUTBLEND_GLOBAL_OUT_FLAGS;
  out_data->out_flags2 = LUTBLEND_GLOBAL_OUT_FLAGS2;

  lutblend::makeIdentity(&gIdentity, 2, true);

  // The shipped plugin scans this folder and builds both menus from it. Scanning
  // happens once per GLOBAL_SETUP rather than per frame.
  gLutFiles = lutlib::scanLutFolder();
  gLutChoices = lutlib::popupChoices(gLutFiles);

  return err;
}

// The six parameters, recovered types and order. See LUTBlendParams.h.
PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data) {
  PF_Err err = PF_Err_NONE;
  PF_ParamDef def;

  // The choice list is the folder scan; index 1 is the first file. num_choices is
  // files+1 because the leading "None" entry is always present, which is what
  // makes a popup value of 1 mean "the first LUT".
  const int numLutChoices = static_cast<int>(gLutFiles.size()) + 1;

  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP(LUTBLEND_PARAM_LUT_A,
               numLutChoices,
               1,
               gLutChoices.c_str(),
               LUTBLEND_LUT_A_DISK_ID);

  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP(LUTBLEND_PARAM_LUT_B,
               numLutChoices,
               1,
               gLutChoices.c_str(),
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
// The per-pixel engine lives in LutRender.cpp so it can be executed without a host.
// See LutRender.h.
//
// These two callbacks are deliberately THIN. They exist so the host path still goes
// through AE's iterate suite, which gives progress reporting and abort checking; the
// arithmetic they call is the same code the tests drive directly.

struct PixelPass {
  const lutrender::FrameParams* params;
  float meanLuma;
};

PF_Err BlendPixel8(void* refcon, A_long xL, A_long yL, PF_Pixel8* inP, PF_Pixel8* outP) {
  (void)xL; (void)yL;
  const PixelPass* pass = reinterpret_cast<const PixelPass*>(refcon);
  if (!pass) return PF_Err_INTERNAL_STRUCT_DAMAGED;

  const float rgb[3] = {inP->red / 255.0f, inP->green / 255.0f, inP->blue / 255.0f};
  float out[3];
  lutrender::blendPixel(*pass->params, pass->meanLuma, rgb, out);

  outP->alpha = inP->alpha;
  outP->red   = static_cast<A_u_char>(out[0] * 255.0f + 0.5f);
  outP->green = static_cast<A_u_char>(out[1] * 255.0f + 0.5f);
  outP->blue  = static_cast<A_u_char>(out[2] * 255.0f + 0.5f);
  return PF_Err_NONE;
}

// 16-bit is REQUIRED by the recovered eGLO, and PF_MAX_CHAN16 is 32768, not 65535.
PF_Err BlendPixel16(void* refcon, A_long xL, A_long yL, PF_Pixel16* inP, PF_Pixel16* outP) {
  (void)xL; (void)yL;
  const PixelPass* pass = reinterpret_cast<const PixelPass*>(refcon);
  if (!pass) return PF_Err_INTERNAL_STRUCT_DAMAGED;

  const float scale = static_cast<float>(PF_MAX_CHAN16);
  const float rgb[3] = {inP->red / scale, inP->green / scale, inP->blue / scale};
  float out[3];
  lutrender::blendPixel(*pass->params, pass->meanLuma, rgb, out);

  outP->alpha = inP->alpha;
  outP->red   = static_cast<A_u_short>(out[0] * scale + 0.5f);
  outP->green = static_cast<A_u_short>(out[1] * scale + 0.5f);
  outP->blue  = static_cast<A_u_short>(out[2] * scale + 0.5f);
  return PF_Err_NONE;
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

  // Resolve the two popups to files and load them, falling back to the identity
  // LUT when a slot is empty or a file fails to parse — the shipped behaviour.
  const std::string* pathA = lutForPopupValue(params[LUTBLEND_LUT_A]->u.pd.value);
  const std::string* pathB = lutForPopupValue(params[LUTBLEND_LUT_B]->u.pd.value);
  std::string loadErr;
  std::shared_ptr<const lutblend::CubeLUT> lutA = pathA ? gLibrary.get(*pathA, &loadErr) : nullptr;
  std::shared_ptr<const lutblend::CubeLUT> lutB = pathB ? gLibrary.get(*pathB, &loadErr) : nullptr;
  const lutblend::CubeLUT* useA = lutA ? lutA.get() : &gIdentity;
  const lutblend::CubeLUT* useB = lutB ? lutB.get() : &gIdentity;

  // The recovered DSP works in 0..1. clampf is the recovered helper, so the
  // normalisation uses it rather than a hand-rolled min/max.
  lutrender::FrameParams rp;
  rp.drive        = drive;
  rp.blend        = lutblend::clampf(static_cast<float>(blendPct) / 100.0f, 0.0f, 1.0f);
  rp.strength     = lutblend::clampf(static_cast<float>(strengthPct) / 100.0f, 0.0f, 1.0f);
  rp.preserveLuma = preserveLuma;
  rp.a            = useA;
  rp.b            = useB;

  const bool deep = PF_WORLD_IS_DEEP(&params[0]->u.ld) != 0;

  lutrender::FrameView sv, dv;
  sv.data = params[0]->u.ld.data;
  sv.width = params[0]->u.ld.width;
  sv.height = params[0]->u.ld.height;
  sv.rowBytes = params[0]->u.ld.rowbytes;
  dv.data = output->data;
  dv.width = output->width;
  dv.height = output->height;
  dv.rowBytes = output->rowbytes;

  // Global Luma drives the mix from the frame's own mean luma, computed by
  // subsampling: the mean only needs to be representative and a stride of 16 keeps
  // the pre-pass cheap at 4K. Depth-aware inside the core.
  const float meanL = lutrender::meanLuma(sv, deep, 16);

  PixelPass pass{&rp, meanL};

  // Depth dispatch through AE's iterate suites rather than a manual loop: iterate
  // reports progress and honours aborts, and the plugin declares
  // SUPPORTS_THREADED_RENDERING so a re-entrant callback is expected. The arithmetic
  // inside is the same code test/lutrender.test.mjs drives directly.
  if (deep) {
    ERR(suites.Iterate16Suite1()->iterate(in_data, 0, output->height,
                                          &params[0]->u.ld, NULL, &pass, BlendPixel16, output));
  } else {
    ERR(suites.Iterate8Suite1()->iterate(in_data, 0, output->height,
                                         &params[0]->u.ld, NULL, &pass, BlendPixel8, output));
  }
  (void)sv; (void)dv;
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
