// Gate for the recovered beatgrid:: namespace.
//
// All twelve `beatgrid::` symbols are recovered. Three of them are the fleet's shared
// DSP, so the parity check applies here too: a copy is trustworthy when it diverges
// from THIS bundle exactly as the spectra original diverges from SpectraKeys.
//
// Note also what is NOT asserted: `subdivisionFactor` compiles to the bundle's 8
// instructions and still reports DIFFER, because the differ compares disassembly TEXT
// and this build annotates the switch table's address symbolically
// (`adrp x9, l_switch.table...@PAGE`) where the shipped listing prints the resolved
// page and offset. The instructions are the same; the text is not.
//
// Four symbols now compile INSTRUCTION-IDENTICAL to the bundle: `clampf`,
// `isPowerOfTwo`, `OnsetEnvelope::frameTimeSeconds` and `analyzeBeatGrid`. The first
// two are the shared DSP; the last two are this lane's own recoveries.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");
const SRC = path.join(ROOT, "src", "beatgrid");
const BUNDLE = "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media"
  + "/BeatGridRuler.plugin/Contents/MacOS/BeatGridRuler";

const SHARED = ["clampf", "isPowerOfTwo", "downmixToMono"];
/**
 * PathScribe's own, named as the DIFFER names them.
 *
 * `frameTimeSeconds` is a member of `OnsetEnvelope`, and the differ truncates a nested
 * name at the second `::` — the way `spectra::Spectrogram` stands for its destructor —
 * so it reports the pair as `beatgrid::OnsetEnvelope`.
 */
const OWN = ["subdivisionFactor", "phaseSawtooth", "phaseInLayerTime", "OnsetEnvelope",
             "computeOnsetEnvelope", "estimateTempoBpm", "estimatePhaseSeconds",
             "generateBeatGrid", "analyzeBeatGrid"];
/** Nothing is outstanding any more: the differ reports zero unrecovered symbols. */
const OUTSTANDING = [];

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

