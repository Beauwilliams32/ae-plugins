// Gate for the recovered beatsnap:: namespace.
//
// Two things are verified here, and the second is the one that makes this a
// recovery rather than a copy of a copy.
//
// 1. The recovered source compiles and its maths verifies, in selfcheck.cpp, with
//    every assertion anchored to something the disassembly establishes.
//
// 2. THE DIVERGENCE SIGNATURE. BeatSnap's `hannWindow`, `fftRadix2`,
//    `downmixToMono`, `isPowerOfTwo` and `clampf` are machine-code-identical to
//    the `spectra::` ones in the shipped bundles, so the source here was
//    transcribed from the spectra copy under a new namespace. That is only
//    legitimate if the transcription is exact, and the strongest available
//    evidence is comparative: the SAME source compared against TWO bundles built
//    from it must diverge in exactly the same way against both. If the copy differs
//    from BeatSnap's binary differently than the original differs from
//    SpectraKeys', something was changed in the copying.
//
//    That matters because four of the five do NOT match instruction-for-instruction
//    — hannWindow is 116 instructions here against 112 shipped, fftRadix2 differs in
//    4 of 100 positions, downmixToMono in 2 of 116 — so "it differs" is not
//    informative on its own. "It differs identically to the original" is.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");
const SRC = path.join(ROOT, "src", "beatsnap");
const BUNDLE = "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media"
  + "/BeatSnap.plugin/Contents/MacOS/BeatSnap";

/** The five shared-DSP functions, each of which came from the spectra copy. */
const SHARED_DSP = ["clampf", "isPowerOfTwo", "hannWindow", "fftRadix2", "downmixToMono"];

/**
 * The two recovered from this bundle alone — BeatSnap's own onset detection.
 *
 * `medianOfWindow` is the strongest result in this file: all 58 of its shipped
 * instructions are reproduced exactly. `adaptiveThreshold` is recovered and
 * behaviourally verified but compiles to 91 instructions against the bundle's 78 —
 * a structural difference in how the two loops walk the vector, not a difference
 * in what they compute, and it is recorded in src/beatsnap/RECOVERY-NOTES.md.
 */
const OWN_RECOVERED = {
  medianOfWindow: "MATCH",
  adaptiveThreshold: "DIFFER",
  computeFlux: "DIFFER",
};

/** The functions in this bundle that are NOT recovered yet. */
const OUTSTANDING = ["pickOnsets", "detectOnsets", "renderValues"];

function bundlesInstalled() {
  return existsSync(BUNDLE);
}

/** Run the differ and return its stdout, whether or not it found a difference. */
function runDiff(ns, symbol) {
  // The fragment is omitted rather than passed as `undefined`: execFileSync turns
  // an undefined argument into the literal string "undefined", which the differ
  // then matches no symbol against and reports an empty namespace.
  const args = [path.join(ROOT, "tools", "codegen-diff.mjs"), ns];
  if (symbol !== undefined) args.push(symbol);
  try {
    return execFileSync("node", args, { cwd: ROOT, encoding: "utf8", stdio: "pipe" });
  } catch (e) {
    return `${e.stdout ?? ""}${e.stderr ?? ""}`;
  }
}

/**
 * Reduce one function's differ result to a namespace-agnostic signature: its
 * status, its instruction counts, and — when it differs — how many positions.
 */
