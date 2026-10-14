// Gate for the .cube loader in plugins/LUTBlend/LutLibrary.cpp.
//
// This is NEW code, not recovered code, so it cannot be checked against the
// shipped binary the way src/ is. What it CAN be checked against is the recovered
// GRAMMAR: every rule asserted below is one that was read out of parseCube's
// disassembly or its string literals, so these tests are the mechanism that keeps
// a fresh implementation faithful to the recovered specification.
//
// Each assertion names the recovered rule it is enforcing.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");

test("the .cube loader implements the recovered grammar", (t) => {
  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) { t.skip("clang++ not available"); return; }

  const dir = mkdtempSync(path.join(tmpdir(), "lutlib-"));

  // A fake HOME containing the RECOVERED folder path, so the discovery layer can
  // be exercised against real files rather than a mocked list.
  const home = path.join(dir, "home");
  const lutDir = path.join(home, "Documents", "Williams Media LUTs");
  mkdirSync(lutDir, { recursive: true });
  const cube = (n) => {
    let t = "LUT_3D_SIZE 2\n";
    const rows = [[0,0,0],[1,0,0],[0,1,0],[1,1,0],[0,0,1],[1,0,1],[0,1,1],[1,1,1]];
    for (const r of rows) t += `${r[0]} ${r[1]} ${r[2]}\n`;
    return t;
  };
  // Deliberately out of alphabetical order on disk, plus a non-cube file and a
  // subdirectory that must both be ignored.
  writeFileSync(path.join(lutDir, "zebra.cube"), cube());
  writeFileSync(path.join(lutDir, "alpha.cube"), cube());
  writeFileSync(path.join(lutDir, "UPPER.CUBE"), cube());   // case-insensitive ext
  writeFileSync(path.join(lutDir, "notes.txt"), "not a lut");
  mkdirSync(path.join(lutDir, "subdir"), { recursive: true });

  try {
    const probe = path.join(dir, "probe.cpp");
    const bin = path.join(dir, "probe");
    writeFileSync(probe, PROBE);
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
      "-I", path.join(ROOT, "src", "lutblend"),
      "-I", path.join(ROOT, "plugins", "LUTBlend"),
      "-o", bin, probe,
      path.join(ROOT, "plugins", "LUTBlend", "LutLibrary.cpp"),
      path.join(ROOT, "src", "lutblend", "lutblend.cpp"),
    ], { stdio: "pipe" });

    let out = "";
    try {
      out = execFileSync(bin, { encoding: "utf8", env: { ...process.env, HOME: home } });
    } catch (e) {
      // The probe exits non-zero when a check fails, and execFileSync throws. Its
      // stdout is the useful part, so surface it rather than "Command failed".
      out = `${e.stdout ?? ""}${e.stderr ?? ""}`;
    }
    assert.match(out, /ALL CUBE GRAMMAR RULES HONOURED/, `loader checks failed:\n${out}`);
    assert.doesNotMatch(out, /\bFAIL\b/, out);

    // Guard against a vacuous run.
    const passes = (out.match(/^  PASS/gm) ?? []).length;
    assert.ok(passes >= 55, `expected >= 55 checks, saw ${passes}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

const PROBE = String.raw`
#include "LutLibrary.h"
#include <cstdio>
#include <string>

static int failures = 0;
static int passes = 0;

static void check(bool ok, const char* what) {
  if (ok) { ++passes; std::printf("  PASS %s\n", what); }
  else    { ++failures; std::printf("  FAIL %s\n", what); }
}

// Parse and report the error text, so message-content rules can be asserted too.
static bool parse(const std::string& text, lutblend::CubeLUT* lut, std::string* err) {
  err->clear();
  return lutlib::parseCubeText(text, lut, err);
}

static bool errHas(const std::string& err, const char* needle) {
  return err.find(needle) != std::string::npos;
}

// Rejection is asserted TOGETHER with the reason, never on its own.
//
// Testing only "did it fail?" is not enough: a file that a broken rule wrongly
// accepts is still rejected by the NEXT rule it violates, so the assertion passes
// and the broken rule hides. Requiring the specific expected message makes each
// rule independently falsifiable. This was found by a negative control that
// loosened the size bound and watched the suite stay green.
//
// NOTE: no backticks anywhere inside this PROBE. It is a String.raw template
// literal, and a backtick in a C++ comment terminates the template and turns the
// rest of the probe into a JS syntax error.
static void rejects(const std::string& text, const char* needle, const char* what) {
  lutblend::CubeLUT l;
  std::string e;
  const bool parsed = lutlib::parseCubeText(text, &l, &e);
  if (parsed) { ++failures; std::printf("  FAIL %s (accepted, should have been rejected)\n", what); return; }
  if (!errHas(e, needle)) {
    ++failures;
    std::printf("  FAIL %s (rejected, but with the WRONG reason: \"%s\")\n", what, e.c_str());
    return;
  }
  ++passes;
  std::printf("  PASS %s\n", what);
}

// A valid 2x2x2 3D identity cube, R fastest, which is the .cube ordering that
// makeIdentity writes and sampleLUT reads.
static std::string kub2x2x2() {
  std::string s = "LUT_3D_SIZE 2\n";
  const float v[8][3] = {{0,0,0},{1,0,0},{0,1,0},{1,1,0},
                         {0,0,1},{1,0,1},{0,1,1},{1,1,1}};
  char buf[128];
  for (auto& r : v) {
    std::snprintf(buf, sizeof(buf), "%g %g %g\n", r[0], r[1], r[2]);
    s += buf;
  }
  return s;
}

int main() {
  lutblend::CubeLUT lut;
  std::string err;

  std::printf("basics\n");
  {
    check(parse(kub2x2x2(), &lut, &err), "a well-formed 2x2x2 cube parses");
    check(lut.is3D && lut.size == 2, "its is3D and size are set");
    check(lut.data.size() == 24, "it carries 3 * size^3 floats");
    check(lut.valid(), "the result passes CubeLUT::valid()");
    // Ordering: entry (b,g,r) must be (r,g,b), R fastest.
    check(lut.data[0] == 0.f && lut.data[1] == 0.f && lut.data[2] == 0.f, "entry 0 is black");
    check(lut.data[3] == 1.f && lut.data[4] == 0.f && lut.data[5] == 0.f, "entry 1 is red (R fastest)");
    check(lut.data[21] == 1.f && lut.data[22] == 1.f && lut.data[23] == 1.f, "the last entry is white");
    float o[3];
    lutblend::sampleLUT(lut, 0.25f, 0.5f, 0.75f, o);
    check(o[0] > 0.24f && o[0] < 0.26f, "the loaded cube round-trips through sampleLUT (R)");
  }

  std::printf("line handling (RECOVERED)\n");
  {
    // Trailing CR dropped before anything else, so CRLF files work.
    std::string crlf;
    for (char c : kub2x2x2()) { if (c == '\n') crlf += '\r'; crlf += c; }
    check(parse(crlf, &lut, &err), "a CRLF file parses identically to LF");
    check(lut.data.size() == 24, "and yields the same payload");

    // '#' starts a comment to end of line.
    std::string hash = "# leading comment\n" + kub2x2x2() + "1 1 1 # trailing comment\n";
    check(parse("# comment-only lines and trailing comments are ignored", &lut, &err) == false,
          "a 9th row is rejected by the row count (so the extra row was really seen)");
    lutblend::CubeLUT h;
    check(parse("# just a comment\n" + kub2x2x2(), &h, &err), "comment lines are skipped");
    check(h.data.size() == 24, "and do not disturb the payload");
  }

  std::printf("the seven directives (RECOVERED)\n");
  {
    lutblend::CubeLUT t;
    check(parse("TITLE my grade\n" + kub2x2x2(), &t, &err),
          "TITLE is accepted and ignored (no arity, no content check)");
    check(parse("TITLE\n" + kub2x2x2(), &t, &err), "TITLE with no argument is also accepted");
    check(parse("TITLE a b c d e\n" + kub2x2x2(), &t, &err), "TITLE with many words is accepted");

    // DOMAIN_MIN / DOMAIN_MAX: three values each, and MAX must strictly exceed MIN.
    lutblend::CubeLUT d;
    check(parse(std::string("LUT_3D_SIZE 2\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n") + kub2x2x2().substr(13),
                &d, &err), "DOMAIN_MIN and DOMAIN_MAX with three values each are accepted");
    check(d.minR == 0.f && d.maxB == 1.f, "the domain lands in the CubeLUT range fields");
    rejects("LUT_3D_SIZE 2\nDOMAIN_MIN 0 0\n", "DOMAIN_MIN needs 3 values",
            "DOMAIN_MIN with two values is rejected as 'needs 3 values'");

    // LUT_*_INPUT_RANGE: two values each.
    lutblend::CubeLUT r;
    check(parse("LUT_3D_SIZE 2\nLUT_3D_INPUT_RANGE 0 1\n" + kub2x2x2().substr(13), &r, &err),
          "LUT_3D_INPUT_RANGE with two values is accepted");
    rejects("LUT_3D_SIZE 2\nLUT_3D_INPUT_RANGE 0 1 2\n", "needs 2 values",
            "LUT_3D_INPUT_RANGE with three values is rejected as 'needs 2 values'");

    // 1D
    lutblend::CubeLUT one;
    check(parse("LUT_1D_SIZE 4\n0 0 0\n0.33 0.33 0.33\n0.66 0.66 0.66\n1 1 1\n", &one, &err),
          "a 1D cube parses");
    check(!one.is3D && one.size == 4 && one.data.size() == 12, "with 3 * size floats");
  }

  std::printf("size rules (RECOVERED)\n");
  {
    lutblend::CubeLUT s;
    rejects("LUT_3D_SIZE 1\n0 0 0\n", "out of range [2,256]", "size 1 is below the bound");
    rejects("LUT_3D_SIZE 257\n", "out of range [2,256]", "size 257 is above the bound");
    rejects("LUT_3D_SIZE 0\n", "out of range [2,256]", "size 0 is below the bound");
    rejects("LUT_3D_SIZE -5\n", "out of range [2,256]", "a negative size is below the bound");
    // 256 IS in range, so it must fail on the ROW COUNT and not on the bound.
    // Asserting the reason is what distinguishes those two.
    rejects("LUT_3D_SIZE 256\n", "expected", "size 256 is in range, so it fails on the row count");

    // A second size declaration is an error, not a last-wins overwrite.
    rejects("LUT_3D_SIZE 2\nLUT_3D_SIZE 3\n", "duplicate LUT size declaration",
            "declaring a size twice is rejected as a duplicate");

    // Truncation, reproduced deliberately.
    lutblend::CubeLUT tr;
    check(parse("LUT_3D_SIZE 2.9\n" + kub2x2x2().substr(13), &tr, &err) && tr.size == 2,
          "a fractional size TRUNCATES (2.9 -> 2), matching the recovered fcvtzs");
  }

  std::printf("row-count rules (RECOVERED)\n");
  {
    lutblend::CubeLUT c;
    rejects("LUT_3D_SIZE 2\n0 0 0\n", "expected 8 data rows, got 1",
            "too few data rows is rejected, reporting both counts");
    rejects(kub2x2x2() + "0 0 0\n", "expected 8 data rows, got 9",
            "too many data rows is rejected, reporting the real count");
    rejects("0 0 0\n", "data before LUT_1D_SIZE/LUT_3D_SIZE",
            "data before any size declaration is rejected");
    rejects("", "missing LUT_1D_SIZE/LUT_3D_SIZE", "empty input reports a missing size");
    // The two whole-file checks carry no line prefix, which is what distinguishes
    // them from the twelve per-line messages. Asserted on a freshly parsed error,
    // not on whatever the previous assertion left behind.
    {
      lutblend::CubeLUT e1; std::string e2;
      (void)lutlib::parseCubeText("", &e1, &e2);
      check(e2.rfind("line ", 0) != 0, "the missing-size message carries no line prefix");
      (void)lutlib::parseCubeText("LUT_3D_SIZE 2\n", &e1, &e2);
      check(e2.rfind("line ", 0) != 0, "nor does the row-count message");
    }
  }

  std::printf("numeric acceptance (RECOVERED)\n");
  {
    lutblend::CubeLUT n;
    // Trailing garbage after a number is refused.
    rejects("LUT_3D_SIZE 2x\n", "has a non-numeric value", "trailing garbage after a size is refused");
    rejects("LUT_3D_SIZE 2\n0 0 0x\n", "non-numeric data value",
            "trailing garbage inside a data row is refused as a non-numeric value");
    // inf and NaN are refused rather than stored: one inf would poison every sample.
    rejects("LUT_3D_SIZE 2\n0 0 0\n0 0 inf\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n",
            "non-numeric data value", "inf is refused");
    rejects("LUT_3D_SIZE 2\n0 0 0\n0 nan 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n",
            "non-numeric data value", "NaN is refused");
    // A first token that is neither keyword nor number.
    rejects("LUT_3D_SIZE 2\nWIBBLE 1 2\n", "unrecognised keyword or malformed data row",
            "an unknown keyword is refused");
  }

  std::printf("domain rule (RECOVERED)\n");
  {
    lutblend::CubeLUT g;
    rejects("LUT_3D_SIZE 2\nDOMAIN_MIN 0.5 0 0\nDOMAIN_MAX 0.5 1 1\n" + kub2x2x2().substr(13),
            "degenerate domain: DOMAIN_MAX must exceed DOMAIN_MIN",
            "an equal-bounds domain is rejected as degenerate");
    // Strictly greater, both directions.
    rejects("LUT_3D_SIZE 2\nDOMAIN_MIN 1 0 0\nDOMAIN_MAX 0 1 1\n" + kub2x2x2().substr(13),
            "degenerate domain", "an INVERTED domain is rejected too");
  }

  std::printf("failure is non-destructive\n");
  {
    lutblend::CubeLUT keep;
    check(parse(kub2x2x2(), &keep, &err), "a good parse fills the output");
    const size_t before = keep.data.size();
    const int beforeSize = keep.size;
    rejects("LUT_3D_SIZE 1\n", "out of range", "a failed parse returns false");
    check(keep.data.size() == before && keep.size == beforeSize,
          "and leaves the output UNTOUCHED (parseCube browses a local)");
  }

  std::printf("discovery (folder scan + cache)\n");
  {
    const std::string folder = lutlib::lutFolderPath();
    check(!folder.empty(), "the folder path resolves from HOME");
    const std::string suffix = "/Documents/Williams Media LUTs";
    check(folder.size() > suffix.size() &&
          folder.compare(folder.size() - suffix.size(), suffix.size(), suffix) == 0,
          "and ends with the recovered suffix");

    const std::vector<std::string> files = lutlib::scanLutFolder();
    check(files.size() == 3, "the scan finds exactly the three .cube files");
    check(files[0].find("UPPER.CUBE") != std::string::npos,
          "the uppercase .CUBE extension is matched case-insensitively");
    check(files[1].find("alpha.cube") != std::string::npos &&
          files[2].find("zebra.cube") != std::string::npos,
          "and the list is sorted, not left in directory order");
    for (const std::string& f : files) {
      check(f.find("notes.txt") == std::string::npos, "notes.txt is not a LUT");
      break;
    }

    const std::string choices = lutlib::popupChoices(files);
    check(choices.find('|') != std::string::npos, "the popup string is pipe-separated");
    check(choices.find(".cube") == std::string::npos, "and carries basenames without extensions");
    check(lutlib::popupChoices({}) == "None", "an empty list still yields one legal choice");

    // The cache must actually cache: same pointer, one parse.
    lutlib::Library lib;
    std::string e3;
    auto first = lib.get(files[1], &e3);
    check(first != nullptr, "a scanned file loads");
    check(first && first->valid(), "and is a valid LUT");
    check(lib.cachedCount() == 1, "the load populates the cache");
    auto again = lib.get(files[1], &e3);
    check(again.get() == first.get(), "a second get returns the SAME object, not a re-parse");
    check(lib.cachedCount() == 1, "and does not grow the cache");
    lib.clear();
    check(lib.cachedCount() == 0, "clear() empties it");

    auto missing = lib.get(folder + "/nope.cube", &e3);
    check(missing == nullptr, "a missing file returns null rather than throwing");
  }

  std::printf("\n%s\n", failures == 0 ? "ALL CUBE GRAMMAR RULES HONOURED"
                                      : "LOADER DEVIATES FROM THE RECOVERED GRAMMAR");
  return failures == 0 ? 0 : 1;
}
`;
