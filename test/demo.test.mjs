// Gate for the fleet demo harness (tools/demo/harness.cpp) and its publisher
// (tools/demo/publish.mjs).
//
// The harness CALLS the recovered namespaces on synthetic input and writes PPM
// panels plus results.json. This test builds it the way tools/demo/build.sh does,
// runs it into a temporary folder, and checks the contract the header promises:
// every listed panel exists as a well-formed P6 image, every number is finite, and
// the disclaimer that none of it is an AE render is present.
//
// The second test then publishes from those numbers and checks the page the site
// actually serves: no "undefined" anywhere (a mistyped stat key must never reach a
// public page), every referenced image present, one section per effect.
//
// It also pins one bug: bandValues(spectrogram, band, hopRateHz) takes the hop RATE
// as its third argument. The first version of the harness passed a time there, which
// returned an empty vector at t = 0 and frame 0 otherwise, so the band peaks came out
// NaN. A finite, non-zero peak per band is the regression check.
//
// Skips (rather than fails) where clang++ is absent, like the other compile tests,
// and where Pillow is absent for the publish half.

import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, readdirSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");

const NAMESPACES = readFileSync(path.join(ROOT, "tools/demo/build.sh"), "utf8")
  .match(/NAMESPACES=\(([^)]*)\)/)[1].trim().split(/\s+/);

const have = (bin, args = ["--version"]) => {
  try { execFileSync(bin, args, { stdio: "ignore" }); return true; }
  catch { return false; }
};

// One build, shared by both tests: the compile is the slow part.
const work = { dir: null, out: null, results: null };

test.after(() => {
  if (work.dir) rmSync(work.dir, { recursive: true, force: true });
});

test("demo harness builds, runs, and writes the panels and numbers it promises", (t) => {
  if (!have("clang++")) { t.skip("clang++ not available — demo harness check skipped"); return; }

  work.dir = mkdtempSync(path.join(process.env.TMPDIR || tmpdir(), "fleet-demo-"));
  const present = NAMESPACES.filter((ns) => existsSync(path.join(ROOT, "src", ns, `${ns}.h`)));
  assert.equal(present.length, NAMESPACES.length, "every namespace build.sh names is recovered");

  const bin = path.join(work.dir, "fleet-demo");
  execFileSync("clang++", [
    "-std=c++17", "-O2", "-Wall", "-Wno-unused-variable", "-Wno-unused-function",
    ...present.map((ns) => `-I${path.join(ROOT, "src", ns)}`),
    path.join(ROOT, "tools/demo/harness.cpp"),
    ...present.map((ns) => path.join(ROOT, "src", ns, `${ns}.cpp`)),
    "-o", bin,
  ], { stdio: "pipe" });

  work.out = path.join(work.dir, "out");
  execFileSync(bin, [work.out], { stdio: "pipe" });

  const raw = readFileSync(path.join(work.out, "results.json"), "utf8");
  assert.doesNotMatch(raw, /NaN|Infinity/, "every number in results.json is finite");
  work.results = JSON.parse(raw);
  assert.equal(work.results.kind, "computed-from-recovered-source");
  assert.match(work.results.disclaimer, /none of this is an AE render/);

  const demos = Object.entries(work.results.demos);
  // The fleet is sixteen effects. Fifteen have recovered source; MotionCurveX does
  // not, and its card says so instead of inventing a function.
  assert.equal(demos.length, 16, "one demo per effect in the fleet");

  const specOnly = [];
  let panelTotal = 0;
  for (const [name, d] of demos) {
    assert.equal((d.panels ?? []).length, (d.panelLabels ?? []).length, `${name}: one label per panel`);
    if ((d.panels ?? []).length === 0) { specOnly.push(name); continue; }
    assert.ok(d.recovered.length > 0, `${name}: names the recovered functions it ran`);
    panelTotal += d.panels.length;
    for (const panel of d.panels) {
      const buf = readFileSync(path.join(work.out, panel));
      const header = buf.subarray(0, 32).toString("latin1").match(/^P6\s+(\d+)\s+(\d+)\s+255\s/);
      assert.ok(header, `${panel} is a binary P6 image`);
      const [, w, h] = header.map(Number);
      assert.equal(buf.length - header[0].length, w * h * 3, `${panel} pixel payload matches ${w}x${h}`);
    }
  }
  assert.deepEqual(specOnly, ["motioncurvex"], "exactly one effect is a spec card, and it is named");
  assert.ok(panelTotal >= 30, `the fleet computes real panels (got ${panelTotal})`);

  // The hop-rate bug: a band peak of 0 or NaN means bandValues was called wrongly.
  const s = work.results.demos.spectra;
  for (const k of ["lowBandPeak", "midBandPeak", "highBandPeak"]) {
    assert.equal(typeof s[k], "number", `${k} is a number, not NaN`);
    assert.ok(s[k] > 0 && s[k] <= 100, `${k} = ${s[k]} sits inside the band's 0..100 output range`);
  }

  // The scored demos are the ones worth pinning: they assert against ground truth.
  const bs = work.results.demos.beatsnap;
  assert.equal(bs.matched, bs.trueClicks, "BeatSnap finds every click it was built with");
  const ck = work.results.demos.chromakey;
  assert.ok(ck.falsePositiveRate <= 0.25 && ck.falseNegativeRate <= 0.35,
    `chroma key scoring stays sane (fp ${ck.falsePositiveRate}, fn ${ck.falseNegativeRate})`);
  const sc = work.results.demos.shake;
  assert.ok(sc.trialsDiffering > 0,
    "shake::median still disagrees with the true median under reordering — the recorded defect");
});

