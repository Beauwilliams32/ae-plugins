// Gate for the recovered pathscribe:: namespace.
//
// The parity check is the centre of this file. Ten of PathScribe's eighteen symbols are
// machine-code-identical to SpectraKeys', so the analysis pipeline here is a copy of the
// one recovered from SpectraKeys — and a copy is only trustworthy if the copying is
// exact. Four of the ten do NOT match instruction-for-instruction, so "it differs" says
// nothing on its own; what says something is that the SAME source, compared against two
// bundles built from it, diverges in exactly the same way against both.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");
const SRC = path.join(ROOT, "src", "pathscribe");
const BUNDLE = "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media"
  + "/PathScribe.plugin/Contents/MacOS/PathScribe";

/**
 * Copied from SpectraKeys, as `[label, fragment]` pairs.
 *
 * The fragment is what the differ is asked for, and it is matched against the MANGLED
 * symbol — so the destructor is asked for by its mangled suffix `D1Ev`. The label is
 * what the answers are compared as. They differ only for `Spectrogram`, which is why
 * the destructor is named explicitly rather than by its class: once the differ could see
 * const members, SpectraKeys' `Spectrogram` had two symbols and the tool began labelling
 * them `Spectrogram(D1Ev)` and `Spectrogram(7hzToBinEf)`.
 */
const SHARED = [["clampf", "clampf"], ["isPowerOfTwo", "isPowerOfTwo"],
                ["linearToDb", "linearToDb"], ["dbToLinear", "dbToLinear"],
                ["hannWindow", "hannWindow"], ["hannCoherentGain", "hannCoherentGain"],
                ["fftRadix2", "fftRadix2"], ["downmixToMono", "downmixToMono"],
                ["analyse", "analyse"], ["Spectrogram", "D1Ev"]];

/** PathScribe's own; waveformColumns joined on 2026-10-27 (row AE-05). */
const OWN = ["spanCoverage", "binEdgeHz", "smoothStep", "waveformColumns", "barLevels"];
const OUTSTANDING = ["smoothedLevelsAtFrame", "rasterizeBars", "rasterizeRibbon"];

function bundlesInstalled() {
  return existsSync(BUNDLE);
}

function runDiff(ns, fragment) {
  const args = [path.join(ROOT, "tools", "codegen-diff.mjs"), ns];
  if (fragment !== undefined) args.push(fragment);
  try {
    return execFileSync("node", args, { cwd: ROOT, encoding: "utf8", stdio: "pipe" });
  } catch (e) {
    return `${e.stdout ?? ""}${e.stderr ?? ""}`;
  }
}

/**
 * One function's differ result, with the namespace name erased.
 *
 * Restricted to the requested symbol's own block. A symbol fragment can match more than
 * one function — asking for `Spectrogram` also reports `rawBandLevel`, whose signature
 * takes a `Spectrogram` — and reading the first detail line in the whole output
 * therefore paired one function's status with another's difference count. That made the
 * parity check fail on a `Spectrogram` that is identical in both bundles.
 */
function signature(ns, label, fragment = label) {
  const out = runDiff(ns, fragment);
  const lines = out.split("\n");
  // The differ labels a symbol by its NAME and appends the mangled suffix only when the
  // name is ambiguous: SpectraKeys prints `Spectrogram(D1Ev)` and `Spectrogram(7hzToBinEf)`
  // while PathScribe, having only one, prints `Spectrogram`. So the label is matched, and
  // when several share it the one whose suffix mentions the fragment asked for wins.
  const candidates = lines.filter((l) =>
    new RegExp(`^\\s{2}(MATCH|DIFFER)\\s+${ns}::${label}(\\([^)]*\\))?\\s`).test(l));
  const chosen = candidates.find((l) => {
    const suffix = l.match(/\(([^)]*)\)/);
    return suffix ? suffix[1].includes(fragment) : true;
  }) ?? candidates[0];
  assert.ok(chosen, `no result line for ${ns}::${label} (fragment "${fragment}"):\n${out}`);

  const start2 = lines.indexOf(chosen);
  const next = lines.findIndex((l, i) => i > start2 && /^\s{2}(MATCH|DIFFER|absent)\s/.test(l));
  const detail = lines.slice(start2 + 1, next < 0 ? undefined : next).find((l) => /^\s+\(/.test(l)) ?? "";
  return `${chosen.replace(`${ns}::`, "NS::").replace(/\s+/g, " ").trim()} `
       + `${detail.replace(/\s+/g, " ").trim()}`.trim();
}

