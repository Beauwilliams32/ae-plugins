// Gate for the recovered glitch:: namespace.
//
// Compiles src/glitch/ and runs its internal property checks. The assertions
// inside selfcheck.cpp are anchored to what the disassembly establishes (the low
// bound winning on an inverted range, the double-precision multiply, the strict
// `> 0` guard), so this is behavioural verification to sit alongside the
// instruction-identical check in test/codegen.test.mjs.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");
const SRC = path.join(ROOT, "src", "glitch");

test("recovered glitch source compiles and its maths verifies", (t) => {
  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) { t.skip("clang++ not available — compile check skipped"); return; }

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
    assert.match(out, /ALL RECOVERED GLITCH MATHS VERIFIED/, `self-check failed:\n${out}`);
    assert.doesNotMatch(out, /\bFAIL\b/, out);
    const passes = (out.match(/^  PASS/gm) ?? []).length;
    assert.ok(passes >= 25, `expected >= 25 checks, saw ${passes}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("the recovered View and Params offsets match the shipped bundle", (t) => {
  // These layouts are CLAIMED in src/glitch/glitch.h, and a claim about a struct's field
  // offsets is the kind that goes stale silently. So the offsets are re-derived here from
  // the bundle's own instructions rather than trusted.
  //
  // The `Params` set is taken ONLY from the prologue, up to the first write to x2: after
  // the mode dispatch `apply<T>` reuses x2 as an output cursor, so every later `[x2, #N]`
  // is reading PIXELS, not parameters. Getting that boundary wrong would have produced a
  // layout with phantom fields, which is why the header states the limit and this test
  // enforces the same one.
  const bundle = "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media"
    + "/GlitchKit.plugin/Contents/MacOS/GlitchKit";
  if (!existsSync(bundle)) { t.skip("GlitchKit bundle is not installed"); return; }

  const asm = execFileSync("otool", ["-tV", "-arch", "arm64", bundle],
                           { encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });
  const lines = asm.split("\n");

  // The offsets the header CLAIMS, read out of the header. Two shapes: the `+0xNN  type`
  // table for Params, and the trailing `// +0xNN` comments on the View<T> members.
  const header = readFileSync(path.join(SRC, "glitch.h"), "utf8");
  const paramsOffsets = [...header.matchAll(/^\/\/\s+\+0x([0-9a-f]+)\s+(?:int|float|BYTE)\b/gm)]
    .map((m) => parseInt(m[1], 16)).sort((a, b) => a - b);
  const viewOffsets = [...header.matchAll(/^\s+(?:T\*|int|long)\s+\w+\s*=\s*[^;]+;\s*\/\/ \+0x([0-9a-f]+)/gm)]
    .map((m) => parseInt(m[1], 16)).sort((a, b) => a - b);
  const doc = { paramsOffsets, viewOffsets };

  assert.ok(doc.paramsOffsets.length >= 6,
    `the header must document its Params offsets in the +0xNN table; found `
    + `${doc.paramsOffsets.length}`);
  assert.deepEqual(doc.viewOffsets, [0x0, 0x8, 0xc, 0x10],
    "the header's View<T> member comments must state the four offsets");

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
      if (m) out.push({ addr: parseInt(m[1], 16), text: `${m[2]} ${m[3]}`.trim() });
    }
    return out;
  };

  const offsetsFor = (body, reg) => {
    const seen = new Set();
    for (let i = 0; i < body.length; i++) {
      const text = body[i].text;
      // The prologue ends where x2 stops being the parameter pointer.
      if (reg === "x2" && i > 0 && /^(ldr|mov|add)\s+x2,\s/.test(text)) break;
      const m = text.match(new RegExp(`\\[${reg}(?:, #(0x[0-9a-f]+))?\\]`));
      if (m) seen.add(m[1] ? parseInt(m[1], 16) : 0);
    }
    return [...seen].sort((a, b) => a - b);
  };

  for (const tag of ["h", "t"]) {
    const sym = `__ZN6glitch5applyI${tag}EEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEjf`;
    const body = sliceFor(sym);
    assert.ok(body.length > 1000, `${tag}: expected a long instantiation, saw ${body.length}`);

    // View<T> — the invariant asserted is that NEITHER view is ever accessed at an
    // offset outside the documented layout. Asserting "reads all four fields" instead
    // would be wrong: the two instantiations read DIFFERENT SUBSETS. The 8-bit build
    // walks the input at 0x0, 0x8, 0xc and 0x10 and the output at 0x0 and 0x10, while the
    // 16-bit build reads the input at only 0x0 and 0x8 and the output at only 0x0. The
    // shared source is what pins the layout; the access sets are a property of how each
    // instantiation happens to be scheduled, and over-asserting them would make this test
    // fail for a reason that has nothing to do with the struct.
    const inOffs = offsetsFor(body, "x0");
    const outOffs = offsetsFor(body, "x1");
    for (const off of inOffs) {
      assert.ok(doc.viewOffsets.includes(off),
        `${tag}: the input view is accessed at +0x${off.toString(16)}, outside View<T>`);
    }
    for (const off of outOffs) {
      assert.ok(doc.viewOffsets.includes(off),
        `${tag}: the output view is accessed at +0x${off.toString(16)}, outside View<T>`);
    }

    // And the shape of the layout is pinned by the 8-bit build, which reads all four.
    if (tag === "h") {
      assert.deepEqual(inOffs, doc.viewOffsets,
        "the 8-bit build must read exactly the offsets the header documents for View<T>");
      // The output's dimensions are NEVER read: the loops are driven entirely by the
      // input, so a mismatched output view is not detected and not clamped.
      assert.deepEqual(outOffs, [0x0, 0x10],
        "the output view is read only at data and rowBytes");
    }

    // Params — exactly the offsets the HEADER documents, and nothing beyond the reuse
    // point. The expected set is parsed out of the header rather than repeated here, so a
    // layout claim and this check cannot drift apart: changing the header changes what
    // this test demands.
    const params = offsetsFor(body, "x2");
    assert.deepEqual(params, doc.paramsOffsets,
      `${tag}: the Params prologue offsets disagree with the header, which documents `
      + `[${doc.paramsOffsets.map((o) => "0x" + o.toString(16)).join(", ")}]`);

    // The mode dispatch: five one-based bodies plus a catch-all, and NOT address-ordered.
    const text = body.map((b) => b.text).join("\n");
    for (const n of [1, 2, 3, 4, 5]) {
      // otool prints immediates in hex, so accept either spelling.
      assert.match(text, new RegExp(`cmp\\s+w\\d+, #(?:0x)?${n}\\b`),
        `${tag}: the dispatch must test mode ${n}`);
    }
    assert.match(text, /_strb|ldrb/, `${tag}: the +0x1c field must be read as a BYTE`);
  }
});
