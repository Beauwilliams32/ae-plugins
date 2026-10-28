// Gate for the recovered chromakey:: namespace.
//
// Three things are checked, and the third is the one that makes the layout claims
// in the header evidence rather than assertion: the offsets `YCbCr` is documented
// with are re-derived from the shipped bundle's own instructions, so a header that
// drifted from the binary fails here.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");
const SRC = path.join(ROOT, "src", "chromakey");
const BUNDLE = "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media"
  + "/ChromaKeyAssist.plugin/Contents/MacOS/ChromaKeyAssist";

/**
 * The functions transcribed here.
 *
 * `spillScore` and `uniformityScore` joined on 2026-10-27; `renderView` and
 * `analyzeFrame` on 2026-10-28 (AE-05).
 *
 * `renderView` and `analyzeFrame` are templates, so the differ sees one symbol
 * per instantiation and names each twice. They are listed twice here on purpose:
 * the differ's recovered set is compared with multiplicity, so losing one
 * instantiation would fail the comparison rather than pass it.
 */
const RECOVERED = [
  "clampf", "rgbToYCbCr", "sensitivityToThreshold", "chromaDistance", "hueAngle",
  "angularDiff", "percentile", "percentileUnsorted", "gradientMagnitude", "spillScore",
  "uniformityScore", "renderView", "renderView",
  "analyzeFrame", "analyzeFrame",
];

/**
 * The functions still unread, as the differ names them.
 *
 * Nothing: every `chromakey::` symbol in the bundle is transcribed. The
 * `StoreReadout(PF_InData*, const Stats&)` host integration is deliberately NOT
 * in `src/` — it takes an Adobe SDK type — and it is not a `chromakey::`
 * symbol the differ tracks, so it does not appear here either.
 */
const OUTSTANDING = [];

function bundlesInstalled() {
  return existsSync(BUNDLE);
}

function runDiff(fragment) {
  const args = [path.join(ROOT, "tools", "codegen-diff.mjs"), "chromakey"];
  if (fragment !== undefined) args.push(fragment);
  try {
    return execFileSync("node", args, { cwd: ROOT, encoding: "utf8", stdio: "pipe" });
  } catch (e) {
    return `${e.stdout ?? ""}${e.stderr ?? ""}`;
  }
}

test("recovered chromakey source compiles and its maths verifies", (t) => {
  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) { t.skip("clang++ not available — compile check skipped"); return; }

  const dir = mkdtempSync(path.join(tmpdir(), "chromakey-check-"));
  const bin = path.join(dir, "selfcheck");
  try {
    // -Werror, so an unused parameter or a missing include fails the gate rather than
    // warning quietly. The header's static_asserts on the two struct sizes are part of
    // this compile: a layout claim that stopped being true would not build.
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
      "-o", bin,
      path.join(SRC, "chromakey.cpp"),
      path.join(SRC, "selfcheck.cpp"),
    ], { stdio: "pipe" });

    const out = execFileSync(bin, { encoding: "utf8" });
    assert.match(out, /ALL RECOVERED CHROMAKEY MATHS VERIFIED/, `self-check failed:\n${out}`);
    assert.doesNotMatch(out, /\bFAIL\b/, out);
    const passes = (out.match(/^  PASS/gm) ?? []).length;
    assert.ok(passes >= 60, `expected >= 60 checks, saw ${passes}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("the YCbCr layout is re-derived from the bundle, not trusted from the header", (t) => {
  if (!bundlesInstalled()) { t.skip("ChromaKeyAssist bundle is not installed"); return; }

  const asm = execFileSync("otool", ["-tV", "-arch", "arm64", BUNDLE],
    { encoding: "utf8", maxBuffer: 256 * 1024 * 1024 });
  const lines = asm.split("\n");
  const start = lines.findIndex((l) => l.startsWith("__ZN9chromakey14chromaDistance"));
  assert.ok(start >= 0, "chromaDistance is not in the bundle");
  const body = [];
  for (const line of lines.slice(start + 1)) {
    if (line && !line.startsWith("0") && line.endsWith(":")) break;
    if (line.trim()) body.push(line);
  }
  const text = body.join("\n");

  // The header claims Y is at +0x0, Cb at +0x4, Cr at +0x8, and that Y is never read.
  // Both halves come from these two loads: they start at +0x4 and there is no load
  // from the struct's own base offset anywhere in the function.
  assert.match(text, /ldp\s+s0, s1, \[x0, #0x4\]/,
    "chromaDistance should read a.cb and a.cr as a pair starting at +0x4");
  assert.match(text, /ldp\s+s2, s3, \[x1, #0x4\]/,
    "and the same for its second argument");
  assert.doesNotMatch(text, /\[x[01]\]/,
    "chromaDistance must not read a YCbCr from its base offset — that is the Y claim");
  // And the function is a square root over the two chroma differences, with no third
  // term: one fsqrt and one fmadd, and no load of anything else.
  assert.equal((text.match(/fsqrt/g) ?? []).length, 1, "exactly one square root");
  assert.equal((text.match(/ldp|ldr/g) ?? []).length, 2, "exactly two loads, both of the chroma pair");
});

test("the recovered and outstanding sets are exactly the ones this file lists", (t) => {
  if (!bundlesInstalled()) { t.skip("ChromaKeyAssist bundle is not installed"); return; }

  const out = runDiff();
  const recovered = [...out.matchAll(/^\s{2}(?:MATCH|DIFFER)\s+chromakey::(\w+)/gm)].map((m) => m[1]).sort();
  const absent = [...out.matchAll(/^\s+absent\s+chromakey::(\S+)/gm)].map((m) => m[1]);

  assert.deepEqual(recovered, [...RECOVERED].sort(),
    "the differ recovered a different set of functions than this file claims");
  assert.equal(absent.length, OUTSTANDING.length,
    `the differ reports ${absent.length} unrecovered symbols, this file lists ${OUTSTANDING.length}`);
  for (const frag of OUTSTANDING) {
    assert.ok(absent.some((a) => a.startsWith(frag)),
      `expected an unrecovered symbol starting "${frag}" — got ${absent.join(", ")}`);
  }

  // The seven instruction-identical ones are the strong evidence, so they are named
  // rather than counted: a regression that turned one of them into a DIFFER would
  // otherwise still satisfy a tally.
  for (const fn of ["clampf", "rgbToYCbCr", "sensitivityToThreshold", "chromaDistance",
                    "hueAngle", "angularDiff", "gradientMagnitude"]) {
    assert.match(out, new RegExp(`MATCH\\s+chromakey::${fn}\\s+\\d+ instructions, identical`),
      `${fn} is no longer instruction-identical to the bundle`);
  }

  const summary = out.trim().split("\n").pop();
  assert.match(summary, /7 identical, 8 differing, 0 not implemented/,
    `the differ's tally moved: ${summary}`);
});

