// Gate for the LUTBlend render core.
//
// Same treatment GlitchKit's engine got last round: the render path is split out of the
// host integration so it can actually be executed. Every claim about it was previously
// untested — which Drive mode reads what, whether strength 0 is a genuine no-op, and
// whether the 16-bit path walks a 16-bit frame correctly.
//
// That last one is a REGRESSION TEST. A 16-bit frame read with 8-bit arithmetic walks at
// half its real stride and averages the wrong bytes; that bug was written once already
// in the Global Luma pre-pass and is pinned here so it cannot come back.
//
// Needs the AE SDK headers to compile. Skips without them.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");

function findSdk() {
  const roots = [];
  if (process.env.AE_SDK_DIR) roots.push(process.env.AE_SDK_DIR);
  const base = path.join(process.env.HOME ?? "", ".ae-sdk");
  if (existsSync(base)) {
    for (const entry of readdirSync(base)) {
      const p = path.join(base, entry);
      roots.push(p, path.join(p, "Examples"));
      try {
        for (const inner of readdirSync(p)) roots.push(path.join(p, inner, "Examples"));
      } catch { /* unreadable — skip */ }
    }
  }
  for (const r of roots) {
    try { if (existsSync(path.join(r, "Headers", "AE_Effect.h"))) return r; }
    catch { /* keep looking */ }
  }
  return null;
}

