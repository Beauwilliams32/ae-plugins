// Gate for the recovered maskflow:: namespace.
//
// Three of the four recovered functions are instruction-identical, which is the strongest
// result in this file. The fourth differs by one instruction (branch against select) and
// is checked in the self-check instead.
//
// Mapping this namespace also put MaskFlow into tools/dsp-duplication.mjs, whose carrier
// list is hardcoded by design — it compares BUNDLES, so a bundle missing from the list is
// a copy going unchecked. MaskFlow's `clampf` was the fifth, and is now verified identical
// to the other four.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");
const SRC = path.join(ROOT, "src", "maskflow");
const BUNDLE = "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media"
  + "/MaskFlow.plugin/Contents/MacOS/MaskFlow";

const RECOVERED = ["clampf", "edgeCostScaleFromTolerance", "smoothstep", "computeDownsampleDims"];
const OUTSTANDING = ["downsampleBoxFilter", "upsampleBilinear", "buildMatte",
                     "computeMatteFullRes", "computeArrivalField"];

function bundlesInstalled() {
  return existsSync(BUNDLE);
}

function runDiff(fragment) {
  const args = [path.join(ROOT, "tools", "codegen-diff.mjs"), "maskflow"];
  if (fragment !== undefined) args.push(fragment);
  try {
    return execFileSync("node", args, { cwd: ROOT, encoding: "utf8", stdio: "pipe" });
  } catch (e) {
    return `${e.stdout ?? ""}${e.stderr ?? ""}`;
  }
}

test("recovered maskflow source compiles and its maths verifies", (t) => {
  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) { t.skip("clang++ not available — compile check skipped"); return; }

  const dir = mkdtempSync(path.join(tmpdir(), "maskflow-check-"));
  const bin = path.join(dir, "selfcheck");
  try {
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
      "-o", bin,
      path.join(SRC, "maskflow.cpp"),
      path.join(SRC, "selfcheck.cpp"),
    ], { stdio: "pipe" });

    const out = execFileSync(bin, { encoding: "utf8" });
    assert.match(out, /ALL RECOVERED MASKFLOW MATHS VERIFIED/, `self-check failed:\n${out}`);
    assert.doesNotMatch(out, /\bFAIL\b/, out);
    const passes = (out.match(/^  PASS/gm) ?? []).length;
    assert.ok(passes >= 38, `expected >= 38 checks, saw ${passes}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("the recovered and outstanding sets are exactly the ones this file lists", (t) => {
  if (!bundlesInstalled()) { t.skip("MaskFlow bundle is not installed"); return; }

  const out = runDiff();
  const recovered = [...out.matchAll(/^\s{2}(?:MATCH|DIFFER)\s+maskflow::(\w+)/gm)].map((m) => m[1]).sort();
  const absent = [...out.matchAll(/^\s+absent\s+maskflow::(\w+)/gm)].map((m) => m[1]).sort();

  assert.deepEqual(recovered, [...RECOVERED].sort(),
    "the differ recovered a different set of functions than this file claims");
  assert.deepEqual(absent, [...OUTSTANDING].sort(),
    "the set of unrecovered functions changed — recover one and it comes off this list");

  // The three that came out identical on the first transcription are named, so a
  // regression to DIFFER is caught rather than absorbed by the tally.
  for (const [fn, count] of [["clampf", 5], ["edgeCostScaleFromTolerance", 12],
                             ["computeDownsampleDims", 32]]) {
    assert.match(out, new RegExp(`MATCH\\s+maskflow::${fn}\\s+${count} instructions, identical`),
      `${fn} is no longer instruction-identical to the bundle`);
  }
  // And smoothstep is expected NOT to be: a branch against a select, one instruction
  // either side depending on the arm order.
  assert.match(out, /DIFFER\s+maskflow::smoothstep\s+shipped=19 mine=18/,
    "smoothstep should compile to within one instruction of the bundle's 19");

  const summary = out.trim().split("\n").pop();
  assert.match(summary, /3 identical, 1 differing, 5 not implemented/,
    `the differ's tally moved: ${summary}`);
});

test("MaskFlow is a carrier in the fleet-wide duplication check", (t) => {
  if (!bundlesInstalled()) { t.skip("MaskFlow bundle is not installed"); return; }

  // The carrier list is hardcoded on purpose — it compares bundles, so a bundle missing
  // from it is a copy nobody is checking. This asserts MaskFlow is in it, and that its
  // clampf is verified identical to the other four rather than merely assumed to be.
  const src = execFileSync("node", [path.join(ROOT, "tools", "dsp-duplication.mjs")],
    { cwd: ROOT, encoding: "utf8", stdio: "pipe" });
  assert.match(src, /clampf\s+5 carriers\s+5 instrs\s+IDENTICAL.*MaskFlow/,
    `MaskFlow should be a verified clampf carrier:\n${src}`);
  assert.match(src, /No drift/, "the shared DSP should still be in sync");
});
