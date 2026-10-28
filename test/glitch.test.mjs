// Gate for the recovered glitch:: namespace.
//
// Three things are verified here, and the third is the one that makes this a
// RECOVERY rather than a guess.
//
// 1. The recovered source compiles and its internal property checks pass.
//
// 2. The recovered `apply<T>` agrees with the SHIPPED bundle, byte for byte.
//    The self-check dlopens GlitchKit.plugin, calls the bundle's own
//    `glitch::apply` on the same frames with the same parameters into a
//    sentinel-filled buffer, and compares the two buffers byte for byte.
//    That is the oracle: the shipped binary, not this file's reading of the
//    assembly, is the ground truth.
//
// 3. The State table in src/glitch/RECOVERY-NOTES.md, the State block in
//    src/glitch/glitch.h and the measurement from tools/codegen-diff.mjs all
//    agree — in both directions, so none of them can drift apart from the
//    others.
//
// HONESTY NOTE, which this file asserts rather than merely asserts-about:
// the two `apply<T>` instantiations are BEHAVIOURAL recoveries, not
// instruction-identical ones. The differ reports them as DIFFERING, and this
// file requires that. An earlier draft of this test asserted that both were
// identical; that was wrong and would have passed a lie.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");
const SRC = path.join(ROOT, "src", "glitch");
const BUNDLE = "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media"
  + "/GlitchKit.plugin/Contents/MacOS/GlitchKit";

/**
 * The State of the namespace, as this file claims it.
 *
 * The four scalar helpers are transcribed instruction for instruction. The two
 * `apply<T>` instantiations are transcribed as SEMANTICS and verified against
 * the shipped bundle, so their instruction streams differ and the differ says
 * so.
 */
const STATE = {
  clampf: { status: "MATCH", shipped: 5, mine: 5, kind: "identical" },
  clampi: { status: "MATCH", shipped: 5, mine: 5, kind: "identical" },
  hashKey: { status: "MATCH", shipped: 34, mine: 34, kind: "identical" },
  patternFrame: { status: "MATCH", shipped: 7, mine: 7, kind: "identical" },
  "apply<unsigned char>": {
    status: "DIFFER", shipped: 1157, mine: 1410, kind: "behavioural", tag: "h",
  },
  "apply<unsigned short>": {
    status: "DIFFER", shipped: 1228, mine: 1478, kind: "behavioural", tag: "t",
  },
};

/**
 * The notes' State table uses the C++ spelling of the overloaded `apply`; the
 * differ prints the mangled one. This maps between them so the two can be
 * compared.
 */
const MANGLED = {
  "apply<unsigned char>": "glitch::apply(IhEE",
  "apply<unsigned short>": "glitch::apply(ItEE",
};

function bundlesInstalled() {
  return existsSync(BUNDLE);
}

/** Run the differ and return its stdout, whether or not it found a difference. */
function runDiff(ns) {
  const args = [path.join(ROOT, "tools", "codegen-diff.mjs"), ns];
  try {
    return execFileSync("node", args, { cwd: ROOT, encoding: "utf8", stdio: "pipe" });
  } catch (e) {
    return `${e.stdout ?? ""}${e.stderr ?? ""}`;
  }
}

/**
 * Find a differ row by symbol.
 *
 * For the overloaded `apply` the differ prints the MANGLED name, which this
 * file matches by its distinguishing prefix (`IhEE` for `unsigned char`,
 * `ItEE` for `unsigned short`) rather than by spelling the whole thing out.
 */
function findRow(rows, symbol) {
  const exact = rows.find((r) => r.symbol === symbol);
  if (exact) return exact;
  const prefixed = rows.filter((r) => r.symbol.startsWith(symbol));
  assert.equal(prefixed.length, 1,
    `expected exactly one differ row starting with ${symbol}, saw ${prefixed.length}`);
  return prefixed[0];
}

