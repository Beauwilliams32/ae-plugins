// Gate for the recovered noisefield:: namespace.
//
// The three functions are checked by the self-check. The GRADIENT TABLE is checked
// against the bundle's own bytes, because a table cannot be verified by inspection and
// a wrong gradient set is a different effect with the same function names.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");
const SRC = path.join(ROOT, "src", "noisefield");
const BUNDLE = "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media"
  + "/NoiseField.plugin/Contents/MacOS/NoiseField";

/** The functions transcribed here. */
const RECOVERED = ["clampf", "clampi", "fbmAmplitudeSum"];

/**
 * The rest, as `tools/codegen-diff.mjs` names them.
 *
 * Two of these names are the differ's, not the source's: `noisefield::PermTable` is
 * `PermTable::build`, and `noisefield::_GLOBAL__N_1` is the anonymous-namespace
 * `curlField`. They are listed as the differ prints them so the assertion is against
 * the measurement rather than against a tidier list.
 */
const OUTSTANDING = [
  "PermTable", "noise3", "fbm3", "fieldAt", "_GLOBAL__N_1",
  "apply(IhEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEf)",
  "apply(ItEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEf)",
];

function bundlesInstalled() {
  return existsSync(BUNDLE);
}

function runDiff(fragment) {
  const args = [path.join(ROOT, "tools", "codegen-diff.mjs"), "noisefield"];
  if (fragment !== undefined) args.push(fragment);
  try {
    return execFileSync("node", args, { cwd: ROOT, encoding: "utf8", stdio: "pipe" });
  } catch (e) {
    return `${e.stdout ?? ""}${e.stderr ?? ""}`;
  }
}

test("recovered noisefield source compiles and its maths verifies", (t) => {
  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) { t.skip("clang++ not available — compile check skipped"); return; }

  const dir = mkdtempSync(path.join(tmpdir(), "noisefield-check-"));
  const bin = path.join(dir, "selfcheck");
  try {
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
      "-o", bin,
      path.join(SRC, "noisefield.cpp"),
      path.join(SRC, "selfcheck.cpp"),
    ], { stdio: "pipe" });

    const out = execFileSync(bin, { encoding: "utf8" });
    assert.match(out, /ALL RECOVERED NOISEFIELD MATHS VERIFIED/, `self-check failed:\n${out}`);
    assert.doesNotMatch(out, /\bFAIL\b/, out);
    const passes = (out.match(/^  PASS/gm) ?? []).length;
    assert.ok(passes >= 24, `expected >= 24 checks, saw ${passes}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("the gradient table in the source is the one in the bundle", (t) => {
  if (!bundlesInstalled()) { t.skip("NoiseField bundle is not installed"); return; }

  // Where the symbol is, from the arm64 slice's own symbol table.
  const symLine = execFileSync("nm", ["-S", "-arch", "arm64", BUNDLE], { encoding: "utf8" })
    .split("\n").find((l) => l.includes("6kGrad3E"));
  assert.ok(symLine, "kGrad3 is not in the bundle's symbol table");
  const symAddr = parseInt(symLine.trim().split(/\s+/)[0], 16);

  // What is AT that address. `otool -s` prints the section's words with their
  // addresses, which avoids having to map a virtual address through a fat binary's
  // slice offsets — an earlier attempt at that arithmetic read 40 zero floats from
  // the wrong slice and looked like an empty table.
  const dump = execFileSync("otool", ["-s", "__TEXT", "__const", "-arch", "arm64", BUNDLE],
    { encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });
  const words = new Map();
  for (const line of dump.split("\n").slice(1)) {
    const parts = line.split("\t");
    if (parts.length < 2) continue;
    const base = Number.parseInt(parts[0], 16);
    if (Number.isNaN(base)) continue;
    parts[1].trim().split(/\s+/).forEach((w, i) => words.set(base + i * 4, Number.parseInt(w, 16)));
  }
  assert.ok(words.size > 0, "could not read the __const section");

  const signed = (v) => (v & 0x80000000 ? v - 0x100000000 : v);
  const fromBundle = [];
  for (let i = 0; i < 12; i++) {
    const triple = [];
    for (let j = 0; j < 3; j++) {
      const addr = symAddr + (i * 3 + j) * 4;
      assert.ok(words.has(addr), `no word at ${addr.toString(16)} — the table runs past __const`);
      triple.push(signed(words.get(addr)));
    }
    fromBundle.push(triple);
  }

  // And what the source says, read out of it rather than duplicated here: the source
  // is the thing under test, so the expectation has to come from the bundle.
  const source = readFileSync(path.join(SRC, "noisefield.cpp"), "utf8");
  const block = source.slice(source.indexOf("const int kGrad3[12][3] = {"));
  const fromSource = [...block.slice(0, block.indexOf("};")).matchAll(/\{(-?\d+),\s*(-?\d+),\s*(-?\d+)\}/g)]
    .map((m) => [Number(m[1]), Number(m[2]), Number(m[3])]);

  assert.equal(fromSource.length, 12, "the source should declare twelve gradients");
  assert.deepEqual(fromSource, fromBundle,
    "the recovered gradient table does not match the bytes in the bundle");

  // The property that makes them the improved-noise edges, asserted on the BUNDLE's
  // copy rather than on the source's, so a wrong table cannot pass by being
  // self-consistent.
  for (const [x, y, z] of fromBundle) {
    assert.equal(x * x + y * y + z * z, 2, `(${x},${y},${z}) is not a unit edge midpoint`);
  }
});

test("the recovered and outstanding sets are exactly the ones this file lists", (t) => {
  if (!bundlesInstalled()) { t.skip("NoiseField bundle is not installed"); return; }

  const out = runDiff();
  const recovered = [...out.matchAll(/^\s{2}(?:MATCH|DIFFER)\s+noisefield::(\w+)/gm)].map((m) => m[1]).sort();
  const absent = [...out.matchAll(/^\s+absent\s+noisefield::(\S+)/gm)].map((m) => m[1]);

  assert.deepEqual(recovered, [...RECOVERED].sort(),
    "the differ recovered a different set of functions than this file claims");
  assert.equal(absent.length, OUTSTANDING.length,
    `the differ reports ${absent.length} unrecovered symbols, this file lists ${OUTSTANDING.length}`);
  for (const frag of OUTSTANDING) {
    assert.ok(absent.some((a) => a.startsWith(frag)),
      `expected an unrecovered symbol starting "${frag}" — got ${absent.join(", ")}`);
  }

  // All three are instruction-identical. That is a stronger claim than "recovered", so
  // it is asserted rather than left to the tally.
  for (const fn of RECOVERED) {
    assert.match(out, new RegExp(`MATCH\\s+noisefield::${fn}\\s+\\d+ instructions, identical`),
      `${fn} is no longer instruction-identical to the bundle`);
  }
  const summary = out.trim().split("\n").pop();
  assert.match(summary, /3 identical, 0 differing, 7 not implemented/,
    `the differ's tally moved: ${summary}`);
});
