// Gate for the plugin build. Assembles the LUTBlend bundle and checks that what
// comes out is structurally what a host would accept.
//
// This is the one test that exercises the WHOLE chain: recovered spec -> PiPL,
// recovered DSP -> object code, new host integration -> bundle. Everything else in
// this repo verifies a piece of the recovery; this verifies that the pieces
// actually join.
//
// It can only check structure. Whether After Effects will LOAD the bundle is not
// testable here and is not claimed — see plugins/LUTBlend/README.md.
//
// Requires an AE SDK. Skips (rather than fails) without one, like the other
// SDK-dependent tests.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, readFileSync, readdirSync, statSync } from "node:fs";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");
const DATA_END = 0x240; // the PiPL payload ends where the resource map begins

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

function findShippedRsrc() {
  for (const d of ["/Applications/Adobe After Effects 2026/Plug-ins/Williams Media",
                   "/Applications/Adobe After Effects 2025/Plug-ins/Williams Media"]) {
    const p = path.join(d, "LUTBlend.plugin", "Contents", "Resources", "LUTBlend.rsrc");
    if (existsSync(p)) return p;
  }
  return null;
}

test("tools/build-plugin.sh assembles a structurally valid LUTBlend bundle", (t) => {
  const sdk = findSdk();
  if (!sdk) {
    t.skip("AE SDK not found — set AE_SDK_DIR to <SDK>/Examples to run the build");
    return;
  }

  try {
    execFileSync("bash", [path.join(ROOT, "tools", "build-plugin.sh"), "LUTBlend"], {
      cwd: ROOT, stdio: "pipe", env: { ...process.env, AE_SDK_DIR: sdk },
    });
  } catch (e) {
    assert.fail(`build failed:\n${e.stdout ?? ""}${e.stderr ?? ""}`);
  }

  const bundle = path.join(ROOT, "build", "LUTBlend.plugin");
  const bin = path.join(bundle, "Contents", "MacOS", "LUTBlend");
  const rsrc = path.join(bundle, "Contents", "Resources", "LUTBlend.rsrc");

  // --- the bundle has the parts a host looks for ---------------------------
  for (const p of [bin, rsrc,
                   path.join(bundle, "Contents", "Info.plist"),
                   path.join(bundle, "Contents", "PkgInfo")]) {
    assert.ok(existsSync(p), `missing ${p.replace(ROOT + "/", "")}`);
  }

  // --- the Mach-O exports EffectMain, which is how AE finds the plugin ------
  // Without this the bundle is inert no matter how good the code is, and it fails
  // SILENTLY (the plugin just never appears), so it is asserted here.
  // Anchored, NOT a substring test. /_EffectMain/ also matches
  // "_EffectMainRenamed", which is how an earlier version of this test managed to
  // pass against a plugin with no usable entry point at all.
  const syms = execFileSync("nm", ["-gU", bin], { encoding: "utf8" });
  assert.match(syms, /^[0-9a-f]+ T _EffectMain$/m,
    `the bundle does not export EffectMain as a defined text symbol:\n${syms}`);

  const archs = execFileSync("lipo", ["-archs", bin], { encoding: "utf8" }).trim();
  assert.equal(archs, "arm64", `unexpected architectures: ${archs}`);

  // --- the PiPL must be the RECOVERED one, not a fresh invention -----------
  const shipped = findShippedRsrc();
  if (shipped) {
    const built = readFileSync(rsrc);
    const real = readFileSync(shipped);
    assert.ok(built.subarray(0, DATA_END).equals(real.subarray(0, DATA_END)),
      "the rebuilt PiPL differs from the shipped bundle's");
  }

  // --- the DSP actually made it in ----------------------------------------
  // A case-insensitive filesystem once made the plugin's object file overwrite the
  // DSP's, and the only symptom was a missing EffectMain. Assert the recovered
  // engine's own symbols survive into the linked binary, so that class of silent
  // object-file collision cannot come back unnoticed.
  const all = execFileSync("nm", [bin], { encoding: "utf8" });
  for (const fn of ["applyLUTBlend", "sampleLUT", "makeIdentity", "luma709"]) {
    assert.match(all, new RegExp(fn), `recovered lutblend::${fn} is not in the bundle`);
  }

  // --- and the bundle is not trivially small -------------------------------
  assert.ok(statSync(bin).size > 4096, "the linked binary is suspiciously small");
});