/** Parse the differ's output into { symbol, status, shipped, mine }. */
function parseDiff(out) {
  const rows = [];
  for (const line of out.split("\n")) {
    let m = line.match(/^\s{2}MATCH\s+(glitch::\S+)\s+(\d+) instructions, identical/);
    if (m) {
      rows.push({ symbol: m[1], status: "MATCH", shipped: Number(m[2]), mine: Number(m[2]) });
      continue;
    }
    m = line.match(/^\s{2}DIFFER\s+(glitch::\S+)\s+shipped=(\d+) mine=(\d+)/);
    if (m) {
      rows.push({ symbol: m[1], status: "DIFFER", shipped: Number(m[2]), mine: Number(m[3]) });
      continue;
    }
    m = line.match(/^\s+absent\s+(glitch::\S+)/);
    if (m) rows.push({ symbol: m[1], status: "absent" });
  }
  return rows;
}

/* --------------------------------------------------------------------------
 * Build-and-run of the self-check, cached across the tests in this file.
 * ----------------------------------------------------------------------- */
let cached = null;

function buildAndRun() {
  if (cached) return cached;
  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) return { skipped: true, reason: "clang++ not available" };

  const dir = mkdtempSync(path.join(tmpdir(), "glitch-check-"));
  const bin = path.join(dir, "selfcheck");
  try {
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
      "-o", bin,
      path.join(SRC, "glitch.cpp"),
      path.join(SRC, "selfcheck.cpp"),
    ], { stdio: "pipe" });
    const out = execFileSync(bin, { encoding: "utf8" });
    cached = { skipped: false, out };
    return cached;
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

test("recovered glitch source compiles and its maths verifies", (t) => {
  const r = buildAndRun();
  if (r.skipped) { t.skip(r.reason); return; }
  const out = r.out;

  assert.match(out, /ALL RECOVERED GLITCH MATHS VERIFIED/, `self-check failed:\n${out}`);
  assert.doesNotMatch(out, /\bFAIL\b/, out);
  const passes = (out.match(/^  PASS/gm) ?? []).length;
  assert.ok(passes >= 25, `expected >= 25 checks, saw ${passes}`);

  // BOTH instantiations are checked. `apply<unsigned short>` is the same
  // source at a wider channel depth, and a mistake that only shows up there —
  // a stride doubled twice, a quantiser grid built from the wrong maximum —
  // would sail past the 8-bit checks untouched.
  assert.match(out, /^glitch::apply<unsigned char>$/m,
    "the self-check did not exercise the 8-bit instantiation");
  assert.match(out, /^glitch::apply<unsigned short>$/m,
    "the self-check did not exercise the 16-bit instantiation");
  const passes16 = (out.match(/^  PASS 16-bit:/gm) ?? []).length;
  assert.ok(passes16 >= 20,
    `expected >= 20 checks against the 16-bit instantiation, saw ${passes16}`);
});

test("the recovered apply agrees with the SHIPPED bundle, byte for byte", (t) => {
  if (!bundlesInstalled()) { t.skip("GlitchKit bundle is not installed"); return; }
  const r = buildAndRun();
  if (r.skipped) { t.skip(r.reason); return; }
  const out = r.out;

  assert.doesNotMatch(out, /the dlopen differential did not run/,
    "the differential did not run even though the bundle is installed:\n" + out);

  const m = out.match(
    /vectors=(\d+) bytes-compared=(\d+) mismatching-vectors=(\d+) vectors-with-a-write=(\d+)/);
  assert.ok(m, `the differential did not report its tally:\n${out}`);

  const vectors = Number(m[1]);
  const bytes = Number(m[2]);
  const mismatching = Number(m[3]);
  const written = Number(m[4]);

  assert.equal(mismatching, 0,
    `the recovered apply differs from the shipped bundle on ${mismatching} of `
    + `${vectors} vectors`);
  assert.ok(vectors > 1000, `only ${vectors} vectors were compared — too few to be evidence`);
  assert.ok(bytes > 0, "no bytes were compared");
  // A comparison of two untouched buffers would also report zero mismatches, so
  // the test insists that a meaningful number of vectors actually wrote.
  assert.ok(written > 1000,
    `only ${written} of ${vectors} vectors produced a write — the differential may be `
    + "comparing buffers that nothing touched");
});

