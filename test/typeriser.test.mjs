// Gate for the recovered typeriser karaoke-timing core.
//
// Third effect with its own namespace, so a third harness rather than sharing
// spectra's or lutblend's.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");
const SRC = path.join(ROOT, "src", "typeriser");

test("recovered typeriser source compiles and its maths verifies", (t) => {
  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) { t.skip("clang++ not available — compile check skipped"); return; }

  const dir = mkdtempSync(path.join(tmpdir(), "typeriser-check-"));
  const bin = path.join(dir, "selfcheck");
  try {
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
      "-o", bin,
      path.join(SRC, "typeriser.cpp"),
      path.join(SRC, "selfcheck.cpp"),
    ], { stdio: "pipe" });

    const out = execFileSync(bin, { encoding: "utf8" });
    assert.match(out, /ALL RECOVERED TYPERISER MATHS VERIFIED/, `self-check failed:\n${out}`);
    assert.doesNotMatch(out, /\bFAIL\b/, out);
    const passes = (out.match(/^  PASS/gm) ?? []).length;
    assert.ok(passes >= 105, `expected >= 105 checks, saw ${passes}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("all THREE recovered clampf copies agree", (t) => {
  // spectra::clampf, lutblend::clampf and typeriser::clampf were recovered from
  // three different bundles in three different namespaces and turned out
  // instruction-for-instruction identical. Asserting all three against each other
  // means editing one without the others fails the gate.
  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) { t.skip("clang++ not available"); return; }

  const dir = mkdtempSync(path.join(tmpdir(), "clamp3-"));
  try {
    const probe = path.join(dir, "probe.cpp");
    const bin = path.join(dir, "probe");
    execFileSync("sh", ["-c", `cat > ${probe} <<'CPP'
#include "spectra.h"
#include "lutblend.h"
#include "typeriser.h"
#include <cstdio>
int main(){
  const float xs[] = {-5.f,-1.f,-0.001f,0.f,0.25f,5.f,9.999f,10.f,10.001f,50.f};
  const float ranges[][2] = {{0.f,10.f},{2.f,1.f},{-1.f,1.f}};
  for (auto& r : ranges) {
    for (float x : xs) {
      float a = spectra::clampf(x, r[0], r[1]);
      float b = lutblend::clampf(x, r[0], r[1]);
      float c = typeriser::clampf(x, r[0], r[1]);
      if (a != b || b != c) {
        std::printf("DIFFER x=%g lo=%g hi=%g: %g / %g / %g\\n", x, r[0], r[1], a, b, c);
        return 1;
      }
    }
  }
  std::printf("ALL THREE AGREE\\n");
  return 0;
}
CPP`]);
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
      "-I", path.join(ROOT, "src", "spectra"),
      "-I", path.join(ROOT, "src", "lutblend"),
      "-I", SRC,
      "-o", bin, probe,
      path.join(ROOT, "src", "spectra", "spectra.cpp"),
      path.join(ROOT, "src", "lutblend", "lutblend.cpp"),
      path.join(SRC, "typeriser.cpp"),
    ], { stdio: "pipe" });
    const out = execFileSync(bin, { encoding: "utf8" });
    assert.match(out, /ALL THREE AGREE/, out);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
