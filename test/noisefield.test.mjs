// Gate for the recovered noisefield:: namespace.
//
// The three instruction-identical functions are checked by the self-check. The
// GRADIENT TABLE is checked against the bundle's own bytes, because a table cannot be
// verified by inspection and a wrong gradient set is a different effect with the same
// function names. The four BEHAVIOURAL recoveries -- fieldAt, curlField and the two
// apply<T> instantiations -- are checked against the SHIPPED BUNDLE itself, byte for
// byte, by the differential harness in src/noisefield/differential.cpp.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { copyFileSync, existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");
const SRC = path.join(ROOT, "src", "noisefield");
const BUNDLE = "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media"
  + "/NoiseField.plugin/Contents/MacOS/NoiseField";

/**
 * Every `noisefield::` symbol the bundle ships, named the way
 * `tools/codegen-diff.mjs` prints it.
 *
 * Two of these names are the differ's, not the source's: `noisefield::PermTable` is
 * `PermTable::build` (the mangled symbol carries no method name the differ prints),
 * and `noisefield::_GLOBAL__N_1` is the anonymous-namespace `curlField`. They are
 * listed as the differ prints them so the assertion is against the measurement
 * rather than against a tidier list.
 */
const RECOVERED = [
  "clampf",
  "clampi",
  "fbmAmplitudeSum",
  "PermTable",
  "noise3",
  "fbm3",
  "fieldAt",
  "_GLOBAL__N_1",
  "apply(IhEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEf)",
  "apply(ItEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEf)",
];

/**
 * Nothing: the namespace is 10 of 10.
 *
 * The list is kept, empty rather than deleted, so that a future regression has
 * somewhere to be written down -- and so the assertion below stays a two-way check
 * between this file and the differ rather than a one-way "at least these".
 */
const OUTSTANDING = [];

function bundlesInstalled() {
  return existsSync(BUNDLE);
}

