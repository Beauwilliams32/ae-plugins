// LutRender.cpp — the LUTBlend render core. See LutRender.h for why it is separate.

#include "LutRender.h"

namespace lutrender {
namespace {

inline float chanMax(bool deep) {
  // PF_MAX_CHAN16 is 32768, NOT 65535: AE's 16-bit channels use [0 .. 2^15].
  return deep ? static_cast<float>(PF_MAX_CHAN16) : static_cast<float>(PF_MAX_CHAN8);
}

// The fallback when a popup is unset or its file failed to parse.
//
// NOT a default-constructed CubeLUT. That has size 0 and fails valid(), so sampling it
// is meaningless — the shipped plugin builds a real identity LUT for this case
// (__ZL11identityLUTv, plus the same fallback inside makeIdentity). Using an empty
// CubeLUT here would have looked like a working fallback and produced a black frame.
const lutblend::CubeLUT& identityLUT() {
  static const lutblend::CubeLUT lut = [] {
    lutblend::CubeLUT l;
    lutblend::makeIdentity(&l, 2, true);
    return l;
  }();
  return lut;
}

}  // namespace

void blendPixel(const FrameParams& p, float meanLumaValue, const float rgb[3], float out[3]) {
  // Which Drive mode applies is resolved HERE rather than in the caller, so the three
  // modes are distinguishable in one place and a test can drive each directly.
  float blend = p.blend;
  if (p.drive == kGlobalLuma) {
    blend = meanLumaValue;
  } else if (p.drive == kPerPixelLuma) {
    blend = lutblend::luma709(rgb[0], rgb[1], rgb[2]);
  }

  const lutblend::CubeLUT& a = p.a ? *p.a : identityLUT();
  const lutblend::CubeLUT& b = p.b ? *p.b : identityLUT();

  lutblend::applyLUTBlend(a, b, blend, p.strength, p.preserveLuma, rgb, out);
  for (int i = 0; i < 3; ++i) out[i] = lutblend::clampf(out[i], 0.0f, 1.0f);
}

float meanLuma(const FrameView& src, bool deep, long stride) {
  if (stride < 1) stride = 1;
  const float scale = chanMax(deep);
  double sum = 0.0;
  long n = 0;

  for (long row = 0; row < src.height; row += stride) {
    const char* rowBase = static_cast<const char*>(src.data) + static_cast<size_t>(row) * src.rowBytes;
    for (long col = 0; col < src.width; col += stride) {
      float rgb[3];
      if (deep) {
        const PF_Pixel16* px = reinterpret_cast<const PF_Pixel16*>(rowBase) + col;
        rgb[0] = px->red / scale; rgb[1] = px->green / scale; rgb[2] = px->blue / scale;
      } else {
        const PF_Pixel8* px = reinterpret_cast<const PF_Pixel8*>(rowBase) + col;
        rgb[0] = px->red / scale; rgb[1] = px->green / scale; rgb[2] = px->blue / scale;
      }
      sum += lutblend::luma709(rgb[0], rgb[1], rgb[2]);
      ++n;
    }
  }
  return n > 0 ? static_cast<float>(sum / static_cast<double>(n)) : 0.5f;
}

void renderFrame(const FrameParams& p, float meanLumaValue,
                 const FrameView& src, const FrameView& dst, bool deep) {
  const float scale = chanMax(deep);

  for (long row = 0; row < dst.height; ++row) {
    const char* inBase = static_cast<const char*>(src.data) + static_cast<size_t>(row) * src.rowBytes;
    char* outBase = static_cast<char*>(dst.data) + static_cast<size_t>(row) * dst.rowBytes;

    for (long col = 0; col < dst.width; ++col) {
      float rgb[3];
      if (deep) {
        const PF_Pixel16* in = reinterpret_cast<const PF_Pixel16*>(inBase) + col;
        PF_Pixel16* out = reinterpret_cast<PF_Pixel16*>(outBase) + col;
        rgb[0] = in->red / scale; rgb[1] = in->green / scale; rgb[2] = in->blue / scale;
        float o[3];
        blendPixel(p, meanLumaValue, rgb, o);
        out->alpha = in->alpha;
        out->red   = static_cast<A_u_short>(o[0] * scale + 0.5f);
        out->green = static_cast<A_u_short>(o[1] * scale + 0.5f);
        out->blue  = static_cast<A_u_short>(o[2] * scale + 0.5f);
      } else {
        const PF_Pixel8* in = reinterpret_cast<const PF_Pixel8*>(inBase) + col;
        PF_Pixel8* out = reinterpret_cast<PF_Pixel8*>(outBase) + col;
        rgb[0] = in->red / scale; rgb[1] = in->green / scale; rgb[2] = in->blue / scale;
        float o[3];
        blendPixel(p, meanLumaValue, rgb, o);
        out->alpha = in->alpha;
        out->red   = static_cast<A_u_char>(o[0] * scale + 0.5f);
        out->green = static_cast<A_u_char>(o[1] * scale + 0.5f);
        out->blue  = static_cast<A_u_char>(o[2] * scale + 0.5f);
      }
    }
  }
}

}  // namespace lutrender
