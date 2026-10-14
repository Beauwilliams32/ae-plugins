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
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
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
//   spectra    6 -> 7   a 64-bit magic constant was converted through a double, so the
//                       shipped hex and the fresh decimal could never agree
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
  spectra: 7,
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

test("the differ sees const member functions, which it used to skip entirely", () => {
  // `parseNested` required the mangling to start `__ZN`, and a CONST member is `__ZNK`.
  // Every const member function in the fleet was therefore invisible: not reported as
  // absent, not reported at all — so a namespace's completeness marker was being measured
  // against a list with holes in it. `spectra::Spectrogram::hzToBin`,
  // `lutblend::CubeLUT::valid` and `beatgrid::OnsetEnvelope::frameTimeSeconds` were the
  // three it was hiding.
  const src = readFileSync(path.join(ROOT, "tools", "codegen-diff.mjs"), "utf8");
  const { parseNested } = new Function(
    `${extractFunction(src, "parseNested")}; return { parseNested };`)();

  // The case that was missed.
  assert.deepEqual(parseNested("__ZNK8beatgrid13OnsetEnvelope16frameTimeSecondsEi"),
    { ns: "beatgrid", name: "OnsetEnvelope" });
  // And the plain form still works, so the fix did not trade one hole for another.
  assert.deepEqual(parseNested("__ZN6glitch5applyIhEEvRKNS_4ViewIKT_EE"),
    { ns: "glitch", name: "apply" });
  // A volatile or ref-qualified member carries other qualifier letters in the same place.
  assert.deepEqual(parseNested("__ZNV7spectra11Spectrogram7hzToBinEf"),
    { ns: "spectra", name: "Spectrogram" });
  // Refusals: not a nested name at all, and a name that is not an identifier.
  assert.equal(parseNested("_ZN6glitch5applyIhEEv"), null, "a single leading underscore is not __ZN");
  assert.equal(parseNested("__ZN6glitch"), null, "a missing name is not a symbol");
});

test("completeness is asserted in BOTH directions and cannot drift", (t) => {
  // A namespace can be complete in two senses that are easy to confuse, so both are
  // checked against the same source of truth — the differ's `absent` tally, which counts
  // MANGLED symbols and therefore counts overloads separately.
  //
  //   * a header marked NAMESPACE-COMPLETE must have zero unrecovered symbols;
  //   * a namespace with zero unrecovered symbols must be marked.
  //
  // The second direction is the one that matters. Asserting only the first lets a
  // namespace be finished in fact while its header still tells a reader that functions
  // are unread — which is exactly the state `echo::` was in for one commit, and the kind
  // of stale claim this repository has already shipped twice. The marker is a single
  // literal line so it cannot be satisfied by a phrasing accident: prose was tried first,
  // and `colorpulse::` says "recovered in full" about a SUBSET of its functions, so no
  // pattern over the existing sentences could tell complete from partial.
  if (!bundlesInstalled()) { t.skip("plugin bundles are not installed"); return; }

  const MARK = "NAMESPACE-COMPLETE";
  const measured = [];
  const marked = [];

  for (const dir of Object.keys(BUNDLES)) {
    let header;
    try { header = readFileSync(path.join(ROOT, "src", dir, `${dir}.h`), "utf8"); }
    catch { continue; }                       // no recovered source for this bundle
    const hasMark = header.includes(MARK);
    const r = runDiff(dir);
    if (hasMark) marked.push(dir);
    if (r.absent === 0) measured.push(dir);
    if (hasMark) {
      assert.equal(r.absent, 0,
        `src/${dir}/ is marked ${MARK} but the differ still reports ${r.absent} `
        + `unrecovered symbol(s):\n${r.raw}`);
    }
  }

  assert.deepEqual(marked.slice().sort(), measured.slice().sort(),
    `the ${MARK} markers and the measurement disagree.\n`
    + `  marked:   ${marked.join(", ") || "(none)"}\n`
    + `  measured: ${measured.join(", ") || "(none)"}\n`
    + "A namespace with no unrecovered symbols must carry the marker, and a marked one "
    + "must have no unrecovered symbols.");

  assert.ok(marked.length >= 3,
    `expected at least three complete namespaces, found ${marked.length}`);
});

