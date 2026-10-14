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

import { BUNDLES, bundlePath } from "../tools/namespaces.mjs";

const ROOT = path.join(import.meta.dirname, "..");

// Instruction-identical functions per namespace, as measured. This is a FLOOR:
// raising it when a transcription is tightened is the point; lowering it is a
// regression and should be justified in the commit that does it.
//
// These numbers were RE-MEASURED after three defects in the differ itself were fixed,
// and four of them changed as a result:
//
//   spectra    3 -> 6   two calls were compared against the lazy-binding stub ADDRESS
//                       rather than the symbol, and one overload was silently collapsed
//   colorpulse 4 -> 5   a three-operand `tbnz` was never resolved, so its target was
//                       compared as label text against a numeric address
//   speedramp  1 -> 2   same cause: every branch into a multi-instruction basic block
//                       resolved to the END of the block on the fresh side and the
//                       START on the shipped side
//   glitch     4 -> 4   unchanged, and now with a second overload listed separately
//                       where one entry had been hiding two functions
//
// The floors are therefore higher than the previous run measured without any
// transcription changing. Each rise is accounted for above; a rise with no explanation
// is the thing to distrust, so they are written down rather than silently bumped.
const IDENTICAL_FLOOR = {
  glitch: 4,
  lutblend: 3,
  typeriser: 1,
  spectra: 6,
  colorpulse: 5,
  speedramp: 2,
  echo: 1,
  autoflickerfix: 7,
};

// Namespaces whose bundles must be installed for the check to run at all.
const NAMESPACES = Object.keys(IDENTICAL_FLOOR);

function bundlesInstalled() {
  // The bundle map lives in tools/namespaces.mjs so this file and the differ cannot
  // disagree about which namespace came out of which plug-in.
  return NAMESPACES.every((dir) => {
    const bin = bundlePath(BUNDLES[dir]);
    return bin !== null && existsSync(bin);
  });
}

/**
 * The text of a top-level `function name(...) {...}` from a source file.
 *
 * Slices to the next top-level `function ` or JSDoc block rather than to a second
 * `indexOf` of a named sibling, so the result does not depend on the order the two
 * happen to be declared in.
 */
function extractFunction(src, name) {
  const start = src.indexOf(`function ${name}(`);
  assert.ok(start >= 0, `function ${name} must exist`);
  // Slice from `start`, NOT `start + 1`: indexOf returns the index of the `f`, so
  // skipping one character decapitates the declaration and `new Function` then reports
  // "Unexpected identifier".
  const rest = src.slice(start);
  const end = rest.slice(1).search(/\n(?:function |\/\*\*)/);
  return end < 0 ? rest : rest.slice(0, end + 1);
}

