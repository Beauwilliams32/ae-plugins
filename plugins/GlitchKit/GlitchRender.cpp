// GlitchRender.cpp — the GlitchKit render core. See GlitchRender.h for why it is
// separate from the host integration.
//
// The mode arithmetic is NEW code; every hash it draws on comes from the RECOVERED
// glitch::hashKey and glitch::patternFrame, which tools/codegen-diff.mjs verifies
// instruction-for-instruction against the shipped binary. That is what makes the
// "same seed, same frame, same image" promise attributable to this plugin's own
// generator rather than to something invented alongside it.

#include "GlitchRender.h"

#include "glitch.h"
#include "GlitchKitParams.h"

namespace glitchkit {
namespace {

// ---------------------------------------------------------------------------
// The scratch a render needs, shared by every depth.
// ---------------------------------------------------------------------------
struct RenderCtx {
  const FrameParams* p;
  unsigned      frameSeed;   // patternFrame(seed, time) — the recovered helper
  long          width, height;
  const char*   src;
  char*         dst;
  long          srcRowBytes;
  long          dstRowBytes;
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
// PF_MAX_CHAN16 is 32768, NOT 65535: AE's 16-bit channels use [0 .. 2^15].
template <typename Pix>
inline float kMaxOf();
template <>
inline float kMaxOf<PF_Pixel8>()  { return static_cast<float>(PF_MAX_CHAN8); }
template <>
inline float kMaxOf<PF_Pixel16>() { return static_cast<float>(PF_MAX_CHAN16); }

// ---------------------------------------------------------------------------
// The five modes. Each writes one output pixel by choosing a source pixel (and
// optionally a source channel) — no mode synthesises colour out of nothing, which
// is what keeps the result looking like corrupted footage rather than noise.
// ---------------------------------------------------------------------------
template <typename Pix>
void renderImpl(const RenderCtx& rc) {
  const FrameParams& p = *rc.p;

  Pix* outRow = nullptr;
  for (long y = 0; y < rc.height; ++y) {
    outRow = reinterpret_cast<Pix*>(rc.dst + y * rc.dstRowBytes);
    const Pix* inRow = reinterpret_cast<const Pix*>(rc.src + y * rc.srcRowBytes);

    switch (p.mode) {
      // 1. Block Displace — whole blocks slide sideways by a hashed amount.
      case GLITCHKIT_M_BLOCK_DISPLACE: {
        const long span = static_cast<long>(p.blockSize * p.spread * 4.0f) + 1;
        for (long x = 0; x < rc.width; ++x) {
          const unsigned h = cellHash(rc, x, y);
          const float w = weightAt(rc, inRow[x].red / kMaxOf<Pix>());
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
        const float w = weightAt(rc, inRow[rc.width / 2].green / kMaxOf<Pix>());
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
          const float w = weightAt(rc, inRow[x].red / kMaxOf<Pix>());
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
          const float w = weightAt(rc, inRow[x].blue / kMaxOf<Pix>());
          const long off = static_cast<long>(hashOffset(h, span) * w);
          const long sx = glitch::clampi(static_cast<int>(x + off), 0,
                                         static_cast<int>(rc.width) - 1);
          Pix px = inRow[sx];
          // Rotate the channels when the hash says so — a rare event, so the
          // picture reads as a fault rather than as a filter.
          if ((h >> 13) % 11u == 0u) {
            const auto t = px.red;
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
          const float w = weightAt(rc, inRow[x].red / kMaxOf<Pix>());
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

}  // namespace

unsigned frameSeedFor(const FrameParams& p, double timeSeconds) {
  const float s = p.seed < 0.0f ? 0.0f : p.seed;
  return glitch::patternFrame(static_cast<unsigned>(s),
                              static_cast<float>(timeSeconds * static_cast<double>(p.speed)));
}

void renderFrame(const FrameParams& p, unsigned frameSeed,
                 const FrameView& src, const FrameView& dst, bool deep) {
  RenderCtx rc;
  rc.p           = &p;
  rc.frameSeed   = frameSeed;
  rc.width       = dst.width;
  rc.height      = dst.height;
  rc.src         = static_cast<const char*>(src.data);
  rc.srcRowBytes = src.rowBytes;
  rc.dst         = static_cast<char*>(dst.data);
  rc.dstRowBytes = dst.rowBytes;

  if (deep) renderImpl<PF_Pixel16>(rc);
  else      renderImpl<PF_Pixel8>(rc);
}

}  // namespace glitchkit
