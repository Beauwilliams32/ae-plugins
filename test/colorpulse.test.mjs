// Gate for the recovered colorpulse:: namespace.
//
// The namespace is COMPLETE: all ten functions. The host integration is not, and a test
// below keeps that visible.

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
    assert.ok(passes >= 120, `expected >= 35 checks, saw ${passes}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("the colorpulse namespace is complete, and the host integration is not", () => {
  // This test used to assert the opposite: that the unrecovered functions stayed ABSENT,
  // because a stub would have made the namespace look finished while implementing colour
  // analysis nobody had read. All ten are now transcribed, so the assertion inverts — and
  // it inverts only because the bodies were read, not because the pressure to look
  // finished went away. `sampleColor` in particular could not be written until
  // `dominantColor` existed, since it tail-calls it.
  const impl = readFileSync(path.join(SRC, "colorpulse.cpp"), "utf8");
  const header = readFileSync(path.join(SRC, "colorpulse.h"), "utf8");

  for (const fn of ["clampf", "smoothScalar", "luma709", "smoothColor",
                    "saturationBoost", "regionBounds", "averageColor",
                    "brightestRegionColor", "dominantColor", "sampleColor"]) {
    assert.match(impl, new RegExp(`\\b${fn}\\s*\\(`), `${fn} must be implemented`);
  }
  assert.match(header, /All TEN `colorpulse::` functions are recovered in full/,
    "the header must say the namespace is complete");
  assert.match(header, /NAMESPACE-COMPLETE/,
    "and must carry the marker test/codegen.test.mjs cross-checks against the differ");

  // The HOST integration is still unrecovered, and that has to stay visible: the whole
  // namespace being done makes it easy to read the namespace as the effect.
  for (const fn of ["EffectMain", "GridFromWorld8"]) {
    assert.doesNotMatch(impl, new RegExp(`\\b${fn}\\s*\\(`),
      `${fn} is host integration and its body was never read`);
  }
  assert.match(header, /identified only/,
    "the header must state that the host integration is unrecovered");

  // The Mode enum's VALUES are part of the recovery and must not drift: they come from the
  // dispatcher's comparisons, and 2/3 are the only two it tests.
  assert.match(header, /Dominant = 2/, "Mode::Dominant must stay 2");
  assert.match(header, /BrightestRegion = 3/, "Mode::BrightestRegion must stay 3");
});
