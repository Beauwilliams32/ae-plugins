// Fleet-wide cross-check of every recovered clampf copy.
//
// WHY THIS EXISTS SEPARATELY. clampf turned out to be the fleet's most duplicated
// idiom: 15 of the 16 bundles carry one, in six different namespaces. Five are recovered
// here, and before this test only THREE of those five were ever compared against each
// other — the per-namespace tests each checked a subset. Two copies could have drifted
// from the rest while every test stayed green.
//
// That gap was found by auditing this repository's own README, which claimed "six
// namespaces" for something that is five recovered copies out of fifteen bundles. The
// prose was wrong and the coverage was thinner than the prose implied.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

import { namespaceOf } from "../tools/namespaces.mjs";

const ROOT = path.join(import.meta.dirname, "..");

// Every recovered DIRECTORY that defines clampf, discovered from the tree rather than
// listed here, so a newly recovered copy is covered without editing this test.
//
// The directory and the C++ namespace are NOT the same name — `src/autoflickerfix/`
// declares `namespace aff` — so each entry carries both. Deriving the namespace from the
// header rather than from the path is what tools/namespaces.mjs exists for; this test
// previously passed the directory straight into `dir::clampf(...)` and the probe failed
// to compile with "use of undeclared identifier 'autoflickerfix'".
function recoveredClampfNamespaces() {
  const out = [];
  for (const dirName of readdirSync(path.join(ROOT, "src"))) {
    const dir = path.join(ROOT, "src", dirName);
    let files;
    try { files = readdirSync(dir); } catch { continue; }
    for (const f of files) {
      if (!f.endsWith(".cpp") || f === "selfcheck.cpp") continue;
      const text = readFileSync(path.join(dir, f), "utf8");
      if (/\bfloat\s+clampf\s*\(/.test(text)) {
        const ns = namespaceOf(ROOT, dirName);
        assert.ok(ns, `${dirName} defines clampf but declares no namespace`);
        out.push({ dir: dirName, ns });
        break;
      }
    }
  }
  return out.sort((a, b) => (a.dir < b.dir ? -1 : a.dir > b.dir ? 1 : 0));
}

test("every recovered clampf copy agrees with every other", (t) => {
  const have = (cmd) => {
    try { execFileSync(cmd, ["--version"], { stdio: "ignore" }); return true; }
    catch { return false; }
  };
  if (!have("clang++")) { t.skip("clang++ not available"); return; }

  const found = recoveredClampfNamespaces();
  assert.ok(found.length >= 5,
    `expected at least 5 recovered clampf namespaces, found ${found.length}: `
    + found.map((x) => `${x.dir}(${x.ns})`).join(", "));

  const dir = mkdtempSync(path.join(tmpdir(), "clampf-all-"));
  try {
    const probe = path.join(dir, "probe.cpp");
    const bin = path.join(dir, "probe");

    const includes = found.map(({ dir }) => `#include "${dir}.h"`).join("\n");
    const calls = found.map(({ ns }, i) =>
      `    const float v${i} = ${ns}::clampf(x, lo, hi);`).join("\n");
    const compares = found.slice(1).map(({ ns }, i) =>
      `      if (v${i + 1} != v0) { std::printf("DIFFER %s x=%g lo=%g hi=%g: v0=%g v%d=%g\\n", ` +
      `"${ns}", x, lo, hi, v0, ${i + 1}, v${i + 1}); return 1; }`).join("\n");

    const cpp = `${includes}
#include <cstdio>
int main(){
  // Includes INVERTED ranges on purpose: the low bound is tested last in every copy, so
  // lo wins when hi < lo. A rewrite using min(max(v,lo),hi) would differ here and only
  // here, which is exactly the case worth cross-checking.
  const float xs[] = {-5.f,-1.f,-0.001f,0.f,0.25f,5.f,9.999f,10.f,10.001f,50.f};
  const float ranges[][2] = {{0.f,10.f},{2.f,1.f},{-1.f,1.f},{5.f,5.f},{10.f,0.f}};
  for (auto& r : ranges) {
    for (float x : xs) {
      const float lo = r[0], hi = r[1];
${calls}
      (void)v0;
${compares}
    }
  }
  std::printf("ALL ${found.length} AGREE\\n");
  return 0;
}
`;
    writeFileSync(probe, cpp);

    const args = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-o", bin, probe];
    for (const { dir } of found) args.push("-I", path.join(ROOT, "src", dir));
    for (const { dir } of found) args.push(path.join(ROOT, "src", dir, `${dir}.cpp`));
    execFileSync("clang++", args, { stdio: "pipe" });

    const out = execFileSync(bin, { encoding: "utf8" });
    assert.match(out, new RegExp(`ALL ${found.length} AGREE`), out);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
