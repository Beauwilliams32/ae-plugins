// Gate for the recovered colorpulse:: namespace.
//
// PARTIAL BY DESIGN: eight of ten functions. `dominantColor` and `sampleColor` are
// absent rather than stubbed, and a test below asserts that. `brightestRegionColor`,
// `averageColor` and `regionBounds` were on that list until they were read.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");
const SRC = path.join(ROOT, "src", "colorpulse");

test("recovered colorpulse source compiles and its maths verifies", (t) => {
  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) { t.skip("clang++ not available — compile check skipped"); return; }

  const dir = mkdtempSync(path.join(tmpdir(), "colorpulse-check-"));
  const bin = path.join(dir, "selfcheck");
  try {
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
      "-o", bin,
      path.join(SRC, "colorpulse.cpp"),
      path.join(SRC, "selfcheck.cpp"),
    ], { stdio: "pipe" });

    const out = execFileSync(bin, { encoding: "utf8" });
    assert.match(out, /ALL RECOVERED COLORPULSE MATHS VERIFIED/, `self-check failed:\n${out}`);
    assert.doesNotMatch(out, /\bFAIL\b/, out);
    const passes = (out.match(/^  PASS/gm) ?? []).length;
    assert.ok(passes >= 88, `expected >= 35 checks, saw ${passes}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("the colorpulse namespace does not claim what it has not recovered", () => {
  // A stub would compile and link and make the namespace look complete while
  // implementing colour analysis that was never read.
  const impl = readFileSync(path.join(SRC, "colorpulse.cpp"), "utf8");
  for (const fn of ["sampleColor", "dominantColor"]) {
    assert.doesNotMatch(impl, new RegExp(`\\b${fn}\\s*\\(`),
      `${fn} appears implemented but its body was never read`);
  }
  // The header must name every function it has NOT recovered, so the scope line cannot
  // drift away from the implementation. Asserting on the names rather than on a stock
  // phrase: the phrase changed when the count did, and the names did not.
  const scope = readFileSync(path.join(SRC, "colorpulse.h"), "utf8");
  assert.match(scope, /Two are NOT/, "the header must state that two are unrecovered");
  for (const fn of ["dominantColor", "sampleColor"]) {
    assert.match(scope, new RegExp("`" + fn + "`"),
      `the header must name ${fn} as unrecovered`);
  }

  // The recovered ones must now be PRESENT, and the scope line must reflect the count.
  assert.match(impl, /\bregionBounds\s*\(/, "regionBounds must be implemented");
  assert.match(impl, /\baverageColor\s*\(/, "averageColor must be implemented");
  assert.match(impl, /\bbrightestRegionColor\s*\(/,
    "brightestRegionColor must be implemented");
  const header = readFileSync(path.join(SRC, "colorpulse.h"), "utf8");
  assert.match(header, /Eight of ten/, "the header's scope line must reflect the count");

  // The sampleColor FINDING must stay: it is the only place `Mode`'s values and the two
  // analysers' arguments are recorded, and losing it would cost a future pass real work.
  assert.match(header, /enum class Mode : int/, "the Mode enum must be declared");
  assert.match(header, /TAIL CALL, depth 8/, "and the dominantColor call and its depth kept");
  assert.match(header, /brightestRegionColor\(pixels, width, height, 4\)/,
    "and the brightestRegionColor call and its step kept");
  // It must NOT be presented as implemented, because it cannot link without the other two.
  assert.match(header, /read but NOT implemented/,
    "the dispatcher is read, not recovered — the header must say so");
});