function divergenceSignature(ns, symbol) {
  const out = runDiff(ns, symbol);
  const lines = out.split("\n");
  const status = lines.find((l) => /^\s{2}(MATCH|DIFFER|absent)\s/.test(l));
  assert.ok(status, `no result line for ${ns}::${symbol}:\n${out}`);
  const detail = lines.find((l) => /^\s+\(/.test(l)) ?? "";
  const identified = status.replace(`${ns}::`, "NS::").replace(/\s+/g, " ").trim();
  return `${identified} ${detail.replace(/\s+/g, " ").trim()}`.trim();
}

test("recovered beatsnap source compiles and its maths verifies", (t) => {
  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) { t.skip("clang++ not available — compile check skipped"); return; }

  const dir = mkdtempSync(path.join(tmpdir(), "beatsnap-check-"));
  const bin = path.join(dir, "selfcheck");
  try {
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
      "-o", bin,
      path.join(SRC, "beatsnap.cpp"),
      path.join(SRC, "selfcheck.cpp"),
    ], { stdio: "pipe" });

    const out = execFileSync(bin, { encoding: "utf8" });
    assert.match(out, /ALL RECOVERED BEATSNAP MATHS VERIFIED/, `self-check failed:\n${out}`);
    assert.doesNotMatch(out, /\bFAIL\b/, out);
    const passes = (out.match(/^  PASS/gm) ?? []).length;
    assert.ok(passes >= 90, `expected >= 90 checks, saw ${passes}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("each recovered copy diverges from BeatSnap exactly as the spectra original does", (t) => {
  if (!bundlesInstalled()) { t.skip("BeatSnap bundle is not installed"); return; }

  const results = SHARED_DSP.map((fn) => ({
    fn,
    beatsnap: divergenceSignature("beatsnap", fn),
    spectra: divergenceSignature("spectra", fn),
  }));

  // Every one of the five must exist in BOTH namespaces: a function that the differ
  // could not find would produce an "absent" line and this would compare two
  // absences, which proves nothing.
  for (const r of results) {
    assert.doesNotMatch(r.beatsnap, /absent/, `${r.fn} is not implemented in beatsnap`);
    assert.doesNotMatch(r.spectra, /absent/, `${r.fn} is not implemented in spectra`);
  }

  // Two of the five are instruction-identical in both. That is asserted separately
  // so that a change which made ALL FIVE differ identically could not pass as
  // success: identical is a higher bar than identical-divergence.
  const identical = results.filter((r) => /MATCH/.test(r.beatsnap)).map((r) => r.fn).sort();
  assert.deepEqual(identical, ["clampf", "isPowerOfTwo"],
    "clampf and isPowerOfTwo are the two copies that should match instruction for instruction");

  for (const r of results) {
    assert.equal(r.beatsnap, r.spectra,
      `${r.fn}: the copy diverges from BeatSnap differently than the original diverges `
      + `from SpectraKeys, so the copying changed something\n  beatsnap: ${r.beatsnap}\n  spectra : ${r.spectra}`);
  }
});

test("the recovered and outstanding sets are exactly the ones this file lists", (t) => {
  if (!bundlesInstalled()) { t.skip("BeatSnap bundle is not installed"); return; }

  const out = runDiff("beatsnap");
  const absent = [...out.matchAll(/^\s+absent\s+beatsnap::(\w+)/gm)].map((m) => m[1]).sort();
  const recovered = [...out.matchAll(/^\s{2}(?:MATCH|DIFFER)\s+beatsnap::(\w+)/gm)].map((m) => m[1]).sort();

  // The seven that are recovered, split by where they came from.
  assert.deepEqual(recovered, [...SHARED_DSP, ...Object.keys(OWN_RECOVERED)].sort(),
    "the differ recovered a different set of functions than this file claims");
  assert.deepEqual(absent, [...OUTSTANDING].sort(),
    "the set of unrecovered functions changed — recover one and it comes off this list, "
    + "so the list and the measurement cannot drift apart");

  // And each own-recovered function's status, so that a behaviour-preserving edit
  // which breaks instruction-identity is caught rather than passing quietly.
  for (const [fn, expected] of Object.entries(OWN_RECOVERED)) {
    const line = out.split("\n").find((l) => l.includes(`beatsnap::${fn}`) && /MATCH|DIFFER/.test(l));
    assert.ok(line, `no result line for beatsnap::${fn}`);
    assert.match(line, new RegExp(expected), `beatsnap::${fn} is no longer ${expected}: ${line.trim()}`);
  }

  // medianOfWindow reproduces the shipped instructions exactly — 58 of them.
  assert.match(out, /MATCH\s+beatsnap::medianOfWindow\s+58 instructions, identical/,
    "medianOfWindow is no longer instruction-identical to the bundle");

  const summary = out.trim().split("\n").pop();
  assert.match(summary, /3 identical, 5 differing, 3 not implemented/,
    `the differ's tally moved: ${summary}`);
});