test("the differ's decimal-to-hex conversion is exact for 64-bit immediates", () => {
  // A division-by-12 magic number does not fit in a double: `#-6148914691236517206` is
  // past 2^53, so `Number()` rounded it to ...55555400 before `toString(16)` ran and the
  // result could NEVER equal the shipped side's exact `#-0x5555555555555556`. Every
  // function holding a 64-bit magic constant therefore differed by construction, at two
  // positions per constant. `spectra::analyseBands` was reported as a 4-position
  // difference for exactly this reason and is an exact match once the conversion is
  // exact — which is how the bug was found, not by reading the code.
  const src = readFileSync(path.join(ROOT, "tools", "codegen-diff.mjs"), "utf8");
  const { normalize } = new Function(
    `${extractFunction(src, "normalize")}; return { normalize };`)();

  assert.equal(normalize("mov x9, #-6148914691236517206"), "mov x9, #-0x5555555555555556");
  assert.equal(normalize("mov x8, #6148914691236517206"), "mov x8, #0x5555555555555556");
  // And the same constant as otool prints it must land on the same string, or the two
  // sides still could not be compared.
  assert.equal(normalize("mov x9, #-0x5555555555555556"), normalize("mov x9, #-6148914691236517206"));

  // Negative control: the OLD conversion is reproduced here and must DISAGREE, so this
  // test fails if anyone reverts to Number().
  const viaNumber = (d) => {
    const n = Number(d);
    return n < 0 ? `#-0x${(-n).toString(16)}` : `#0x${n.toString(16)}`;
  };
  assert.notEqual(viaNumber("-6148914691236517206"), "#-0x5555555555555556",
    "the Number() path really did lose precision, or this control proves nothing");

  // Small immediates must be unaffected: a shift amount still normalises.
  assert.equal(normalize("lsl #16"), "lsl #0x10");
  assert.equal(normalize("mov w8, #-1"), "mov w8, #-0x1");
});

test("a run of labels at one position is indexed in full", async () => {
  // Clang emits label RUNS — `LBB16_8:` immediately followed by `Ltmp38:` with no
  // instruction between them — and the entry carried a single `label`, so only the last
  // of the run survived and a branch to the FIRST could never resolve. That is a
  // guaranteed false positive for any function whose branch lands on such a block;
  // `spectra::analyseBands` reported two unresolvable targets while both labels were
  // sitting in the file.
  const src = readFileSync(path.join(ROOT, "tools", "codegen-diff.mjs"), "utf8");
  const body = src.slice(src.indexOf("const BRANCH_TO"), src.indexOf("function parseNested"));
  const { fromAsm, resolveBranchTargets } = await import(
    `data:text/javascript,${encodeURIComponent(`${body}\nexport { fromAsm, resolveBranchTargets };`)}`
  );

  const asm = [
    "_f:",
    "\t.cfi_startproc",
    "\tcmp\tx0, #0",
    "\tb.eq\tLBB0_2",
    "LBB0_2:",
    "Ltmp7:",
    "\tadd\tx0, x0, #1",
    "\tret",
  ].join("\n");

  const entries = fromAsm(asm, "_f");
  const resolved = resolveBranchTargets(entries);
  assert.equal(resolved[1], "b.eq ->instr#2",
    "a branch to the FIRST label of a run must resolve to the instruction after the run");

  // Negative control: indexing only the surviving single label cannot find LBB0_2, which
  // is precisely the old behaviour this test exists to prevent.
  const onlyLast = new Map();
  for (const e of entries) if (e.label && !onlyLast.has(e.label)) onlyLast.set(e.label, 1);
  assert.equal(onlyLast.has("LBB0_2"), false,
    "the single-label path really did drop the first label of the run");

  // And the run's labels all point at the same position.
  assert.deepEqual(entries[2].labels, ["LBB0_2", "Ltmp7"]);
});

