// Gate for the differential codegen check.
//
// tools/codegen-diff.mjs compiles a recovered namespace and compares each
// function's instruction stream against the shipped bundle's. This is the
// strongest verification in the repo, and it is also the one whose PARTIAL
// results are easiest to overstate, so the distinction is encoded here:
//
//   * glitch is gated on ZERO differences. For small integer routines the streams
//     come out byte-identical — all 34 instructions of hashKey, all 16 constants,
//     every shift and madd. That is not a stylistic match, it is the artefact.
//
//   * The other namespaces are gated on not REGRESSING. Their larger functions
//     differ in codegen for legitimate reasons (the original inlined std::vector
//     growth and luma709; this transcription calls out where doing so is
//     bit-identical, and clang's register allocation differs). Saying "verified"
//     about those would be wrong; saying "unverified" would also be wrong, since
//     their behaviour is covered by self-checks and oracles. The ratchet below is
//     what keeps that honest and moving in one direction.
//
// A function that is instruction-identical is verified twice over. A function that
// merely differs is verified by its self-check, not by this.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");

// Instruction-identical functions per namespace, as measured. This is a FLOOR:
// raising it when a transcription is tightened is the point; lowering it is a
// regression and should be justified in the commit that does it.
const IDENTICAL_FLOOR = {
  glitch: 4,
  lutblend: 3,
  typeriser: 1,
  spectra: 3,
};

// Namespaces whose bundles must be installed for the check to run at all.
const NAMESPACES = Object.keys(IDENTICAL_FLOOR);

function bundlesInstalled() {
  const base = "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media";
  const map = { glitch: "GlitchKit", lutblend: "LUTBlend", typeriser: "TypeRiser",
                spectra: "SpectraKeys" };
  return NAMESPACES.every((ns) =>
    existsSync(path.join(base, `${map[ns]}.plugin`, "Contents", "MacOS", map[ns])));
}

function runDiff(ns) {
  // The tool exits non-zero when it finds differences, which is the useful
  // behaviour for CI but makes execFileSync throw. The tally is on stdout, so
  // capture it either way rather than treating a difference as a crash.
  let out;
  try {
    out = execFileSync("node",
      [path.join(ROOT, "tools", "codegen-diff.mjs"), ns],
      { cwd: ROOT, encoding: "utf8", stdio: "pipe" });
  } catch (e) {
    out = `${e.stdout ?? ""}${e.stderr ?? ""}`;
  }
  const summary = out.trim().split("\n").pop();
  const m = summary.match(/(\d+) identical, (\d+) differing, (\d+) not implemented/);
  assert.ok(m, `could not parse codegen-diff summary for ${ns}: ${summary}`);
  return {
    identical: Number(m[1]),
    differing: Number(m[2]),
    absent: Number(m[3]),
    raw: out,
  };
}

test("glitch is instruction-for-instruction identical to the shipped bundle", (t) => {
  if (!bundlesInstalled()) {
    t.skip("plugin bundles are not installed on this machine");
    return;
  }
  const r = runDiff("glitch");
  assert.equal(r.differing, 0,
    `glitch should match the shipped binary exactly:\n${r.raw}`);
  assert.equal(r.identical, IDENTICAL_FLOOR.glitch,
    `expected ${IDENTICAL_FLOOR.glitch} identical glitch functions`);

  // The point of the check is that it is exact, so assert on a specific function
  // rather than only on the tally: hashKey is 34 instructions over 16 constants.
  assert.match(r.raw, /MATCH\s+glitch::hashKey\s+34 instructions, identical/,
    "hashKey must match all 34 instructions");
  assert.match(r.raw, /MATCH\s+glitch::patternFrame\s+7 instructions, identical/,
    "patternFrame must match all 7 instructions");
});

test("codegen fidelity across the recovered namespaces does not regress", (t) => {
  if (!bundlesInstalled()) {
    t.skip("plugin bundles are not installed on this machine");
    return;
  }
  for (const ns of NAMESPACES) {
    const r = runDiff(ns);
    assert.ok(r.identical >= IDENTICAL_FLOOR[ns],
      `${ns}: ${r.identical} instruction-identical functions, floor is ${IDENTICAL_FLOOR[ns]}`
      + `\n${r.raw}`);
  }
});

test("branch targets are compared structurally, not as label text", () => {
  // The shipped binary branches to a resolved ADDRESS and fresh assembly branches to
  // a LABEL. Comparing those textually reports a difference for EVERY branch, which
  // is what made speedramp's four functions look wrong when only register allocation
  // actually differed. Both sides are now reduced to "branch to instruction N".
  const src = readFileSync(path.join(ROOT, "tools", "codegen-diff.mjs"), "utf8");
  assert.match(src, /function resolveBranchTargets/, "the resolver must exist");
  assert.match(src, /->instr#/, "targets must be rewritten to an instruction index");

  // And it must be wired into the comparison, not merely defined.
  assert.match(src, /resolveBranchTargets\(fromOtool/, "the shipped side must be resolved");
  assert.match(src, /resolveBranchTargets\(fromAsm/, "and the fresh side too");
});

test("the codegen differ distinguishes a real mismatch from whitespace", (t) => {
  // The tool is only worth trusting if its comparison is normalised for the things
  // that are NOT differences — otool separates operands with a space where clang
  // uses a tab, and clang prints shift amounts in decimal where the binary
  // disassembles to hex — while still catching a real one. That normalisation is
  // exactly what a slip would break, so it is pinned here rather than assumed.
  const src = execFileSync("node", ["-e", `
    const s = require("fs").readFileSync(${JSON.stringify(path.join(ROOT, "tools", "codegen-diff.mjs"))}, "utf8");
    // Re-derive the normaliser from the file so this test exercises the shipped one.
    const body = s.slice(s.indexOf("function normalize"), s.indexOf("function isLabel"));
    eval(body);
    const cases = [
      ["fcmp\\ts0, s2", "fcmp s0, s2", true],
      ["lsl #16", "lsl #0x10", true],
      ["mov\\tw8, #51819", "mov w8, #0xca6b", true],
      ["csel w0, w8, wzr, gt", "csel w0, w8, wzr, hi", false],
      ["mul w8, w0, w8", "madd w9, w0, w9, w1", false],
    ];
    for (const [a, b, want] of cases) {
      const got = normalize(a) === normalize(b);
      if (got !== want) { console.log("MISMATCH " + a + " vs " + b); process.exit(1); }
    }
    console.log("NORMALIZER OK");
  `], { encoding: "utf8" });
  assert.match(src, /NORMALIZER OK/, src);
});
