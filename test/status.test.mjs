// Gate for RECOVERY-STATUS.md.
//
// The document states the fleet's coverage in numbers, and it is the one piece of
// documentation a reader is most likely to trust without re-deriving — so it is GENERATED
// and this test re-generates it. A hand-written coverage table had already gone stale twice
// in this repository's history before the generator existed.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { readFileSync, existsSync } from "node:fs";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");
const DOC = path.join(ROOT, "RECOVERY-STATUS.md");

test("RECOVERY-STATUS.md matches the measurement", (t) => {
  if (!existsSync(DOC)) {
    assert.fail("RECOVERY-STATUS.md is missing — run: node tools/recovery-status.mjs");
  }
  try {
    execFileSync("node", [path.join(ROOT, "tools", "recovery-status.mjs"), "--check"],
      { cwd: ROOT, encoding: "utf8", stdio: "pipe" });
  } catch (e) {
    assert.fail(`RECOVERY-STATUS.md is stale — regenerate it:\n${e.stdout ?? ""}${e.stderr ?? ""}`);
  }
});

test("the document's headline numbers agree with the per-effect rows", () => {
  // The generator computes both, so this is a check on the generator's own arithmetic
  // rather than on a human's transcription: the per-effect rows have to sum to the
  // fleet totals the document states above them.
  const text = readFileSync(DOC, "utf8");
  const total = (label) => {
    const m = text.match(new RegExp(`\\| ${label} \\| (\\d+)`));
    assert.ok(m, `no "${label}" row in the document`);
    return Number(m[1]);
  };

  // Eight cells: Effect | Namespace | Symbols | Identical | Differing | Absent |
  // Instructions | Recovered. The Instructions cell has to be in the pattern or the
  // anchoring `$` never matches and the row list comes back empty.
  const rows = [...text.matchAll(/^\| (\w+) \| (`\w+::`|—) \| (\d+)\/(\d+) \| (\d+) \| (\d+) \| (\d+) \| ([\d/]+|—) \| ([\d.]+%|—) \|$/gm)];
  assert.ok(rows.length === 16, `expected 16 effect rows, saw ${rows.length}`);

  const identical = rows.reduce((n, r) => n + Number(r[5]), 0);
  const differing = rows.reduce((n, r) => n + Number(r[6]), 0);
  const absent = rows.reduce((n, r) => n + Number(r[7]), 0);
  assert.equal(identical, total("— instruction-identical to the shipped binary"),
    "the identical column does not sum to the stated total");
  assert.equal(differing, total("— recovered but differing in codegen"),
    "the differing column does not sum to the stated total");
  assert.equal(identical + differing, total("Named symbols recovered"),
    "the recovered total is not identical + differing");

  // And the symbol columns agree with the "N of M" cell beside them.
  for (const r of rows) {
    assert.equal(Number(r[5]) + Number(r[6]), Number(r[3]),
      `${r[1]}: the recovered column does not match the "N of M" cell`);
    assert.equal(Number(r[3]) + Number(r[7]), Number(r[4]),
      `${r[1]}: symbols + absent does not match the total`);
  }
});
