// End-to-end proof that the recovered spec is COMPLETE for a real effect.
//
// tools/recover.mjs reads the PiPL out of a surviving bundle. This test does the
// opposite direction: it builds a PiPL from `build/SpectraKeysPiPL.r` using the
// Adobe AE SDK and Rez, and demands the result be byte-identical to the PiPL in
// the installed bundle.
//
// That round trip is the strongest available evidence that the recovered spec
// (name, category, code entry points, PiPL version, spec version, effect
// version, info flags, both global out-flag words, match name) is right and has
// no missing field — a single wrong flag word changes the bytes.
//
// The SDK is Adobe-licensed and is NOT vendored here, and the plugin bundle is
// only present on a machine where the fleet is installed. Both are therefore
// conditional and the test SKIPS (rather than fails) when either is absent, in
// keeping with how the clang++ compile tests behave.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync, execSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, readdirSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");

// The PiPL resource file is a classic Mac resource file: a 16-byte header, then
// the resource DATA, then the resource MAP. Only the first two are PiPL content;
// the map describes the file itself. Compare up to the map offset.
const DATA_END = 0x240; // 576

function findSdk() {
  // Explicit override wins. Otherwise scan ~/.ae-sdk (where the SDK is unpacked
  // by hand, outside any repo — it is Adobe-licensed and must not be vendored)
  // for any unpacked SDK rather than pinning one version string.
  const roots = [];
  if (process.env.AE_SDK_DIR) roots.push(process.env.AE_SDK_DIR);

  const home = process.env.HOME ?? "";
  const base = path.join(home, ".ae-sdk");
  if (existsSync(base)) {
    for (const entry of readdirSync(base)) {
      const p = path.join(base, entry);
      // the archive nests one directory deep under the version folder
      roots.push(p, path.join(p, "Examples"));
      for (const inner of readdirSync(p)) roots.push(path.join(p, inner, "Examples"));
    }
  }

  for (const r of roots) {
    try {
      if (existsSync(path.join(r, "Headers", "AE_Effect.h"))) return r;
    } catch { /* unreadable candidate — keep looking */ }
  }
  return null;
}

function findBundleRsrc(plugin, file) {
  const dirs = [
    "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media",
    "/Applications/Adobe After Effects 2025/Plug-ins/Williams Media",
  ];
  for (const d of dirs) {
    const p = path.join(d, `${plugin}.plugin`, "Contents", "Resources", file);
    if (existsSync(p)) return p;
  }
  return null;
}

function have(cmd) {
  try { execSync(`command -v ${cmd}`, { stdio: "ignore" }); return true; }
  catch { return false; }
}

test("the rebuilt SpectraKeys PiPL is byte-identical to the shipped one", (t) => {
  const sdk = findSdk();
  if (!sdk) {
    t.skip("AE SDK not found — set AE_SDK_DIR to <SDK>/Examples to run this proof");
    return;
  }
  if (!have("Rez")) {
    t.skip("Rez not available — install the Xcode command line tools");
    return;
  }
  const shipped = findBundleRsrc("SpectraKeys", "SpectraKeys.rsrc");
  if (!shipped) {
    t.skip("SpectraKeys.plugin is not installed on this machine");
    return;
  }

  const dir = mkdtempSync(path.join(tmpdir(), "pipl-"));
  const out = path.join(dir, "SpectraKeys.rsrc");
  try {
    execFileSync("Rez", [
      "-useDF",
      "-o", out,
      "-I", path.join(sdk, "Headers"),
      "-I", path.join(sdk, "Resources"),
      "-d", "AE_OS_MAC",
      "-d", "AE_PROC_ARM64",
      path.join(ROOT, "packaging", "SpectraKeysPiPL.r"),
    ], { stdio: "pipe" });

    const built = readFileSync(out);
    const real = readFileSync(shipped);

    // Same overall size, and identical everywhere before the resource map.
    assert.equal(built.length, real.length, "resource file size differs");
    assert.ok(built.subarray(0, DATA_END).equals(real.subarray(0, DATA_END)),
      "the PiPL payload differs from the shipped bundle");

    // Prove the PiPL actually carries the claims, so a vacuously identical pair
    // of empty resources could never pass this test.
    const text = built.subarray(0, DATA_END).toString("latin1");
    assert.match(text, /eFKT/, "kind atom missing");
    assert.match(text, /SpectraKeys/, "name atom missing");
    assert.match(text, /Williams Media/, "category atom missing");
    assert.match(text, /WMH SpectraKeys/, "match-name atom missing");
    assert.match(text, /EffectMain/, "code entry point missing");
    assert.match(text, /ePVR/, "PiPL version atom missing");
    assert.match(text, /eSVR/, "spec version atom missing");
    assert.match(text, /eVER/, "effect version atom missing");
    assert.match(text, /eGLO/, "global out-flags atom missing");
    assert.match(text, /eGL2/, "global out-flags 2 atom missing");
    assert.match(text, /eMNA/, "match-name atom missing");
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("the SDK on this machine is the one the fleet was built against", (t) => {
  // The fleet reports spec version 0x000d001d, i.e. PF_PLUG_IN_VERSION 13 and
  // PF_PLUG_IN_SUBVERS 29. If the local SDK does not agree, a rebuilt bundle
  // would claim a different spec version than the shipped ones.
  const sdk = findSdk();
  if (!sdk) { t.skip("AE SDK not found"); return; }

  const vers = readFileSync(path.join(sdk, "Headers", "AE_EffectVers.h"), "utf8");
  const major = vers.match(/PF_PLUG_IN_VERSION\s+(\d+)/);
  const sub = vers.match(/PF_PLUG_IN_SUBVERS\s+(\d+)/);
  assert.ok(major && sub, "could not read PF_PLUG_IN_VERSION / SUBVERS");

  const encoded = (Number(major[1]) << 16) | Number(sub[1]);
  assert.equal(encoded, 0x000d001d,
    `SDK at ${sdk} encodes spec version 0x${encoded.toString(16).padStart(8, "0")}, `
    + "but the fleet reports 0x000d001d");
});
