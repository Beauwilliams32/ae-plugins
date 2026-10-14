// Self-check for the recovered echo:: namespace.

#include "echo.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {
int failures = 0, passes = 0;
void check(bool ok, const char* why) {
  if (ok) { ++passes; std::printf("  PASS %s\n", why); }
  else    { ++failures; std::printf("  FAIL %s\n", why); }
}
void near(float got, float want, float tol, const char* why) {
  if (std::fabs(got - want) <= tol) { ++passes; std::printf("  PASS %s\n", why); }
  else { ++failures; std::printf("  FAIL %s  (got %.6g want %.6g)\n", why, got, want); }
}
}  // namespace

int main() {
  using echo::BlendMode;

  std::printf("echo::clampf\n");
  {
    near(echo::clampf(5.f, 0.f, 10.f), 5.f, 0.f, "in range passes through");
    near(echo::clampf(-1.f, 0.f, 10.f), 0.f, 0.f, "below lo clamps to lo");
    near(echo::clampf(11.f, 0.f, 10.f), 10.f, 0.f, "above hi clamps to hi");
    near(echo::clampf(5.f, 10.f, 0.f), 10.f, 0.f, "inverted range yields lo (lo tested last)");
  }

  std::printf("echo::blendPixel\n");
  {
    // Opaque mid-grey destination, opaque green source, half amount.
    //   dst = (1,0,0,1) premultiplied red, src = (0,1,0,1) premultiplied green
    //   a = 0.5, d = 1  ->  wSrc = 0, wBoth = 0.5, wDst = 0.5
    //   out = 0.5*src + 0.5*dst, alpha 1
    float dst[4] = {1.f, 0.f, 0.f, 1.f};
    const float src[4] = {0.f, 1.f, 0.f, 1.f};
    echo::blendPixel(dst, src, BlendMode::OverA, 0.5f);
    near(dst[0], 0.5f, 1e-6f, "over at 50% is a half mix (R)");
    near(dst[1], 0.5f, 1e-6f, "over at 50% is a half mix (G)");
    near(dst[2], 0.0f, 1e-6f, "with no blue from either");
    near(dst[3], 1.0f, 1e-6f, "and the alpha stays 1 for two opaque layers");

    // amount 0 changes nothing, and returns early.
    float d0[4] = {0.25f, 0.5f, 0.75f, 1.f};
    const float s0[4] = {1.f, 1.f, 1.f, 1.f};
    echo::blendPixel(d0, s0, BlendMode::OverA, 0.0f);
    near(d0[0], 0.25f, 0.f, "amount 0 leaves the destination untouched");

    // A source with zero alpha also returns early, even at full amount.
    float d1[4] = {0.25f, 0.5f, 0.75f, 1.f};
    const float s1[4] = {1.f, 1.f, 1.f, 0.f};
    echo::blendPixel(d1, s1, BlendMode::OverA, 1.0f);
    near(d1[0], 0.25f, 0.f, "a fully transparent source leaves the destination untouched");

    // over and its twin agree; the enum has two indistinguishable values.
    float a1[4] = {0.f, 0.f, 0.f, 1.f}, a2[4] = {0.f, 0.f, 0.f, 1.f};
    echo::blendPixel(a1, src, BlendMode::OverA, 0.5f);
    echo::blendPixel(a2, src, BlendMode::OverB, 0.5f);
    check(a1[0] == a2[0] && a1[1] == a2[1] && a1[3] == a2[3],
          "OverA and OverB are the same curve in the binary");

    // ALPHA is mode-independent: `a + d*(1-a)`.
    float a3[4] = {0.f, 0.f, 0.f, 0.5f};
    const float half[4] = {0.25f, 0.25f, 0.25f, 0.5f};   // premultiplied 0.5
    echo::blendPixel(a3, half, BlendMode::Add, 1.0f);
    near(a3[3], 0.5f + 0.5f * 0.5f, 1e-6f, "add: alpha is a + d*(1-a)");

    // ADD is brighter than OVER for the same inputs.
    float ov[4] = {0.5f, 0.5f, 0.5f, 1.f};
    float ad[4] = {0.5f, 0.5f, 0.5f, 1.f};
    const float grey[4] = {0.5f, 0.5f, 0.5f, 1.f};
    echo::blendPixel(ov, grey, BlendMode::OverA, 1.0f);
    echo::blendPixel(ad, grey, BlendMode::Add, 1.0f);
    check(ad[0] > ov[0], "add is brighter than over");

    // SCREEN is brighter than over but never exceeds 1 for in-range inputs.
    float sc[4] = {0.5f, 0.5f, 0.5f, 1.f};
    echo::blendPixel(sc, grey, BlendMode::Screen, 1.0f);
    check(sc[0] > 0.5f && sc[0] <= 1.0f + 1e-6f, "screen brightens without exceeding 1");
    // screen(a,b) = a + b - ab, with both here unpremultiplied 0.5 -> 0.75
    near(sc[0], 0.75f, 1e-6f, "screen follows a + b - a*b");

    // LIGHTEN takes the brighter of the two, per channel.
    float lt[4] = {0.2f, 0.8f, 0.5f, 1.f};
    const float other[4] = {0.6f, 0.3f, 0.5f, 1.f};
    echo::blendPixel(lt, other, BlendMode::Lighten, 1.0f);
    near(lt[0], 0.6f, 1e-6f, "lighten picks the brighter channel (R)");
    near(lt[1], 0.8f, 1e-6f, "and independently per channel (G)");

    // PREMULTIPLIED input is expected: a half-alpha white is stored as 0.5, and the
    // unpremultiply step must recover 1.0 before blending.
    float pd[4] = {0.f, 0.f, 0.f, 1.f};
    const float premul[4] = {0.5f, 0.5f, 0.5f, 0.5f};   // straight colour 1.0
    echo::blendPixel(pd, premul, BlendMode::OverA, 1.0f);
    near(pd[0], 0.5f, 1e-6f, "a premultiplied half-alpha white composites as white");
    near(pd[3], 1.0f, 1e-6f, "and its alpha lands on 1 over an opaque destination");

    // A transparent destination must not divide by zero, and must not produce NaN.
    float td[4] = {0.7f, 0.1f, 0.3f, 0.f};   // colour stored, alpha 0
    echo::blendPixel(td, src, BlendMode::OverA, 1.0f);
    check(td[0] == td[0] && td[1] == td[1] && td[2] == td[2] && td[3] == td[3],
          "a transparent destination produces no NaN");
    near(td[3], 1.0f, 1e-6f, "and the source's alpha wins outright");
  }

  std::printf("echo::compositeOver\n");
  {
    // 2x2 destination, uniform source, over at full amount.
    std::vector<float> d(2 * 2 * 4, 0.0f);
    std::vector<float> s(2 * 2 * 4, 0.0f);
    for (int i = 0; i < 4; ++i) { d[i * 4 + 0] = 0.f; d[i * 4 + 3] = 1.f; }
    for (int i = 0; i < 4; ++i) { s[i * 4 + 1] = 1.f; s[i * 4 + 3] = 1.f; }
    echo::ImageF dst{2, 2, d.data()}, srci{2, 2, s.data()};
    echo::compositeOver(dst, srci, BlendMode::OverA, 1.0f);
    check(d[1] == 1.f && d[5] == 1.f && d[9] == 1.f && d[13] == 1.f,
          "every pixel of the image is composited, not just the first");

    // A size mismatch is a SILENT no-op, not a partial composite.
    std::vector<float> d2(2 * 2 * 4, 0.25f), s2(3 * 2 * 4, 1.0f);
    echo::ImageF dst2{2, 2, d2.data()}, srci2{3, 2, s2.data()};
    echo::compositeOver(dst2, srci2, BlendMode::OverA, 1.0f);
    bool untouched = true;
    for (float v : d2) if (v != 0.25f) untouched = false;
    check(untouched, "differing widths leave the destination completely untouched");

    std::vector<float> d3(2 * 2 * 4, 0.25f), s3(2 * 3 * 4, 1.0f);
    echo::ImageF dst3{2, 2, d3.data()}, srci3{2, 3, s3.data()};
    echo::compositeOver(dst3, srci3, BlendMode::OverA, 1.0f);
    untouched = true;
    for (float v : d3) if (v != 0.25f) untouched = false;
    check(untouched, "differing heights too — the test is on BOTH dimensions");

    // A degenerate image is a no-op rather than a runaway loop.
    std::vector<float> dz;
    echo::ImageF dstz{0, 0, dz.data()}, srciz{0, 0, dz.data()};
    echo::compositeOver(dstz, srciz, BlendMode::OverA, 1.0f);
    check(true, "a zero-area image does nothing and terminates");
  }

  std::printf("\n%s\n", failures == 0
                ? "ALL RECOVERED ECHO MATHS VERIFIED"
                : "TRANSCRIPTION ERRORS PRESENT");
  return failures == 0 ? 0 : 1;
}
