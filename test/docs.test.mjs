// Audit the documentation's factual claims against the artifacts.
//
// WHY THIS EXISTS. Several rounds of careful code verification went alongside prose that
// nobody checked, and the prose is what a reader sees first. Auditing it once found the
// README claiming "six namespaces" for what was five recovered copies out of fifteen
// bundles — and that wrongness was hiding a real test gap, because the cross-checks only
// ever compared three of the five.
//
// So the audit is mechanised rather than repeated by hand. Every check below was written
// by asking "how would I know if this sentence stopped being true?" and then answering it
// with the artifact rather than with memory.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, readFileSync, readdirSync } from "node:fs";
import path from "node:path";

import { namespaceOf } from "../tools/namespaces.mjs";

const ROOT = path.join(import.meta.dirname, "..");
const DOCS = ["README.md", "FEATURE-MAP.md", "CATALOG.md", "PARAMETERS.md", "CAPABILITIES.md"];

/**
 * Recovered functions the differ cannot compare, and why.
 *
 * The differ counts functions it can find in BOTH the shipped bundle and the
 * rebuilt object. A function the compiler inlines everywhere has no out-of-line
 * symbol on one side, so it is recoverable and verified but invisible to that
 * count. Each entry here has to say why, and a namespace that develops the same
 * situation without an entry fails the check below rather than passing quietly.
 */
const NOT_COMPARABLE = {
  // EMPTY, and it is kept as an empty object on purpose: the one entry it had was
  // `lutblend: 1` for `CubeLUT::valid()`, which the differ could not see because
  // `parseNested` only accepted `__ZN…` and a const member is `__ZNK…`. Fixing that
  // made the function visible, so the exception was not needed any more — which is the
  // good outcome for an exception table: it shrank because the measurement improved,
  // not because the claim was dropped.
};

const read = (p) => readFileSync(path.join(ROOT, p), "utf8");

// Each recovered DIRECTORY with the C++ NAMESPACE it declares. They are not the same
// name — `src/autoflickerfix/` declares `namespace aff` — so the README is checked
// against the namespace (which is what it documents) while the differ is invoked with
// the directory (which is what it takes). Assuming one for the other is the mistake
// tools/namespaces.mjs was written to stop, and this test made it in both directions.
function recoveredNamespaces() {
  return readdirSync(path.join(ROOT, "src"))
    .filter((d) => existsSync(path.join(ROOT, "src", d, `${d}.cpp`)))
    .sort()
    .map((dir) => {
      const ns = namespaceOf(ROOT, dir);
      assert.ok(ns, `src/${dir}/ declares no namespace`);
      return { dir, ns };
    });
}

test("documentation does not hardcode a test count", () => {
  // A count in prose must be updated on every commit that adds a test, and it tells the
  // reader nothing they cannot get by running the command. Both README and FEATURE-MAP
  // carried one and both went stale — the README said 32 while the suite had grown.
  //
  // So the rule is the opposite of a stale-count check: the documents must NOT state a
  // count at all. A number that cannot drift is better than a number kept in sync by hand.
  //
  // (Counting test CASES from source is not reliable either: test/build.test.mjs generates
  // two tests from one `test(...)` call inside a loop, so `test(` counting gives 37 where
  // node reports 38. That is the second reason not to put a number in prose.)
  const NUMERAL = /\b\d+\s+(?:test cases|tests)\b/i;
  const WORD = /\b(?:thirty|forty|fifty)(?:-[a-z]+)?\s+tests\b/i;

  for (const doc of DOCS) {
    const text = read(doc);
    assert.doesNotMatch(text, NUMERAL, `${doc} hardcodes a test count`);
    assert.doesNotMatch(text, WORD, `${doc} hardcodes a test count`);
  }

  // And the gate must still be discoverable, so removing the count did not remove the
  // instruction to run it.
  assert.match(read("README.md"), /npm test/, "README should still tell the reader to run the gate");
});