test("the functions that do not match are documented as such", () => {
  // A DIFFER is not a failure here, but an undocumented one would be: the notes have
  // to name every function whose instructions are not reproduced exactly, and say how
  // far off it is.
  const notes = readFileSync(path.join(SRC, "RECOVERY-NOTES.md"), "utf8");
  for (const fn of ["percentile", "percentileUnsorted", "spillScore", "uniformityScore",
                    "renderView", "analyzeFrame"]) {
    assert.match(notes, new RegExp(fn), `RECOVERY-NOTES.md does not mention ${fn}`);
  }
  assert.match(notes, /instruction-identical/i, "the notes should say which ones match exactly");
  // A behavioural recovery has to say so in the notes, in the header and in this
  // file. Anything else would let a reader assume the numbers were reproduced
  // exactly when only the OUTPUT was.
  assert.match(notes, /behavioural/i,
    "RECOVERY-NOTES.md must label the behavioural recoveries as such");
  const header = readFileSync(path.join(SRC, "chromakey.h"), "utf8");
  assert.match(header, /\[behavioural\]/,
    "chromakey.h must label the behavioural recoveries as such");
});

// ---------------------------------------------------------------------------
// The differential against the shipped bundle.
//
// The bundle is a fat Mach-O plugin. `dlopen` on it resolves a handle but finds
// NO exported symbol under it — it is a plugin, not a dylib with a published
// export table — so the functions are called at the addresses the arm64 listing
// gives them, once the bundle's LOAD BASE is known:
//
//   __ZN9chromakey17gradientMagnitudeEPKfiilii
//     body 0x2b28 .. 0x2bdc   (47 instructions)
//   __ZN9chromakey10renderViewIhEEvRKNS_4ViewIKT_EERNS1_IS2_EEfRKNS_6ParamsENS_8ViewModeEfPKfSE_
//     body 0x25bc .. 0x2b24   (347 instructions)
//   __ZN9chromakey10renderViewItEEvRKNS_4ViewIKT_EERNS1_IS2_EEfRKNS_6ParamsENS_8ViewModeEfPKfSE_
//     body 0x2be4 .. 0x3158   (350 instructions)
////   __ZN9chromakey12analyzeFrameIhEENS_5StatsERKNS_4ViewIKT_EEfRKNS_6ParamsEPfSB_
//     body 0x7d8 .. 0x1398   (752 instructions; hot path returns at 0x12f8)
//
// The load base comes from `_dyld_get_image_header` over the loaded image list,
// matched on the image path. The recovered source is compiled into the same
// harness and the two are run over the same inputs.
//
// NOTE ON THE LOADED BUNDLE: dlopen runs the bundle's initialisers. Those
// succeeded here — the harness calls the shipped code tens of thousands of times
// — but nothing here is a claim that After Effects itself would load the
// bundle. No AE host has ever loaded a rebuilt bundle.
//
// An earlier revision of this file copied the shipped function's bytes out of a
// `lipo -thin` slice into a MAP_JIT page and called that. That approach was
// fragile: it faulted (SIGBUS) depending on the harness binary's layout, and
// the fault had nothing to do with the recovered code. Calling the shipped code
// IN PLACE, in the bundle the loader mapped, is both simpler and a stronger
// measurement — it is the actual shipped instruction stream, not a copy of it.

const SHIPPED_GRADIENT = "__ZN9chromakey17gradientMagnitudeEPKfiilii";

