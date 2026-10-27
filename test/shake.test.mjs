// Gate for the recovered shake:: namespace.
//
// Two functions, one of them shared with four other bundles. The interesting claim is in
// the self-check rather than here: `shake::median` averages the two middle values on an odd
// count and an even one differently from `beatsnap::medianOfWindow`, and for a LARGE even
// count it is not the true median at all — the shipped second `nth_element` call covers a
// one-element range.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");
const SRC = path.join(ROOT, "src", "shake");
const BUNDLE = "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media"
  + "/ShakeCapture.plugin/Contents/MacOS/ShakeCapture";

// movingAverage, highPass, extractJitter and the ShakeProfile destructor joined on 2026-10-27
// (row AE-05): modelling the struct first is what unblocked the other three.
const RECOVERED = ["clampf", "median", "ShakeProfile", "extractJitter", "movingAverage", "highPass"];
const OUTSTANDING = ["trackTranslation", "accumulate", "normalizeUnitAmplitude", "sampleLooped",
                     "resampleProfile"];

function bundlesInstalled() {
  return existsSync(BUNDLE);
}

function runDiff(fragment) {
  const args = [path.join(ROOT, "tools", "codegen-diff.mjs"), "shake"];
  if (fragment !== undefined) args.push(fragment);
  try {
    return execFileSync("node", args, { cwd: ROOT, encoding: "utf8", stdio: "pipe" });
  } catch (e) {
    return `${e.stdout ?? ""}${e.stderr ?? ""}`;
  }
}

test("recovered shake source compiles and its maths verifies", (t) => {
  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) { t.skip("clang++ not available — compile check skipped"); return; }

  const dir = mkdtempSync(path.join(tmpdir(), "shake-check-"));
  const bin = path.join(dir, "selfcheck");
  try {
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
      "-o", bin,
      path.join(SRC, "shake.cpp"),
      path.join(SRC, "selfcheck.cpp"),
    ], { stdio: "pipe" });

    const out = execFileSync(bin, { encoding: "utf8" });
    assert.match(out, /ALL RECOVERED SHAKE MATHS VERIFIED/, `self-check failed:\n${out}`);
    assert.doesNotMatch(out, /\bFAIL\b/, out);
    const passes = (out.match(/^  PASS/gm) ?? []).length;
    assert.ok(passes >= 17, `expected >= 17 checks, saw ${passes}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("the recovered and outstanding sets are exactly the ones this file lists", (t) => {
  if (!bundlesInstalled()) { t.skip("ShakeCapture bundle is not installed"); return; }

  const out = runDiff();
  const recovered = [...out.matchAll(/^\s{2}(?:MATCH|DIFFER)\s+shake::(\w+)/gm)].map((m) => m[1]).sort();
  const absent = [...out.matchAll(/^\s+absent\s+shake::(\w+)/gm)].map((m) => m[1]).sort();

  assert.deepEqual(recovered, [...RECOVERED].sort(),
    "the differ recovered a different set of functions than this file claims");
  assert.deepEqual(absent, [...OUTSTANDING].sort(),
    "the set of unrecovered functions changed — recover one and it comes off this list");

  assert.match(out, /MATCH\s+shake::clampf\s+5 instructions, identical/,
    "clampf is no longer instruction-identical to the bundle");
  // median is 32 here against the bundle's 43, and the difference is the compiler's: the
  // shipped makes a second nth_element call over a one-element range and this build elides
  // it as a provable no-op.
  assert.match(out, /DIFFER\s+shake::median\s+shipped=43 mine=32/,
    "median should report the elided-call difference");

  const summary = out.trim().split("\n").pop();
  assert.match(summary, /3 identical, 3 differing, 5 not implemented/,
    `the differ's tally moved: ${summary}`);
});