test("every source path the documentation links to exists", () => {
  // Backticked paths are how these documents reference files. A moved or renamed file
  // leaves a dead reference that reads as authoritative, so each is resolved.
  //
  // TWO RULES, because prose uses two kinds of reference and treating them alike gives
  // false failures both ways:
  //   * a path WITH a directory component must resolve from the repository root;
  //   * a BARE basename must exist somewhere in the tree — `selfcheck.cpp` names nine
  //     different files across src/, and demanding it resolve from the root would be
  //     wrong about all nine.
  // Files belonging to the AE SDK are named in prose but deliberately not vendored, so
  // they are listed as known-external rather than silently skipped.
  const EXTERNAL = new Set(["AE_Effect.h", "AE_EffectCB.h", "AE_Config.h", "AEConfig.h",
                            "AE_General.r", "AE_EffectVers.h"]);

  // This repo's OWN generated artefacts: named in prose, written by `npm run demo`,
  // and deliberately gitignored. A fresh clone does not have them until the demo
  // harness has been run, so demanding they resolve makes `npm test` fail on a clean
  // checkout - which means the gate nobody can run without an uncommitted build
  // artefact, and a gate that is quietly not a gate. Listing them keeps the check
  // meaningful (the name must be one this repo actually generates) instead of
  // demanding a file that is not supposed to be committed.
  const GENERATED = new Set(["results.json"]);

  const allFiles = new Set();
  const walk = (dir, rel) => {
    for (const e of readdirSync(dir, { withFileTypes: true })) {
      if (e.name === ".git" || e.name === "build" || e.name === "node_modules") continue;
      const r = rel ? `${rel}/${e.name}` : e.name;
      if (e.isDirectory()) walk(path.join(dir, e.name), r);
      else { allFiles.add(r); allFiles.add(e.name); }
    }
  };
  walk(ROOT, "");

  const missing = [];
  for (const doc of DOCS) {
    const text = read(doc);
    for (const m of text.matchAll(/`([a-zA-Z0-9_./-]+\.(?:mjs|cpp|h|md|json|r|sh|rsrc))`/g)) {
      const p = m[1];
      if (p.includes("*")) continue;                    // globs are illustrative
      if (/^(https?:|\/)/.test(p)) continue;            // absolute or external
      if (EXTERNAL.has(p)) continue;                    // SDK files, not vendored
      if (GENERATED.has(p)) continue;                  // gitignored build output
      if (allFiles.has(p)) continue;                    // resolves by whichever rule fits
      missing.push(`${doc}: \`${p}\``);
    }
  }
  assert.deepEqual(missing, [], `documentation references files that do not exist:\n${missing.join("\n")}`);
});

test("the README's headline figures match the generated coverage table", () => {
  // The README carries the fleet's headline numbers in prose, because a reader meets the
  // prose before the table. That copy is hand-written, and it drifted twice: it said
  // "114 of 155 ... 35.9%" while the generated table said 142 of 166 and 59.4%, and it
  // still said "one - MotionCurveX - has no recovered source at all" months after
  // MotionCurveX had a namespace. A hand-written copy of a generated number is a
  // number that goes stale, so it is checked here against the artifact rather than
  // trusted.
  //
  // Only the RECOVERY claims are checked. The line count is deliberately NOT: it moves
  // with every commit that touches src/, and a README that has to be edited for every
  // lane's commit is a README that will be left alone instead.
  const table = read("RECOVERY-STATUS.md");
  const readme = read("README.md");

  const symbols = table.match(/\| Named symbols recovered \| (\d+) of (\d+) \|/);
  assert.ok(symbols, "RECOVERY-STATUS.md has no 'Named symbols recovered' row");
  const [, recovered, total] = symbols.map(Number);

  const identical = table.match(/\| — instruction-identical to the shipped binary \| (\d+) \|/);
  assert.ok(identical, "RECOVERY-STATUS.md has no 'instruction-identical' row");

  const instructions = table.match(/\| Authored instructions recovered \| (\d+) of (\d+) \(([\d.]+)%\) \|/);
  assert.ok(instructions, "RECOVERY-STATUS.md has no 'Authored instructions recovered' row");
  const [, instrRecovered, instrTotal, instrPct] = instructions;

  assert.ok(readme.includes(`**${recovered} of ${total} named symbols recovered`),
    `the README does not say "${recovered} of ${total} named symbols recovered"`);
  assert.ok(readme.includes(`${identical[1]} of them instruction-identical`),
    `the README does not say "${identical[1]} of them instruction-identical"`);
  // The README writes these with thousands separators; the table does not.
  const grouped = (n) => n.replace(/\B(?=(\d{3})+(?!\d))/g, ",");
  assert.ok(readme.includes(`${grouped(instrRecovered)} of the fleet's `
    + `${grouped(instrTotal)} authored instructions (${instrPct}%)`),
    `the README does not quote the table's instruction coverage `
    + `(${instrRecovered} of ${instrTotal}, ${instrPct}%)`);
});

test("the README does not claim an effect is missing a recovered namespace", () => {
  // A stale sentence of the shape "X has no recovered source" survives every edit that
  // touches the code, because nothing reads it. When the fleet reached full coverage
  // those sentences became false and the README still made them. The check is on the
  // SHAPE of the claim rather than on a list of names, so it keeps holding as effects
  // are added.
  const readme = read("README.md");
  for (const phrase of [
    "has no recovered source",
    "still exists only as an installed binary",
    "no recovered namespace",
  ]) {
    assert.ok(!readme.includes(phrase),
      `the README still claims "${phrase}" - every effect has a recovered namespace now`);
  }
});

