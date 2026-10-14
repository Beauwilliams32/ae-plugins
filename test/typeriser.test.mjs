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
    assert.ok(passes >= 130, `expected >= 130 checks, saw ${passes}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("parseTimingLine matches a naive JS oracle over a table", (t) => {
  // parseTimingLine was read off the arm64 as a composition rather than an
  // algorithm: line.find("-->"), the left side parsed raw, the right side
  // trimmed and then cut at its first space, and the strict end > start accept.
  // Re-implementing that in JS and diffing over a table is what pins the
  // COMPOSITION, which is the part a plausible-but-wrong transcription gets
  // wrong (e.g. trimming the left side too, or using >= instead of >).
  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) { t.skip("clang++ not available"); return; }

  const CASES = [
    // plain, padded, tight, coordinate suffix, CR-free and CR-bearing
    "00:00:01,000 --> 00:00:02,500",
    "00:00:01,000 -->   00:00:02,500   ",
    "00:00:01,000-->00:00:02,500",
    "00:00:01,000 --> 00:00:02,500 X1:1 X2:2 Y1:3 Y2:4",
    "00:01:23,456 --> 00:01:25,000",
    "01:00:00,000 --> 02:30:00,000",
    "00:00:00,000 --> 00:00:00,001",
    // rejections: degenerate, reversed, malformed, structural
    "00:00:02,000 --> 00:00:02,000",
    "00:00:05,000 --> 00:00:01,000",
    "not a time --> 00:00:02,000",
    "00:00:01,000 --> not a time",
    "00:00:01,000 --> 00:00:0",
    "00:00:01 --> 00:00:02,000",
    "00:00:01,000 --> 00:00:02",
    "",
    "-->",
    "00:00:01,000",
    "00:00:01,000 -> 00:00:02,000",
    "00:00:01,000 -- 00:00:02,000",
    "X1:1 00:00:01,000 --> 00:00:02,000",
    "-",
    "--",
    "--> -->",
    "00:00:01,000 --> 00:00:02,500 --> 00:00:03,000",
  ];

  const dir = mkdtempSync(path.join(tmpdir(), "ptl-oracle-"));
  try {
    const probe = path.join(dir, "probe.cpp");
    const bin = path.join(dir, "probe");
    const lines = CASES.map((c) => `  ${JSON.stringify(c)},`).join("\n");
    execFileSync("sh", ["-c", `cat > ${probe} <<'CPP'
#include "typeriser.h"
#include <cstdio>
#include <string>
int main(){
  const std::string cases[] = {
${lines}
  };
  for (const std::string& c : cases) {
    long long a = -7, b = -7;
    bool ok = typeriser::parseTimingLine(c, &a, &b);
    if (ok) std::printf("%d %lld %lld\\n", 1, a, b);
    else    std::printf("%d %lld %lld\\n", 0, a, b);
  }
  return 0;
}
CPP`]);
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
      "-I", SRC, "-o", bin, probe, path.join(SRC, "typeriser.cpp"),
    ], { stdio: "pipe" });

    const got = execFileSync(bin, { encoding: "utf8" }).trim().split("\n");

    // --- the oracle: the recovered composition, written independently ---------
    const trim = (s) => s.replace(/^\s+|\s+$/g, "");
    const ts = (raw) => {
      const s = trim(raw);
      let field = 0, digits = 0, bad = false;
      const acc = [0, 0, 0, 0];
      for (const ch of s) {
        if (ch >= "0" && ch <= "9") {
          acc[field] = acc[field] * 10 + Number(ch);
          if (digits > 3) return -1;
          ++digits; continue;
        }
        if (ch === ":" && field <= 1) { if (digits === 0) return -1; digits = 0; ++field; continue; }
        if ((ch === "," || ch === ".") && field === 2 && digits !== 0) { digits = 0; field = 3; continue; }
        return -1;
      }
      if (field !== 3 || digits === 0) return -1;
      if (acc[1] > 59 || acc[2] > 59) return -1;
      const mul = digits === 1 ? 100 : digits === 2 ? 10 : digits === 3 ? 1 : null;
      if (mul === null) return -1;
      return ((acc[0] * 60 + acc[1]) * 60 + acc[2]) * 1000 + acc[3] * mul;
    };
    const oracle = (line) => {
      if (line.length < 3) return [0, -7, -7];
      const pos = line.indexOf("-->");
      if (pos < 0) return [0, -7, -7];
      const start = ts(line.slice(0, pos));           // raw, no trim, no space-cut
      let rest = trim(line.slice(pos + 3));
      const sp = rest.indexOf(" ");
      if (sp >= 0) rest = rest.slice(0, sp);          // drop the coordinate suffix
      const end = ts(rest);
      if (start !== -1 && end !== -1 && end > start) return [1, start, end];
      return [0, -7, -7];
    };

    for (let i = 0; i < CASES.length; i++) {
      const [ok, a, b] = oracle(CASES[i]);
      assert.equal(got[i], `${ok} ${a} ${b}`, `oracle disagrees on ${JSON.stringify(CASES[i])}`);
    }

    // Guard against a vacuously-true table: assert the mix we meant to test.
    const accepts = got.filter((l) => l.startsWith("1 ")).length;
    assert.equal(accepts, 8, "the table must contain exactly 8 accepted lines");
    assert.equal(got.length, CASES.length);
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