test("the publisher builds a page whose every figure comes from those numbers", (t) => {
  if (!work.results) { t.skip("harness did not run — publish check skipped"); return; }
  if (!have("python3", ["-c", "import PIL"])) {
    t.skip("python3 + Pillow not available — publish check skipped");
    return;
  }

  const pageDir = path.join(work.dir, "site");
  execFileSync("node", [
    path.join(ROOT, "tools/demo/publish.mjs"),
    "--out", pageDir,
    "--results", path.join(work.out, "results.json"),
  ], { stdio: "pipe" });

  const html = readFileSync(path.join(pageDir, "index.html"), "utf8");
  // A stat template that resolves to nothing is the failure mode this gate exists
  // for: it must never reach a page a visitor can read.
  assert.doesNotMatch(html, /undefined|NaN|\[object Object\]/, "no unresolved template values");
  assert.doesNotMatch(html, /\{[a-zA-Z]+\}/, "no unsubstituted {key} placeholders");

  // Every image the page points at exists next to it. The src is root-absolute
  // (the site's integrity test rejects relative references), so the file on disk is
  // the basename of that path.
  const srcs = [...html.matchAll(/src="(\/products\/after-effects-plugins\/demos\/[^"]+\.webp)"/g)]
    .map((m) => m[1]);
  assert.ok(srcs.length >= 30, `the page shows the computed panels (${srcs.length})`);
  const referenced = srcs.map((s) => path.basename(s));
  for (const img of referenced) {
    assert.ok(existsSync(path.join(pageDir, img)), `${img} was published alongside the page`);
  }
  // And nothing orphaned was written.
  const onDisk = readdirSync(pageDir).filter((f) => f.endsWith(".webp"));
  assert.equal(onDisk.length, referenced.length, "every converted panel is referenced");

  // One section per effect, and the spec card is present but marked.
  for (const key of Object.keys(work.results.demos)) {
    assert.match(html, new RegExp(`id="${key}"`), `section for ${key}`);
  }
  assert.equal((html.match(/class="chip ok"/g) || []).length, 15, "fifteen cards claim computed output");
  assert.equal((html.match(/class="chip wait"/g) || []).length, 1, "one card is marked spec only");
  assert.match(html, /not After Effects renders/i, "the page says what these are not");
});