function haveClang() {
  try { execFileSync("clang++", ["--version"], { stdio: "ignore" }); return true; }
  catch { return false; }
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

/**
 * Build the differential harness against a directory holding noisefield.h,
 * noisefield.cpp and differential.cpp, and run it.
 *
 * `-fvisibility=hidden` matters: without it the recovered `noisefield::apply` could
 * interpose the shipped one inside the loader-mapped bundle, and the comparison
 * would be the recovered code compared with itself.
 */
function buildAndRunHarness(dir) {
  const bin = path.join(dir, "differential");
  execFileSync("clang++", [
    "-std=c++17", "-O2", "-fvisibility=hidden",
    "-I", dir,
    "-o", bin,
    path.join(dir, "differential.cpp"),
    path.join(dir, "noisefield.cpp"),
  ], { stdio: "pipe" });

  let status = 0;
  let stdout = "";
  try {
    stdout = execFileSync(bin, { encoding: "utf8", stdio: "pipe" });
  } catch (e) {
    status = e.status ?? 1;
    stdout = `${e.stdout ?? ""}${e.stderr ?? ""}`;
  }
  const field = (name) => {
    const m = stdout.match(new RegExp(`^${name} (-?\\d+)$`, "m"));
    return m ? Number(m[1]) : null;
  };
  return {
    status,
    stdout,
    scalars: field("SCALAR_MISMATCHES"),
    u8Vectors: field("APPLY_U8_VECTORS"),
    u8Mismatches: field("APPLY_U8_MISMATCH_VECTORS"),
    u8Bytes: field("APPLY_U8_DIFFERING_BYTES"),
    u16Vectors: field("APPLY_U16_VECTORS"),
    u16Mismatches: field("APPLY_U16_MISMATCH_VECTORS"),
    u16Bytes: field("APPLY_U16_DIFFERING_BYTES"),
    vectors: field("VECTORS"),
    mismatchVectors: field("MISMATCH_VECTORS"),
    differingBytes: field("DIFFERING_BYTES"),
  };
}

/** Copy the harness sources into a scratch directory, so a copy can be perturbed. */
function stageSources(dir) {
  for (const f of ["noisefield.h", "noisefield.cpp", "differential.cpp"]) {
    copyFileSync(path.join(SRC, f), path.join(dir, f));
  }
}

test("recovered noisefield source compiles and its maths verifies", (t) => {
  if (!haveClang()) { t.skip("clang++ not available — compile check skipped"); return; }

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
    assert.ok(passes >= 44, `expected >= 44 checks, saw ${passes}`);
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
  const recovered = [...out.matchAll(/^\s{2}(?:MATCH|DIFFER)\s+noisefield::(\S+)/gm)]
    .map((m) => m[1]).sort();
  const absent = [...out.matchAll(/^\s+absent\s+noisefield::(\S+)/gm)].map((m) => m[1]);

  assert.deepEqual(recovered, [...RECOVERED].sort(),
    "the differ recovered a different set of functions than this file claims");
  assert.equal(absent.length, OUTSTANDING.length,
    `the differ reports ${absent.length} unrecovered symbols, this file lists ${OUTSTANDING.length}`);
  for (const frag of OUTSTANDING) {
    assert.ok(absent.some((a) => a.startsWith(frag)),
      `expected an unrecovered symbol starting "${frag}" — got ${absent.join(", ")}`);
  }

  // Three are instruction-identical, and that is a stronger claim than "recovered", so
  // the three are named rather than left to the tally.
  for (const fn of ["clampf", "clampi", "fbmAmplitudeSum"]) {
    assert.match(out, new RegExp(`MATCH\\s+noisefield::${fn}\\s+\\d+ instructions, identical`),
      `${fn} is no longer instruction-identical to the bundle`);
  }

  // `noise3` and `fbm3` compile to the right instruction COUNT (183 and 51) with a
  // different register schedule, and `PermTable::build` differs in how clang expands
  // the 256-byte copy. Their behaviour is verified against the textbook oracle in the
  // self-check instead, which is the stronger test for this kind of code — so the
  // counts are pinned here so that a schedule change is visible rather than silent.
  assert.match(out, /DIFFER\s+noisefield::noise3\s+shipped=183 mine=183/,
    "noise3 should compile to the same instruction count as the bundle");
  assert.match(out, /DIFFER\s+noisefield::fbm3\s+shipped=51 mine=51/,
    "fbm3 should compile to the same instruction count as the bundle");

  // The four behavioural recoveries: the SHIPPED instruction counts are pinned, because
  // a change in them means the bundle under test is not the bundle this lane measured.
  assert.match(out, /DIFFER\s+noisefield::fieldAt\s+shipped=226 mine=226/,
    "fieldAt should compile to the same instruction count as the bundle");
  assert.match(out, /DIFFER\s+noisefield::_GLOBAL__N_1\s+shipped=163 mine=163/,
    "curlField should compile to the same instruction count as the bundle");
  assert.match(out, /DIFFER\s+noisefield::apply\(IhEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEf\)\s+shipped=408/,
    "the shipped apply<unsigned char> is 408 instructions");
  assert.match(out, /DIFFER\s+noisefield::apply\(ItEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEf\)\s+shipped=412/,
    "the shipped apply<unsigned short> is 412 instructions");

  const summary = out.trim().split("\n").pop();
  assert.match(summary, /3 identical, 7 differing, 0 not implemented/,
    `the differ's tally moved: ${summary}`);
});

test("the recovered renderer is bit-identical to the shipped bundle", (t) => {
  if (!bundlesInstalled()) { t.skip("NoiseField bundle is not installed"); return; }
  if (!haveClang()) { t.skip("clang++ not available — differential skipped"); return; }

  const dir = mkdtempSync(path.join(tmpdir(), "noisefield-diff-"));
  try {
    stageSources(dir);
    const r = buildAndRunHarness(dir);

    assert.equal(r.status, 0,
      `the differential harness reported differences:\n${r.stdout}`);
    assert.equal(r.scalars, 0,
      `the scalar helpers disagree with the shipped bundle:\n${r.stdout}`);
    assert.equal(r.mismatchVectors, 0,
      `${r.mismatchVectors} render vectors disagreed with the shipped bundle:\n${r.stdout}`);
    assert.equal(r.differingBytes, 0,
      `${r.differingBytes} destination bytes disagreed with the shipped bundle:\n${r.stdout}`);

    // Both instantiations are measured separately, and both are measured at scale:
    // a harness that quietly only exercised one of them would still report a clean
    // total.
    assert.ok(r.u8Vectors >= 20000, `only ${r.u8Vectors} apply<unsigned char> vectors`);
    assert.ok(r.u16Vectors >= 20000, `only ${r.u16Vectors} apply<unsigned short> vectors`);
    assert.equal(r.u8Mismatches, 0, `apply<unsigned char> mismatched:\n${r.stdout}`);
    assert.equal(r.u16Mismatches, 0, `apply<unsigned short> mismatched:\n${r.stdout}`);
    assert.equal(r.u8Bytes, 0, `apply<unsigned char> bytes differed:\n${r.stdout}`);
    assert.equal(r.u16Bytes, 0, `apply<unsigned short> bytes differed:\n${r.stdout}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("the differential harness can fail — a perturbed constant is caught", (t) => {
  if (!bundlesInstalled()) { t.skip("NoiseField bundle is not installed"); return; }
  if (!haveClang()) { t.skip("clang++ not available — mutation check skipped"); return; }

  // The REAL source is never touched: the perturbation is applied to a COPY, and the
  // copy is what gets compiled. The source is read before and after to prove it.
  const before = readFileSync(path.join(SRC, "noisefield.cpp"), "utf8");

  const dir = mkdtempSync(path.join(tmpdir(), "noisefield-mut-"));
  try {
    stageSources(dir);
    const cppPath = path.join(dir, "noisefield.cpp");
    const src = readFileSync(cppPath, "utf8");
    // One constant in `curlField`: the divisor the X component of the curl is scaled
    // by. Changing it by one part in ten thousand must change the rendered pixels.
    const target = "  out.x = (s1 - s2) / 0.02f;";
    assert.equal(src.split(target).length - 1, 1,
      "the perturbation target must appear exactly once in the source");
    writeFileSync(cppPath, src.replace(target, "  out.x = (s1 - s2) / 0.0201f;"));

    const r = buildAndRunHarness(dir);

    assert.notEqual(r.status, 0,
      "the harness passed a build with a perturbed constant — it cannot detect errors");
    assert.ok(r.mismatchVectors > 0,
      `the perturbed build reported no mismatching vectors:\n${r.stdout}`);
    assert.ok(r.differingBytes > 0,
      `the perturbed build reported no differing bytes:\n${r.stdout}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
    assert.equal(readFileSync(path.join(SRC, "noisefield.cpp"), "utf8"), before,
      "the real source was modified by the mutation test");
  }
});
