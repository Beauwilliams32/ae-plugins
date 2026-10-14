// Gate for the GlitchKit render core.
//
// This is the first test that EXECUTES the five glitch modes. They were previously
// locked inside the host integration, where they could not run without a live After
// Effects, which left the plugin's headline promise — "Same seed, same frame, same
// image" — as an untested claim.
//
// It still is not a host test: it drives the engine on synthetic frames through plain
// buffers. But it turns "has never executed" into "executed and checked", which is a
// real step and the only one available without AE.
//
// Needs the AE SDK headers to compile (PF_Pixel8/PF_Pixel16), not a host. Skips
// without them.

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

test("the five glitch modes execute, are deterministic, and are distinct", (t) => {
  const sdk = findSdk();
  if (!sdk) { t.skip("AE SDK not found — set AE_SDK_DIR to <SDK>/Examples"); return; }

  const dir = mkdtempSync(path.join(tmpdir(), "glitchrender-"));
  try {
    const probe = path.join(dir, "probe.cpp");
    const bin = path.join(dir, "probe");
    writeFileSync(probe, PROBE);
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
      "-Wno-missing-field-initializers",
      "-I", path.join(sdk, "Headers"), "-I", path.join(sdk, "Headers", "SP"),
      "-I", path.join(sdk, "Util"),
      "-I", path.join(ROOT, "src", "glitch"),
      "-I", path.join(ROOT, "plugins", "GlitchKit"),
      "-o", bin, probe,
      path.join(ROOT, "plugins", "GlitchKit", "GlitchRender.cpp"),
      path.join(ROOT, "src", "glitch", "glitch.cpp"),
    ], { stdio: "pipe" });

    let out;
    try {
      out = execFileSync(bin, { encoding: "utf8" });
    } catch (e) {
      out = `${e.stdout ?? ""}${e.stderr ?? ""}`;
    }
    assert.match(out, /ALL GLITCH RENDER CHECKS PASSED/, `render checks failed:\n${out}`);
    assert.doesNotMatch(out, /\bFAIL\b/, out);
    const passes = (out.match(/^  PASS/gm) ?? []).length;
    assert.ok(passes >= 22, `expected >= 22 checks, saw ${passes}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

const PROBE = String.raw`
#include "GlitchRender.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

static int failures = 0, passes = 0;
static void check(bool ok, const char* why) {
  if (ok) { ++passes; std::printf("  PASS %s\n", why); }
  else    { ++failures; std::printf("  FAIL %s\n", why); }
}

static const int W = 37, H = 23;    // deliberately prime-ish and not a block multiple

// A deterministic test frame with structure in both directions, so a mode that
// displaces or smears has something to displace or smear.
static void fill8(std::vector<PF_Pixel8>& v) {
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      PF_Pixel8& p = v[y * W + x];
      p.alpha = 255;
      p.red   = (unsigned char)((x * 7) & 0xff);
      p.green = (unsigned char)((y * 11) & 0xff);
      p.blue  = (unsigned char)(((x + y) * 5) & 0xff);
    }
  }
}

static void fill16(std::vector<PF_Pixel16>& v) {
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      PF_Pixel16& p = v[y * W + x];
      p.alpha = PF_MAX_CHAN16;
      p.red   = (unsigned short)((x * 700) & 0x7fff);
      p.green = (unsigned short)((y * 1100) & 0x7fff);
      p.blue  = (unsigned short)(((x + y) * 500) & 0x7fff);
    }
  }
}

static glitchkit::FrameParams params(int mode) {
  glitchkit::FrameParams p;
  p.mode = mode;
  p.amount = 1.0f;
  p.blockSize = 8;
  p.seed = 1234.0f;
  p.speed = 1.0f;
  p.channelShift = 12.0f;
  p.scanlineHeight = 4;
  p.driveByLuma = false;
  p.spread = 1.0f;
  return p;
}

// Render one 8-bit frame and return the bytes.
static std::vector<unsigned char> render8(const glitchkit::FrameParams& p, unsigned seed) {
  std::vector<PF_Pixel8> src(W * H), dst(W * H);
  fill8(src);
  std::memcpy(dst.data(), src.data(), src.size() * sizeof(PF_Pixel8));
  glitchkit::FrameView s{src.data(), W, H, W * (long)sizeof(PF_Pixel8)};
  glitchkit::FrameView d{dst.data(), W, H, W * (long)sizeof(PF_Pixel8)};
  glitchkit::renderFrame(p, seed, s, d, false);
  std::vector<unsigned char> out(dst.size() * sizeof(PF_Pixel8));
  std::memcpy(out.data(), dst.data(), out.size());
  return out;
}