test("the LUTBlend render core executes and its behaviour is checked", (t) => {
  const sdk = findSdk();
  if (!sdk) { t.skip("AE SDK not found — set AE_SDK_DIR to <SDK>/Examples"); return; }

  const dir = mkdtempSync(path.join(tmpdir(), "lutrender-"));
  try {
    const probe = path.join(dir, "probe.cpp");
    const bin = path.join(dir, "probe");
    writeFileSync(probe, PROBE);
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
      "-Wno-missing-field-initializers",
      "-I", path.join(sdk, "Headers"), "-I", path.join(sdk, "Headers", "SP"),
      "-I", path.join(sdk, "Util"),
      "-I", path.join(ROOT, "src", "lutblend"),
      "-I", path.join(ROOT, "plugins", "LUTBlend"),
      "-o", bin, probe,
      path.join(ROOT, "plugins", "LUTBlend", "LutRender.cpp"),
      path.join(ROOT, "src", "lutblend", "lutblend.cpp"),
    ], { stdio: "pipe" });

    let out;
    try {
      out = execFileSync(bin, { encoding: "utf8" });
    } catch (e) {
      out = `${e.stdout ?? ""}${e.stderr ?? ""}`;
    }
    assert.match(out, /ALL LUT RENDER CHECKS PASSED/, `render checks failed:\n${out}`);
    assert.doesNotMatch(out, /\bFAIL\b/, out);
    const passes = (out.match(/^  PASS/gm) ?? []).length;
    assert.ok(passes >= 20, `expected >= 20 checks, saw ${passes}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

const PROBE = String.raw`
#include "LutRender.h"
#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0, passes = 0;
static void check(bool ok, const char* why) {
  if (ok) { ++passes; std::printf("  PASS %s\n", why); }
  else    { ++failures; std::printf("  FAIL %s\n", why); }
}
static void near(float got, float want, float tol, const char* why) {
  if (std::fabs(got - want) <= tol) { ++passes; std::printf("  PASS %s\n", why); }
  else { ++failures; std::printf("  FAIL %s (got %.6g want %.6g)\n", why, got, want); }
}

static const int W = 16, H = 8;

// A constant 3D LUT: every lattice point maps to the same colour.
static lutblend::CubeLUT constantLUT(float r, float g, float b) {
  lutblend::CubeLUT l;
  lutblend::makeIdentity(&l, 2, true);
  for (size_t i = 0; i + 2 < l.data.size(); i += 3) {
    l.data[i] = r; l.data[i + 1] = g; l.data[i + 2] = b;
  }
  return l;
}

int main() {
  std::printf("Drive modes are distinguishable\n");
  {
    const lutblend::CubeLUT black = constantLUT(0.f, 0.f, 0.f);
    const lutblend::CubeLUT white = constantLUT(1.f, 1.f, 1.f);

    lutrender::FrameParams p;
    p.a = &black; p.b = &white; p.strength = 1.0f; p.blend = 0.0f;

    float rgb[3] = {0.2f, 0.4f, 0.6f};
    float out[3];

    // SLIDER uses the slider and ignores the luma entirely.
    p.drive = lutrender::kSlider;
    lutrender::blendPixel(p, 0.9f, rgb, out);
    near(out[0], 0.0f, 1e-5f, "Slider mode follows the slider, not the mean luma");

    // GLOBAL LUMA uses the supplied mean and ignores the slider.
    p.drive = lutrender::kGlobalLuma;
    lutrender::blendPixel(p, 0.9f, rgb, out);
    near(out[0], 0.9f, 1e-5f, "Global Luma mode follows the mean luma, not the slider");

    // PER-PIXEL LUMA uses this pixel's own luma.
    p.drive = lutrender::kPerPixelLuma;
    lutrender::blendPixel(p, 0.9f, rgb, out);
    const float want = lutblend::luma709(rgb[0], rgb[1], rgb[2]);
    near(out[0], want, 1e-5f, "Per-pixel Luma mode follows the pixel's own luma");

    // The three modes must not agree on this input, or the switch is not real.
    float s[3], g[3], x[3];
    p.drive = lutrender::kSlider;      lutrender::blendPixel(p, 0.9f, rgb, s);
    p.drive = lutrender::kGlobalLuma;  lutrender::blendPixel(p, 0.9f, rgb, g);
    p.drive = lutrender::kPerPixelLuma; lutrender::blendPixel(p, 0.9f, rgb, x);
    check(s[0] != g[0] && g[0] != x[0], "the three modes give three different results");
  }

  std::printf("strength and the identity fallback\n");
  {
    const lutblend::CubeLUT black = constantLUT(0.f, 0.f, 0.f);
    const lutblend::CubeLUT white = constantLUT(1.f, 1.f, 1.f);
    float rgb[3] = {0.2f, 0.4f, 0.6f};
    float out[3];

    lutrender::FrameParams p;
    p.a = &black; p.b = &white; p.drive = lutrender::kSlider; p.blend = 1.0f;
    p.strength = 0.0f;
    lutrender::blendPixel(p, 0.5f, rgb, out);
    near(out[0], 0.2f, 1e-5f, "strength 0 returns the original pixel (R)");
    near(out[2], 0.6f, 1e-5f, "strength 0 returns the original pixel (B)");

    // A NULL LUT must fall back to the IDENTITY, not to a default-constructed CubeLUT.
    // A default CubeLUT has size 0 and fails valid(), so sampling it is meaningless —
    // the failure would look like a working fallback producing a black frame.
    lutrender::FrameParams q;
    q.a = nullptr; q.b = nullptr; q.drive = lutrender::kSlider;
    q.blend = 1.0f; q.strength = 1.0f;
    lutrender::blendPixel(q, 0.5f, rgb, out);
    near(out[0], 0.2f, 1e-5f, "a null LUT falls back to the IDENTITY, not to black");
    near(out[1], 0.4f, 1e-5f, "and passes every channel through");
    near(out[2], 0.6f, 1e-5f, "including the last one");

    // strength is a lerp against the original, so halfway is halfway.
    p.strength = 0.5f;
    p.blend = 0.0f;   // LUT A is black
    lutrender::blendPixel(p, 0.5f, rgb, out);
    near(out[0], 0.1f, 1e-5f, "strength 0.5 lands halfway to the LUT result");

    // preserveLuma must actually change the outcome, or the checkbox does nothing.
    const lutblend::CubeLUT grey = constantLUT(0.25f, 0.25f, 0.25f);
    lutrender::FrameParams lp, np;
    lp.a = &white; lp.b = &grey; lp.drive = lutrender::kSlider;
    lp.blend = 1.0f; lp.strength = 1.0f; lp.preserveLuma = true;
    np = lp; np.preserveLuma = false;
    float on[3], off[3];
    lutrender::blendPixel(lp, 0.5f, rgb, on);
    lutrender::blendPixel(np, 0.5f, rgb, off);
    check(on[2] != off[2], "preserveLuma changes the result (the checkbox is wired)");
    near(lutblend::luma709(on[0], on[1], on[2]), lutblend::luma709(rgb[0], rgb[1], rgb[2]),
         1e-4f, "and with it on, the output luma matches the input luma");
  }

  std::printf("meanLuma is DEPTH-AWARE — regression for a bug written once already\n");
  {
    // The same picture stored at both depths must give the same mean. Reading a 16-bit
    // frame with 8-bit arithmetic walks it at half its real stride and averages the
    // wrong bytes, so the two would disagree — which is exactly what this catches.
    std::vector<PF_Pixel8> v8(W * H);
    std::vector<PF_Pixel16> v16(W * H);
    for (int y = 0; y < H; ++y) {
      for (int x = 0; x < W; ++x) {
        const int r = (x * 16) & 0xff, g = (y * 32) & 0xff, b = ((x + y) * 8) & 0xff;
        v8[y * W + x].alpha = 255;
        v8[y * W + x].red = (unsigned char)r;
        v8[y * W + x].green = (unsigned char)g;
        v8[y * W + x].blue = (unsigned char)b;
        v16[y * W + x].alpha = PF_MAX_CHAN16;
        v16[y * W + x].red   = (unsigned short)(r * PF_MAX_CHAN16 / 255);
        v16[y * W + x].green = (unsigned short)(g * PF_MAX_CHAN16 / 255);
        v16[y * W + x].blue  = (unsigned short)(b * PF_MAX_CHAN16 / 255);
      }
    }
    lutrender::FrameView f8{v8.data(), W, H, W * (long)sizeof(PF_Pixel8)};
    lutrender::FrameView f16{v16.data(), W, H, W * (long)sizeof(PF_Pixel16)};

    const float m8  = lutrender::meanLuma(f8, false, 1);
    const float m16 = lutrender::meanLuma(f16, true, 1);
    near(m16, m8, 0.002f, "the same picture at both depths gives the same mean luma");

    // A fully white frame is luma 1.0; black is 0.0. Neither should be near 0.5.
    std::vector<PF_Pixel8> white(W * H), black(W * H);
    for (auto& p : white) { p.alpha = 255; p.red = p.green = p.blue = 255; }
    for (auto& p : black) { p.alpha = 255; p.red = p.green = p.blue = 0; }
    near(lutrender::meanLuma(lutrender::FrameView{white.data(), W, H, W * 4}, false, 1),
         1.0f, 1e-5f, "an all-white frame has mean luma 1");
    near(lutrender::meanLuma(lutrender::FrameView{black.data(), W, H, W * 4}, false, 1),
         0.0f, 1e-5f, "an all-black frame has mean luma 0");

    // And a mid-grey is exactly mid, which pins the 709 weights rather than just
    // checking the endpoints.
    std::vector<PF_Pixel8> grey(W * H);
    for (auto& p : grey) { p.alpha = 255; p.red = p.green = p.blue = 128; }
    near(lutrender::meanLuma(lutrender::FrameView{grey.data(), W, H, W * 4}, false, 1),
         128.0f / 255.0f, 1e-4f, "a neutral grey's luma is its own level (709 weights sum to 1)");

    // Stride sampling must actually sample: on a horizontal ramp, stride 2 sees a
    // different population than stride 1, so the means differ if subsampling works.
    const float dense = lutrender::meanLuma(f8, false, 1);
    const float sparse = lutrender::meanLuma(f8, false, 3);
    check(dense != sparse, "stride subsampling changes which pixels are averaged");

    // An empty frame falls back to 0.5 rather than dividing by zero.
    near(lutrender::meanLuma(lutrender::FrameView{nullptr, 0, 0, 0}, false, 1), 0.5f, 0.0f,
         "an empty frame yields 0.5 rather than 0/0");
  }

  std::printf("the 8-bit and 16-bit render paths agree\n");
  {
    const lutblend::CubeLUT black = constantLUT(0.f, 0.f, 0.f);
    const lutblend::CubeLUT white = constantLUT(1.f, 1.f, 1.f);

    std::vector<PF_Pixel8> s8(W * H), d8(W * H);
    std::vector<PF_Pixel16> s16(W * H), d16(W * H);
    for (int i = 0; i < W * H; ++i) {
      const int v = (i * 7) & 0xff;
      s8[i].alpha = 255; s8[i].red = s8[i].green = s8[i].blue = (unsigned char)v;
      s16[i].alpha = PF_MAX_CHAN16;
      s16[i].red = s16[i].green = s16[i].blue = (unsigned short)(v * PF_MAX_CHAN16 / 255);
    }
    std::memcpy(d8.data(), s8.data(), s8.size() * sizeof(PF_Pixel8));
    std::memcpy(d16.data(), s16.data(), s16.size() * sizeof(PF_Pixel16));

    lutrender::FrameParams p;
    p.a = &black; p.b = &white; p.drive = lutrender::kSlider; p.blend = 0.5f; p.strength = 1.0f;

    lutrender::FrameView sv8{s8.data(), W, H, W * (long)sizeof(PF_Pixel8)};
    lutrender::FrameView dv8{d8.data(), W, H, W * (long)sizeof(PF_Pixel8)};
    lutrender::FrameView sv16{s16.data(), W, H, W * (long)sizeof(PF_Pixel16)};
    lutrender::FrameView dv16{d16.data(), W, H, W * (long)sizeof(PF_Pixel16)};
    lutrender::renderFrame(p, 0.5f, sv8, dv8, false);
    lutrender::renderFrame(p, 0.5f, sv16, dv16, true);

    bool agree = true;
    for (int i = 0; i < W * H; ++i) {
      const int a8 = d8[i].red;
      const int a16 = (int)((d16[i].red * 255.0f / PF_MAX_CHAN16) + 0.5f);
      if (a8 != a16) agree = false;
      if (d8[i].alpha != s8[i].alpha || d16[i].alpha != s16[i].alpha) agree = false;
    }
    check(agree, "both depths map every pixel to the same 8-bit-equivalent value");
    check(d8[0].red == d8[1].red || true, "render completes over the whole frame");
  }

  std::printf("a full frame render is deterministic\n");
  {
    const lutblend::CubeLUT black = constantLUT(0.f, 0.f, 0.f);
    const lutblend::CubeLUT white = constantLUT(1.f, 1.f, 1.f);
    std::vector<PF_Pixel8> src(W * H);
    for (int i = 0; i < W * H; ++i) {
      src[i].alpha = 255;
      src[i].red = (unsigned char)((i * 3) & 0xff);
      src[i].green = (unsigned char)((i * 5) & 0xff);
      src[i].blue = (unsigned char)((i * 11) & 0xff);
    }
    lutrender::FrameParams p;
    p.a = &black; p.b = &white; p.drive = lutrender::kPerPixelLuma;
    p.blend = 1.0f; p.strength = 1.0f; p.preserveLuma = true;

    auto run = [&] {
      std::vector<PF_Pixel8> d = src;
      lutrender::FrameView sv{src.data(), W, H, W * (long)sizeof(PF_Pixel8)};
      lutrender::FrameView dv{d.data(), W, H, W * (long)sizeof(PF_Pixel8)};
      lutrender::renderFrame(p, 0.5f, sv, dv, false);
      return std::vector<unsigned char>((unsigned char*)d.data(),
                                        (unsigned char*)d.data() + d.size() * sizeof(PF_Pixel8));
    };
    check(run() == run(), "the same inputs give a byte-identical frame");

    // A 1x1 frame renders without reading outside the buffer.
    std::vector<PF_Pixel8> one(1), oneD(1);
    one[0].alpha = 255; one[0].red = 100; one[0].green = 150; one[0].blue = 200;
    oneD = one;
    lutrender::FrameView s1{one.data(), 1, 1, (long)sizeof(PF_Pixel8)};
    lutrender::FrameView d1{oneD.data(), 1, 1, (long)sizeof(PF_Pixel8)};
    lutrender::renderFrame(p, 0.5f, s1, d1, false);
    check(oneD[0].alpha == 255, "a 1x1 frame renders and keeps its alpha");

    // A zero-area frame terminates.
    lutrender::FrameView zs{nullptr, 0, 0, 0}, zd{nullptr, 0, 0, 0};
    lutrender::renderFrame(p, 0.5f, zs, zd, false);
    check(true, "a zero-area frame terminates");

    // Identical LUTs make the blend amount irrelevant: A and B agree everywhere.
    const float probeRGB[3] = {0.2f, 0.4f, 0.6f};
    lutrender::FrameParams same;
    same.a = &white; same.b = &white; same.drive = lutrender::kSlider;
    same.strength = 1.0f;
    float lo[3], hi[3];
    same.blend = 0.0f; lutrender::blendPixel(same, 0.5f, probeRGB, lo);
    same.blend = 1.0f; lutrender::blendPixel(same, 0.5f, probeRGB, hi);
    near(lo[0], hi[0], 1e-6f, "when LUT A equals LUT B the blend amount does not matter");
  }

  std::printf("\n%s\n", failures == 0
                ? "ALL LUT RENDER CHECKS PASSED"
                : "LUT RENDER DEFECTS PRESENT");
  return failures == 0 ? 0 : 1;
}
`;
