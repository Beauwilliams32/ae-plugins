// Gate for the recovered colorpulse:: namespace.
//
// PARTIAL BY DESIGN: six of ten functions. The four unread ones are the colour
// analysers (averageColor, sampleColor, brightestRegionColor, dominantColor), which are
// absent rather than stubbed, and a test below asserts that. `regionBounds` was on that
// list until it was read.

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
    assert.ok(passes >= 55, `expected >= 35 checks, saw ${passes}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("the colorpulse namespace does not claim what it has not recovered", () => {
  // A stub would compile and link and make the namespace look complete while
  // implementing colour analysis that was never read.
  const impl = readFileSync(path.join(SRC, "colorpulse.cpp"), "utf8");
  for (const fn of ["averageColor", "sampleColor",
                    "brightestRegionColor", "dominantColor"]) {
    assert.doesNotMatch(impl, new RegExp(`\\b${fn}\\s*\\(`),
      `${fn} appears implemented but its body was never read`);
  }
  assert.match(readFileSync(path.join(SRC, "colorpulse.h"), "utf8"),
    /are NOT: they are identified/,
    "the header must state that four functions are unrecovered");

  // regionBounds must now be PRESENT, and the header's scope line must say six of ten.
  assert.match(impl, /\bregionBounds\s*\(/, "regionBounds must be implemented");
  assert.match(readFileSync(path.join(SRC, "colorpulse.h"), "utf8"),
    /Six of ten/,
    "the header's scope line must reflect the recovered count");
});