function runDiff(ns) {  // The tool exits non-zero when it finds differences, which is the useful
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

test("the branch resolver handles three-operand branches and picks the block start", async () => {
  // Two defects found together, both of which made the differ report differences that
  // were not there. They are asserted on the resolver's BEHAVIOUR rather than on its
  // text, because a regex that merely looks right is what caused both.
  const src = readFileSync(path.join(ROOT, "tools", "codegen-diff.mjs"), "utf8");
  const body = src.slice(src.indexOf("const BRANCH_TO"), src.indexOf("function parseNested"));
  const { resolveBranchTargets } = await import(
    `data:text/javascript,${encodeURIComponent(`${body}\nexport { resolveBranchTargets };`)}`
  );

  // (1) `tbnz w10, #0x2, 0xc88` has THREE operands, so a target captured with `(\S+)$`
  // never matched it and the branch was compared as raw text forever.
  const three = [
    { addr: 0x10, label: null, text: "tbnz w10, #0x2, 0x20" },
    { addr: 0x14, label: null, text: "nop" },
    { addr: 0x18, label: null, text: "nop" },
    { addr: 0x1c, label: null, text: "nop" },
    { addr: 0x20, label: null, text: "ret" },
  ];
  assert.equal(resolveBranchTargets(three)[0].text ?? resolveBranchTargets(three)[0],
    "tbnz ->instr#4", "a three-operand tbnz must resolve to its target index");

  // ...and the register/bit-count operands must not be mistaken for the target.
  const cbz = [
    { addr: 0x0, label: null, text: "cbz x1, 0x8" },
    { addr: 0x4, label: null, text: "nop" },
    { addr: 0x8, label: null, text: "ret" },
  ];
  assert.equal(resolveBranchTargets(cbz)[0], "cbz ->instr#2");

  // (2) A label sticks to every following instruction until the next label, so an
  // unconditional `set` left each label pointing at the LAST instruction of its block
  // while the shipped side (indexed by exact address) pointed at the FIRST. Every branch
  // into a block of more than one instruction then differed by construction.
  const blocks = [
    { addr: null, label: "LBB0_1", text: "cmp x0, x1" },
    { addr: null, label: "LBB0_1", text: "b.eq LBB0_3" },
    { addr: null, label: "LBB0_2", text: "mov x0, #0x1" },
    { addr: null, label: "LBB0_2", text: "add x0, x0, #0x1" },
    { addr: null, label: "LBB0_2", text: "ret" },
    { addr: null, label: "LBB0_3", text: "mov x0, #0x0" },
    { addr: null, label: "LBB0_3", text: "ret" },
  ];
  assert.equal(resolveBranchTargets(blocks)[1], "b.eq ->instr#5",
    "LBB0_3 must resolve to its FIRST instruction, not to the `ret` that follows it");

  // Negative control for (2): the opposite rule is what the old code did, and it gives a
  // different answer here — so this assertion would fail if the fix were reverted.
  const lastWins = new Map();
  blocks.forEach((e, i) => { if (e.label) lastWins.set(e.label, i); });
  assert.equal(lastWins.get("LBB0_3"), 6, "last-index-wins really did point past the block");
});

test("the differ resolves a call through its lazy-binding stub to the symbol", () => {
  // otool prints a call through the stub as `bl 0x38b8 ; symbol stub for: ___exp10f`
  // while clang prints `bl ___exp10f`, so the operand looked like an address on one side
  // and a name on the other. Every function that calls anything differed at that
  // instruction by construction, which is why spectra and colorpulse both gained matches
  // when it was fixed.
  const src = readFileSync(path.join(ROOT, "tools", "codegen-diff.mjs"), "utf8");
  assert.match(src, /function otoolOperands/, "the operand reducer must exist");
  assert.match(src, /symbol stub for/, "it must recognise the stub annotation");
  assert.match(src, /otoolOperands\(m\[3\]\)/, "and it must be wired into fromOtool");

  // The other `;` annotations are ordinary comments and must be dropped, or an
  // immediate's convenience comment would keep masking a real difference in the
  // register or the addressing mode.
  //
  // Extracted BY NAME rather than by slicing between two `indexOf` results: the first
  // version sliced from `function otoolOperands` to `function fromOtool`, and because
  // otoolOperands is defined AFTER fromOtool in the file that produced an empty string
  // and a ReferenceError. Order-dependent slicing is a trap worth not re-entering.
  const { otoolOperands } = new Function(`${extractFunction(src, "otoolOperands")}; return { otoolOperands };`)();
  assert.equal(otoolOperands("0x38b8 ; symbol stub for: ___exp10f"), "___exp10f");
  assert.equal(otoolOperands("0x38b8 ; symbol stub for: _bzero"), "_bzero");
  assert.equal(otoolOperands("w8, #0x3f800000 ; =1.00000000"), "w8, #0x3f800000");
  assert.equal(otoolOperands("x0, [x8] ; literal pool for: \"PF ANSI Suite\""), "x0, [x8]");
  assert.equal(otoolOperands("s0, s1"), "s0, s1", "no comment, no change");

  // Negative control: a genuine difference in the SYMBOL must still show. Reducing the
  // stub must not make two different calls compare equal.
  assert.notEqual(otoolOperands("0x38b8 ; symbol stub for: ___exp10f"),
                  otoolOperands("0x38dc ; symbol stub for: __Znwm"));
});

test("the differ keeps overloads apart instead of collapsing them by name", () => {
  // Keying on the bare function name merged every overload into one entry and then
  // compared only whichever the shipped dump listed first. `aff::trimmedMean` has two
  // overloads: the 230-instruction pointer form was checked and the 5-instruction
  // tail-call wrapper was never looked at, while the run still reported on the name as
  // though both had been covered.
  const src = readFileSync(path.join(ROOT, "tools", "codegen-diff.mjs"), "utf8");
  assert.match(src, /for \(const mangled of targets\)/,
    "the comparison loop must iterate symbols, not names");
  assert.doesNotMatch(src, /new Set\(symbols\)/,
    "deduplicating by name is the defect this test exists to prevent");
  assert.match(src, /distinguish\(mangled, name, nameCounts\)/,
    "overloads must be labelled distinctly in the output");

  // And the shipped run must actually show the wrapper as its own entry, which is the
  // observable consequence rather than an assertion about the source text.
  if (!bundlesInstalled()) return;
  const r = runDiff("autoflickerfix");
  assert.match(r.raw, /MATCH\s+aff::trimmedMean\(RKNSt3__16vector[^\s)]*\)\s+5 instructions/,
    `the vector overload must be compared in its own right:\n${r.raw}`);
  assert.match(r.raw, /DIFFER\s+aff::trimmedMean\(PKfmf\)/,
    `and the pointer overload must be distinguishable from it:\n${r.raw}`);
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