test("the namespace list in the documentation matches src/", () => {
  // README enumerates the recovered namespaces in a table. Every one must exist, and a
  // newly recovered namespace must appear, so the table cannot silently fall behind.
  const actual = recoveredNamespaces();
  const text = read("README.md");

  for (const { ns } of actual) {
    assert.match(text, new RegExp(`\`${ns}::\``),
      `README does not mention the recovered namespace ${ns}::`);
  }

  // And the stated count, wherever it appears.
  // "N namespaces" and the two shapes this README actually uses: "N recovered
  // namespaces" and "N source namespaces". The narrower pattern missed the second
  // shape entirely, which is how a "ten recovered namespaces" claim against nine
  // directories passed — the word the check looked at was "recovered".
  for (const m of text.matchAll(/(\w+)\s+(?:(?:recovered|source)\s+)?namespaces?/g)) {
    const word = m[1].toLowerCase();
    // The map used to stop at "eight", which is exactly how far the README's own
    // count word had got: adding the ninth recovered namespace left a "nine" claim
    // unchecked, and an earlier edition had already drifted to saying "nine" while
    // src/ held eight. Any number word the README might plausibly use is checked, so
    // a count claim cannot escape by being a word this map does not know.
    const nums = {
      one: 1, two: 2, three: 3, four: 4, five: 5, six: 6, seven: 7, eight: 8,
      nine: 9, ten: 10, eleven: 11, twelve: 12, thirteen: 13, fourteen: 14,
      fifteen: 15, sixteen: 16,
    };
    if (nums[word] !== undefined) {
      assert.equal(nums[word], actual.length,
        `README says "${m[0]}" but src/ holds ${actual.length} namespaces: `
        + actual.map((x) => x.ns).join(", "));
    }
  }
});

test("the per-namespace recovered counts in the documentation match codegen-diff", (t) => {
  const bundles = "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media";
  if (!existsSync(bundles)) { t.skip("fleet not installed"); return; }

  const text = read("README.md");
  for (const { dir, ns } of recoveredNamespaces()) {
    let out;
    try {
      out = execFileSync("node", [path.join(ROOT, "tools", "codegen-diff.mjs"), dir],
                         { cwd: ROOT, encoding: "utf8", stdio: "pipe" });
    } catch (e) {
      out = `${e.stdout ?? ""}`;
    }
    const m = out.trim().split("\n").pop()
      ?.match(/(\d+) identical, (\d+) differing, (\d+) not implemented/);
    if (!m) continue;   // namespace not mapped to a bundle
    const [, identical, differing, absent] = m.map(Number);
    const implemented = identical + differing;

    // If the README states "N of M" for this namespace, it must be right.
    //
    // The claim is looked for in the namespace's OWN table row, not by searching the
    // document for "N of M" near a namespace name. Both earlier attempts got that
    // wrong in the same way: a row about one namespace legitimately names another —
    // the `beatsnap::` row cites the `spectra::` originals its shared DSP was copied
    // from — so the first `beatsnap::` row ever written made this test report
    // "README says 7 of 11 for spectra::". Scoping to the line fixed the cross-row
    // case and not that one, because the citation is on the same line.
    const row = text.split("\n").find((line) => line.startsWith(`| \`${ns}::\``));
    const claim = row?.match(/\*\*([0-9]+) of ([0-9]+)\*\*/);
    if (claim) {
      const stated = Number(claim[1]);
      const total = Number(claim[2]);
      const invisible = NOT_COMPARABLE[ns]?.count ?? 0;
      assert.equal(stated, implemented + invisible,
        `${ns}:: row says ${stated} of ${total}, but the differ accounts for `
        + `${implemented} implemented function(s)`
        + (invisible ? ` plus ${invisible} it cannot compare (${NOT_COMPARABLE[ns].why})` : ""));
      if (!Number.isNaN(absent)) {
        assert.equal(total, stated + absent,
          `${ns}:: row says ${stated} of ${total}, but the differ counts ${absent} absent, `
          + `so the total should be ${stated + absent}`);
      }
    }
  }
});

test("the fleet-wide claims about the bundles hold", () => {
  // Statements about ALL SIXTEEN are the easiest to get wrong, because they are written
  // once and the fleet never changes underneath them — the claim just quietly ages.
  const specs = readdirSync(path.join(ROOT, "specs"))
    .filter((f) => f.endsWith(".json") && f !== "index.json");
  assert.equal(specs.length, 16, "the fleet is 16 effects, and the specs say so");

  const text = read("README.md");
  assert.match(text, /all 16|16 effects|Sixteen/i, "README should state the fleet size");

  // "all 16" and "all 5" claims in README refer to PiPL byte-identity and clampf copies
  // respectively; the former is proven by test/pipl.test.mjs and the latter by
  // test/clampf.test.mjs, so this checks the DOCUMENT names those tests.
  assert.match(text, /test\/pipl\.test\.mjs|packaging\//,
    "the README's byte-identity claim should point at the test or the packaging directory");

  // Every parameter claim must match PARAMETERS.md's own generated table.
  const params = read("PARAMETERS.md");
  const total = (params.match(/^\| \d+ \| |^\| — \| /gm) ?? []).length;
  for (const m of text.matchAll(/(\d+)\s+parameters/g)) {
    assert.equal(Number(m[1]), total,
      `README says "${m[0]}" but PARAMETERS.md contains ${total} parameter rows`);
  }
});