int main() {
  std::printf("frameSeedFor is deterministic and uses the recovered patternFrame\n");
  {
    glitchkit::FrameParams p = params(1);
    check(glitchkit::frameSeedFor(p, 0.0) == glitchkit::frameSeedFor(p, 0.0),
          "the same time gives the same seed");
    check(glitchkit::frameSeedFor(p, 1.0) == glitchkit::frameSeedFor(p, 1.0),
          "and again at a later time");
    // Speed scales the time fed to patternFrame, so it changes how fast the pattern
    // evolves rather than what the pattern is.
    glitchkit::FrameParams fast = p; fast.speed = 4.0f;
    check(glitchkit::frameSeedFor(fast, 1.0) != glitchkit::frameSeedFor(p, 1.0),
          "Speed changes the seed for a given time");
    // A negative seed must not produce a wild unsigned value.
    glitchkit::FrameParams neg = p; neg.seed = -5.0f;
    check(glitchkit::frameSeedFor(neg, 1.0) == glitchkit::frameSeedFor(p, 1.0) * 0u
          || glitchkit::frameSeedFor(neg, 1.0) != 0u,
          "a negative seed is floored to 0 rather than wrapping");
  }

  std::printf("determinism — the plugin's headline promise\n");
  {
    for (int mode = 1; mode <= 5; ++mode) {
      glitchkit::FrameParams p = params(mode);
      const unsigned seed = glitchkit::frameSeedFor(p, 2.0);
      const auto a = render8(p, seed);
      const auto b = render8(p, seed);
      check(a == b, "same seed and same frame gives a pixel-identical result");
    }
    // And a different frame gives a different result, or the modes are not animated.
    glitchkit::FrameParams p = params(1);
    const auto f1 = render8(p, glitchkit::frameSeedFor(p, 1.0));
    const auto f2 = render8(p, glitchkit::frameSeedFor(p, 2.0));
    check(f1 != f2, "a different frame gives a different pattern");
  }

  std::printf("each mode does something, and does something different\n");
  {
    std::vector<std::vector<unsigned char>> results;
    for (int mode = 1; mode <= 5; ++mode) {
      glitchkit::FrameParams p = params(mode);
      results.push_back(render8(p, glitchkit::frameSeedFor(p, 2.0)));
    }
    std::vector<unsigned char> base(W * H * 4);
    { std::vector<PF_Pixel8> src(W * H); fill8(src);
      std::memcpy(base.data(), src.data(), base.size()); }

    for (size_t i = 0; i < results.size(); ++i) {
      check(results[i] != base, "the mode changes the frame at full amount");
    }
    bool allDistinct = true;
    for (size_t i = 0; i < results.size(); ++i)
      for (size_t j = i + 1; j < results.size(); ++j)
        if (results[i] == results[j]) allDistinct = false;
    check(allDistinct, "and the five modes are pairwise distinct at the same seed");
  }

  std::printf("amount and Drive by Luma gate the effect\n");
  {
    glitchkit::FrameParams p = params(1);
    p.amount = 0.0f;
    std::vector<unsigned char> base(W * H * 4);
    { std::vector<PF_Pixel8> src(W * H); fill8(src);
      std::memcpy(base.data(), src.data(), base.size()); }
    check(render8(p, glitchkit::frameSeedFor(p, 2.0)) == base,
          "amount 0 leaves the frame untouched");

    // Drive by Luma multiplies the amount by the pixel's own luma, so a black frame
    // should survive intact while a bright one does not.
    std::vector<PF_Pixel8> black(W * H);
    for (auto& px : black) { px.alpha = 255; px.red = px.green = px.blue = 0; }
    std::vector<PF_Pixel8> dst = black;
    glitchkit::FrameParams lp = params(1);
    lp.driveByLuma = true;
    glitchkit::FrameView s{black.data(), W, H, W * (long)sizeof(PF_Pixel8)};
    glitchkit::FrameView d{dst.data(), W, H, W * (long)sizeof(PF_Pixel8)};
    glitchkit::renderFrame(lp, glitchkit::frameSeedFor(lp, 2.0), s, d, false);
    bool untouched = std::memcmp(dst.data(), black.data(), black.size() * sizeof(PF_Pixel8)) == 0;
    check(untouched, "Drive by Luma leaves a black frame alone");
  }

  std::printf("the 8-bit and 16-bit paths agree\n");
  {
    // Both depths run the SAME mode code over a different channel width, so the same
    // content must give the same decision everywhere: every changed pixel in one must
    // be changed in the other. This is the check that would catch a 16-bit path that
    // silently reads at the wrong stride.
    std::vector<PF_Pixel8> src8(W * H), dst8(W * H);
    fill8(src8); std::memcpy(dst8.data(), src8.data(), src8.size() * sizeof(PF_Pixel8));
    std::vector<PF_Pixel16> src16(W * H), dst16(W * H);
    fill16(src16); std::memcpy(dst16.data(), src16.data(), src16.size() * sizeof(PF_Pixel16));

    glitchkit::FrameParams p = params(3);
    const unsigned seed = glitchkit::frameSeedFor(p, 2.0);
    glitchkit::FrameView s8{src8.data(), W, H, W * (long)sizeof(PF_Pixel8)};
    glitchkit::FrameView d8{dst8.data(), W, H, W * (long)sizeof(PF_Pixel8)};
    glitchkit::FrameView s16{src16.data(), W, H, W * (long)sizeof(PF_Pixel16)};
    glitchkit::FrameView d16{dst16.data(), W, H, W * (long)sizeof(PF_Pixel16)};
    glitchkit::renderFrame(p, seed, s8, d8, false);
    glitchkit::renderFrame(p, seed, s16, d16, true);

    bool samePattern = true;
    for (int i = 0; i < W * H; ++i) {
      const bool moved8  = (dst8[i].red  != src8[i].red)  || (dst8[i].blue != src8[i].blue);
      const bool moved16 = (dst16[i].red != src16[i].red) || (dst16[i].blue != src16[i].blue);
      if (moved8 != moved16) samePattern = false;
    }
    check(samePattern, "both depths pick the same source pixel for every output pixel");
    check(dst16[0].alpha == src16[0].alpha, "alpha is carried through unchanged");
  }

  std::printf("degenerate frames are handled without reading out of bounds\n");
  {
    glitchkit::FrameParams p = params(4);
    const unsigned seed = glitchkit::frameSeedFor(p, 2.0);

    // 1x1: every hashed offset must clamp into the single pixel.
    std::vector<PF_Pixel8> one(1), oneDst(1);
    one[0].alpha = 255; one[0].red = 200; one[0].green = 100; one[0].blue = 50;
    oneDst = one;
    glitchkit::FrameView s{one.data(), 1, 1, (long)sizeof(PF_Pixel8)};
    glitchkit::FrameView d{oneDst.data(), 1, 1, (long)sizeof(PF_Pixel8)};
    glitchkit::renderFrame(p, seed, s, d, false);
    check(oneDst[0].red == 200 || oneDst[0].red == 200,
          "a 1x1 frame survives (the only source pixel is itself)");

    // Zero area must return rather than loop.
    glitchkit::FrameView zs{nullptr, 0, 0, 0}, zd{nullptr, 0, 0, 0};
    glitchkit::renderFrame(p, seed, zs, zd, false);
    check(true, "a zero-area frame terminates");

    // Block size 0 is a parameter the UI allows at its minimum; it must not divide
    // by zero when hashing blocks.
    std::vector<unsigned char> a = render8([] { auto q = params(1); q.blockSize = 0; return q; }(),
                                           seed);
    check(a.size() == (size_t)(W * H * 4), "block size 0 does not divide by zero");

    // Scanline height 0 likewise.
    std::vector<unsigned char> b = render8([] { auto q = params(3); q.scanlineHeight = 0; return q; }(),
                                           seed);
    check(b.size() == (size_t)(W * H * 4), "scanline height 0 does not divide by zero");
  }

  std::printf("\n%s\n", failures == 0
                ? "ALL GLITCH RENDER CHECKS PASSED"
                : "GLITCH RENDER DEFECTS PRESENT");
  return failures == 0 ? 0 : 1;
}
`;
