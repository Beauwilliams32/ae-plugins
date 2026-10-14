// Gate for tools/param-table.mjs.
//
// The tool decodes PF_ADD_PARAM blocks out of EffectMain, which is where parameter
// types, IDs, names and defaults live — the PiPL does not carry them. Its output
// therefore feeds straight into rebuilding an effect, so a wrong decode is a wrong
// plugin rather than a wrong footnote.
//
// What gives this test its teeth is that LUTBlend's six parameters were established
// BY HAND from the disassembly in an earlier round, before the tool existed. The
// tool must reproduce that table exactly. That is a genuine independent check
// rather than the tool agreeing with itself.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");
const TOOL = path.join(ROOT, "tools", "param-table.mjs");

function fleetInstalled() {
  return existsSync("/Applications/Adobe After Effects 2026/Plug-ins/Williams Media");
}

function decode(plugin) {
  // Passing an empty name would be treated as a plugin to look up rather than as
  // "all of them", so the argument is omitted entirely.
  const args = plugin ? [TOOL, plugin, "--json"] : [TOOL, "--json"];
  const out = execFileSync("node", args, { encoding: "utf8", maxBuffer: 32 * 1024 * 1024 });
  return JSON.parse(out);
}

test("LUTBlend's decoded parameter table matches the hand-derived one", (t) => {
  if (!fleetInstalled()) { t.skip("fleet not installed"); return; }

  const table = decode("LUTBlend").LUTBlend;
  assert.ok(Array.isArray(table) && table.length === 6,
    `expected 6 parameters, got ${table?.length}`);

  // Values taken from the disassembly by hand in an earlier round, and recorded in
  // plugins/LUTBlend/LUTBlendParams.h and src/lutblend/RECOVERY-NOTES.md.
  const expected = [
    { id: 1, type: "POPUP",        name: "LUT A" },
    { id: 2, type: "POPUP",        name: "LUT B" },
    { id: 3, type: "FLOAT_SLIDER", name: "Blend (%)" },
    { id: 4, type: "POPUP",        name: "Drive" },
    { id: 5, type: "FLOAT_SLIDER", name: "Strength (%)" },
    { id: 6, type: "CHECKBOX",     name: "Preserve Luma" },
  ];
  for (let i = 0; i < expected.length; i++) {
    assert.equal(table[i].id, expected[i].id, `parameter ${i + 1} id`);
    assert.equal(table[i].type, expected[i].type, `parameter ${i + 1} type`);
    assert.equal(table[i].name, expected[i].name, `parameter ${i + 1} name`);
  }

  // The recovered slider bounds: valid_max and slider_max are both 100.0f, which is
  // 0x42c80000. Asserting the raw bit pattern keeps the check exact.
  const blend = table[2];
  const bound = Object.values(blend.union).filter((v) => v === "0x42c80000");
  assert.ok(bound.length >= 2,
    `Blend (%) should carry two 100.0f bounds (valid_max, slider_max), got ${JSON.stringify(blend.union)}`);
});

test("every effect in the fleet yields a parameter table", (t) => {
  if (!fleetInstalled()) { t.skip("fleet not installed"); return; }

  const all = decode("");
  const plugins = Object.keys(all);
  assert.equal(plugins.length, 16, `expected 16 effects, got ${plugins.length}`);

  let total = 0, named = 0, nameable = 0;
  for (const [plugin, params] of Object.entries(all)) {
    assert.ok(params.length > 0, `${plugin} decoded zero parameters`);
    for (const p of params) {
      ++total;
      // The TYPE must always decode. A "?" here means the param_type store was not
      // found at all, which would put the whole block's offset table in doubt.
      // A "?" type means the param_type field was never established, which puts the
      // whole block in doubt. It used to fire for every layer parameter in the fleet,
      // because PF_Param_LAYER is 0 and a layer handler never STORES its type — the
      // opening SIMD clear sets it, so the clear has to be read as a store of zero.
      assert.notEqual(p.type, "?", `${plugin}: parameter ${p.id} has an unknown type`);

      // GROUP_START / GROUP_END genuinely carry no name — the handler stores only
      // the id and the type — so an empty name is correct for them and a sign of a
      // decode gap for everything else.
      if (p.type === "GROUP_START" || p.type === "GROUP_END") continue;
      ++nameable;
      // A name carrying the doubled-word signature of a botched overlap MUST be
      // flagged. The flagging is a heuristic, so the assertion is one-directional:
      // every suspect name is marked, and no trusted name has the signature.
      const words = (p.name || "").match(/[A-Za-z]{4,}/g) ?? [];
      let doubled = false;
      for (let i = 0; i + 1 < words.length; i++) {
        if (words[i].toLowerCase() === words[i + 1].toLowerCase()) doubled = true;
      }
      assert.equal(!!p.nameSuspect, doubled,
        `${plugin}: parameter ${p.id} suspect flag disagrees with its name ${JSON.stringify(p.name)}`);

      if (p.name) {
        ++named;
        assert.ok(!/[\x00-\x1f]/.test(p.name),
          `${plugin}: parameter ${p.id} name has control characters: ${JSON.stringify(p.name)}`);
        // A name that is actually a popup CHOICE STRING is the specific failure
        // mode of the annotation fallback that was tried and reverted, so it is
        // pinned here: no parameter name in this fleet contains a pipe.
        assert.ok(!p.name.includes("|"),
          `${plugin}: parameter ${p.id} name looks like a popup choice string: ${p.name}`);
      }
    }
  }

  assert.ok(total >= 150, `only ${total} parameters decoded across the fleet`);

  // NAMES ARE NOT FULLY SOLVED, and the floor records that honestly rather than
  // asserting a completeness the tool does not have. Five store shapes have been
  // handled so far (immediate, 4-byte, halfword, 64-bit literal, 128-bit SIMD); the
  // residue is mostly long FLOAT_SLIDER names written as two overlapping literal
  // loads. Every one of them decodes its id, type and bounds correctly — only the
  // name is missing — so a rebuild would still know what the parameter IS.
  const pct = (named / nameable) * 100;
  assert.ok(pct >= 94,
    `only ${named}/${nameable} nameable parameters decoded a name (${pct.toFixed(1)}%)`);
});
