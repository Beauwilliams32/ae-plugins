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
#include "GlitchKitParams.h"

namespace {

// Every parameter of this effect, in the units the render path wants.
struct Params {
  int   mode;
  float amount;      // 0..1
  int   blockSize;   // pixels
  float seed;
  float speed;
  float channelShift; // pixels
  int   scanlineHeight;
  bool  driveByLuma;
  float spread;       // 0..1
};

// ---------------------------------------------------------------------------
// The scratch a render needs, shared by every depth.
// ---------------------------------------------------------------------------
struct RenderCtx {
  const Params* p;
  unsigned      frameSeed;   // patternFrame(seed, time) — the recovered helper
  long          width, height;
  const char*   src;
  char*         dst;
  long          srcRowBytes;
  long          dstRowBytes;
  // Mean luma of the frame, used when Drive by Luma is on. Depth-independent
  // because the 0..1 pixels are the same either way.
  float         meanLuma;
};

// Coordinate hash for one (blockX, blockY, frame) cell, via the RECOVERED hashKey.
inline unsigned cellHash(const RenderCtx& rc, long x, long y) {
  const unsigned bx = static_cast<unsigned>(x / (rc.p->blockSize > 0 ? rc.p->blockSize : 1));
  const unsigned by = static_cast<unsigned>(y / (rc.p->blockSize > 0 ? rc.p->blockSize : 1));
  return glitch::hashKey(bx, by, rc.frameSeed);
}

// Map a hash to a signed displacement in [-span, span].
inline long hashOffset(unsigned h, long span) {
  if (span <= 0) return 0;
  const long range = span * 2 + 1;
  return static_cast<long>(h % static_cast<unsigned>(range)) - span;
}

// How much this effect should do at this pixel. Amount and Drive by Luma combine:
// with the checkbox off, the mode applies uniformly; with it on, dark areas are
// left alone and bright areas glitch, which is the usual reason to want the switch.
inline float weightAt(const RenderCtx& rc, float pixelLuma) {
  float w = rc.p->amount;
  if (rc.p->driveByLuma) w *= pixelLuma;
  return glitch::clampf(w, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Per-depth pixel access. Each trait gives the channel type, its maximum, and how
// to read/write one channel as a 0..1 float.
//
// PF_MAX_CHAN16 is 32768, NOT 65535: AE's 16-bit channels use [0 .. 2^15].
// ---------------------------------------------------------------------------
template <typename Pix, typename Chan>
struct Depth;

template <>
struct Depth<PF_Pixel8, A_u_char> {
  static constexpr float kMax = static_cast<float>(PF_MAX_CHAN8);
  static const PF_Pixel8* row(const RenderCtx& rc, long y) {
    return reinterpret_cast<const PF_Pixel8*>(rc.src + y * rc.srcRowBytes);
  }
};

template <>
struct Depth<PF_Pixel16, A_u_short> {
  static constexpr float kMax = static_cast<float>(PF_MAX_CHAN16);
  static const PF_Pixel16* row(const RenderCtx& rc, long y) {
    return reinterpret_cast<const PF_Pixel16*>(rc.src + y * rc.srcRowBytes);
  }
};

// ---------------------------------------------------------------------------
// The five modes. Each writes one output pixel by choosing a source pixel (and
// optionally a source channel) — no mode synthesises colour out of nothing, which
// is what keeps the result looking like corrupted footage rather than noise.
// ---------------------------------------------------------------------------
template <typename Pix, typename Chan>
void renderImpl(const RenderCtx& rc) {
  using D = Depth<Pix, Chan>;
  const Params& p = *rc.p;

  Pix* outRow = nullptr;
  for (long y = 0; y < rc.height; ++y) {
    outRow = reinterpret_cast<Pix*>(rc.dst + y * rc.dstRowBytes);
    const Pix* inRow = D::row(rc, y);

    switch (p.mode) {
      // 1. Block Displace — whole blocks slide sideways by a hashed amount.
      case GLITCHKIT_M_BLOCK_DISPLACE: {
        const long span = static_cast<long>(p.blockSize * p.spread * 4.0f) + 1;
        for (long x = 0; x < rc.width; ++x) {
          const unsigned h = cellHash(rc, x, y);
          const float w = weightAt(rc, inRow[x].red / D::kMax);
          const long off = static_cast<long>(hashOffset(h, span) * w);
          const long sx = glitch::clampi(static_cast<int>(x + off), 0,
                                         static_cast<int>(rc.width) - 1);
          outRow[x] = inRow[sx];
        }
        break;
      }

      // 2. RGB Channel Shift — the channels of one row no longer line up.
      case GLITCHKIT_M_CHANNEL_SHIFT: {
        const unsigned h = glitch::hashKey(0u, static_cast<unsigned>(y), rc.frameSeed);
        const float w = weightAt(rc, inRow[rc.width / 2].green / D::kMax);
        const long rg = static_cast<long>(p.channelShift * w);
        const long rb = static_cast<long>(p.channelShift * w * 1.7f);
        for (long x = 0; x < rc.width; ++x) {
          const long xg = glitch::clampi(static_cast<int>(x + hashOffset(h, rg ? rg : 1)), 0,
                                         static_cast<int>(rc.width) - 1);
          const long xb = glitch::clampi(static_cast<int>(x - hashOffset(h >> 8, rb ? rb : 1)), 0,
                                         static_cast<int>(rc.width) - 1);
          outRow[x] = inRow[x];
          outRow[x].green = inRow[xg].green;
          outRow[x].blue = inRow[xb].blue;
        }
        break;
      }

      // 3. Scanline Jitter — every band of `scanlineHeight` rows tears sideways.
      case GLITCHKIT_M_SCANLINE_JITTER: {
        const long band = p.scanlineHeight > 0 ? p.scanlineHeight : 1;
        const long span = static_cast<long>(p.spread * 64.0f) + 1;
        for (long x = 0; x < rc.width; ++x) {
          const unsigned h = glitch::hashKey(static_cast<unsigned>(y / band),
                                             static_cast<unsigned>(x / 32), rc.frameSeed);
          const float w = weightAt(rc, inRow[x].red / D::kMax);
          const long off = static_cast<long>(hashOffset(h, span) * w);
          const long sx = glitch::clampi(static_cast<int>(x + off), 0,
                                         static_cast<int>(rc.width) - 1);
          outRow[x] = inRow[sx];
        }
        break;
      }

      // 4. Bad Signal — displacement plus a channel swap, which is what a
      //    mistimed sync actually looks like.
      case GLITCHKIT_M_BAD_SIGNAL: {
        const long span = static_cast<long>(p.spread * 96.0f) + 1;
        for (long x = 0; x < rc.width; ++x) {
          const unsigned h = glitch::hashKey(static_cast<unsigned>(x),
                                             static_cast<unsigned>(y), rc.frameSeed);
          const float w = weightAt(rc, inRow[x].blue / D::kMax);
          const long off = static_cast<long>(hashOffset(h, span) * w);
          const long sx = glitch::clampi(static_cast<int>(x + off), 0,
                                         static_cast<int>(rc.width) - 1);
          Pix px = inRow[sx];
          // Rotate the channels when the hash says so — a rare event, so the
          // picture reads as a fault rather than as a filter.
          if ((h >> 13) % 11u == 0u) {
            const Chan t = px.red;
            px.red = px.green; px.green = px.blue; px.blue = t;
          }
          outRow[x] = px;
        }
        break;
      }

      // 5. Datamosh Smear — each row is dragged from the row above it, so motion
      //    in the source bleeds downward and accumulates.
      case GLITCHKIT_M_DATAMOSH_SMEAR: {
        const Pix* prev = (y > 0)
            ? reinterpret_cast<const Pix*>(rc.src + (y - 1) * rc.srcRowBytes)
            : inRow;
        const long span = static_cast<long>(p.spread * 48.0f) + 1;
        for (long x = 0; x < rc.width; ++x) {
          const unsigned h = glitch::hashKey(static_cast<unsigned>(y),
                                             static_cast<unsigned>(x / 8), rc.frameSeed);
          const float w = weightAt(rc, inRow[x].red / D::kMax);
          const long off = static_cast<long>(hashOffset(h, span) * w);
          const long sx = glitch::clampi(static_cast<int>(x + off), 0,
                                         static_cast<int>(rc.width) - 1);
          outRow[x] = prev[sx];
        }
        break;
      }

      default:
        for (long x = 0; x < rc.width; ++x) outRow[x] = inRow[x];
        break;
    }
  }
}

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

  Params p;
  p.mode          = params[GLITCHKIT_MODE]->u.pd.value;
  p.amount        = glitch::clampf(static_cast<float>(params[GLITCHKIT_AMOUNT]->u.fs_d.value) / 100.0f, 0.0f, 1.0f);
  p.blockSize     = static_cast<int>(params[GLITCHKIT_BLOCK_SIZE]->u.fs_d.value);
  p.seed          = static_cast<float>(params[GLITCHKIT_SEED]->u.fs_d.value);
  p.speed         = static_cast<float>(params[GLITCHKIT_SPEED]->u.fs_d.value);
  p.channelShift  = static_cast<float>(params[GLITCHKIT_CHANNEL_SHIFT]->u.fs_d.value);
  p.scanlineHeight= static_cast<int>(params[GLITCHKIT_SCANLINE_HEIGHT]->u.fs_d.value);
  p.driveByLuma   = params[GLITCHKIT_DRIVE_BY_LUMA]->u.bd.value != 0;
  p.spread        = glitch::clampf(static_cast<float>(params[GLITCHKIT_SPREAD]->u.fs_d.value) / 100.0f, 0.0f, 1.0f);

  RenderCtx rc;
  rc.p           = &p;
  rc.width       = output->width;
  rc.height      = output->height;
  rc.src         = reinterpret_cast<const char*>(params[0]->u.ld.data);
  rc.dst         = reinterpret_cast<char*>(output->data);
  rc.srcRowBytes = params[0]->u.ld.rowbytes;
  rc.dstRowBytes = output->rowbytes;
  rc.meanLuma    = 0.5f;

  // The seed for this frame comes from the RECOVERED patternFrame, which is what
  // makes "same seed, same frame, same image" true rather than aspirational. Time
  // is the frame time scaled by Speed, so Speed changes how fast the pattern
  // evolves rather than what it is.
  const float t = static_cast<float>(in_data->current_time) /
                  static_cast<float>(in_data->time_scale) * p.speed;
  rc.frameSeed = glitch::patternFrame(static_cast<unsigned>(p.seed < 0 ? 0 : p.seed), t);

  const bool deep = PF_WORLD_IS_DEEP(&params[0]->u.ld) != 0;
  if (deep) renderImpl<PF_Pixel16, A_u_short>(rc);
  else      renderImpl<PF_Pixel8, A_u_char>(rc);

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