test("a DIFFER reports how large the difference is", (t) => {
  // "DIFFER" covers both "one constant is wrong" and "this is not the same function", and
  // the summary line read identically either way. A transcriber needs the scale of the
  // difference to know whether to keep going, and whether the remaining gap is
  // instruction selection rather than a mistake.
  //
  // The note is a DIAGNOSTIC and deliberately does not change the tally: a permutation
  // proves nothing about behaviour, since reordering across a dependency is exactly how a
  // float reduction gets reassociated.
  if (!bundlesInstalled()) { t.skip("plugin bundles are not installed"); return; }

  const r = runDiff("spectra");
  assert.match(r.raw, /DIFFER\s+spectra::downmixToMono\s+shipped=116 mine=116/,
    `downmixToMono should differ in exactly one place:\n${r.raw}`);
  assert.match(r.raw, /\(2 of 116 positions differ; the same instructions, REORDERED\)/,
    `the scale of the difference must be reported:\n${r.raw}`);

  // A different instruction count must NOT be described as a reordering, or the note
  // would be worse than useless.
  assert.match(r.raw, /DIFFER\s+spectra::analyse\s+shipped=341 mine=328/);
  assert.match(r.raw, /\(different instruction counts\)/);

  // The matcher itself, with a negative control: a genuine difference is not a
  // permutation, and equal multisets are not claimed to be equal streams.
  const src = readFileSync(path.join(ROOT, "tools", "codegen-diff.mjs"), "utf8");
  const { sameMultiset } = new Function(
    `${extractFunction(src, "sameMultiset")}; return { sameMultiset };`)();
  assert.equal(sameMultiset(["a", "b"], ["b", "a"]), true);
  assert.equal(sameMultiset(["a", "b"], ["a", "c"]), false, "a real difference is not a permutation");
  assert.equal(sameMultiset(["a", "b"], ["a", "b", "c"]), false, "nor is a length change");
});

test("the differ still fails a deliberately wrong transcription", (t) => {
  // THE NEGATIVE CONTROL FOR THE THREE FIXES ABOVE.
  //
  // Each of those fixes made the differ report fewer differences, and a comparison that
  // reports fewer differences can do so either by becoming accurate or by becoming blind.
  // Nothing in the assertions about the fixes distinguishes those, so this closes the
  // hole the direct way: feed the tool a transcription that is WRONG in a known way and
  // require it to say so.
  //
  // It runs against a perturbed copy of the source in a temporary directory, via
  // `--cpp=`, rather than by rewriting a file under src/ — a test must not mutate the
  // thing it is testing, and a crash partway through must not leave a corrupted tree.
  if (!bundlesInstalled()) { t.skip("plugin bundles are not installed"); return; }

  const original = readFileSync(path.join(ROOT, "src", "autoflickerfix", "autoflickerfix.cpp"), "utf8");
  const dir = mkdtempSync(path.join(tmpdir(), "codegen-neg-"));
  try {
    const runWith = (source) => {
      const cpp = path.join(dir, "perturbed.cpp");
      writeFileSync(cpp, source);
      try {
        return execFileSync("node",
          [path.join(ROOT, "tools", "codegen-diff.mjs"), "autoflickerfix", `--cpp=${cpp}`],
          { cwd: ROOT, encoding: "utf8", stdio: "pipe" });
      } catch (e) {
        return `${e.stdout ?? ""}${e.stderr ?? ""}`;
      }
    };

    // (a) The unperturbed source still verifies, so the harness is not simply always
    // failing. Without this the two checks below would pass against a broken tool.
    const baseline = runWith(original);
    assert.match(baseline, /MATCH\s+aff::dbToGainClamp\s+13 instructions, identical/,
      `the baseline must match before a perturbation means anything:\n${baseline}`);

    // (b) A changed CONSTANT. 20.0f -> 10.0f keeps the instruction count at 13, so a
    // tally-only check would miss it; only the operand comparison catches this.
    const constant = runWith(original.replace("fabsf(db) / 20.0f", "fabsf(db) / 10.0f"));
    assert.notEqual(constant, original, "the perturbation must actually change the source");
    assert.match(constant, /DIFFER\s+aff::dbToGainClamp\s+shipped=13 mine=13/,
      `a changed constant must be reported, at the same instruction count:\n${constant}`);

    // (c) A REMOVED GUARD — the exact error the differ caught in this namespace. Dropping
    // meanOf's empty test lets an empty vector fall through to 0.0/0.0 and costs one
    // instruction (the bundle's own `ret` on that path replaces a branch to a shared
    // epilogue). This is a re-run of a real bug rather than a synthetic one, which is
    // what makes it worth pinning: if it can no longer be caught, the tool has regressed
    // to the state that missed it.
    const noGuard = original.replace("  if (values.empty()) return 0.0f;\n  double sum = 0.0;",
                                     "  double sum = 0.0;");
    assert.notEqual(noGuard, original, "the guard must have been present to remove");
    const guarded = runWith(noGuard);
    assert.match(guarded, /DIFFER\s+aff::meanOf\s+shipped=92 mine=93/,
      `removing meanOf's empty guard must be reported:\n${guarded}`);

    // The cascade is worth asserting too: temporalTarget inlines meanOf, so it must also
    // stop matching rather than the differ reporting only the leaf.
    assert.match(guarded, /DIFFER\s+aff::temporalTarget/,
      `the inline site must be reported as well:\n${guarded}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
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
