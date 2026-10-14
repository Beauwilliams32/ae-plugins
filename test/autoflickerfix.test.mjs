// Gate for the recovered aff:: namespace (AutoFlickerFix).
//
// COMPLETE AT THE NAMESPACE LEVEL, unlike the other recovered namespaces: all ten `aff::`
// functions are transcribed. The host integration — EffectMain, SampleWorldStat and
// DrawStatsBar — is NOT, and a test below asserts that nothing pretends otherwise.
//
// This namespace is also the one where the codegen differ earned its keep: it caught a
// real semantic error (`meanOf` returning NaN for an empty vector where the bundle
// returns 0.0f) that a self-check written from the same misreading had confirmed as
// correct. The differ assertions below therefore pin specific functions by name.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");
const SRC = path.join(ROOT, "src", "autoflickerfix");

// The namespace inside the directory is `aff`, NOT `autoflickerfix`. Anywhere a name is
// needed it is read from the header rather than assumed from the path — see
// tools/namespaces.mjs, which exists because assuming they were the same silently made
// the differ report "no symbols" for this namespace.
function namespaceName() {
  const m = readFileSync(path.join(SRC, "autoflickerfix.h"), "utf8")
    .match(/^\s*namespace\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{/m);
  assert.ok(m, "autoflickerfix.h must declare a namespace");
  return m[1];
}

/**
 * Strip C++ comments.
 *
 * Load-bearing, not cosmetic: these files TALK about the functions they have not
 * recovered — the header's SCOPE paragraph names `EffectMain`, `SampleWorldStat` and
 * `DrawStatsBar` in order to say they are unread. A check for "the name must not appear"
 * therefore failed against the sentence explaining the omission. The check has to run on
 * code, with the prose removed.
 */
function stripComments(src) {
  return src.replace(/\/\*[\s\S]*?\*\//g, " ").replace(/\/\/[^\n]*/g, " ");
}

/**
 * The body of a function definition, from its signature to the end of the file.
 *
 * Searches the COMMENT-STRIPPED source, and accepts either spelling — `float ns::name(`
 * or `float name(` — because the recovered `.cpp` puts its definitions inside
 * `namespace ns {` and therefore writes them unqualified.
 *
 * Both of those are corrections to a first version that searched the raw text for the
 * qualified form. It matched the function's own documentation comment instead — every
 * recovered function here is preceded by a comment naming it — and then matched nothing
 * when the slice missed. It failed against correct source twice, for one reason: prose
 * about a function is not the function.
 */
function definitionOf(impl, ns, name) {
  const code = stripComments(impl);
  const qualified = code.indexOf(`${ns}::${name}`);
  const plain = code.search(new RegExp(`\\b${name}\\s*\\(`));
  const at = qualified >= 0 ? qualified : plain;
  assert.ok(at >= 0, `${name} must be defined in the implementation`);
  return code.slice(at);
}

test("recovered aff source compiles and its maths verifies", (t) => {
  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) { t.skip("clang++ not available — compile check skipped"); return; }

  const dir = mkdtempSync(path.join(tmpdir(), "aff-check-"));
  const bin = path.join(dir, "selfcheck");
  try {
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
      "-o", bin,
      path.join(SRC, "autoflickerfix.cpp"),
      path.join(SRC, "selfcheck.cpp"),
    ], { stdio: "pipe" });

    const out = execFileSync(bin, { encoding: "utf8" });
    assert.match(out, /ALL RECOVERED AFF MATHS VERIFIED/, `self-check failed:\n${out}`);
    assert.doesNotMatch(out, /\bFAIL\b/, out);
    const passes = (out.match(/^  PASS/gm) ?? []).length;
    assert.ok(passes >= 80, `expected >= 80 checks, saw ${passes}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("the empty-vector answer is 0, not NaN", () => {
  // The specific error the codegen differ caught. Asserted on the SOURCE as well as
  // through the self-check, because a later refactor that drops the guard would compile
  // and every other check would still pass.
  const impl = readFileSync(path.join(SRC, "autoflickerfix.cpp"), "utf8");
  const body = definitionOf(impl, namespaceName(), "meanOf");
  assert.match(body.slice(0, 400), /if\s*\(\s*values\.empty\(\)\s*\)\s*return\s+0\.0f\s*;/,
    "meanOf must return 0.0f for an empty vector rather than dividing 0.0 by 0.0");

  // Negative control: the guard is genuinely load-bearing. Without it the division is
  // 0.0/0.0, which is NaN, and the assertion above is the only thing that keeps it.
  assert.match(body.slice(0, 400), /sum\s*\/\s*static_cast<double>\(values\.size\(\)\)/,
    "meanOf must still divide by the size, so removing the guard really does give NaN");
});

test("dbToGainClamp returns the GainClamp window, not a single float", () => {
  const header = readFileSync(path.join(SRC, "autoflickerfix.h"), "utf8");
  const impl = readFileSync(path.join(SRC, "autoflickerfix.cpp"), "utf8");
  const ns = namespaceName();

  assert.match(header, new RegExp(`${ns}::GainClamp\\s+dbToGainClamp|\\bGainClamp\\s+dbToGainClamp`),
    "the declaration must return GainClamp — reading it as float is the misreading that "
    + "the bundle's `mov.16b v1, v0` rules out");

  // The returned pair must be {1/gain, gain}. Asserting only the low bound would pass
  // against a half-reading that got the high bound wrong, which is the version written
  // first; the self-check asserts the symmetry, and this asserts it is in the source.
  assert.match(impl, /\{\s*gain\s*>\s*0\.0f\s*\?\s*1\.0f\s*\/\s*gain\s*:\s*1\.0f\s*,\s*gain\s*\}/,
    "the two members must be the reciprocal and the gain itself");
});

test("computeGain takes current before target", () => {
  const header = readFileSync(path.join(SRC, "autoflickerfix.h"), "utf8");
  const impl = readFileSync(path.join(SRC, "autoflickerfix.cpp"), "utf8");
  const ns = namespaceName();

  assert.match(header, /float\s+computeGain\s*\(\s*float\s+current\s*,\s*float\s+target\s*,/,
    "the first parameter must be `current`: the bundle divides s1 by s0 and EffectMain "
    + "passes the per-frame measurement in v0 and temporalTarget(...) in v1");

  // The definition sits inside `namespace aff {`, so it is spelled unqualified there.
  const at = impl.indexOf(`float ${ns}::computeGain`);
  const from = at >= 0 ? at : impl.indexOf("float computeGain");
  assert.ok(from >= 0, "computeGain must be defined in autoflickerfix.cpp");
  assert.match(impl.slice(from), /target\s*\/\s*current/,
    "the body must divide target by current");

  // Negative control: the swapped spelling must NOT be present, or the assertion above
  // could be satisfied by a signature that disagrees with its own body.
  assert.doesNotMatch(impl, /float\s+computeGain\s*\(\s*float\s+target\s*,\s*float\s+current\s*,/,
    "computeGain must not declare target first");
});

test("the aff namespace does not claim host integration it has not recovered", () => {
  // The whole `aff::` namespace is recovered, which makes it easy to read the namespace
  // as the effect. It is not: EffectMain, SampleWorldStat and DrawStatsBar touch the AE
  // world and none of their bodies have been read. A stub would look complete.
  for (const f of ["autoflickerfix.cpp", "autoflickerfix.h"]) {
    const code = stripComments(readFileSync(path.join(SRC, f), "utf8"));
    for (const fn of ["EffectMain", "SampleWorldStat", "DrawStatsBar"]) {
      assert.doesNotMatch(code, new RegExp(`\\b${fn}\\s*\\(`),
        `${fn} appears in code in ${f} but its body was never read`);
    }
  }

  // Negative control on the comment stripper: the check above is only meaningful if the
  // stripper removes a mention and keeps a definition. Both directions are asserted,
  // because a stripper that emptied everything would pass the loop above vacuously.
  assert.doesNotMatch(stripComments("// EffectMain PF_Err x( void ) {}"), /\bEffectMain\s*\(/,
    "a mention inside a comment must be stripped");
  assert.match(stripComments("PF_Err EffectMain( PF_Cmd c ) {}"), /\bEffectMain\s*\(/,
    "a real definition must survive stripping");

  assert.match(readFileSync(path.join(SRC, "autoflickerfix.h"), "utf8"),
    /What is NOT recovered is the host integration/,
    "the header must state that the host integration is unrecovered");

  // And there must be no host layer for this effect. LUTBlend and GlitchKit have one;
  // AutoFlickerFix must not, because nothing here was read well enough to reimplement.
  assert.equal(
    (() => { try { readFileSync(path.join(ROOT, "plugins", "AutoFlickerFix", "AutoFlickerFix.cpp")); return true; }
             catch { return false; } })(),
    false,
    "plugins/AutoFlickerFix must not exist: there is no recovered host integration to build from");
});