/** One function's result, namespace-agnostic and scoped to its own block. */
function signature(ns, symbol) {
  const out = runDiff(ns, symbol);
  const lines = out.split("\n");
  const start = lines.findIndex((l) => new RegExp(`^\\s{2}(MATCH|DIFFER)\\s+${ns}::${symbol}\\s`).test(l));
  assert.ok(start >= 0, `no result line for ${ns}::${symbol}:\n${out}`);
  const next = lines.findIndex((l, i) => i > start && /^\s{2}(MATCH|DIFFER|absent)\s/.test(l));
  const detail = lines.slice(start + 1, next < 0 ? undefined : next).find((l) => /^\s+\(/.test(l)) ?? "";
  return `${lines[start].replace(`${ns}::`, "NS::").replace(/\s+/g, " ").trim()} `
       + `${detail.replace(/\s+/g, " ").trim()}`.trim();
}

test("recovered beatgrid source compiles and its maths verifies", (t) => {
  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) { t.skip("clang++ not available — compile check skipped"); return; }

  const dir = mkdtempSync(path.join(tmpdir(), "beatgrid-check-"));
  const bin = path.join(dir, "selfcheck");
  try {
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
      "-o", bin,
      path.join(SRC, "beatgrid.cpp"),
      path.join(SRC, "selfcheck.cpp"),
    ], { stdio: "pipe" });

    const out = execFileSync(bin, { encoding: "utf8" });
    assert.match(out, /ALL RECOVERED BEATGRID MATHS VERIFIED/, `self-check failed:\n${out}`);
    assert.doesNotMatch(out, /\bFAIL\b/, out);
    const passes = (out.match(/^  PASS/gm) ?? []).length;
    assert.ok(passes >= 32, `expected >= 32 checks, saw ${passes}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("the shared copies diverge from BeatGridRuler as the spectra originals do", (t) => {
  if (!bundlesInstalled()) { t.skip("BeatGridRuler bundle is not installed"); return; }

  const results = SHARED.map((fn) => ({
    fn,
    beatgrid: signature("beatgrid", fn),
    spectra: signature("spectra", fn),
  }));
  for (const r of results) {
    assert.doesNotMatch(r.beatgrid, /absent/, `${r.fn} is not implemented in beatgrid`);
    assert.equal(r.beatgrid, r.spectra,
      `${r.fn}: the copy diverges from BeatGridRuler differently than the original `
      + `diverges from SpectraKeys\n  beatgrid: ${r.beatgrid}\n  spectra : ${r.spectra}`);
  }
  // clampf and isPowerOfTwo are identical in both; downmixToMono is not identical in
  // either, and the parity above is what covers it.
  assert.match(results.find((r) => r.fn === "clampf").beatgrid, /MATCH/);
  assert.match(results.find((r) => r.fn === "isPowerOfTwo").beatgrid, /MATCH/);
});

test("the recovered and outstanding sets are exactly the ones this file lists", (t) => {
  if (!bundlesInstalled()) { t.skip("BeatGridRuler bundle is not installed"); return; }

  const out = runDiff("beatgrid");
  const recovered = [...out.matchAll(/^\s{2}(?:MATCH|DIFFER)\s+beatgrid::(\w+)/gm)].map((m) => m[1]).sort();
  const absent = [...out.matchAll(/^\s+absent\s+beatgrid::(\w+)/gm)].map((m) => m[1]).sort();

  assert.deepEqual(recovered, [...SHARED, ...OWN].sort(),
    "the differ recovered a different set of functions than this file claims");
  assert.deepEqual(absent, [...OUTSTANDING].sort(),
    "the set of unrecovered functions changed — recover one and it comes off this list");

  // The member function is recovered AND compared: it is the one this file's differ fix
  // was needed for, so its presence is asserted rather than implied by the tally.
  assert.ok(recovered.includes("OnsetEnvelope"),
    "the OnsetEnvelope member should be recovered and compared, not silently skipped");

  // Every recovered function compiles to the bundle's instruction COUNT, including the
  // four that differ in schedule. Pinned so a source change that alters the shape is
  // visible rather than absorbed by the tally.
  for (const [fn, count] of [["subdivisionFactor", 8], ["phaseInLayerTime", 24],
                             ["phaseSawtooth", 31], ["downmixToMono", 116],
                             ["generateBeatGrid", 94]]) {
    assert.match(out, new RegExp(`DIFFER\\s+beatgrid::${fn}\\s+shipped=${count} mine=${count}`),
      `${fn} should compile to the bundle's ${count} instructions`);
  }

  // The three BIG ones are behavioural recoveries: they are close, but not the same
  // instruction stream, and the counts are pinned so a regression is visible.
  for (const [fn, shipped, mine] of [["computeOnsetEnvelope", 436, 434],
                                     ["estimateTempoBpm", 578, 508],
                                     ["estimatePhaseSeconds", 138, 137]]) {
    assert.match(out, new RegExp(`DIFFER\\s+beatgrid::${fn}\\s+shipped=${shipped} mine=${mine}`),
      `${fn} should compile to ${mine} instructions against the bundle's ${shipped}`);
  }

  const summary = out.trim().split("\n").pop();
  // `frameTimeSeconds` is a CONST member (`__ZNK…`) and the differ used to skip those
  // entirely, so this function was never compared at all until `parseNested` learnt about
  // qualifier letters. It now compiles IDENTICALLY — the shipped code folds the
  // half-window scale into the convert (`scvtf d3, w8, #0x1`) and so does this build.
  assert.match(out, /MATCH\s+beatgrid::OnsetEnvelope\s+13 instructions, identical/,
    "frameTimeSeconds should now compile instruction-identical to the bundle");
  // `analyzeBeatGrid` came back identical as well: 57 instructions, transcribed line
  // for line from the listing.
  assert.match(out, /MATCH\s+beatgrid::analyzeBeatGrid\s+57 instructions, identical/,
    "analyzeBeatGrid should compile instruction-identical to the bundle");
  assert.match(summary, /4 identical, 8 differing, 0 not implemented/,
    `the differ's tally moved: ${summary}`);
});
