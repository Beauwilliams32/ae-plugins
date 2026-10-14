// LUTBlendParams.h — the recovered parameter table for LUTBlend.
//
// PROVENANCE: every value in this header was RECOVERED from the shipped
// LUTBlend.plugin bundle by disassembling EffectMain's PF_Cmd_PARAMS_SETUP
// handler. It is not invented and not guessed. The derivation, with instruction
// addresses, is in ../../src/lutblend/RECOVERY-NOTES.md.
//
// These values live in the BINARY, not in the PiPL: the PiPL carries the plugin's
// identity and version words, but parameter types and IDs are registered at
// runtime through PF_ADD_PARAM, so the only surviving record of them is the
// disassembly of that handler.
//
// Parameter IDs are contiguous from 1 in registration order.

#pragma once

// --- Recovered from the PiPL resource (see packaging/LUTBlendPiPL.r) ---------
#define LUTBLEND_NAME            "LUTBlend"
#define LUTBLEND_MATCH_NAME      "WM_LUTBlend"
#define LUTBLEND_CATEGORY        "Williams Media"

// eVER / eGLO / eGL2, read straight out of the shipped PiPL atoms.
#define LUTBLEND_VERSION         0x00080001
#define LUTBLEND_GLOBAL_OUT_FLAGS  0x02000000
#define LUTBLEND_GLOBAL_OUT_FLAGS2 0x08000000

// --- Recovered from EffectMain's PF_Cmd_PARAMS_SETUP ------------------------
//
//   id  param_type                 name              recovered settings
//    1  PF_Param_POPUP             "LUT A"
//    2  PF_Param_POPUP             "LUT B"
//    3  PF_Param_FLOAT_SLIDER      "Blend (%)"       valid_max=100, slider_max=100, precision=1
//    4  PF_Param_POPUP             "Drive"           choices "Slider|Global Luma|Per-pixel Luma"
//    5  PF_Param_FLOAT_SLIDER      "Strength (%)"    100.0 values, precision=1
//    6  PF_Param_CHECKBOX          "Preserve Luma"

enum {
  LUTBLEND_LUT_A = 1,
  LUTBLEND_LUT_B,
  LUTBLEND_BLEND,
  LUTBLEND_DRIVE,
  LUTBLEND_STRENGTH,
  LUTBLEND_PRESERVE_LUMA,
  LUTBLEND_NUM_PARAMS
};

#define LUTBLEND_PARAM_LUT_A          "LUT A"
#define LUTBLEND_PARAM_LUT_B          "LUT B"
#define LUTBLEND_PARAM_BLEND          "Blend (%)"
#define LUTBLEND_PARAM_DRIVE          "Drive"
#define LUTBLEND_PARAM_STRENGTH       "Strength (%)"
#define LUTBLEND_PARAM_PRESERVE_LUMA  "Preserve Luma"

// The Drive popup's choice string is recovered verbatim, pipe-separated, exactly
// as the PF_ADD_POPUP call passes it.
#define LUTBLEND_DRIVE_CHOICES        "Slider|Global Luma|Per-pixel Luma"

enum {
  LUTBLEND_DRIVE_SLIDER = 1,
  LUTBLEND_DRIVE_GLOBAL_LUMA,
  LUTBLEND_DRIVE_PER_PIXEL_LUMA
};

// Recovered bounds: valid_max and slider_max are both 100.0f, and precision is 1.
//
// NOT recovered: the per-parameter `flags` words, the `value_desc` strings, and
// the DEFAULT values for the two sliders. The defaults below are therefore the
// only values in this header that are a judgement call rather than a recovery,
// and they are called out here rather than buried. Blend defaults to 100 because
// both the valid_max and slider_max atoms are 100.0f and a blend control that
// opened at zero would make the effect a no-op on insert.
#define LUTBLEND_BLEND_MIN            0.0
#define LUTBLEND_BLEND_MAX            100.0
#define LUTBLEND_BLEND_DEFAULT        100.0

#define LUTBLEND_STRENGTH_MIN         0.0
#define LUTBLEND_STRENGTH_MAX         100.0
#define LUTBLEND_STRENGTH_DEFAULT     100.0

// Disk IDs. NOT recovered — the shipped plugin has no saved projects to be
// compatible with any more, so these are new and stable-by-construction.
enum {
  LUTBLEND_LUT_A_DISK_ID = 1,
  LUTBLEND_LUT_B_DISK_ID,
  LUTBLEND_BLEND_DISK_ID,
  LUTBLEND_DRIVE_DISK_ID,
  LUTBLEND_STRENGTH_DISK_ID,
  LUTBLEND_PRESERVE_LUMA_DISK_ID
};

// The LUT folder. The string "/Documents/Williams Media LUTs" was recovered from
// the bundle's parameter strings, and the ABOUT message says "Drop .cube files
// into ~%s". No PATH parameter was found among the six registered params, so this
// belongs to the plugin's own LUT-discovery layer (LB_LutFolderPath), which is
// NOT recovered.
#define LUTBLEND_LUT_FOLDER_SUFFIX    "/Documents/Williams Media LUTs"
