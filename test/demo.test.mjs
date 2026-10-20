// Gate for the fleet demo harness (tools/demo/harness.cpp).
//
// The harness CALLS the recovered namespaces on synthetic input and writes PPM
// panels plus results.json. This test builds it the way tools/demo/build.sh does,
// runs it into a temporary folder, and checks the contract the header promises:
// every listed panel exists as a well-formed P6 image, every number is finite,
// and the disclaimer that none of it is an AE render is present.
//
// It also pins one bug: bandValues(spectrogram, band, hopRateHz) takes the hop
// RATE as its third argument. The first version of the harness passed a time
// there, which returned an empty vector at t = 0 and frame 0 otherwise, so the
// band peaks came out NaN. A finite, non-zero peak per band is the regression check.
//
// Skips (rather than fails) where clang++ is absent, like the other compile tests.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");

const NAMESPACES = readFileSync(path.join(ROOT, "tools/demo/build.sh"), "utf8")
  .match(/NAMESPACES=\(([^)]*)\)/)[1].trim().split(/\s+/);

const haveClang = () => {
  try { execFileSync("clang++", ["--version"], { stdio: "ignore" }); return true; }
  catch { return false; }
};

test("demo harness builds, runs, and writes the panels and numbers it promises", (t) => {
  if (!haveClang()) { t.skip("clang++ not available — demo harness check skipped"); return; }

  const dir = mkdtempSync(path.join(process.env.TMPDIR || tmpdir(), "fleet-demo-"));
  try {
    const present = NAMESPACES.filter((ns) => existsSync(path.join(ROOT, "src", ns, `${ns}.h`)));
    assert.equal(present.length, NAMESPACES.length, "every namespace build.sh names is recovered");
    const bin = path.join(dir, "fleet-demo");
    execFileSync("clang++", [
      "-std=c++17", "-O2", "-Wall", "-Wno-unused-variable", "-Wno-unused-function",
      ...present.map((ns) => `-I${path.join(ROOT, "src", ns)}`),
      path.join(ROOT, "tools/demo/harness.cpp"),
      ...present.map((ns) => path.join(ROOT, "src", ns, `${ns}.cpp`)),
      "-o", bin,
    ], { stdio: "pipe" });

    const out = path.join(dir, "out");
    execFileSync(bin, [out], { stdio: "pipe" });

    const raw = readFileSync(path.join(out, "results.json"), "utf8");
    assert.doesNotMatch(raw, /NaN|Infinity/, "every number in results.json is finite");
    const results = JSON.parse(raw);
    assert.equal(results.kind, "computed-from-recovered-source");
    assert.match(results.disclaimer, /none of this is an AE render/);

    const demos = Object.entries(results.demos);
    assert.ok(demos.length >= 8, "all eight demos ran");
    for (const [name, d] of demos) {
      assert.equal(d.panels.length, d.panelLabels.length, `${name}: one label per panel`);
      assert.ok(d.recovered.length > 0, `${name}: names the recovered functions it ran`);
      for (const panel of d.panels) {
        const buf = readFileSync(path.join(out, panel));
        const header = buf.subarray(0, 32).toString("latin1").match(/^P6\s+(\d+)\s+(\d+)\s+255\s/);
        assert.ok(header, `${panel} is a binary P6 image`);
        const [, w, h] = header.map(Number);
        assert.equal(buf.length - header[0].length, w * h * 3, `${panel} pixel payload matches ${w}x${h}`);
      }
    }

    const s = results.demos.spectra;
    for (const k of ["lowBandPeak", "midBandPeak", "highBandPeak"]) {
      assert.equal(typeof s[k], "number", `${k} is a number, not NaN`);
      assert.ok(s[k] > 0 && s[k] <= 100, `${k} = ${s[k]} sits inside the band's 0..100 output range`);
    }
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
