// Gate for the recovered lutblend (LUT engine) primitives.
//
// Same shape as the spectra test: compile the recovered source with -Werror and
// run its own property checks. The LUTBlend bundle is a separate effect with its
// own namespace, so it gets its own harness rather than sharing spectra's.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");
const SRC = path.join(ROOT, "src", "lutblend");

test("recovered lutblend source compiles and its maths verifies", (t) => {
  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) {
    t.skip("clang++ not available — compile check skipped");
    return;
  }

  const dir = mkdtempSync(path.join(tmpdir(), "lutblend-check-"));
  const bin = path.join(dir, "selfcheck");
  try {
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
      "-o", bin,
      path.join(SRC, "lutblend.cpp"),
      path.join(SRC, "selfcheck.cpp"),
    ], { stdio: "pipe" });

    const out = execFileSync(bin, { encoding: "utf8" });
    assert.match(out, /ALL RECOVERED LUTBLEND MATHS VERIFIED/, `self-check failed:\n${out}`);
    assert.doesNotMatch(out, /\bFAIL\b/, out);
    const passes = (out.match(/^  PASS/gm) ?? []).length;
    assert.ok(passes >= 20, `expected >= 20 checks, saw ${passes}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("the two recovered clampf copies agree", (t) => {
  // spectra::clampf and lutblend::clampf were recovered from DIFFERENT bundles in
  // DIFFERENT namespaces and turned out instruction-for-instruction identical.
  // asserted here so a future edit to one is caught against the other.
  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) { t.skip("clang++ not available"); return; }

  const dir = mkdtempSync(path.join(tmpdir(), "clamp-agree-"));
  try {
    const probe = path.join(dir, "probe.cpp");
    const bin = path.join(dir, "probe");
    execFileSync("sh", ["-c", `cat > ${probe} <<'CPP'
#include "spectra.h"
#include "lutblend.h"
#include <cstdio>
int main(){
  const float xs[] = {-5.f,-0.1f,0.f,0.5f,1.f,9.9f,10.f,11.f};
  for(float x : xs){
    float a = spectra::clampf(x, 0.f, 10.f);
    float b = lutblend::clampf(x, 0.f, 10.f);
    if(a != b){ std::printf("DIFFER at %g: %g vs %g\\n", x, a, b); return 1; }
  }
  std::printf("AGREE\\n");
  return 0;
}
CPP`]);
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
      "-I", path.join(ROOT, "src", "spectra"),
      "-I", SRC,
      "-o", bin,
      probe,
      path.join(ROOT, "src", "spectra", "spectra.cpp"),
      path.join(SRC, "lutblend.cpp"),
    ], { stdio: "pipe" });
    const out = execFileSync(bin, { encoding: "utf8" });
    assert.match(out, /AGREE/, out);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
