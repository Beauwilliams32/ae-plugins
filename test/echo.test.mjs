// Gate for the recovered echo:: namespace.
//
// PARTIAL BY DESIGN, and this test asserts only what was actually recovered.
// clampf, blendPixel and compositeOver are transcribed in full; buildLadder,
// sampleBilinear and transformBilinear are identified with known signatures and
// sizes but unread bodies, and are absent rather than stubbed.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");
const SRC = path.join(ROOT, "src", "echo");

test("recovered echo source compiles and its maths verifies", (t) => {
  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) { t.skip("clang++ not available — compile check skipped"); return; }

  const dir = mkdtempSync(path.join(tmpdir(), "echo-check-"));
  const bin = path.join(dir, "selfcheck");
  try {
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
      "-o", bin,
      path.join(SRC, "echo.cpp"),
      path.join(SRC, "selfcheck.cpp"),
    ], { stdio: "pipe" });

    const out = execFileSync(bin, { encoding: "utf8" });
    assert.match(out, /ALL RECOVERED ECHO MATHS VERIFIED/, `self-check failed:\n${out}`);
    assert.doesNotMatch(out, /\bFAIL\b/, out);
    const passes = (out.match(/^  PASS/gm) ?? []).length;
    assert.ok(passes >= 25, `expected >= 25 checks, saw ${passes}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("the echo namespace does not claim the functions it has not recovered", () => {
  // The three unread functions must stay absent. A stub would compile and link and
  // make the namespace look complete while producing a trail geometry that was never
  // recovered, which is the failure mode this repository's method exists to avoid.

  const header = readFileSync(path.join(SRC, "echo.h"), "utf8");
  const impl = readFileSync(path.join(SRC, "echo.cpp"), "utf8");
  for (const fn of ["buildLadder", "sampleBilinear", "transformBilinear"]) {
    assert.doesNotMatch(impl, new RegExp(`\\b${fn}\\s*\\(`),
      `${fn} appears implemented but its body was never read`);
  }
  // And the header must SAY so rather than leaving a reader to guess.
  assert.match(header, /NOT: they are identified/,
    "the header must state that three functions are unrecovered");
});
