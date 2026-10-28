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
 * `spillScore` and `uniformityScore` joined on 2026-10-27; `renderView` on
 * 2026-10-28 (AE-05).
 *
 * `renderView` is a template, so the differ sees one symbol per instantiation and
 * names it twice. It is listed twice here on purpose: the differ's recovered set
 * is compared with multiplicity, so losing one instantiation would fail the
 * comparison rather than pass it.
 */
const RECOVERED = [
  "clampf", "rgbToYCbCr", "sensitivityToThreshold", "chromaDistance", "hueAngle",
  "angularDiff", "percentile", "percentileUnsorted", "gradientMagnitude", "spillScore",
  "uniformityScore", "renderView", "renderView",
];

/**
 * The functions still unread, as the differ names them.
 *
 * `analyzeFrame` is a template, so the differ sees one symbol per instantiation:
 * two symbols for one function.
 */
const OUTSTANDING = [
  "analyzeFrame(IhEENS_5StatsERKNS_4ViewIKT_EEfRKNS_6ParamsEPfSB_)",
  "analyzeFrame(ItEENS_5StatsERKNS_4ViewIKT_EEfRKNS_6ParamsEPfSB_)",
];

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
  assert.match(summary, /7 identical, 6 differing, 2 not implemented/,
    `the differ's tally moved: ${summary}`);
});

test("the functions that do not match are documented as such", () => {
  // A DIFFER is not a failure here, but an undocumented one would be: the notes have
  // to name every function whose instructions are not reproduced exactly, and say how
  // far off it is.
  const notes = readFileSync(path.join(SRC, "RECOVERY-NOTES.md"), "utf8");
  for (const fn of ["percentile", "percentileUnsorted", "spillScore", "uniformityScore",
                    "renderView"]) {
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
    const std::size_t count = static_cast<std::size_t>(stride)
                            * (static_cast<std::size_t>(height) + 2) + 16;
    std::vector<float> field(count);
    for (std::size_t i = 0; i < count; ++i) {
      field[i] = static_cast<float>(static_cast<int32_t>(nextRandom() % 2001) - 1000) / 100.0f;
    }
    const int x = static_cast<int>(nextRandom() % static_cast<uint64_t>(width + 2)) - 1;
    const int y = static_cast<int>(nextRandom() % static_cast<uint64_t>(height + 2)) - 1;

    const float a = shipped(field.data(), width, height, stride, x, y);
    const float b = chromakey::gradientMagnitude(field.data(), width, height, stride, x, y);
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
                bool structured, const float* forcedAlpha, const float* forcedSpill) {
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
  if (forcedAlpha) alphaBuf.assign(forcedAlpha, forcedAlpha + alphaBuf.size());
  if (forcedSpill) spillBuf.assign(forcedSpill, forcedSpill + spillBuf.size());
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
        false, nullptr, nullptr);
  }

  // Structured edge cases: every mode including out of range, the mix extremes,
  // a null alpha, a null spill, mismatched source/destination widths, a padded
  // stride, a 1x1 frame, and alpha/spill values straddling the 0.02 threshold.
  const float edge[] = {0.0f, 0.02f, 0.0200001f, 0.25f, 0.5f};
  for (int mode = -1; mode <= 6; ++mode) {
    for (int m = 0; m < 5; ++m) {
      const float mix = (m == 0) ? -1.0f : (m == 1) ? 0.0f : (m == 2) ? 1.0f : (m == 3) ? 2.0f : 0.5f;
      const float sens = (m == 4) ? -5.0f : 50.0f;
      one(3, 3, 3, 0, 0, mode, mix, 255.0f, sens, false, false, true, edge, edge);
      one(1, 1, 1, 0, 0, mode, 1.0f, 65535.0f, 0.0f, true, true, true, edge, edge);
      one(4, 2, 2, 3, 1, mode, 0.25f, 1.0f, 100.0f, false, true, true, edge, edge);
    }
  }
}

int main() {
  openOracle();
  gradientCases();
  renderCases();
  std::printf("grad_cases=%ld grad_mismatches=%ld\n", g_gradCases, g_gradMismatches);
  std::printf("vectors=%ld mism8=%ld mism16=%ld\n", g_total, g_mism8, g_mism16);
  return (g_gradMismatches || g_mism8 || g_mism16) ? 1 : 0;
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
