// Gate for the recovered shake:: namespace.
//
// Ten authored functions, nine recovered and the tracker outstanding, and one of the nine
// shared with four other bundles. The interesting claims are in the self-check rather than
// here: `shake::median` averages the two middle values on an even count, differently from
// `beatsnap::medianOfWindow`, it is the exact median, and it does not depend on the order
// of its inputs; and `shake::sampleLooped` in Clamp mode answers 0.0f — not the end sample
// — anywhere OUTSIDE the range.

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
const RECOVERED = ["clampf", "median", "ShakeProfile", "extractJitter", "movingAverage", "highPass",
                   "accumulate", "normalizeUnitAmplitude", "sampleLooped", "resampleProfile"];
const OUTSTANDING = ["trackTranslation"];

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
    assert.ok(passes >= 72, `expected >= 72 checks, saw ${passes}`);
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
  // median is 43 here against the bundle's 43, and the 12 positions that differ are register
  // allocation only — the shipped makes its second nth_element selection over the lower
  // partition, and so does this build.
  assert.match(out, /DIFFER\s+shake::median\s+shipped=43 mine=43/,
    "median should still report the register-allocation difference");

  const summary = out.trim().split("\n").pop();
  assert.match(summary, /5 identical, 5 differing, 1 not implemented/,
    `the differ's tally moved: ${summary}`);
});