const HARNESS_SOURCE = String.raw`
// Generated by test/chromakey.test.mjs — do not edit.
//
// Calls the SHIPPED bundle's own gradientMagnitude and renderView<T> machine
// code, and the RECOVERED chromakey:: versions, side by side over randomised
// and edge-cased inputs. Reports the mismatch counts. Exits non-zero if the two
// ever disagree.

#include <dlfcn.h>
#include <mach-o/dyld.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "chromakey.h"

// The shipped View<T> layout, kept separate from chromakey::View<T> so that a
// drift in the recovered layout cannot silently make the call ABI-compatible.
struct ShippedView8 { unsigned char* data; int width; int height; long rowBytes; };
struct ShippedView16 { unsigned short* data; int width; int height; long rowBytes; };

using Params = chromakey::Params;

static uintptr_t g_base = 0;

static void openOracle() {
  const char* kBundle =
    "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/"
    "ChromaKeyAssist.plugin/Contents/MacOS/ChromaKeyAssist";
  void* dl = dlopen(kBundle, RTLD_NOW | RTLD_LOCAL);
  if (!dl) { std::printf("dlopen failed: %s\n", dlerror()); std::exit(1); }
  const uint32_t n = _dyld_image_count();
  for (uint32_t i = 0; i < n; ++i) {
    const char* nm = _dyld_get_image_name(i);
    if (nm && std::strstr(nm, "ChromaKeyAssist")) {
      g_base = reinterpret_cast<uintptr_t>(_dyld_get_image_header(i));
    }
  }
  if (!g_base) { std::printf("image not found\n"); std::exit(1); }
}

static uint32_t rngState = 12345u;
static uint32_t rnd() {
  rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
  return rngState;
}
static float frnd(float lo, float hi) {
  return lo + (hi - lo) * static_cast<float>(rnd() % 100000u) / 100000.0f;
}

// ---------------------------------------------------------------------------
// gradientMagnitude
// ---------------------------------------------------------------------------

static long g_gradCases = 0, g_gradMismatches = 0;

static void gradientCases() {
  typedef float (*GradFn)(const float*, int, int, long, int, int);
  const GradFn shipped = reinterpret_cast<GradFn>(g_base + 0x2b28);
  uint64_t state = 0x9e3779b97f4a7c15ULL;
  auto nextRandom = [&state]() {
    state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state;
  };
  for (int trial = 0; trial < 4000; ++trial) {
    const int width = 1 + static_cast<int>(nextRandom() % 24);
    const int height = 1 + static_cast<int>(nextRandom() % 24);
    const long stride = width + static_cast<long>(nextRandom() % 9);
    // The horizontal pair is read on row y itself, and y may be one past the last
    // row when the clamp is being exercised, so the buffer carries a whole extra
    // row of slack.
    //
    // It ALSO carries slack BEFORE the first element, and that is not optional.
    // The shipped kernel clamps the COLUMN indices (xm1 = max(x-1,0),
    // xp1 = min(x+1,width-1)) but it does NOT clamp the ROW index y: the centre row
    // is addressed as field[stride*y + ...] with the raw y. When y is -1 that
    // address is a whole stride before the start of the buffer, and when x is -1
    // the vertical pair reads field[stride*ym1 + x] = field[-1]. An earlier
    // revision of this harness allocated slack only at the end, so those reads
    // landed outside the allocation and the harness died with SIGBUS on roughly
    // five per cent of runs - a fault in the ORACLE, not in the recovered code,
    // because the recovered code has the identical addressing.
    //
    // The slack is zero-filled, so both the shipped kernel and the recovered one
    // read the same well-defined values and the comparison stays deterministic.
    const std::size_t count = static_cast<std::size_t>(stride)
                            * (static_cast<std::size_t>(height) + 2) + 16;
    const std::size_t front = static_cast<std::size_t>(stride) + 2;
    std::vector<float> storage(count + front, 0.0f);
    float* const field = storage.data() + front;
    for (std::size_t i = 0; i < count; ++i) {
      field[i] = static_cast<float>(static_cast<int32_t>(nextRandom() % 2001) - 1000) / 100.0f;
    }
    const int x = static_cast<int>(nextRandom() % static_cast<uint64_t>(width + 2)) - 1;
    const int y = static_cast<int>(nextRandom() % static_cast<uint64_t>(height + 2)) - 1;

    const float a = shipped(field, width, height, stride, x, y);
    const float b = chromakey::gradientMagnitude(field, width, height, stride, x, y);
    ++g_gradCases;
    const bool same = (a == b) || (std::isnan(a) && std::isnan(b));
    if (!same) {
      ++g_gradMismatches;
      if (g_gradMismatches <= 10) {
        std::printf("GRAD MISMATCH trial=%d w=%d h=%d stride=%ld x=%d y=%d shipped=%.9g mine=%.9g\n",
                    trial, width, height, stride, x, y, a, b);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// renderView
// ---------------------------------------------------------------------------

static long g_total = 0, g_mism8 = 0, g_mism16 = 0;

static void one(int srcW, int dstW, int H, int pad8, int pad16, int mode, float mix,
                float scale, float sens, bool alphaNull, bool spillNull,
                bool structured, const float* forcedAlpha, int forcedCount) {
  const long rb8 = static_cast<long>(dstW) * 4 + pad8;
  const long rb16 = static_cast<long>(dstW) * 4 + pad16;
  const long srb8 = static_cast<long>(srcW) * 4 + pad8;
  const long srb16 = static_cast<long>(srcW) * 4 + pad16;
  Params P{frnd(0.0f, 255.0f), frnd(0.0f, 255.0f), frnd(0.0f, 255.0f), sens};

  const int W = dstW < srcW ? dstW : srcW;
  std::vector<float> alphaBuf(static_cast<std::size_t>(W > 0 ? W : 1) * (H > 0 ? H : 1));
  std::vector<float> spillBuf(alphaBuf.size());
  for (std::size_t i = 0; i < alphaBuf.size(); ++i) {
    alphaBuf[i] = structured ? static_cast<float>(i % 5) * 0.25f : frnd(0.0f, 1.0f);
    spillBuf[i] = structured ? static_cast<float>(i % 7) * 0.03f : frnd(-0.1f, 1.2f);
  }
  // The forced values are a PATTERN, not a buffer: they are repeated cyclically to
  // fill the frame. An earlier revision of this harness copied
  // forcedAlpha + alphaBuf.size() elements out of a five-element array, so the
  // last four elements of every 3x3 frame were uninitialised stack memory. That
  // made the comparison depend on whatever happened to be on the stack: the
  // harness reported a mismatch at the final pixel of the structured frames on
  // roughly one run in eighty, and the mismatch was in the ORACLE's input, not
  // in either implementation. The pattern is now expanded explicitly, so both
  // the shipped kernel and the recovered one are handed the same well-defined
  // values.
  if (forcedAlpha) {
    for (std::size_t i = 0; i < alphaBuf.size(); ++i)
      alphaBuf[i] = forcedAlpha[i % static_cast<std::size_t>(forcedCount)];
  }
  if (forcedAlpha) {
    for (std::size_t i = 0; i < spillBuf.size(); ++i)
      spillBuf[i] = forcedAlpha[i % static_cast<std::size_t>(forcedCount)];
  }
  const float* alpha = alphaNull ? nullptr : alphaBuf.data();
  const float* spill = spillNull ? nullptr : spillBuf.data();

  {
    std::vector<unsigned char> s8(static_cast<std::size_t>(srb8) * (H > 0 ? H : 1));
    std::vector<unsigned char> dA(static_cast<std::size_t>(rb8) * (H > 0 ? H : 1), 0xAA);
    std::vector<unsigned char> dB(static_cast<std::size_t>(rb8) * (H > 0 ? H : 1), 0xAA);
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < srcW; ++x) {
        unsigned char* p = &s8[static_cast<std::size_t>(y) * srb8 + x * 4];
        p[0] = static_cast<unsigned char>(rnd() & 0xff);
        p[1] = static_cast<unsigned char>(rnd() & 0xff);
        p[2] = static_cast<unsigned char>(rnd() & 0xff);
        p[3] = static_cast<unsigned char>(rnd() & 0xff);
      }
    ShippedView8 S{s8.data(), srcW, H, srb8}, DA{dA.data(), dstW, H, rb8}, DB{dB.data(), dstW, H, rb8};
    (reinterpret_cast<void (*)(const ShippedView8&, ShippedView8&, float, const Params&,
                               int, float, const float*, const float*)>(g_base + 0x25bc))
        (S, DA, scale, P, mode, mix, alpha, spill);
    chromakey::View<const unsigned char> myS8{s8.data(), srcW, H, srb8};
    chromakey::View<unsigned char> myD8{dB.data(), dstW, H, rb8};
    chromakey::renderView<unsigned char>(myS8, myD8, scale, P,
                                          static_cast<chromakey::ViewMode>(mode), mix,
                                          alpha, spill);
    ++g_total;
    if (std::memcmp(dA.data(), dB.data(), dA.size()) != 0) ++g_mism8;
  }
  {
    std::vector<unsigned short> s16(static_cast<std::size_t>(srb16) * (H > 0 ? H : 1));
    std::vector<unsigned short> dA(static_cast<std::size_t>(rb16) * (H > 0 ? H : 1), 0xAAAA);
    std::vector<unsigned short> dB(static_cast<std::size_t>(rb16) * (H > 0 ? H : 1), 0xAAAA);
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < srcW; ++x) {
        unsigned short* p = &s16[static_cast<std::size_t>(y) * srb16 + x * 4];
        p[0] = static_cast<unsigned short>(rnd() & 0xffff);
        p[1] = static_cast<unsigned short>(rnd() & 0xffff);
        p[2] = static_cast<unsigned short>(rnd() & 0xffff);
        p[3] = static_cast<unsigned short>(rnd() & 0xffff);
      }
    ShippedView16 S{s16.data(), srcW, H, srb16}, DA{dA.data(), dstW, H, rb16}, DB{dB.data(), dstW, H, rb16};
    (reinterpret_cast<void (*)(const ShippedView16&, ShippedView16&, float, const Params&,
                               int, float, const float*, const float*)>(g_base + 0x2be4))
        (S, DA, scale, P, mode, mix, alpha, spill);
    chromakey::View<const unsigned short> myS16{s16.data(), srcW, H, srb16};
    chromakey::View<unsigned short> myD16{dB.data(), dstW, H, rb16};
    chromakey::renderView<unsigned short>(myS16, myD16, scale, P,
                                          static_cast<chromakey::ViewMode>(mode), mix,
                                          alpha, spill);
    ++g_total;
    if (std::memcmp(dA.data(), dB.data(), dA.size()) != 0) ++g_mism16;
  }
}

static void renderCases() {
  for (int iter = 0; iter < 20000; ++iter) {
    const int srcW = 1 + static_cast<int>(rnd() % 7);
    const int dstW = 1 + static_cast<int>(rnd() % 7);
    const int H = 1 + static_cast<int>(rnd() % 5);
    const int pad8 = static_cast<int>(rnd() % 5);
    const int pad16 = static_cast<int>(rnd() % 5);
    const bool alphaNull = (rnd() % 4) == 0;
    const bool spillNull = (rnd() % 4) == 0;
    const int mode = static_cast<int>(rnd() % 8) - 1;   // -1..6, includes out of range
    const float mix = frnd(-2.0f, 2.0f);
    const float scale = (rnd() % 3 == 0) ? 255.0f
                      : ((rnd() % 3 == 0) ? 65535.0f : frnd(1.0f, 1000.0f));
    const float sens = frnd(-20.0f, 150.0f);
    one(srcW, dstW, H, pad8, pad16, mode, mix, scale, sens, alphaNull, spillNull,
        false, nullptr, 0);
  }

  // Structured edge cases: every mode including out of range, the mix extremes,
  // a null alpha, a null spill, mismatched source/destination widths, a padded
  // stride, a 1x1 frame, and alpha/spill values straddling the 0.02 threshold.
  const float edge[] = {0.0f, 0.02f, 0.0200001f, 0.25f, 0.5f};
  for (int mode = -1; mode <= 6; ++mode) {
    for (int m = 0; m < 5; ++m) {
      const float mix = (m == 0) ? -1.0f : (m == 1) ? 0.0f : (m == 2) ? 1.0f : (m == 3) ? 2.0f : 0.5f;
      const float sens = (m == 4) ? -5.0f : 50.0f;
      one(3, 3, 3, 0, 0, mode, mix, 255.0f, sens, false, false, true, edge, 5);
      one(1, 1, 1, 0, 0, mode, 1.0f, 65535.0f, 0.0f, true, true, true, edge, 5);
      one(4, 2, 2, 3, 1, mode, 0.25f, 1.0f, 100.0f, false, true, true, edge, 5);
    }
  }
}

// ---------------------------------------------------------------------------
// analyzeFrame<unsigned char> and analyzeFrame<unsigned short>
//
// The shipped bodies are at 0x7d8 (8-bit) and 0x19b4 (16-bit) in the bundle's
// own address space, and are called IN PLACE, through the loader's mapping of
// the bundle.
//
// Its Stats come back through a hidden pointer in x8 rather than in registers:
// five floats is not a homogeneous aggregate of at most four members, so the
// AAPCS64 returns it indirectly. The shipped body zeroes twenty bytes through
// that pointer (0x808/0x80c) and then stores uniformity at +0x0, the mean
// spill at +0x4, p5 and p95 as one eight-byte store at +0x8, and keyedFraction
// at +0x10.
//
// ShippedStats therefore carries a non-trivial destructor ON PURPOSE: that is
// what makes the harness's own call use the same indirect-return convention,
// so the result pointer really lands in x8 while the other arguments keep x0
// (the View), x1 (the Params), v0 (the scale) and x2/x3 (the two output
// buffers).
// ---------------------------------------------------------------------------

struct ShippedStats {
  float uniformity;
  float meanSpill;
  float p5;
  float p95;
  float keyedFraction;
  ShippedStats() : uniformity(0), meanSpill(0), p5(0), p95(0), keyedFraction(0) {}
  ~ShippedStats() {}
};
static_assert(sizeof(ShippedStats) == 20, "the shipped Stats is twenty bytes");

// The shipped Params layout, kept separate from chromakey::Params for the same
// reason ShippedView is: a drift in the recovered layout has to show up as a
// mismatch rather than hide behind an accidentally compatible call.
struct ShippedParams { float keyR; float keyG; float keyB; float sensitivity; };

using AF8 = ShippedStats (*)(const ShippedView8&, float, const ShippedParams&,
                             float*, float*);
using AF16 = ShippedStats (*)(const ShippedView16&, float, const ShippedParams&,
                              float*, float*);

static long g_anVectors8 = 0, g_anVectors16 = 0;
static long g_anMism8 = 0, g_anStatMism8 = 0;
static long g_anMism16 = 0, g_anStatMism16 = 0;

// 'kind' selects the pixel pattern, and it matters:
//
//   0  every pixel a random colour
//   1  every pixel grey (achromatic)
//   2  a mixture of grey and coloured pixels
//   3  every pixel the key colour
//   4  every pixel one fixed colour
//
// The grey patterns are what reach the ACHROMATIC SHORTCUT at 0xc08..0xc18.
// That shortcut fires when EITHER the pixel OR the key is achromatic — an OR,
// not an AND, because the fccmp at 0xc14 forces NZCV to 0x8 (N=1, so "less
// than") on the path where the pixel's magnitude was already below 1e-6. The
// exported 'spillScore' uses the same OR (its two early returns at 0x1490 and
// 0x14ac), so the two shipped functions AGREE here. A frame of grey pixels
// under a coloured key, and a frame of coloured pixels under a grey key, are
// the two cases that pin the OR down.
//
// 'normaliseKey' decides the UNITS of the key colour handed to the shipped
// code. The shipped code divides every PIXEL channel by the scale (0x988,
// 0x98c, 0x998) but does NOT divide the KEY (0x8ac..0x900) — normalising the
// key is the caller's job. With the key normalised the KEYED path is
// reachable at all: pixels fall inside the chroma radius, so the keyed
// fraction, the uniformity over the KEYED distances and the keyed/non-keyed
// split all carry real values. With the key left in raw units it sits far
// away from every pixel, nothing is keyed, and the mean spill and the per
// pixel scores carry the frame instead. Both are exercised.
template <typename T, typename V>
static void analyzeOne(int width, int height, int pad, float scale, float sens,
                       float keyR, float keyG, float keyB, int kind, bool nullOut,
                       bool normaliseKey, uintptr_t shippedAddr,
                       long& mismatches, long& statMismatches) {
  const unsigned kMax = (sizeof(T) == 1) ? 0xffu : 0xffffu;
  const long rb = static_cast<long>(width) * 4 + pad;
  const std::size_t total = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

  std::vector<T> s(static_cast<std::size_t>(rb) * static_cast<std::size_t>(height) + 64,
                   static_cast<T>(0));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      T* p = &s[static_cast<std::size_t>(y) * rb + x * 4];
      unsigned r, g, b;
      if (kind == 1) {
        r = g = b = rnd() & kMax;
      } else if (kind == 2) {
        r = rnd() & kMax;
        g = rnd() & kMax;
        b = rnd() & kMax;
        if (rnd() & 1) { g = r; b = r; }
      } else if (kind == 3) {
        r = static_cast<unsigned>(keyR);
        g = static_cast<unsigned>(keyG);
        b = static_cast<unsigned>(keyB);
      } else if (kind == 4) {
        r = 7; g = 9; b = 11;
      } else {
        r = rnd() & kMax;
        g = rnd() & kMax;
        b = rnd() & kMax;
      }
      p[0] = static_cast<T>(kMax);        // the alpha component is never read
      p[1] = static_cast<T>(r);
      p[2] = static_cast<T>(g);
      p[3] = static_cast<T>(b);
    }
  }

  // The destinations are poisoned, so a missing write shows up as a difference
  // rather than passing because the buffer happened to hold the right bytes.
  std::vector<float> dA(total ? total : 1), sA(total ? total : 1);
  std::vector<float> dB(total ? total : 1), sB(total ? total : 1);
  for (std::size_t i = 0; i < dA.size(); ++i) {
    dA[i] = -7.0f; sA[i] = -7.0f; dB[i] = -7.0f; sB[i] = -7.0f;
  }

  const float kR = normaliseKey ? keyR / scale : keyR;
  const float kG = normaliseKey ? keyG / scale : keyG;
  const float kB = normaliseKey ? keyB / scale : keyB;

  V S{s.data(), width, height, rb};
  ShippedParams SP{kR, kG, kB, sens};
  using AF = ShippedStats (*)(const V&, float, const ShippedParams&, float*, float*);
  ShippedStats rA = reinterpret_cast<AF>(shippedAddr)
      (S, scale, SP, nullOut ? nullptr : dA.data(), nullOut ? nullptr : sA.data());

  chromakey::View<const T> myS{s.data(), width, height, rb};
  chromakey::Params MP{kR, kG, kB, sens};
  chromakey::Stats rB = chromakey::analyzeFrame<T>
      (myS, scale, MP, nullOut ? nullptr : dB.data(), nullOut ? nullptr : sB.data());

  if (sizeof(T) == 1) ++g_anVectors8; else ++g_anVectors16;
  const bool statsSame = std::memcmp(&rA, &rB, sizeof(ShippedStats)) == 0;
  if (!statsSame) ++statMismatches;
  const bool buffersSame =
      (nullOut || total == 0) ||
      (std::memcmp(dA.data(), dB.data(), total * sizeof(float)) == 0 &&
       std::memcmp(sA.data(), sB.data(), total * sizeof(float)) == 0);
  if (!statsSame || !buffersSame) {
    ++mismatches;
    if (mismatches <= 8) {
      std::printf("AN MISMATCH width=%zu w=%d h=%d pad=%d kind=%d scale=%g sens=%g "
                  "key=(%g,%g,%g) nullOut=%d norm=%d\n",
                  sizeof(T), width, height, pad, kind, scale, sens, kR, kG, kB,
                  static_cast<int>(nullOut), static_cast<int>(normaliseKey));
      std::printf("   shipped: u=%g m=%g p5=%g p95=%g kf=%g\n",
                  rA.uniformity, rA.meanSpill, rA.p5, rA.p95, rA.keyedFraction);
      std::printf("   mine   : u=%g m=%g p5=%g p95=%g kf=%g\n",
                  rB.uniformity, rB.meanSpill, rB.p5, rB.p95, rB.keyedFraction);
      for (std::size_t i = 0; i < total && i < 4; ++i) {
        std::printf("   px%zu: dist %g/%g  score %g/%g\n", i, dA[i], dB[i], sA[i], sB[i]);
      }
    }
  }
}

// One case, run at BOTH widths.
//
// The 16-bit instantiation scales the row stride by two itself (0x1b40:
// lsl x11, x11, #1), so rowBytes is in ELEMENTS of T for both, and the key
// and the scale are carried over by 257 so that the two widths measure the same
// colour space: the 16-bit key raw value is 257 times the 8-bit one and the
// 16-bit scale is 257 times the 8-bit one, so a normalised key is identical in
// both and a raw key is the same multiple of the pixels in both.
static void analyzeCase(int width, int height, int pad, float scale, float sens,
                        float keyR, float keyG, float keyB, int kind, bool nullOut,
                        bool normaliseKey) {
  analyzeOne<unsigned char, ShippedView8>(width, height, pad, scale, sens,
      keyR, keyG, keyB, kind, nullOut, normaliseKey, g_base + 0x7d8,
      g_anMism8, g_anStatMism8);
  analyzeOne<unsigned short, ShippedView16>(width, height, pad, scale * 257.0f, sens,
      keyR * 257.0f, keyG * 257.0f, keyB * 257.0f, kind, nullOut, normaliseKey,
      g_base + 0x19b4, g_anMism16, g_anStatMism16);
}

static void analyzeCases() {
  // Random frames over a spread of shapes, scales, sensitivities, key colours
  // and pixel patterns, in both key conventions.
  for (int iter = 0; iter < 8000; ++iter) {
    const int w = 1 + static_cast<int>(rnd() % 9);
    const int h = 1 + static_cast<int>(rnd() % 9);
    const int pad = static_cast<int>(rnd() % 4);
    const int kind = static_cast<int>(rnd() % 5);
    const float scale = (rnd() % 4 == 0) ? 1.0f
                      : ((rnd() % 2 == 0) ? 255.0f : frnd(1.0f, 400.0f));
    const float sens = frnd(-20.0f, 150.0f);
    const bool norm = (rnd() % 2) == 0;
    analyzeCase(w, h, pad, scale, sens, frnd(0.0f, 255.0f), frnd(0.0f, 255.0f),
                frnd(0.0f, 255.0f), kind, (rnd() % 4) == 0, norm);
  }

  // Structured edge cases.
  //
  //   * a 1x1 frame, which is the only case the percentile's single-sample
  //     branch (0x1154..0x1160) can reach;
  //   * single-row and single-column frames;
  //   * a frame with no pixels at all, which the shipped code answers with the
  //     zeroed Stats and without touching either output buffer;
  //   * a frame where every pixel is the key colour, so nothing is left to
  //     score and the mean spill has no denominator;
  //   * a frame of grey pixels under a COLOURED key, and a frame of coloured
  //     pixels under a GREY key — the pair that separates the OR of the
  //     achromatic shortcut from an AND;
  //   * a wholly grey frame under a wholly grey key, where the shortcut does
  //     fire;
  //   * the sensitivity and scale extremes.
  for (int kind = 0; kind < 5; ++kind) {
    analyzeCase(1, 1, 0, 255.0f, 50.0f, 10.0f, 200.0f, 30.0f, kind, false, true);
    analyzeCase(1, 1, 0, 255.0f, 50.0f, 10.0f, 200.0f, 30.0f, kind, true, true);
    analyzeCase(1, 7, 2, 255.0f, 50.0f, 10.0f, 200.0f, 30.0f, kind, false, true);
    analyzeCase(7, 1, 2, 255.0f, 50.0f, 10.0f, 200.0f, 30.0f, kind, false, true);
    analyzeCase(0, 3, 1, 255.0f, 50.0f, 10.0f, 200.0f, 30.0f, kind, false, true);
    analyzeCase(3, 0, 1, 255.0f, 50.0f, 10.0f, 200.0f, 30.0f, kind, false, true);
    // Grey pixels under a coloured key: the pixel is achromatic, the key is not.
    analyzeCase(4, 4, 0, 255.0f, 50.0f, 10.0f, 200.0f, 30.0f, 1, false, true);
    // Coloured pixels under a grey key: the key is achromatic, the pixel is not.
    // A grey key is grey in any units, so this one works un-normalised too.
    analyzeCase(4, 4, 0, 255.0f, 50.0f, 128.0f, 128.0f, 128.0f, 0, false, false);
    analyzeCase(4, 4, 0, 255.0f, 50.0f, 128.0f, 128.0f, 128.0f, 0, false, true);
    // Both achromatic: the shortcut fires.
    analyzeCase(4, 4, 0, 255.0f, 50.0f, 128.0f, 128.0f, 128.0f, 1, false, false);
    // Every pixel the key colour: everything is keyed.
    analyzeCase(5, 5, 0, 255.0f, 50.0f, 20.0f, 180.0f, 40.0f, 3, false, true);
    analyzeCase(5, 5, 0, 255.0f, 50.0f, 20.0f, 180.0f, 40.0f, 3, false, false);
    // The sensitivity and scale extremes.
    for (int s = 0; s < 4; ++s) {
      const float sens = (s == 0) ? -100.0f : (s == 1) ? 0.0f : (s == 2) ? 100.0f : 1000.0f;
      const float scale = (s % 2 == 0) ? 1.0f : 255.0f;
      analyzeCase(3, 3, 1, scale, sens, 10.0f, 200.0f, 30.0f, 0, false, true);
      analyzeCase(3, 3, 1, scale, sens, 10.0f, 200.0f, 30.0f, 2, false, true);
    }
  }
}

int main() {
  openOracle();
  gradientCases();
  renderCases();
  analyzeCases();
  std::printf("grad_cases=%ld grad_mismatches=%ld\n", g_gradCases, g_gradMismatches);
  std::printf("vectors=%ld mism8=%ld mism16=%ld\n", g_total, g_mism8, g_mism16);
  std::printf("an_vectors=%ld an_mism8=%ld an_statmism8=%ld\n",
              g_anVectors8, g_anMism8, g_anStatMism8);
  std::printf("an_vectors16=%ld an_mism16=%ld an_statmism16=%ld\n",
              g_anVectors16, g_anMism16, g_anStatMism16);
  return (g_gradMismatches || g_mism8 || g_mism16 || g_anMism8 || g_anMism16) ? 1 : 0;
}
`;