test("recovered pathscribe source compiles and its maths verifies", (t) => {
  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) { t.skip("clang++ not available — compile check skipped"); return; }

  const dir = mkdtempSync(path.join(tmpdir(), "pathscribe-check-"));
  const bin = path.join(dir, "selfcheck");
  try {
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
      "-o", bin,
      path.join(SRC, "pathscribe.cpp"),
      path.join(SRC, "selfcheck.cpp"),
    ], { stdio: "pipe" });

    const out = execFileSync(bin, { encoding: "utf8" });
    assert.match(out, /ALL RECOVERED PATHSCRIBE MATHS VERIFIED/, `self-check failed:\n${out}`);
    assert.doesNotMatch(out, /\bFAIL\b/, out);
    const passes = (out.match(/^  PASS/gm) ?? []).length;
    assert.ok(passes >= 40, `expected >= 40 checks, saw ${passes}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("every shared function diverges from PathScribe as the spectra original does", (t) => {
  if (!bundlesInstalled()) { t.skip("PathScribe bundle is not installed"); return; }

  const results = SHARED.map(([label, fragment]) => ({
    fn: label,
    // The differ's overload suffix, `(D1Ev)`, is dropped before comparing: it is a label
    // aid and it is present on one side only when the other namespace has fewer symbols
    // of that name.
    pathscribe: signature("pathscribe", label, fragment).replace(/\([^)]*\)/, ""),
    spectra: signature("spectra", label, fragment).replace(/\([^)]*\)/, ""),
  }));
  for (const r of results) {
    assert.doesNotMatch(r.pathscribe, /absent/, `${r.fn} is not implemented in pathscribe`);
  }

  // Six of the ten match in BOTH bundles. Asserted separately so that a change making
  // all ten differ identically could not pass as success: identical is a higher bar
  // than identical-divergence.
  const identical = results.filter((r) => /MATCH/.test(r.pathscribe)).map((r) => r.fn).sort();
  assert.deepEqual(identical,
    ["Spectrogram", "clampf", "dbToLinear", "hannCoherentGain", "isPowerOfTwo", "linearToDb"],
    "the set of instruction-identical copies changed");

  for (const r of results) {
    assert.equal(r.pathscribe, r.spectra,
      `${r.fn}: the copy diverges from PathScribe differently than the original diverges `
      + `from SpectraKeys, so the copying changed something\n  pathscribe: ${r.pathscribe}\n`
      + `  spectra   : ${r.spectra}`);
  }
});

test("the recovered and outstanding sets are exactly the ones this file lists", (t) => {
  if (!bundlesInstalled()) { t.skip("PathScribe bundle is not installed"); return; }

  const out = runDiff("pathscribe");
  const recovered = [...out.matchAll(/^\s{2}(?:MATCH|DIFFER)\s+pathscribe::(\w+)/gm)].map((m) => m[1]).sort();
  const absent = [...out.matchAll(/^\s+absent\s+pathscribe::(\w+)/gm)].map((m) => m[1]).sort();

  assert.deepEqual(recovered, [...SHARED.map(([label]) => label), ...OWN].sort(),
    "the differ recovered a different set of functions than this file claims");
  assert.deepEqual(absent, [...OUTSTANDING].sort(),
    "the set of unrecovered functions changed — recover one and it comes off this list");

  // The two PathScribe-only functions that came out identical are named, so a
  // regression to DIFFER is caught rather than absorbed by the tally.
  assert.match(out, /MATCH\s+pathscribe::spanCoverage\s+16 instructions, identical/,
    "spanCoverage is no longer instruction-identical to the bundle");
  assert.match(out, /MATCH\s+pathscribe::binEdgeHz\s+19 instructions, identical/,
    "binEdgeHz is no longer instruction-identical to the bundle");
  // And smoothStep is expected NOT to be, at 25 instructions either way.
  assert.match(out, /DIFFER\s+pathscribe::smoothStep\s+shipped=25 mine=25/,
    "smoothStep should compile to the bundle's instruction count");

  const summary = out.trim().split("\n").pop();
  assert.match(summary, /8 identical, 7 differing, 3 not implemented/,
    `the differ's tally moved: ${summary}`);
});
