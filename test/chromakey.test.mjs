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

/** The eleven functions transcribed here (spillScore and uniformityScore joined on 2026-10-27, AE-05). */
const RECOVERED = [
  "clampf", "rgbToYCbCr", "sensitivityToThreshold", "chromaDistance", "hueAngle",
  "angularDiff", "percentile", "percentileUnsorted", "gradientMagnitude", "spillScore",
  "uniformityScore",
];

/**
 * The functions still unread, as the differ names them.
 *
 * `analyzeFrame` and `renderView` are templates, so the differ sees one symbol per
 * instantiation: four symbols for two functions.
 */
const OUTSTANDING = [
  "analyzeFrame(IhEENS_5StatsERKNS_4ViewIKT_EEfRKNS_6ParamsEPfSB_)",
  "analyzeFrame(ItEENS_5StatsERKNS_4ViewIKT_EEfRKNS_6ParamsEPfSB_)",
  "renderView(IhEEvRKNS_4ViewIKT_EERNS1_IS2_EEfRKNS_6ParamsENS_8ViewMod",
  "renderView(ItEEvRKNS_4ViewIKT_EERNS1_IS2_EEfRKNS_6ParamsENS_8ViewMod",
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
  assert.match(summary, /7 identical, 4 differing, 4 not implemented/,
    `the differ's tally moved: ${summary}`);
});

test("the four functions that do not match are documented as such", () => {
  // A DIFFER is not a failure here, but an undocumented one would be: the notes have
  // to name every function whose instructions are not reproduced exactly, and say how
  // far off it is.
  const notes = readFileSync(path.join(SRC, "RECOVERY-NOTES.md"), "utf8");
  for (const fn of ["percentile", "percentileUnsorted", "spillScore", "uniformityScore"]) {
    assert.match(notes, new RegExp(fn), `RECOVERY-NOTES.md does not mention ${fn}`);
  }
  assert.match(notes, /instruction-identical/i, "the notes should say which ones match exactly");
});

// ---------------------------------------------------------------------------
// The measurement that caught the gradientMagnitude transcription error.
//
// The bundle is a fat Mach-O and `chromakey::gradientMagnitude` is one of its
// exported symbols, but `dlopen` on the bundle resolves the handle and then
// finds NO exported symbol under it — the bundle is a plugin, not a dylib with
// a published export table. So the bytes are extracted directly instead:
//
//   1. `lipo -thin arm64` the bundle into the temporary directory;
//   2. `nm` gives the function's address, and the NEXT symbol's address gives
//      its length;
//   3. in the thin slice __TEXT has vmaddr 0 and fileoff 0, so the virtual
//      address IS the file offset;
//   4. those bytes are copied into a MAP_JIT page and called directly.
//
// The recovered source is compiled into the same harness and the two are run
// over the same randomised inputs.

const SHIPPED_GRADIENT = "__ZN9chromakey17gradientMagnitudeEPKfiilii";

const HARNESS_SOURCE = String.raw`
// Generated by test/chromakey.test.mjs — do not edit.
//
// Calls the SHIPPED bundle's own gradientMagnitude machine code and the
// RECOVERED source side by side over randomised inputs and reports the
// mismatch count. Exits non-zero if the two ever disagree.

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <vector>

#include "chromakey.h"

typedef float (*GradFn)(const float*, int, int, long, int, int);

int main(int argc, char** argv) {
  if (argc != 4) {
    std::printf("usage: %s <slice> <offset> <length>\n", argv[0]);
    return 2;
  }
  const uint64_t offset = std::strtoull(argv[2], nullptr, 0);
  const size_t length = static_cast<size_t>(std::strtoull(argv[3], nullptr, 0));

  const int fd = open(argv[1], O_RDONLY);
  if (fd < 0) { std::printf("open failed: %s\n", strerror(errno)); return 1; }
  std::vector<unsigned char> bytes(length);
  const ssize_t got = pread(fd, bytes.data(), length, static_cast<off_t>(offset));
  close(fd);
  if (got != static_cast<ssize_t>(length)) {
    std::printf("read failed: %zd %s\n", got, strerror(errno));
    return 1;
  }

  void* page = mmap(nullptr, length, PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
  if (page == MAP_FAILED) { std::printf("mmap failed: %s\n", strerror(errno)); return 1; }
  pthread_jit_write_protect_np(0);
  std::memcpy(page, bytes.data(), length);
  pthread_jit_write_protect_np(1);

  const GradFn shipped = reinterpret_cast<GradFn>(page);

  uint64_t state = 0x9e3779b97f4a7c15ULL;
  auto nextRandom = [&state]() {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
  };

  long cases = 0;
  long mismatches = 0;
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
    ++cases;
    const bool same = (a == b) || (std::isnan(a) && std::isnan(b));
    if (!same) {
      ++mismatches;
      if (mismatches <= 10) {
        std::printf("MISMATCH trial=%d w=%d h=%d stride=%ld x=%d y=%d shipped=%.9g mine=%.9g\n",
                    trial, width, height, stride, x, y, a, b);
      }
    }
  }
  std::printf("cases=%ld mismatches=%ld\n", cases, mismatches);
  return mismatches == 0 ? 0 : 1;
}
`;

test("the recovered gradientMagnitude matches the shipped binary's own machine code", (t) => {
  if (!bundlesInstalled()) { t.skip("ChromaKeyAssist bundle is not installed"); return; }
  if (process.arch !== "arm64") { t.skip("the shipped slice is executed in place — arm64 only"); return; }

  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) { t.skip("clang++ not available — differential run skipped"); return; }
  // `lipo` has no --version flag and its usage message exits non-zero, so it is
  // probed by asking it about the bundle's architectures.
  try { execFileSync("lipo", ["-info", BUNDLE], { stdio: "ignore" }); }
  catch { t.skip("lipo not available — differential run skipped"); return; }

  const dir = mkdtempSync(path.join(tmpdir(), "chromakey-shipped-"));
  try {
    const slice = path.join(dir, "ChromaKeyAssist.arm64");
    execFileSync("lipo", ["-thin", "arm64", "-output", slice, BUNDLE], { stdio: "pipe" });

    // The function's address, and the next symbol's address, give its length.
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
    const length = Number(end - start);
    assert.equal(length, 47 * 4, "the shipped gradientMagnitude is not 47 instructions long");

    const harness = path.join(dir, "shipped-harness.cpp");
    writeFileSync(harness, HARNESS_SOURCE);
    const bin = path.join(dir, "shipped-harness");
    execFileSync("clang++", [
      "-std=c++17", "-O2",
      "-I", SRC,
      "-o", bin,
      harness,
      path.join(SRC, "chromakey.cpp"),
    ], { stdio: "pipe" });

    const out = execFileSync(bin, [slice, String(start), String(length)],
      { encoding: "utf8", stdio: "pipe" });
    assert.match(out, /mismatches=0/, `the shipped and recovered kernels disagree:\n${out}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