function haveTool(cmd) {
  try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
  catch { return false; }
}

function buildHarness(dir, sourcePath) {
  const harness = path.join(dir, "shipped-harness.cpp");
  writeFileSync(harness, HARNESS_SOURCE);
  const bin = path.join(dir, "shipped-harness");
  execFileSync("clang++", [
    "-std=c++17", "-O2",
    "-I", SRC,
    "-o", bin,
    harness,
    sourcePath,
  ], { stdio: "pipe" });
  return bin;
}

function differentialPrerequisites(t) {
  if (!bundlesInstalled()) { t.skip("ChromaKeyAssist bundle is not installed"); return false; }
  if (process.arch !== "arm64") { t.skip("the shipped bundle is executed in place — arm64 only"); return false; }
  if (!haveTool("clang++")) { t.skip("clang++ not available — differential run skipped"); return false; }
  return true;
}

test("the shipped gradientMagnitude is the 47 instructions the listing says", (t) => {
  // The differential below calls the shipped kernel; this pins its LENGTH from the
  // symbol table, so a bundle that changed under us cannot pass silently.
  if (!bundlesInstalled()) { t.skip("ChromaKeyAssist bundle is not installed"); return; }
  try { execFileSync("lipo", ["-info", BUNDLE], { stdio: "ignore" }); }
  catch { t.skip("lipo not available — length check skipped"); return; }

  const dir = mkdtempSync(path.join(tmpdir(), "chromakey-len-"));
  try {
    const slice = path.join(dir, "ChromaKeyAssist.arm64");
    execFileSync("lipo", ["-thin", "arm64", "-output", slice, BUNDLE], { stdio: "pipe" });
    const symbols = execFileSync("nm", ["-n", slice], { encoding: "utf8" })
      .split("\n")
      .map((line) => {
        const m = line.match(/^([0-9a-fA-F]+)\s+\S\s+(\S+)$/);
        return m ? { address: BigInt("0x" + m[1]), name: m[2] } : null;
      })
      .filter(Boolean)
      .sort((a, b) => (a.address < b.address ? -1 : a.address > b.address ? 1 : 0));

    const index = symbols.findIndex((s) => s.name === SHIPPED_GRADIENT);
    assert.ok(index >= 0, `${SHIPPED_GRADIENT} is not in the shipped arm64 slice`);
    const start = symbols[index].address;
    const end = symbols[index + 1].address;
    assert.ok(end > start, "the shipped gradientMagnitude has no length");
    assert.equal(Number(end - start), 47 * 4,
      "the shipped gradientMagnitude is not 47 instructions long");
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("the recovered gradientMagnitude matches the shipped binary's own machine code", (t) => {
  if (!differentialPrerequisites(t)) return;

  const dir = mkdtempSync(path.join(tmpdir(), "chromakey-grad-"));
  try {
    const bin = buildHarness(dir, path.join(SRC, "chromakey.cpp"));
    const out = execFileSync(bin, [], { encoding: "utf8", stdio: "pipe" });
    assert.match(out, /grad_mismatches=0/,
      `the shipped and recovered kernels disagree:\n${out}`);
    const cases = Number(out.match(/grad_cases=(\d+)/)?.[1] ?? 0);
    assert.equal(cases, 4000, `only ${cases} gradient cases were compared`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("the recovered renderView matches the shipped bundle's own machine code", (t) => {
  if (!differentialPrerequisites(t)) return;

  const dir = mkdtempSync(path.join(tmpdir(), "chromakey-render-"));
  try {
    const bin = buildHarness(dir, path.join(SRC, "chromakey.cpp"));
    const out = execFileSync(bin, [], { encoding: "utf8", stdio: "pipe" });
    assert.match(out, /mism8=0 mism16=0/,
      `the shipped and recovered renderView disagree:\n${out}`);
    const vectors = Number(out.match(/vectors=(\d+)/)?.[1] ?? 0);
    assert.ok(vectors >= 20000, `only ${vectors} vectors were compared`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("the recovered analyzeFrame matches the shipped bundle's own machine code", (t) => {
  if (!differentialPrerequisites(t)) return;

  const dir = mkdtempSync(path.join(tmpdir(), "chromakey-analyze-"));
  try {
    const bin = buildHarness(dir, path.join(SRC, "chromakey.cpp"));
    const out = execFileSync(bin, [], { encoding: "utf8", stdio: "pipe" });
    assert.match(out, /an_mism8=0 an_statmism8=0/,
      `the shipped and recovered analyzeFrame disagree:\n${out}`);
    assert.match(out, /an_mism16=0 an_statmism16=0/,
      `the shipped and recovered 16-bit analyzeFrame disagree:\n${out}`);
    const vectors = Number(out.match(/an_vectors=(\d+)/)?.[1] ?? 0);
    assert.ok(vectors >= 8000, `only ${vectors} analyzeFrame vectors were compared`);
    const vectors16 = Number(out.match(/an_vectors16=(\d+)/)?.[1] ?? 0);
    assert.ok(vectors16 >= 8000,
      `only ${vectors16} 16-bit analyzeFrame vectors were compared`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("the analyzeFrame differential can see an AND where the bundle has an OR", (t) => {
  // The achromatic shortcut at 0xc08..0xc18 fires when EITHER the pixel OR the
  // key is achromatic. Reading the fccmp at 0xc14 as a plain two-way select
  // turns it into an AND, and the recovered source would then score a coloured
  // pixel under a grey key on its hue — which the shipped bundle never does.
  //
  // This mutates the `||` in the recovered source into `&&` and requires the
  // harness to notice. The mutation is applied to a COPY in the temporary
  // directory; the recovered source in the repository is not touched.
  if (!differentialPrerequisites(t)) return;

  const dir = mkdtempSync(path.join(tmpdir(), "chromakey-or-mutate-"));
  try {
    const original = readFileSync(path.join(SRC, "chromakey.cpp"), "utf8");
    const mutated = original.replace(
      "pixelMag < 1e-6f || keyMag < 1e-6f",
      "pixelMag < 1e-6f && keyMag < 1e-6f");
    assert.notEqual(mutated, original,
      "the mutation target (the achromatic shortcut) was not found in chromakey.cpp");

    const mutatedSrc = path.join(dir, "chromakey-mutated.cpp");
    writeFileSync(mutatedSrc, mutated);
    const bin = buildHarness(dir, mutatedSrc);
    let out = "";
    let failed = false;
    try {
      out = execFileSync(bin, [], { encoding: "utf8", stdio: "pipe" });
    } catch (e) {
      failed = true;
      out = `${e.stdout ?? ""}${e.stderr ?? ""}`;
    }
    assert.ok(failed, `the harness reported success on a deliberately mutated source:\n${out}`);
    const m8 = Number(out.match(/an_mism8=(\d+)/)?.[1] ?? 0);
    const m16 = Number(out.match(/an_mism16=(\d+)/)?.[1] ?? 0);
    assert.ok(m8 + m16 > 0,
      `the mutated source produced no mismatches at all:\n${out}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("the analyzeFrame differential can fail", (t) => {
  // The analyzeFrame differential is only evidence if it CAN report a mismatch.
  // This mutates one constant in the recovered source — the 5th-percentile
  // fraction, 0.05f -> 0.06f — and requires the harness to notice. The
  // mutation is applied to a COPY in the temporary directory; the recovered
  // source in the repository is not touched.
  if (!differentialPrerequisites(t)) return;

  const dir = mkdtempSync(path.join(tmpdir(), "chromakey-an-mutate-"));
  try {
    const original = readFileSync(path.join(SRC, "chromakey.cpp"), "utf8");
    const mutated = original.replace("analyzePercentile(sorted, 0.05f)",
                                     "analyzePercentile(sorted, 0.06f)");
    assert.notEqual(mutated, original,
      "the mutation target (the 5th-percentile fraction) was not found in chromakey.cpp");

    const mutatedSrc = path.join(dir, "chromakey-mutated.cpp");
    writeFileSync(mutatedSrc, mutated);
    const bin = buildHarness(dir, mutatedSrc);
    let out = "";
    let failed = false;
    try {
      out = execFileSync(bin, [], { encoding: "utf8", stdio: "pipe" });
    } catch (e) {
      failed = true;
      out = `${e.stdout ?? ""}${e.stderr ?? ""}`;
    }
    assert.ok(failed, `the harness reported success on a deliberately mutated source:\n${out}`);
    const m8 = Number(out.match(/an_mism8=(\d+)/)?.[1] ?? 0);
    assert.ok(m8 > 0, `the mutated source produced no mismatches at all:\n${out}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("the differential harness can fail", (t) => {
  // A differential harness that reports zero mismatches is only evidence if it
  // CAN report a mismatch. This mutates one constant in the recovered source —
  // the mode-2 spill threshold, 0.02f -> 0.03f — and requires the harness to
  // notice. The mutation is applied to a COPY in the temporary directory; the
  // recovered source in the repository is not touched.
  if (!differentialPrerequisites(t)) return;

  const dir = mkdtempSync(path.join(tmpdir(), "chromakey-mutate-"));
  try {
    const original = readFileSync(path.join(SRC, "chromakey.cpp"), "utf8");
    const mutated = original.replace("sv <= 0.02f", "sv <= 0.03f");
    assert.notEqual(mutated, original,
      "the mutation target (the mode-2 spill threshold) was not found in chromakey.cpp");

    const mutatedSrc = path.join(dir, "chromakey-mutated.cpp");
    writeFileSync(mutatedSrc, mutated);
    const bin = buildHarness(dir, mutatedSrc);
    let out = "";
    let failed = false;
    try {
      out = execFileSync(bin, [], { encoding: "utf8", stdio: "pipe" });
    } catch (e) {
      failed = true;
      out = `${e.stdout ?? ""}${e.stderr ?? ""}`;
    }
    assert.ok(failed, `the harness reported success on a deliberately mutated source:\n${out}`);
    const m8 = Number(out.match(/mism8=(\d+)/)?.[1] ?? 0);
    const m16 = Number(out.match(/mism16=(\d+)/)?.[1] ?? 0);
    assert.ok(m8 + m16 > 0,
      `the mutated source produced no mismatches at all:\n${out}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
