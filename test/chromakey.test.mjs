// Gate for the recovered chromakey:: namespace.
//
// Three things are checked, and the third is the one that makes the layout claims
// in the header evidence rather than assertion: the offsets `YCbCr` is documented
// with are re-derived from the shipped bundle's own instructions, so a header that
// drifted from the binary fails here.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");
const SRC = path.join(ROOT, "src", "chromakey");
const BUNDLE = "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media"
  + "/ChromaKeyAssist.plugin/Contents/MacOS/ChromaKeyAssist";

/** The nine functions transcribed here. */
const RECOVERED = [
  "clampf", "rgbToYCbCr", "sensitivityToThreshold", "chromaDistance", "hueAngle",
  "angularDiff", "percentile", "percentileUnsorted", "gradientMagnitude",
];

/**
 * The functions still unread, as the differ names them.
 *
 * `analyzeFrame` and `renderView` are templates, so the differ sees one symbol per
 * instantiation: four symbols for two functions.
 */
const OUTSTANDING = [
  "spillScore", "uniformityScore",
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

  // The six instruction-identical ones are the strong evidence, so they are named
  // rather than counted: a regression that turned one of them into a DIFFER would
  // otherwise still satisfy a tally.
  for (const fn of ["clampf", "rgbToYCbCr", "sensitivityToThreshold", "chromaDistance",
                    "hueAngle", "angularDiff"]) {
    assert.match(out, new RegExp(`MATCH\\s+chromakey::${fn}\\s+\\d+ instructions, identical`),
      `${fn} is no longer instruction-identical to the bundle`);
  }

  const summary = out.trim().split("\n").pop();
  assert.match(summary, /6 identical, 3 differing, 6 not implemented/,
    `the differ's tally moved: ${summary}`);
});

test("the three functions that do not match are documented as such", () => {
  // A DIFFER is not a failure here, but an undocumented one would be: the notes have
  // to name every function whose instructions are not reproduced exactly, and say how
  // far off it is.
  const notes = readFileSync(path.join(SRC, "RECOVERY-NOTES.md"), "utf8");
  for (const fn of ["percentile", "percentileUnsorted", "gradientMagnitude"]) {
    assert.match(notes, new RegExp(fn), `RECOVERY-NOTES.md does not mention ${fn}`);
  }
  assert.match(notes, /instruction-identical/i, "the notes should say which ones match exactly");
});
