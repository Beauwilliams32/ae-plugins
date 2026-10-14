// Gate for the recovered echo:: namespace.
//
// PARTIAL BY DESIGN, and this test asserts only what was actually recovered.
// ALL SIX echo:: functions are transcribed in full. The host integration — EffectMain and
// WorldToImageSized — is not, and a test below keeps that visible.

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

test("the echo namespace is complete, and says which part is not", () => {
  // This test used to assert the opposite: that buildLadder, sampleBilinear and
  // transformBilinear stayed ABSENT, because a stub would have made the namespace look
  // complete while producing a trail geometry nobody had read. All three are now
  // transcribed from the disassembly and compared against the bundle by
  // tools/codegen-diff.mjs, so the assertion inverts — and it inverts only because the
  // bodies were read, not because the pressure to look finished went away.

  const header = readFileSync(path.join(SRC, "echo.h"), "utf8");
  const impl = readFileSync(path.join(SRC, "echo.cpp"), "utf8");

  for (const fn of ["buildLadder", "sampleBilinear", "transformBilinear",
                    "clampf", "blendPixel", "compositeOver"]) {
    assert.match(impl, new RegExp(`\\b${fn}\\s*\\(`), `${fn} must be implemented`);
  }
  assert.match(header, /All SIX `echo::` functions are recovered in full/,
    "the header must state that the namespace is complete at the namespace level");

  // The host integration is still NOT recovered, and that has to stay visible: the whole
  // namespace being done makes it easy to read the namespace as the effect.
  for (const fn of ["EffectMain", "WorldToImageSized"]) {
    assert.doesNotMatch(impl, new RegExp(`\\b${fn}\\s*\\(`),
      `${fn} is host integration and its body was never read`);
  }
  assert.match(header, /The host integration is not/,
    "the header must state that the host integration is unrecovered");
});

test("ImageF is modelled as the 32-byte struct the binary actually uses", () => {
  // THE LAYOUT WAS CORRECTED HERE. `data` was modelled as a raw `float*` on the strength
  // of `ldr x8, [x0, #0x8]` — which is what std::vector::data() compiles to, so a READ
  // cannot tell a pointer from the first field of a vector. transformBilinear resizes the
  // image and a raw pointer has nothing to resize: the bundle writes begin/end/cap at
  // +0x8/+0x10/+0x18 and calls operator delete on the old buffer.
  const header = readFileSync(path.join(SRC, "echo.h"), "utf8");
  assert.match(header, /std::vector<float> pixels;/,
    "ImageF::pixels must be a vector, which is what the resize path proves");
  assert.doesNotMatch(header, /float\* data = nullptr;/,
    "the raw-pointer model has no end, no capacity and nothing to deallocate");
  assert.match(header, /static_assert\(sizeof\(ImageF\) == 32/,
    "and the size must be pinned, so a re-narrowing to 16 bytes fails loudly");

  // The static_assert has to be real, so the header has to compile with it in place.
  const probe = path.join(SRC, "echo.h");
  assert.match(readFileSync(probe, "utf8"), /begin@0x8, end@0x10, cap@0x18/,
    "the assertion message should say which offsets it is holding");
});
