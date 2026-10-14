// Gate for the recovered spectra DSP core.
//
// Two independent things are checked:
//
//   1. The constants read out of the disassembly decode to the values claimed
//      for them. This validates the *reading* of the machine code, which is
//      where a transcription error would hide.
//   2. The recovered C++ actually compiles and its maths holds, including a
//      cross-check between two functions recovered from unrelated code paths.
//
// If clang++ is unavailable the compile check is skipped rather than failed:
// this repository must stay usable for reading even without a toolchain.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");
const SRC = path.join(ROOT, "src", "spectra");

const floatFromHex = (hex) => {
  const b = Buffer.alloc(4);
  b.writeUInt32BE(parseInt(hex, 16));
  return b.readFloatBE(0);
};

test("float immediates in the disassembly decode to the claimed values", () => {
  // linearToDb's log guard: `mov w8,#0x705f / movk w8,#0x3089,lsl#16`
  assert.equal(floatFromHex("3089705f"), Math.fround(1e-9));
  // hannWindow's 2*pi: `mov w8,#0xfdb / movk w8,#0x40c9,lsl#16`
  assert.equal(floatFromHex("40c90fdb"), Math.fround(2 * Math.PI));
  // the 0.5 multiplier (movi.4s #0x3f,lsl#24) and the n==1 fill (0x3f800000)
  assert.equal(floatFromHex("3f000000"), 0.5);
  assert.equal(floatFromHex("3f800000"), 1);
});

test("recovered spectra.cpp compiles and its maths verifies", (t) => {
  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) {
    t.skip("clang++ not available — compile check skipped");
    return;
  }

  const dir = mkdtempSync(path.join(tmpdir(), "spectra-check-"));
  const bin = path.join(dir, "selfcheck");
  try {
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
      "-o", bin,
      path.join(SRC, "spectra.cpp"),
      path.join(SRC, "selfcheck.cpp"),
    ], { stdio: "pipe" });

    const out = execFileSync(bin, { encoding: "utf8" });
    assert.match(out, /ALL RECOVERED MATHS VERIFIED/, `self-check reported failure:\n${out}`);
    assert.doesNotMatch(out, /\bFAIL\b/, out);
    // Guard against the harness silently doing nothing.
    const passes = (out.match(/^  PASS/gm) ?? []).length;
    assert.ok(passes >= 25, `expected >= 25 checks to run, saw ${passes}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("the recovery is reproducible from the installed bundles", (t) => {
  const bundles = "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media";
  if (!existsSync(bundles)) {
    t.skip("fleet not installed on this machine");
    return;
  }
  // Re-running the extractor must reproduce the committed specs byte-for-byte,
  // otherwise the specs cannot be diffed and drift would go unnoticed.
  const out = execFileSync("node", [path.join(ROOT, "tools", "recover.mjs")], { encoding: "utf8" });
  assert.match(out, /wrote 16 specs/);
  const status = execFileSync("git", ["-C", ROOT, "status", "--porcelain", "specs/"], { encoding: "utf8" });
  assert.equal(status.trim(), "", `recover.mjs produced different specs:\n${status}`);
});

test("the shared DSP is identical across every carrier effect", (t) => {
  // Four effects carry the same audio/analysis module under different
  // namespaces. If one copy is ever edited in isolation the others silently
  // keep the old behaviour, so this asserts they still agree — at the machine
  // code level, with relocations and the namespace rename normalised out.
  const tool = path.join(ROOT, "tools", "dsp-duplication.mjs");
  let out, code = 0;
  try {
    out = execFileSync("node", [tool], { encoding: "utf8" });
  } catch (err) {
    out = err.stdout ?? "";
    code = err.status ?? 1;
  }
  if (/no fleet installed/.test(out) || out.trim() === "") {
    t.skip("fleet not installed on this machine");
    return;
  }
  assert.equal(code, 0, `DSP copies have drifted:\n${out}`);
  assert.match(out, /No drift/);
  // Guard against the tool silently comparing nothing.
  const verified = Number(out.match(/(\d+) functions verified identical/)?.[1] ?? 0);
  assert.ok(verified >= 5, `expected several shared functions, saw ${verified}\n${out}`);
});