test("the recovered and outstanding sets are exactly the ones this file lists", (t) => {
  if (!bundlesInstalled()) { t.skip("GlitchKit bundle is not installed"); return; }

  const out = runDiff("glitch");
  const rows = parseDiff(out);
  const absent = rows.filter((r) => r.status === "absent").map((r) => r.symbol).sort();
  const recovered = rows.filter((r) => r.status !== "absent").map((r) => r.symbol).sort();

  assert.deepEqual(absent, [],
    "the differ still reports unrecovered glitch symbols — recover them and take them "
    + "off this list, so the list and the measurement cannot drift apart");

  assert.equal(recovered.length, 6,
    `expected six recovered glitch symbols, saw ${recovered.length}: ${recovered.join(", ")}`);

  // Each function's status and its instruction counts, so that a
  // behaviour-preserving edit which breaks instruction-identity is caught
  // rather than passing quietly.
  for (const [fn, want] of Object.entries(STATE)) {
    const symbol = MANGLED[fn] ?? `glitch::${fn}`;
    const row = findRow(rows, symbol);
    assert.ok(row, `no differ row for ${symbol}; the differ reported: ${recovered.join(", ")}`);
    assert.equal(row.status, want.status,
      `${fn} is ${row.status} in the differ but this file claims ${want.status}`);
    assert.equal(row.shipped, want.shipped,
      `${fn} is ${row.shipped} instructions in the bundle but this file claims ${want.shipped}`);
    assert.equal(row.mine, want.mine,
      `${fn} compiles to ${row.mine} instructions but this file claims ${want.mine}`);
  }

  // The four scalar helpers really are instruction-identical, each with its
  // own count — asserted separately so that "all six differ" could not pass.
  const identical = rows.filter((r) => r.status === "MATCH").map((r) => r.symbol).sort();
  assert.deepEqual(identical,
    ["glitch::clampf", "glitch::clampi", "glitch::hashKey", "glitch::patternFrame"],
    "the set of instruction-identical glitch functions changed");
  const counts = Object.fromEntries(rows.filter((r) => r.status === "MATCH")
    .map((r) => [r.symbol, r.shipped]));
  assert.equal(counts["glitch::clampf"], 5);
  assert.equal(counts["glitch::clampi"], 5);
  assert.equal(counts["glitch::hashKey"], 34);
  assert.equal(counts["glitch::patternFrame"], 7);

  // The two `apply` instantiations are behavioural, and their shipped counts
  // are the whole effect body.
  assert.equal(findRow(rows, MANGLED["apply<unsigned char>"]).shipped, 1157);
  assert.equal(findRow(rows, MANGLED["apply<unsigned short>"]).shipped, 1228);

  const summary = out.trim().split("\n").pop();
  assert.match(summary, /4 identical, 2 differing, 0 not implemented/,
    `the differ's tally moved: ${summary}`);
});

test("the notes' State table agrees with the differ", (t) => {
  if (!bundlesInstalled()) { t.skip("GlitchKit bundle is not installed"); return; }

  const notes = readFileSync(path.join(SRC, "RECOVERY-NOTES.md"), "utf8");
  const rows = [...notes.matchAll(/^\|\s*`([^`]+)`\s*\|\s*(\d+)\s*\|\s*(.+?)\s*\|\s*$/gm)]
    .map((m) => ({ fn: m[1], shipped: Number(m[2]), state: m[3] }));
  assert.equal(rows.length, 6,
    `the notes' State table should have six rows, saw ${rows.length}`);

  const out = runDiff("glitch");
  const diffRows = parseDiff(out);

  for (const row of rows) {
    const symbol = MANGLED[row.fn] ?? `glitch::${row.fn}`;
    const measured = findRow(diffRows, symbol);
    assert.ok(measured, `the differ has no row for ${symbol}`);
    assert.equal(row.shipped, measured.shipped,
      `the notes say ${row.fn} is ${row.shipped} instructions; the bundle has `
      + `${measured.shipped}`);

    if (measured.status === "MATCH") {
      assert.match(row.state, /instruction-identical/i,
        `the notes call ${row.fn} something other than instruction-identical, but the `
        + "differ says it is instruction-identical");
    } else {
      assert.match(row.state, /behavioural/i,
        `the notes must label ${row.fn} as a behavioural recovery — the differ says it `
        + "DIFFERS from the shipped instructions, and calling that anything else "
        + "would overstate what has been recovered");
    }
  }
});

