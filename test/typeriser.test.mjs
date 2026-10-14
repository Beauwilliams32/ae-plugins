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
    assert.ok(passes >= 170, `expected >= 170 checks, saw ${passes}`);
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

test("parseSrt matches a naive JS oracle over a corpus", (t) => {
  // parseSrt is 512 instructions of composition: BOM strip, a byte-at-a-time
  // line splitter whose CR branch eats a following LF, a positional block walk,
  // a '\n'-joined text accumulator, and a stable sort by start. Re-implementing
  // all of that in JS and diffing is what pins the COMPOSITION — the part a
  // plausible-but-wrong transcription gets wrong (e.g. treating CR and LF as two
  // breaks, or relying on blank lines as block delimiters).
  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) { t.skip("clang++ not available"); return; }

  const CORPUS = [
    "1\n00:00:01,000 --> 00:00:02,500\nHello\n\n2\n00:00:03,000 --> 00:00:04,000\nWorld\n",
    "1\r\n00:00:01,000 --> 00:00:02,500\r\nHello\r\n\r\n",
    "1\r00:00:01,000 --> 00:00:02,500\rHello\r\r",
    "\uFEFF1\n00:00:01,000 --> 00:00:02,000\nBOM\n",
    "00:00:01,000 --> 00:00:02,000\nNo ordinal\n",
    "1\n00:00:01,000 --> 00:00:02,500\nLine one\nLine two\n\n",
    "1\n00:00:01,000 --> 00:00:02,500\n\n2\n00:00:03,000 --> 00:00:04,000\nKept\n",
    "1\n00:00:09,000 --> 00:00:10,000\nLate\n\n2\n00:00:01,000 --> 00:00:02,000\nEarly\n",
    "7\n00:00:01,000 --> 00:00:02,000\nFirst\n\n8\n00:00:01,000 --> 00:00:03,000\nSecond\n",
    "1\nGARBAGE\nText\n\n2\n00:00:03,000 --> 00:00:04,000\nGood\n",
    "1\nJust text, no times\n",
    "",
    "\n\n\n",
    "-->",
    "1\n00:00:01,000 --> 00:00:02,000\nTail",
    "1\n00:00:00,500 --> 00:00:02,000\nIt was the best of times\n\n2\n00:00:02,100 --> 00:00:04,000\nit was the worst of times\n\n3\n00:00:04,200 --> 00:00:06,000\nit was the age of wisdom\n",
    "   \n\n1\n00:00:01,000 --> 00:00:02,000\n  padded  \n",
    "42\n00:01:00,000 --> 00:01:02,000\nA\n\n43\n00:01:03,000 --> 00:01:04,000\nB\n\n44\n00:01:05,000 --> 00:01:06,000\nC\n",
    "1\n00:00:05,000 --> 00:00:01,000\nReversed, dropped\n",
    "1 \n00:00:01,000 --> 00:00:02,000 \nspaces around fields\n",
  ];

  const dir = mkdtempSync(path.join(tmpdir(), "srt-oracle-"));
  try {
    const probe = path.join(dir, "probe.cpp");
    const bin = path.join(dir, "probe");
    const lits = CORPUS.map((c) => `  ${JSON.stringify(c)},`).join("\n");
    execFileSync("sh", ["-c", `cat > ${probe} <<'CPP'
#include "typeriser.h"
#include <cstdio>
#include <string>
int main(){
  const std::string cases[] = {
${lits}
  };
  for (int ci = 0; ci < (int)(sizeof(cases)/sizeof(cases[0])); ++ci) {
    std::vector<typeriser::Cue> cues = typeriser::parseSrt(cases[ci]);
    for (size_t i = 0; i < cues.size(); ++i)
      std::printf("%d|%d|%lld|%lld|%s\\n", ci, cues[i].index, cues[i].start,
                  cues[i].end, cues[i].text.c_str());
  }
  return 0;
}
CPP`]);
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
      "-I", SRC, "-o", bin, probe, path.join(SRC, "typeriser.cpp"),
    ], { stdio: "pipe" });
    const got = execFileSync(bin, { encoding: "utf8" }).trim();

    // --- the oracle: the recovered composition, written independently -------
    const isSpace = (ch) => /\s/.test(ch);
    const trim = (s) => {
      let a = 0, b = s.length;
      while (a < b && isSpace(s[a])) a++;
      while (b > a && isSpace(s[b - 1])) b--;
      return s.slice(a, b);
    };
    const ts = (raw) => {
      const s = trim(raw);
      let field = 0, digits = 0;
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
    const timingLine = (line) => {
      if (line.length < 3) return null;
      const pos = line.indexOf("-->");
      if (pos < 0) return null;
      const start = ts(line.slice(0, pos));
      let rest = trim(line.slice(pos + 3));
      const sp = rest.indexOf(" ");
      if (sp >= 0) rest = rest.slice(0, sp);
      const end = ts(rest);
      if (start !== -1 && end !== -1 && end > start) return [start, end];
      return null;
    };
    // C atoi: skip leading whitespace, optional sign, digits, else 0.
    const atoi = (s) => {
      const m = /^\s*([+-]?\d+)/.exec(s);
      return m ? Number(m[1]) | 0 : 0;
    };
    const parseSrt = (input) => {
      let s = input;
      if (s.charCodeAt(0) === 0xFEFF || (s.charCodeAt(0) === 0xEF && s.charCodeAt(1) === 0xBB && s.charCodeAt(2) === 0xBF)) {
        s = s.charCodeAt(0) === 0xFEFF ? s.slice(1) : s.slice(3);
      }
      const lines = [];
      let line = "";
      for (let i = 0; i < s.length; ++i) {
        const c = s[i];
        if (c === "\r") {
          lines.push(line); line = "";
          if (i + 1 < s.length && s[i + 1] === "\n") ++i;   // CRLF is ONE break
          continue;
        }
        if (c === "\n") { lines.push(line); line = ""; continue; }
        line += c;
      }
      lines.push(line);   // unconditional: a trailing newline adds an empty line

      const cues = [];
      let i = 0;
      while (i < lines.length) {
        if (trim(lines[i]) === "") { ++i; continue; }
        let index = 0, start = 0, end = 0, ok;
        const first = trim(lines[i]);
        const t = timingLine(first);
        if (t) { start = t[0]; end = t[1]; ++i; ok = true; }
        else {
          index = atoi(first);
          if (i + 1 < lines.length) {
            const u = timingLine(trim(lines[i + 1]));
            ok = !!u;
            if (u) { start = u[0]; end = u[1]; }
            i += ok ? 2 : 1;
          } else { ok = false; ++i; }
        }
        let text = "";
        while (i < lines.length) {
          if (trim(lines[i]) === "") break;
          if (text !== "") text += "\n";
          text += trim(lines[i]);
          ++i;
        }
        if (!ok) continue;
        if (trim(text) === "") continue;
        cues.push({ index, start, end, text });
      }
      // stable sort by start
      return cues.map((c, k) => [c, k])
                 .sort((A, B) => A[0].start - B[0].start || A[1] - B[1])
                 .map(([c]) => c);
    };

    const want = [];
    CORPUS.forEach((src, ci) => {
      for (const c of parseSrt(src)) want.push(`${ci}|${c.index}|${c.start}|${c.end}|${c.text}`);
    });

    assert.equal(got, want.join("\n"), "the oracle disagrees with parseSrt");

    // Guard against a vacuous corpus: it must actually produce cues.
    assert.ok(want.length >= 15, `corpus too thin, only ${want.length} cues`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
