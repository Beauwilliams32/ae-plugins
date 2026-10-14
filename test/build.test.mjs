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

function findShippedRsrc(plugin) {
  for (const d of ["/Applications/Adobe After Effects 2026/Plug-ins/Williams Media",
                   "/Applications/Adobe After Effects 2025/Plug-ins/Williams Media"]) {
    const p = path.join(d, `${plugin}.plugin`, "Contents", "Resources", `${plugin}.rsrc`);
    if (existsSync(p)) return p;
  }
  return null;
}

// Both built effects, so a second one joining the fleet is covered rather than
// needing a new test copied per plugin.
const BUILT = [
  { plugin: "LUTBlend", rsrc: "LUTBlend.rsrc",
    dsp: ["applyLUTBlend", "sampleLUT", "makeIdentity", "luma709"] },
  { plugin: "GlitchKit", rsrc: "GlitchKit.rsrc",
    dsp: ["hashKey", "patternFrame", "clampi", "clampf"] },
];

for (const { plugin, dsp } of BUILT) {
test(`tools/build-plugin.sh assembles a structurally valid ${plugin} bundle`, (t) => {
  const sdk = findSdk();
  if (!sdk) {
    t.skip("AE SDK not found — set AE_SDK_DIR to <SDK>/Examples to run the build");
    return;
  }

  try {
    execFileSync("bash", [path.join(ROOT, "tools", "build-plugin.sh"), plugin], {
      cwd: ROOT, stdio: "pipe", env: { ...process.env, AE_SDK_DIR: sdk },
    });
  } catch (e) {
    assert.fail(`build failed:\n${e.stdout ?? ""}${e.stderr ?? ""}`);
  }

  const bundle = path.join(ROOT, "build", `${plugin}.plugin`);
  const bin = path.join(bundle, "Contents", "MacOS", plugin);
  const rsrc = path.join(bundle, "Contents", "Resources", `${plugin}.rsrc`);

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
  const shipped = findShippedRsrc(plugin);
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
  for (const fn of dsp) {
    assert.match(all, new RegExp(fn), `recovered ${fn} is not in the ${plugin} bundle`);
  }

  // --- and the bundle is not trivially small -------------------------------
  assert.ok(statSync(bin).size > 4096, "the linked binary is suspiciously small");
});
}

test("the plugin's declared capability matches the recovered words", (t) => {
  // Whatever GLOBAL_SETUP writes into out_flags/out_flags2 is what AE copies into
  // the PiPL's eGLO/eGL2 atoms, so these are RECOVERED values and not free
  // choices. An earlier revision of LUTBlend.cpp built them from named PF_OutFlag
  // constants instead and invented three of them — including
  // PF_OutFlag2_FLOAT_COLOR_AWARE, which would have had AE hand 32-bpc float data
  // to a render path that reads 8-bit pixels. This test is what stops that class
  // of drift, and it needs no SDK or host to run.
  const src = readFileSync(path.join(ROOT, "plugins", "LUTBlend", "LUTBlendParams.h"), "utf8");
  const impl = readFileSync(path.join(ROOT, "plugins", "LUTBlend", "LUTBlend.cpp"), "utf8");
  const spec = JSON.parse(readFileSync(path.join(ROOT, "specs", "LUTBlend.json"), "utf8"));

  // Comments must be stripped before the "is this flag claimed?" checks. Without
  // this the test fails on its own explanatory comment, which mentions the very
  // flag it is asserting must NOT be set.
  const stripComments = (text) =>
    text.replace(/\/\*[\s\S]*?\*\//g, " ").replace(/\/\/[^\n]*/g, " ");
  const code = stripComments(impl);

  const grab = (text, name) => {
    const m = text.match(new RegExp(`${name}\\s+(0x[0-9a-fA-F]+)`));
    return m ? Number(m[1]) : null;
  };

  const wantGlo = Number(spec.pipl.eGLO);
  const wantGl2 = Number(spec.pipl.eGL2);
  assert.equal(grab(src, "LUTBLEND_GLOBAL_OUT_FLAGS"), wantGlo,
    "LUTBLEND_GLOBAL_OUT_FLAGS must equal the recovered eGLO");
  assert.equal(grab(src, "LUTBLEND_GLOBAL_OUT_FLAGS2"), wantGl2,
    "LUTBLEND_GLOBAL_OUT_FLAGS2 must equal the recovered eGL2");

  // The recovered words must actually be USED, not merely declared.
  assert.match(impl, /out_data->out_flags\s*=\s*LUTBLEND_GLOBAL_OUT_FLAGS\s*;/,
    "GLOBAL_SETUP must assign the recovered out_flags");
  assert.match(impl, /out_data->out_flags2\s*=\s*LUTBLEND_GLOBAL_OUT_FLAGS2\s*;/,
    "GLOBAL_SETUP must assign the recovered out_flags2");

  // Capability the recovered words do NOT claim must not be claimed in code.
  assert.doesNotMatch(code, /PF_OutFlag2_FLOAT_COLOR_AWARE/,
    "the recovered eGL2 does not set FLOAT_COLOR_AWARE; claiming it would have AE "
    + "supply float data the render path cannot read");
  assert.doesNotMatch(code, /PF_OutFlag2_SUPPORTS_SMART_RENDER/,
    "the recovered eGL2 does not set SUPPORTS_SMART_RENDER, and no "
    + "PF_Cmd_SMART_RENDER handler exists");

  // BEHAVIOUR THE FLAGS OBLIGE. DEEP_COLOR_AWARE means AE will supply 16-bit
  // worlds, so a 16-bit path must exist; THREADED_RENDERING means renders may run
  // concurrently, so the shared LUT cache must be locked.
  assert.match(code, /BlendPixel16/, "DEEP_COLOR_AWARE is declared, so a 16-bit path is required");
  assert.match(code, /PF_WORLD_IS_DEEP/, "the render must dispatch on world depth");
  assert.match(code, /Iterate16Suite1/, "and the 16-bit iterate suite must actually be called");

  const lib = readFileSync(path.join(ROOT, "plugins", "LUTBlend", "LutLibrary.cpp"), "utf8");
  assert.match(lib, /lock_guard/, "THREADED_RENDERING is declared, so the cache must be locked");
  assert.match(lib, /clear\(\)[\s\S]{0,80}lock_guard|lock_guard[\s\S]{0,80}cache_\.clear/,
    "Library::clear must also take the lock");
});