test("the header's State block agrees with the differ, and says BEHAVIOURAL where it must", (t) => {
  if (!bundlesInstalled()) { t.skip("GlitchKit bundle is not installed"); return; }

  const header = readFileSync(path.join(SRC, "glitch.h"), "utf8");
  const rows = [...header.matchAll(
    /^\/\/\s+glitch::(.+?)\s+(\d+)\s+instructions\s+(.+?)\s*$/gm)]
    .map((m) => ({ fn: m[1].trim(), shipped: Number(m[2]), state: m[3].trim() }));
  assert.equal(rows.length, 6,
    `the header's State block should have six rows, saw ${rows.length}: `
    + rows.map((r) => r.fn).join(", "));

  const out = runDiff("glitch");
  const diffRows = parseDiff(out);

  for (const row of rows) {
    // The header spells the overloads the C++ way; the differ prints mangled.
    const fn = row.fn.replace(/^apply<(.+)>$/, "apply<$1>");
    const symbol = MANGLED[fn] ?? `glitch::${fn}`;
    const measured = findRow(diffRows, symbol);
    assert.ok(measured, `the differ has no row for ${symbol}`);
    assert.equal(row.shipped, measured.shipped,
      `the header says ${fn} is ${row.shipped} instructions; the bundle has ${measured.shipped}`);

    if (measured.status === "MATCH") {
      assert.match(row.state, /INSTRUCTION-IDENTICAL/,
        `the header must call ${fn} instruction-identical`);
    } else {
      assert.match(row.state, /BEHAVIOURAL/,
        `the header must call ${fn} a BEHAVIOURAL RECOVERY — the differ says its `
        + "instructions DIFFER from the shipped ones");
      assert.doesNotMatch(row.state, /INSTRUCTION-IDENTICAL/,
        `the header calls ${fn} instruction-identical, which is false`);
    }
  }
});

test("the header's completeness marker agrees with the differ, in both directions", (t) => {
  if (!bundlesInstalled()) { t.skip("GlitchKit bundle is not installed"); return; }

  // The marker is assembled here rather than written out, so that this file
  // never contains the literal the guard looks for. The guard itself is
  // integrator-owned and lives in test/codegen.test.mjs; this is the same
  // two-way check, scoped to this namespace.
  const MARK = "NAMESPACE-" + "COMPLETE";
  const header = readFileSync(path.join(SRC, "glitch.h"), "utf8");
  const hasMark = header.includes(MARK);

  // The marker must be on a line of its OWN, not buried in prose.
  if (hasMark) {
    assert.ok(header.split("\n").some((l) => l.trim() === `// ${MARK}`),
      "the completeness marker must sit alone on its own line");
  }

  const out = runDiff("glitch");
  const absent = parseDiff(out).filter((r) => r.status === "absent").map((r) => r.symbol);

  if (absent.length === 0) {
    assert.ok(hasMark,
      "the differ reports no unrecovered symbols, so the header must carry the "
      + "completeness marker");
  } else {
    assert.ok(!hasMark,
      `the differ still reports ${absent.length} unrecovered symbol(s) `
      + `(${absent.join(", ")}), so the header must not carry the completeness marker`);
  }
});

