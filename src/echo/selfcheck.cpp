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
// ImageF now holds a std::vector<float> rather than a borrowed pointer — the layout was
// corrected once transformBilinear was recovered — so a test image has to own its pixels.
// These helpers COPY, which also means a mistake in the recovered code cannot scribble on
// the vector the test still holds for comparison.
echo::ImageF makeImage(int w, int h, const std::vector<float>& src) {
  echo::ImageF img;
  img.width = w;
  img.height = h;
  img.pixels = src;
  return img;
}

std::vector<float> uniformImage(int w, int h, float r, float g, float b, float a) {
  std::vector<float> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 0.0f);
  for (size_t i = 0; i < px.size(); i += 4) {
    px[i + 0] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = a;
  }
  return px;
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
    echo::ImageF dst = makeImage(2, 2, d), srci = makeImage(2, 2, s);
    echo::compositeOver(dst, srci, BlendMode::OverA, 1.0f);
    // Asserted on dst.pixels, NOT on `d`. makeImage copies, so a check against `d` would
    // read the untouched original and pass no matter what compositeOver did — which is
    // exactly how this assertion failed the first time it ran, and why it is worth
    // spelling out rather than silently repairing.
    check(dst.pixels[1] == 1.f && dst.pixels[5] == 1.f && dst.pixels[9] == 1.f
              && dst.pixels[13] == 1.f,
          "every pixel of the image is composited, not just the first");

    // A size mismatch is a SILENT no-op, not a partial composite.
    std::vector<float> d2(2 * 2 * 4, 0.25f), s2(3 * 2 * 4, 1.0f);
    echo::ImageF dst2 = makeImage(2, 2, d2), srci2 = makeImage(3, 2, s2);
    echo::compositeOver(dst2, srci2, BlendMode::OverA, 1.0f);
    bool untouched = true;
    for (float v : dst2.pixels) if (v != 0.25f) untouched = false;
    check(untouched, "differing widths leave the destination completely untouched");

    std::vector<float> d3(2 * 2 * 4, 0.25f), s3(2 * 3 * 4, 1.0f);
    echo::ImageF dst3 = makeImage(2, 2, d3), srci3 = makeImage(2, 3, s3);
    echo::compositeOver(dst3, srci3, BlendMode::OverA, 1.0f);
    untouched = true;
    for (float v : dst3.pixels) if (v != 0.25f) untouched = false;
    check(untouched, "differing heights too — the test is on BOTH dimensions");

    // A degenerate image is a no-op rather than a runaway loop.
    std::vector<float> dz;
    echo::ImageF dstz = makeImage(0, 0, dz), srciz = makeImage(0, 0, dz);
    echo::compositeOver(dstz, srciz, BlendMode::OverA, 1.0f);
    check(true, "a zero-area image does nothing and terminates");
  }

  std::printf("echo::buildLadder\n");
  {
    // n < 1 is EMPTY, not one rung and not a clamped rung.
    check(echo::buildLadder(0, 0.9f, 5.0f, 0.1f).empty(), "n == 0 gives an empty ladder");
    check(echo::buildLadder(-7, 0.9f, 5.0f, 0.1f).empty(), "a negative n gives an empty ladder");

    const auto five = echo::buildLadder(5, 0.9f, 5.0f, 0.1f);
    check(five.size() == 5, "n == 5 gives five rungs");

    // The count is CLAMPED at 16, it is not a wrap and not an error.
    check(echo::buildLadder(17, 1.0f, 1.0f, 1.0f).size() == 16, "17 rungs clamp to 16");
    check(echo::buildLadder(1000, 1.0f, 1.0f, 1.0f).size() == 16, "and so does 1000");

    // The scale is clamped into [0, 1] — trails shrink, they never grow.
    const auto wide = echo::buildLadder(3, 4.0f, 0.0f, 0.0f);
    near(wide[0].scale, 1.0f, 1e-6f, "a scale above 1 clamps to 1");
    near(echo::buildLadder(3, -2.0f, 0.0f, 0.0f)[0].scale, 0.0f, 1e-6f,
         "a negative scale clamps to 0");

    // The angle is FLOORED at 1e-4f, never 0 — the consumer treats
    // `scale == 1 && angle == 0` as identity and skips the resample entirely.
    near(echo::buildLadder(3, 1.0f, 0.0f, 0.0f)[0].angleDeg, 1e-4f, 1e-9f,
         "a zero angle is floored to 1e-4, not left at 0");
    near(echo::buildLadder(3, 1.0f, -9.0f, 0.0f)[0].angleDeg, 1e-4f, 1e-9f,
         "so is a negative angle");
    near(echo::buildLadder(3, 1.0f, 5.0f, 0.0f)[2].angleDeg, 5.0f, 1e-6f,
         "an angle above the floor passes through untouched");

    // The amount is the step increment times a ONE-BASED index, so rung 0 is NOT zero.
    bool oneBased = true;
    for (size_t j = 0; j < five.size(); ++j) {
      if (std::fabs(five[j].amount - 0.1f * static_cast<float>(j + 1)) > 1e-6f) oneBased = false;
    }
    check(oneBased, "the rung opacity is amountStep * (j + 1) — a one-based index");
    near(five[0].amount, 0.1f, 1e-6f, "so the first rung is one step, not zero");
  }

  std::printf("echo::sampleBilinear\n");
  {
    // A 2x2 image with four distinct colours, so every tap is identifiable.
    echo::ImageF img = makeImage(2, 2, {
        1.0f, 0.0f, 0.0f, 1.0f,   0.0f, 1.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 1.0f,   1.0f, 1.0f, 1.0f, 1.0f});
    float out[4];

    // At an integer coordinate the sample is exactly that texel: fx = fy = 0.
    echo::sampleBilinear(img, 0.0f, 0.0f, out);
    near(out[0], 1.0f, 1e-6f, "sampling at (0,0) gives the (0,0) texel red");
    near(out[1], 0.0f, 1e-6f, "and its green");
    echo::sampleBilinear(img, 1.0f, 1.0f, out);
    near(out[0], 1.0f, 1e-6f, "sampling at (1,1) gives the (1,1) texel red");
    near(out[1], 1.0f, 1e-6f, "and its green");

    // Halfway between four texels is the mean of the four.
    echo::sampleBilinear(img, 0.5f, 0.5f, out);
    near(out[0], (1.0f + 0.0f + 0.0f + 1.0f) / 4.0f, 1e-6f, "a centre sample averages red");
    near(out[1], (0.0f + 1.0f + 0.0f + 1.0f) / 4.0f, 1e-6f, "and green");
    near(out[3], 1.0f, 1e-6f, "and alpha, which is 1 everywhere");

    // THE EDGE BEHAVIOUR. Half a pixel outside the left border keeps only the two taps
    // that are inside, contributing HALF the weight, and there is no renormalisation —
    // so the result is half brightness rather than a clamped edge texel.
    echo::sampleBilinear(img, -0.5f, 0.0f, out);
    near(out[0], 0.5f * 1.0f, 1e-6f,
         "half a pixel off the left edge keeps half the weight (red), not the edge texel");
    near(out[1], 0.0f, 1e-6f, "and the green follows the same two taps");

    // Fully outside is black, not a crash and not a clamp.
    echo::sampleBilinear(img, -5.0f, -5.0f, out);
    check(out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f && out[3] == 0.0f,
          "sampling well outside gives zero, not a clamped edge texel");

    // `out` is zeroed first, so stale caller data cannot leak through.
    float dirty[4] = {9.f, 9.f, 9.f, 9.f};
    echo::sampleBilinear(img, 50.0f, 50.0f, dirty);
    check(dirty[0] == 0.0f && dirty[3] == 0.0f, "a fully-missed sample clears out[]");

    // The fractional part is FLOOR-based, so a negative coordinate uses floor and not a
    // truncating cast: -0.25 floors to -1 with fx = 0.75.
    echo::sampleBilinear(img, -0.25f, 0.0f, out);
    near(out[0], 0.75f * 1.0f, 1e-6f,
         "x = -0.25 floors to -1 with fx = 0.75 (a truncating cast would give 0)");
  }

  std::printf("echo::transformBilinear\n");
  {
    // Identity: scale 1, and an angle that rounds to no rotation.
    echo::ImageF src = makeImage(4, 4, uniformImage(4, 4, 0.2f, 0.4f, 0.6f, 1.0f));
    echo::ImageF dst;
    echo::transformBilinear(src, dst, 1.0f, 0.0f);
    check(dst.width == 4 && dst.height == 4, "the destination is resized to the source");
    check(dst.pixels.size() == 4u * 4u * 4u, "and holds width*height*4 floats");
    near(dst.pixels[0], 0.2f, 1e-5f, "a uniform image survives an identity transform (red)");
    near(dst.pixels[3], 1.0f, 1e-5f, "and its alpha");

    // A UNIFORM image is the right probe for the transform: bilinear sampling of a
    // constant field must reproduce the field wherever the sample lands inside it, so any
    // error in the centre, the rotation or the scale shows up as a change in value.
    echo::transformBilinear(src, dst, 0.5f, 0.0f);
    near(dst.pixels[(2 * 4 + 2) * 4 + 0], 0.2f, 1e-5f,
         "a uniform image stays uniform under a 0.5 scale (the centre maps to itself)");

    // A rotation about the centre leaves the centre fixed for a uniform image, and the
    // corners become edge-darkened because samples fall outside and are not renormalised.
    echo::transformBilinear(src, dst, 1.0f, 45.0f);
    near(dst.pixels[(2 * 4 + 2) * 4 + 0], 0.2f, 1e-5f, "45 degrees leaves the centre fixed");
    check(dst.pixels[0] < 0.2f - 1e-4f,
          "a 45-degree rotation darkens the corner: out-of-range taps contribute ZERO");

    // A non-positive scale is not an error and not a copy: dst comes back BLACK.
    echo::transformBilinear(src, dst, 0.0f, 30.0f);
    bool black = true;
    for (float v : dst.pixels) if (v != 0.0f) black = false;
    check(black, "scale 0 returns a zeroed destination rather than a copy");
    echo::transformBilinear(src, dst, -1.0f, 30.0f);
    black = true;
    for (float v : dst.pixels) if (v != 0.0f) black = false;
    check(black, "and so does a negative scale");

    // The buffer is RESIZED when the dimensions differ, not merely reused — this is the
    // behaviour that proved ImageF holds a resizeable container.
    echo::ImageF small = makeImage(2, 2, uniformImage(2, 2, 1.0f, 1.0f, 1.0f, 1.0f));
    echo::ImageF grow;
    echo::transformBilinear(small, grow, 1.0f, 0.0f);
    check(grow.width == 2 && grow.height == 2 && grow.pixels.size() == 16,
          "a fresh destination is sized to the source");
    echo::ImageF wrong = makeImage(3, 3, uniformImage(3, 3, 1.0f, 1.0f, 1.0f, 1.0f));
    echo::transformBilinear(small, wrong, 1.0f, 0.0f);
    check(wrong.width == 2 && wrong.height == 2 && wrong.pixels.size() == 16,
          "a destination of the wrong SIZE is resized to the source");

    // But a buffer whose dimensions ALREADY MATCH is left at whatever size it has: only
    // `std::fill` runs, and it fills the buffer rather than resizing it. So a
    // size-inconsistent ImageF stays inconsistent. That is the recovered behaviour, not a
    // wish — it is what "the resize is guarded on the dimensions" means.
    echo::ImageF skewed = makeImage(2, 2, uniformImage(8, 8, 1.0f, 1.0f, 1.0f, 1.0f));
    echo::transformBilinear(small, skewed, 1.0f, 0.0f);
    check(skewed.pixels.size() == 8u * 8u * 4u,
          "matching dimensions skip the resize, so an oversized buffer is only ZEROED");

    // A zero-area source is handled rather than looping.
    echo::ImageF empty;
    echo::transformBilinear(echo::ImageF{}, empty, 1.0f, 10.0f);
    check(empty.pixels.empty(), "a zero-area source produces a zero-area destination");
  }

  std::printf("\n%s\n", failures == 0
                ? "ALL RECOVERED ECHO MATHS VERIFIED"
                : "TRANSCRIPTION ERRORS PRESENT");
  return failures == 0 ? 0 : 1;
}
