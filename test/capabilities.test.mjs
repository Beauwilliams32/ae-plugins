// Gate for CAPABILITIES.md and the flag table behind it.
//
// Two things are checked, and the first is the one that matters:
//
//   1. The flag values hard-coded in tools/decode-flags.mjs are re-derived from
//      the AE SDK's own AE_Effect.h and compared. The whole point of that table is
//      to name the bits in the recovered eGLO/eGL2 atoms, so a wrong entry would
//      silently mislabel an effect's obligations. Skipped without an SDK.
//
//   2. CAPABILITIES.md is reproducible from the specs, so it cannot drift from the
//      data it claims to summarise.
//
// It also asserts the fleet-wide claims the document makes, so a regenerated file
// that quietly loses a flag fails here rather than in a reader's head.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, readFileSync, readdirSync } from "node:fs";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");

function findSdk() {
  const roots = [];
  if (process.env.AE_SDK_DIR) roots.push(process.env.AE_SDK_DIR);
  const base = path.join(process.env.HOME ?? "", ".ae-sdk");
  if (existsSync(base)) {
    for (const entry of readdirSync(base)) {
      const p = path.join(base, entry);
      roots.push(p, path.join(p, "Examples"));
      try {
        for (const inner of readdirSync(p)) roots.push(path.join(p, inner, "Examples"));
      } catch { /* unreadable — skip */ }
    }
  }
  for (const r of roots) {
    try {
      if (existsSync(path.join(r, "Headers", "AE_Effect.h"))) return r;
    } catch { /* keep looking */ }
  }
  return null;
}

// Pull `NAME = 1 << n` (and `1L << n`) definitions out of the SDK header.
function sdkFlags(header, prefix) {
  const out = new Map();
  const re = new RegExp(`(${prefix}[A-Z0-9_]+)\\s*=\\s*1L?\\s*<<\\s*(\\d+)`, "g");
  for (const m of header.matchAll(re)) out.set(m[1], 1 << Number(m[2]));
  return out;
}

// The table as the generator actually declares it.
function toolFlags(source, constName) {
  const body = source.split(`const ${constName} = {`)[1]?.split("};")[0];
  assert.ok(body, `could not find ${constName} in decode-flags.mjs`);
  const out = new Map();
  for (const m of body.matchAll(/([A-Za-z0-9_]+)\s*:\s*1\s*<<\s*(\d+)/g)) {
    out.set(m[1], 1 << Number(m[2]));
  }
  return out;
}

test("the flag table matches the AE SDK's own header", (t) => {
  const sdk = findSdk();
  if (!sdk) {
    t.skip("AE SDK not found — set AE_SDK_DIR to <SDK>/Examples to verify the flag table");
    return;
  }
  const header = readFileSync(path.join(sdk, "Headers", "AE_Effect.h"), "utf8");
  const tool = readFileSync(path.join(ROOT, "tools", "decode-flags.mjs"), "utf8");

  for (const [constName, prefix] of [["OUT_FLAGS", "PF_OutFlag_"],
                                     ["OUT_FLAGS2", "PF_OutFlag2_"]]) {
    const want = sdkFlags(header, prefix);
    const got = toolFlags(tool, constName);
    assert.ok(want.size > 0, `no ${prefix} definitions found in AE_Effect.h`);
    assert.ok(got.size > 0, `no ${constName} entries found in decode-flags.mjs`);

    // Every entry the generator names must carry the SDK's value. A missing entry
    // is fine — the table deliberately covers only what the fleet uses — but a
    // WRONG value is not, because it would mislabel a bit.
    for (const [name, value] of got) {
      if (!want.has(name)) continue; // not in this SDK version; reported below
      assert.equal(value, want.get(name),
        `${name} is 1<<${Math.log2(value)} in decode-flags.mjs but `
        + `0x${want.get(name).toString(16)} in the SDK header`);
    }
  }
});

test("CAPABILITIES.md is reproducible from the specs", () => {
  const before = readFileSync(path.join(ROOT, "CAPABILITIES.md"), "utf8");
  execFileSync("node", [path.join(ROOT, "tools", "decode-flags.mjs")], {
    cwd: ROOT, stdio: "pipe",
  });
  const after = readFileSync(path.join(ROOT, "CAPABILITIES.md"), "utf8");
  assert.equal(after, before, "CAPABILITIES.md changed on re-generation");
});

test("the fleet-wide obligations the document claims are actually present", () => {
  const doc = readFileSync(path.join(ROOT, "CAPABILITIES.md"), "utf8");
  const specs = readdirSync(path.join(ROOT, "specs"))
    .filter((f) => f.endsWith(".json") && f !== "index.json")
    .map((f) => JSON.parse(readFileSync(path.join(ROOT, "specs", f), "utf8")));

  assert.equal(specs.length, 16, "the fleet is 16 effects");

  // Derived here independently of the generator, so this is a real cross-check.
  const DEEP = 1 << 25;
  const FLOAT = 1 << 12;
  const SMART = 1 << 10;
  const THREADED = 1 << 27;

  const deep = specs.filter((s) => (Number(s.pipl.eGLO) & DEEP) !== 0);
  const floaty = specs.filter((s) => (Number(s.pipl.eGL2) & FLOAT) !== 0);
  const smart = specs.filter((s) => (Number(s.pipl.eGL2) & SMART) !== 0);
  const threaded = specs.filter((s) => (Number(s.pipl.eGL2) & THREADED) !== 0);

  assert.equal(deep.length, 16, "every effect is DEEP_COLOR_AWARE");
  assert.equal(threaded.length, 16, "every effect declares threaded rendering");
  assert.equal(floaty.length, 4, "four effects claim float capability");
  assert.equal(smart.length, 4, "four effects claim smart render");

  // Float and smart must coincide, or the document's two lists would disagree.
  assert.deepEqual(floaty.map((s) => s.plugin).sort(), smart.map((s) => s.plugin).sort(),
    "the float and smart-render sets differ, which the document does not allow for");

  // The document must not contradict the data it summarises.
  for (const s of floaty) {
    assert.match(doc, new RegExp(s.plugin), `${s.plugin} is float-capable but absent from the document`);
  }
  assert.doesNotMatch(doc, /leaves 0x[0-9a-f]+ unclaimed/,
    "some bit in a recovered atom is not named by the flag table");

  // The obligation that a rebuild must honour most carefully.
  assert.equal(deep.length, 16,
    "DEEP_COLOR_AWARE on all 16 means every rebuilt effect needs a 16-bit path");
});

test("LUTBlend's declared flags match its own recovered atoms", () => {
  // The concrete instance of the general rule. LUTBlend shipped with three of
  // these invented, so its own numbers are pinned here as well as fleet-wide.
  const header = readFileSync(
    path.join(ROOT, "plugins", "LUTBlend", "LUTBlendParams.h"), "utf8");
  const spec = JSON.parse(
    readFileSync(path.join(ROOT, "specs", "LUTBlend.json"), "utf8"));

  const grab = (name) => {
    const m = header.match(new RegExp(`${name}\\s+(0x[0-9a-fA-F]+)`));
    return m ? Number(m[1]) : null;
  };
  assert.equal(grab("LUTBLEND_GLOBAL_OUT_FLAGS"), Number(spec.pipl.eGLO));
  assert.equal(grab("LUTBLEND_GLOBAL_OUT_FLAGS2"), Number(spec.pipl.eGL2));
});