test("the recovered View and Params offsets match the shipped bundle", (t) => {
  // These layouts are CLAIMED in src/glitch/glitch.h, and a claim about a struct's
  // field offsets is the kind that goes stale silently. So the offsets are
  // re-derived here from the bundle's own instructions rather than trusted.
  if (!bundlesInstalled()) { t.skip("GlitchKit bundle is not installed"); return; }

  const asm = execFileSync("otool", ["-tV", "-arch", "arm64", BUNDLE],
                           { encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });
  const lines = asm.split("\n");

  const sliceFor = (mangled) => {
    const out = [];
    let on = false;
    for (const line of lines) {
      if (/^_[A-Za-z_$]/.test(line)) {
        if (on) break;
        on = line.startsWith(`${mangled}:`);
        continue;
      }
      if (!on) continue;
      const m = line.match(/^([0-9a-f]+)\t(\S+)\t?(.*)$/);
      if (m) out.push({ addr: parseInt(m[1], 16), op: m[2], ops: (m[3] || "").trim() });
    }
    return out;
  };

  // The offsets the header CLAIMS, read out of the header. Two shapes: the
  // `+0xNN  type` table for Params, and the trailing `// +0xNN` comments on the
  // View<T> members.
  const header = readFileSync(path.join(SRC, "glitch.h"), "utf8");
  const paramsOffsets = [...header.matchAll(/^\/\/\s+\+0x([0-9a-f]+)\s+(?:int|float|BYTE)\b/gm)]
    .map((m) => parseInt(m[1], 16)).sort((a, b) => a - b);
  const viewOffsets = [...header.matchAll(
    /^\s+(?:T\*|int|long)\s+\w+\s*=\s*[^;]+;\s*\/\/ \+0x([0-9a-f]+)/gm)]
    .map((m) => parseInt(m[1], 16)).sort((a, b) => a - b);

  assert.deepEqual(paramsOffsets, [0x0, 0x4, 0x8, 0xc, 0x10, 0x14, 0x18, 0x1c, 0x20],
    "the header must document all nine Params offsets");
  assert.deepEqual(viewOffsets, [0x0, 0x8, 0xc, 0x10],
    "the header's View<T> member comments must state the four offsets");

  // A small interpreter for the arm64 subset the two `apply` bodies use.
  //
  // The naive version of this check — "grep for [x0, #imm]" — gets the 16-bit
  // build WRONG, because that build SPILLS both view pointers at the top of its
  // prologue (`stp x1, x0, [sp, #0x18]`) and then reads the strides through the
  // reloaded pointers, and because it reads width and height with ONE paired
  // load (`ldp w9, w28, [x0, #0x8]`) that a `[reg, #offset]` grep sees as only
  // "+0x8". So this tracks: which register holds which view, the stack slots
  // that views are spilled into, the paired-load stride (4 for `w` registers,
  // 8 for `x` registers), and the registers a view pointer is MOVED into.
  const NO_DST = new Set(["cmp", "cmn", "fcmp", "fcmpe", "tst", "ret", "nop",
                          "br", "blr", "ccmp", "mrs", "msr"]);
  const parseImm = (s) => {
    if (s === undefined) return 0;
    const t = s.replace("#", "");
    return /^0x/i.test(t) ? parseInt(t, 16) : parseInt(t, 10);
  };
  const viewAccess = (body) => {
    const regView = new Map([["x0", "in"], ["x1", "out"]]);
    const slot = new Map();
    const acc = { in: new Set(), out: new Set() };
    for (const { op, ops } of body) {
      const mem = ops.match(/\[(\w+)(?:,\s*#(-?0x[0-9a-f]+|-?\d+))?\]/);
      const base = mem ? mem[1] : null;
      const off = mem ? parseImm(mem[2]) : 0;
      const memIdx = ops.indexOf("[");
      const regPart = memIdx >= 0 ? ops.slice(0, memIdx) : ops;
      const regs = regPart.split(",").map((s) => s.trim()).filter((s) => /^[wx]\d+$/.test(s));
      const viewOf = (r) => regView.get(r);

      if (op === "ldp" || op === "stp") {
        const stride = regs[0] && regs[0][0] === "w" ? 4 : 8;
        if (base === "sp") {
          if (op === "stp") {
            slot.set(off, viewOf(regs[0]));
            slot.set(off + stride, viewOf(regs[1]));
          } else {
            regView.set(regs[0], slot.get(off));
            regView.set(regs[1], slot.get(off + stride));
          }
        } else {
          const v = viewOf(base);
          if (v) { acc[v].add(off); acc[v].add(off + stride); }
          for (const r of regs) regView.delete(r);
        }
        continue;
      }
      if (/^ldr/.test(op)) {
        const rd = regs[0];
        if (!rd) continue;
        if (base === "sp") {
          const v = slot.get(off);
          if (v) regView.set(rd, v); else regView.delete(rd);
        } else {
          const v = viewOf(base);
          if (v) acc[v].add(off);
          regView.delete(rd);
        }
        continue;
      }
      if (/^str/.test(op)) {
        const v = viewOf(base);
        if (v) acc[v].add(off);
        continue;
      }
      if (op === "mov") {
        if (regs.length >= 2) regView.set(regs[0], viewOf(regs[1]));
        else if (regs.length === 1) regView.delete(regs[0]);
        continue;
      }
      if (NO_DST.has(op)) continue;
      if (/^b/.test(op) || /^cb/.test(op) || /^tb/.test(op)) continue;
      if (regs.length) regView.delete(regs[0]);
    }
    return {
      in: [...acc.in].sort((a, b) => a - b),
      out: [...acc.out].sort((a, b) => a - b),
    };
  };

  // The Params offsets: every `[x2, #imm]` in the WHOLE body. x2 starts life as
  // the &Params pointer; the 8-bit build later repurposes it as an output
  // cursor, but it never reads a parameter through it afterwards, so the whole
  // body is the right window. Both instantiations must yield the same nine.
  const paramsOffsetsIn = (body) => {
    const seen = new Set();
    for (const b of body) {
      const m = b.ops.match(/\[x2(?:,\s*#(-?0x[0-9a-f]+|-?\d+))?\]/);
      if (m) seen.add(m[1] ? parseImm(m[1]) : 0);
    }
    return [...seen].sort((a, b) => a - b);
  };

  for (const [fn, want] of Object.entries(STATE)) {
    const tag = want.tag;
    if (!tag) continue;
    const sym = `__ZN6glitch5applyI${tag}EEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEjf`;
    const body = sliceFor(sym);
    assert.ok(body.length > 1000,
      `${fn}: expected a long instantiation, saw ${body.length} instructions`);
    assert.equal(body.length, want.shipped,
      `${fn}: the listing slice holds ${body.length} instructions; the differ says the `
      + `bundle has ${want.shipped}`);

    // View<T> — NEITHER view is ever accessed at an offset outside the
    // documented layout, and BOTH instantiations read the input at all four
    // offsets and the output at the data pointer and the stride.
    const access = viewAccess(body);
    assert.deepEqual(access.in, viewOffsets,
      `${fn}: the input view is read at ${access.in.map((o) => "0x" + o.toString(16))}, `
      + `not the offsets the header documents`);
    assert.deepEqual(access.out, [0x0, 0x10],
      `${fn}: the output view is read at ${access.out.map((o) => "0x" + o.toString(16))}; `
      + "it should be read only at the data pointer and the stride — its width and "
      + "height are never read");

    // Params — exactly the offsets the HEADER documents.
    assert.deepEqual(paramsOffsetsIn(body), paramsOffsets,
      `${fn}: the Params offsets disagree with the header, which documents `
      + `[${paramsOffsets.map((o) => "0x" + o.toString(16)).join(", ")}]`);

    // The mode dispatch: five one-based bodies plus a catch-all, and NOT
    // address-ordered.
    const text = body.map((b) => `${b.op} ${b.ops}`.trim()).join("\n");
    for (const n of [1, 2, 3, 4, 5]) {
      assert.match(text, new RegExp(`cmp\\s+w\\d+, #(?:0x)?${n}\\b`),
        `${fn}: the dispatch must test mode ${n}`);
    }
    // The luma flag is a BYTE, so it is read with ldrb.
    assert.match(text, /ldrb\s+w\d+, \[x2, #0x1c\]/,
      `${fn}: the +0x1c field must be read as a BYTE`);
  }
});
