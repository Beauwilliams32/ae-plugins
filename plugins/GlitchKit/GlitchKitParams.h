// GlitchKitParams.h — the recovered parameter table for GlitchKit.
//
// PROVENANCE: every value here was RECOVERED from the shipped GlitchKit.plugin by
// tools/param-table.mjs, which reads the PF_ADD_PARAM blocks out of EffectMain.
// Derivation: PARAMETERS.md, and src/lutblend/RECOVERY-NOTES.md for the method.
//
// These live in the BINARY, not the PiPL: parameter types and IDs are registered at
// runtime through PF_ADD_PARAM, so the disassembly is the only surviving record.

#pragma once

// Recovered from the PiPL resource (packaging/GlitchKitPiPL.r).
#define GLITCHKIT_NAME        "GlitchKit"
#define GLITCHKIT_MATCH_NAME  "WM_GlitchKit"
#define GLITCHKIT_CATEGORY    "Williams Media"

// eVER / eGLO / eGL2. Decoded: DEEP_COLOR_AWARE (1<<25) and
// SUPPORTS_THREADED_RENDERING (1<<27), and nothing else. See CAPABILITIES.md —
// GlitchKit does NOT claim float, so there is deliberately no float path below.
#define GLITCHKIT_VERSION           0x00080001
#define GLITCHKIT_GLOBAL_OUT_FLAGS  0x02000004   // DEEP_COLOR_AWARE | NON_PARAM_VARY
#define GLITCHKIT_GLOBAL_OUT_FLAGS2 0x08000000   // SUPPORTS_THREADED_RENDERING

enum {
  GLITCHKIT_MODE = 1,
  GLITCHKIT_AMOUNT,
  GLITCHKIT_BLOCK_SIZE,
  GLITCHKIT_SEED,
  GLITCHKIT_SPEED,
  GLITCHKIT_CHANNEL_SHIFT,
  GLITCHKIT_SCANLINE_HEIGHT,
  GLITCHKIT_DRIVE_BY_LUMA,
  GLITCHKIT_SPREAD,
  GLITCHKIT_NUM_PARAMS
};

// Recovered names, exactly as stored in the binary.
#define GLITCHKIT_P_MODE        "Mode"
#define GLITCHKIT_P_AMOUNT      "Amount (%)"
#define GLITCHKIT_P_BLOCK       "Block Size"
#define GLITCHKIT_P_SEED        "Seed"
#define GLITCHKIT_P_SPEED       "Speed"
#define GLITCHKIT_P_SHIFT       "Channel Shift (px)"
#define GLITCHKIT_P_SCANLINE    "Scanline Height"
#define GLITCHKIT_P_LUMA        "Drive by Luma"
#define GLITCHKIT_P_SPREAD      "Spread"

// The five modes, recovered verbatim from the popup's choice string. This is the
// plugin's own description of itself: "Five deterministic, seeded glitch modes."
#define GLITCHKIT_MODE_CHOICES \
  "Block Displace|RGB Channel Shift|Scanline Jitter|Bad Signal|Datamosh Smear"

enum {
  GLITCHKIT_M_BLOCK_DISPLACE = 1,
  GLITCHKIT_M_CHANNEL_SHIFT,
  GLITCHKIT_M_SCANLINE_JITTER,
  GLITCHKIT_M_BAD_SIGNAL,
  GLITCHKIT_M_DATAMOSH_SMEAR
};

// Recovered slider geometry. `u+0x34` is valid_max/slider_max and `u+0x40` is
// dephault; the raw bits are kept as the disassembly carries them.
#define GLITCHKIT_AMOUNT_MIN      0.0
#define GLITCHKIT_AMOUNT_MAX      100.0    // 0x42c80000
#define GLITCHKIT_BLOCK_MIN       1.0
#define GLITCHKIT_BLOCK_MAX       32.0     // 0x42000000, and the default
#define GLITCHKIT_SEED_MIN        0.0
#define GLITCHKIT_SEED_MAX        100000.0 // 0x47c35000
#define GLITCHKIT_SPEED_MIN       0.0
#define GLITCHKIT_SPEED_MAX       10.0     // 0x41200000
#define GLITCHKIT_SPEED_PRECISION 2        // recovered: precision 2
#define GLITCHKIT_SHIFT_MIN       0.0
#define GLITCHKIT_SHIFT_MAX       200.0    // 0x43480000
#define GLITCHKIT_SCANLINE_MIN    1.0
#define GLITCHKIT_SCANLINE_MAX    4.0      // 0x40800000, and the default
#define GLITCHKIT_SPREAD_MIN      0.0
#define GLITCHKIT_SPREAD_MAX      100.0

// Disk IDs are NEW, not recovered: the shipped plugin has no saved projects left to
// stay compatible with, so these are stable by construction.
enum {
  GLITCHKIT_MODE_DISK_ID = 1,
  GLITCHKIT_AMOUNT_DISK_ID,
  GLITCHKIT_BLOCK_DISK_ID,
  GLITCHKIT_SEED_DISK_ID,
  GLITCHKIT_SPEED_DISK_ID,
  GLITCHKIT_SHIFT_DISK_ID,
  GLITCHKIT_SCANLINE_DISK_ID,
  GLITCHKIT_LUMA_DISK_ID,
  GLITCHKIT_SPREAD_DISK_ID
};
