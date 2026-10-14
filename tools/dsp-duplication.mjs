#!/usr/bin/env node
// Prove — or refute — that the effects share one copied DSP module.
//
// Every effect wraps its code in its own namespace (spectra::, pathscribe::,
// beatsnap::, beatgrid::, lutblend::, typeriser:: ...). The audio/analysis
// helpers appear under several of them, which looks like copy-paste. Looks are
// not evidence, so this compares the actual machine code.
//
//   node tools/dsp-duplication.mjs            # report
//   node tools/dsp-duplication.mjs --json     # machine-readable
//
// Exits non-zero if a function that exists in ≥2 effects is NOT identical
// everywhere, so drift in the copied DSP fails the gate.
//
// WHY THE NORMALISATION IS SO AGGRESSIVE — every step here exists because
// skipping it produced a WRONG "these differ" verdict during recovery:
//
//   1. addresses           0x… differ between binaries, always
//   2. adrp page numbers   a bare decimal ("adrp x8, 7" vs "6"); easy to miss
//                          because it is not 0x-prefixed
//   3. trailing "; 0x…"    the address echoed as a comment
//   4. namespace in call targets  `bl __ZN7spectra9fftRadix2` vs
//                          `bl __ZN10pathscribe9fftRadix2` — a rename changes
//                          the mangled callee name by construction
//   5. nested NS0_/NS1_ substitutions in the same mangled names
//
// A first attempt at this reported two false drifts and one false "identical"
// (comparing two empty extractions). Hence the empty-extraction guard below.

import { execFileSync } from "node:child_process";
import { existsSync } from "node:fs";
import { createHash } from "node:crypto";
import path from "node:path";

const PLUGINS_DIR = "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media";

// effect -> its namespace, for every bundle that carries shared DSP.
//
// This list is hardcoded rather than discovered from `src/`, because the point is to
// compare BUNDLES: a namespace with no recovered source still has a copy in its binary,
// and one that is missing here is a copy going unchecked. MaskFlow was missing until it
// had a namespace to map through, which meant its `clampf` — a fifth copy — was outside
// the comparison for the whole life of this tool.
const CARRIERS = [
  ["SpectraKeys", "spectra"],
  ["PathScribe", "pathscribe"],
  ["BeatSnap", "beatsnap"],
  ["BeatGridRuler", "beatgrid"],
  ["MaskFlow", "maskflow"],
];

const NS_ALT = CARRIERS.map(([, ns]) => ns).join("|");

const FUNCTIONS = [
  "fftRadix2", "hannWindow", "hannCoherentGain", "downmixToMono",
  "linearToDb", "dbToLinear", "isPowerOfTwo", "clampf", "analyse", "Spectrogram",
];

const MIN_INSTRUCTIONS = 3; // below this, the extraction failed rather than found a stub

function sh(cmd, args) {
  try { return execFileSync(cmd, args, { encoding: "utf8", maxBuffer: 128 * 1024 * 1024 }); }
  catch (err) { return err.stdout ?? ""; }
}

function extractFunction(binary, ns, fn) {
  const asm = sh("otool", ["-tV", "-arch", "arm64", binary]);
  if (!asm) return null;
  const start = new RegExp(`^__ZN[0-9]+${ns}[0-9]+${fn}`);
  const lines = [];
  let inside = false;
  for (const line of asm.split("\n")) {
    if (start.test(line)) { inside = true; lines.push(line); continue; }
    if (inside) {
      if (/^__/.test(line)) break; // next symbol
      lines.push(line);
    }
  }
  return lines.length ? lines.join("\n") : null;
}

function normalise(asm) {
  return asm
    .split("\n")
    .map((l) => l.match(/^\s*[0-9a-f]+\s+(.*)$/))
    .filter(Boolean)
    .map((m) => m[1])
    .map((l) => l.replace(/;.*$/, ""))
    .map((l) => l.replace(/0x[0-9a-f]+/g, "ADDR"))
    .map((l) => l.replace(/^(adrp\s+x\d+,)\s*\d+/, "$1 PAGE"))
    .map((l) => l.replace(new RegExp(`__ZNK?[0-9]+(?:${NS_ALT})`, "g"), "__ZNns"))
    .map((l) => l.replace(/NS\d+_/g, "NS_"))
    .map((l) => l.replace(/\s+/g, " ").trim())
    .join("\n");
}

function main() {
  const json = process.argv.includes("--json");
  if (!existsSync(PLUGINS_DIR)) {
    console.error(`no fleet installed at ${PLUGINS_DIR}`);
    return 1;
  }

  const results = [];
  let drifted = 0;

  for (const fn of FUNCTIONS) {
    const seen = [];
    for (const [plugin, ns] of CARRIERS) {
      const binary = path.join(PLUGINS_DIR, `${plugin}.plugin`, "Contents", "MacOS", plugin);
      if (!existsSync(binary)) continue;
      const asm = extractFunction(binary, ns, fn);
      if (!asm) continue;
      const norm = normalise(asm);
      const count = norm.split("\n").filter(Boolean).length;
      if (count < MIN_INSTRUCTIONS) continue; // extraction failure, not a finding
      seen.push({ plugin, count, hash: createHash("md5").update(norm).digest("hex") });
    }
    if (seen.length < 2) {
      results.push({ fn, carriers: seen.map((s) => s.plugin), verdict: "single-carrier" });
      continue;
    }
    const hashes = new Set(seen.map((s) => s.hash));
    const identical = hashes.size === 1;
    if (!identical) drifted++;
    results.push({
      fn,
      carriers: seen.map((s) => s.plugin),
      instructions: seen[0].count,
      versions: hashes.size,
      verdict: identical ? "identical" : "DRIFTED",
    });
    if (!json) {
      const names = seen.map((s) => s.plugin).join(" ");
      console.log(
        `  ${fn.padEnd(18)} ${String(seen.length)} carriers  ${String(seen[0].count).padStart(4)} instrs  ` +
        (identical ? `IDENTICAL  (${names})` : `DRIFTED — ${hashes.size} versions: ${names}`),
      );
    }
  }

  if (json) { console.log(JSON.stringify(results, null, 2)); return drifted ? 1 : 0; }

  const shared = results.filter((r) => r.verdict === "identical");
  const total = shared.reduce((n, r) => n + r.carriers.length, 0);
  console.log(
    `\n${shared.length} functions verified identical across carriers ` +
    `(${total} copies one recovery would cover).`,
  );
  if (drifted) {
    console.log(`\n${drifted} function(s) DRIFTED — the copies have diverged.`);
    return 1;
  }
  console.log("No drift: the copied DSP is in sync.");
  return 0;
}

process.exitCode = main();
