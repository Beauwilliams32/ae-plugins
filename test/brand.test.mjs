// Gate for the Trident Studio brand system as applied to this repository.
//
// WHAT THE BRAND TOUCHES HERE. This repo ships no web UI, no installer and no icon slot:
// an After Effects effect is a PiPL resource plus code, and AE draws its own panel. So the
// brand lives in four places, and this file holds each of them to the source of truth:
//
//   1. brand/            the tile, the icon set, the tokens, the usage rules, the book
//   2. README.md         wears the tile and has a Brand section pointing at brand/DESIGN.md
//   3. tools/demo/       the demo harness's chart palette is the dark-theme token set
//   4. FEATURE-MAP.md    has the rows that say so
//
// WHY HASHES. "Drift" for a tile is any byte changing, so the files are pinned by SHA-256.
// That is deliberately strict: if Beau reissues the tile, copy the new files from
// build/brand-systems/ae/ into brand/ and update EXPECTED below in the same commit, which
// makes the change a visible, reviewable act rather than a quiet overwrite. The checks
// after the hash test (sizes, token values, contrast) say WHAT is wrong when the hash
// test says THAT something is.
//
// Contrast rules come from brand/DESIGN.md: 4.5:1 for text, 3:1 for large text, UI
// borders and chart marks.

import test from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { existsSync, readFileSync, readdirSync, statSync } from "node:fs";
import path from "node:path";

const ROOT = path.join(import.meta.dirname, "..");
const BRAND = path.join(ROOT, "brand");
const read = (p) => readFileSync(path.join(ROOT, p), "utf8");
const bytes = (p) => readFileSync(path.join(ROOT, p));
const sha = (p) => createHash("sha256").update(bytes(p)).digest("hex");

// Every file under brand/, pinned. Adding or removing a file there must touch this table.
const EXPECTED = {
  "brand/DESIGN.md": "fbb796d0ae898adc8292bbd03dd657a5a1d1fb1cf4034a7b94ed81eed7c5f878",
  "brand/brand-book.pdf": "17ae7d91a8d003f2476a833858b27bf871a4689658ed719f8a7a1dea88d30457",
  "brand/tokens.css": "1a8531e1c40318042489b3a7845487b588c5ddc89c9272376d13deb2a24aa0cc",
  "brand/tokens.json": "974fc36e17b2efdc7afc749b1b2ba5ba04364669237d8ab905b25e175343248c",
  "brand/assets/app-icon-1024.png": "7c2e7e888cfaf28a1a4b57270cc3bf916221b0685a0b22bff37e31cb02e5e502",
  "brand/assets/apple-touch-icon.png": "a5f5367cd116f80ceedd68de5f6de9d719a6cd4ae57d93a6a1c412b326b9236e",
  "brand/assets/favicon.ico": "4d19b7c87b328463a63ab96f2520575986321f5cc606eb61a84aeb483346cc33",
  "brand/assets/glyph-white.svg": "7e7f70d15b20d46efff21a44b7d5492aecae0cba040c86404b4382dceb9291a0",
  "brand/assets/icon-192.png": "b3c486f847ea841927cff255ef0aa3ddf820b3848366bca3d569291d1a6ac1e2",
  "brand/assets/icon-512.png": "5ee7ed9d8d3c396a6511e77e6ff8bb1b0ba8c21edef4c5cb5fc247557209bc95",
  "brand/assets/icon-maskable-512.png": "a1e518f95c9ee3a5904d77a0212a259deb31e24ec48ead9be76178e4dea092fb",
  "brand/assets/tile-bleed.svg": "fba82c24b301500691d204b0e2bcf13615770dff63385b451fbacbf81fcad2ba",
  "brand/assets/tile-maskable.svg": "2fbb18d4ae29ee66de1a6bd9fe5422939e05601b8456bbafb7186b214922e1c7",
  "brand/assets/tile.svg": "76615a75a9d4cf8fd44958fb6ca46f6b8159eb6bc00bf2d57e5f01c4993449d9",
};

const filesUnder = (dir, rel = "") =>
  readdirSync(dir, { withFileTypes: true }).flatMap((e) => {
    const r = rel ? `${rel}/${e.name}` : e.name;
    return e.isDirectory() ? filesUnder(path.join(dir, e.name), r) : [r];
  });

// ---- colour maths (WCAG 2.x relative luminance) -------------------------------------------

const rgb = (hex) => [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16));
const lin = (v) => { const c = v / 255; return c <= 0.03928 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4; };
const lum = (hex) => { const [r, g, b] = rgb(hex).map(lin); return 0.2126 * r + 0.7152 * g + 0.0722 * b; };
const contrast = (a, b) => {
  const [hi, lo] = [lum(a), lum(b)].sort((x, y) => y - x);
  return (hi + 0.05) / (lo + 0.05);
};

// ---- tokens.css parsing -------------------------------------------------------------------

// Returns { dark: {--name: value}, light: {--name: value} }. The light block only overrides,
// so light is dark with the light block laid over it, exactly as the cascade does.
function themes() {
  const css = read("brand/tokens.css");
  const block = (selector) => {
    const m = css.match(new RegExp(`${selector}\\s*\\{([^}]*)\\}`));
    assert.ok(m, `tokens.css has no ${selector} block`);
    return Object.fromEntries(
      [...m[1].matchAll(/(--[\w-]+)\s*:\s*([^;]+);/g)].map((x) => [x[1], x[2].trim()]));
  };
  const dark = block(String.raw`:root,\s*\[data-theme="dark"\]`);
  const light = { ...dark, ...block(String.raw`\[data-theme="light"\]`) };
  return { dark, light };
}

test("brand/ holds exactly the pinned files, byte for byte", () => {
  const actual = filesUnder(BRAND).map((f) => `brand/${f}`).sort();
  assert.deepEqual(actual, Object.keys(EXPECTED).sort(),
    "brand/ gained or lost a file: update EXPECTED in test/brand.test.mjs in the same commit");
  for (const [file, want] of Object.entries(EXPECTED)) {
    assert.equal(sha(file), want,
      `${file} has changed. If the tile or tokens were reissued on purpose, update EXPECTED`);
  }
});

test("the icon set exists at the sizes each platform asks for", () => {
  const PNG_SIG = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
  const png = (file) => {
    const b = bytes(file);
    assert.ok(b.subarray(0, 8).equals(PNG_SIG), `${file} is not a PNG`);
    // IHDR: width, height, bit depth, colour type (2 = RGB, 6 = RGBA).
    return { w: b.readUInt32BE(16), h: b.readUInt32BE(20), colourType: b[25] };
  };

  // [file, size, colour type]. The opaque ones (RGB) are the ones a store or the OS rounds
  // itself: iOS app icon, apple-touch-icon, maskable. The two PWA "any" icons keep the
  // tile's own rounded corners, so they carry alpha.
  const WANT = [
    ["brand/assets/icon-192.png", 192, 6],
    ["brand/assets/icon-512.png", 512, 6],
    ["brand/assets/icon-maskable-512.png", 512, 2],
    ["brand/assets/apple-touch-icon.png", 180, 2],
    ["brand/assets/app-icon-1024.png", 1024, 2],
  ];
  for (const [file, size, colourType] of WANT) {
    const got = png(file);
    assert.deepEqual(got, { w: size, h: size, colourType }, `${file} should be ${size}x${size}, colour type ${colourType}`);
  }

  // favicon.ico: an ICONDIR of three PNG-compressed images, 16, 32 and 48.
  const ico = bytes("brand/assets/favicon.ico");
  assert.equal(ico.readUInt16LE(0), 0, "ICO reserved field");
  assert.equal(ico.readUInt16LE(2), 1, "ICO type is icon");
  const count = ico.readUInt16LE(4);
  const sizes = [];
  for (let i = 0; i < count; i++) {
    const e = 6 + 16 * i;
    sizes.push(ico[e] || 256);
    const off = ico.readUInt32LE(e + 12);
    assert.ok(ico.subarray(off, off + 8).equals(PNG_SIG), `favicon.ico image ${i} is PNG data`);
  }
  assert.deepEqual(sizes, [16, 32, 48], "favicon.ico carries 16, 32 and 48 px images");
});

test("the tile is the product tile: coral to pink at 135 degrees, rounded 22%, white glyph", () => {
  const tokens = JSON.parse(read("brand/tokens.json"));
  assert.deepEqual(tokens.color.tile, ["#FF6B5E", "#E04E8B"]);
  assert.equal(tokens.tile.radius, "22%");
  assert.equal(tokens.tile["gradient-angle"], 135);

  for (const f of ["tile.svg", "tile-bleed.svg", "tile-maskable.svg"]) {
    const svg = read(`brand/assets/${f}`);
    const stops = [...svg.matchAll(/stop-color:(#[0-9A-Fa-f]{6})/g)].map((m) => m[1].toUpperCase());
    assert.deepEqual(stops, tokens.color.tile, `${f} gradient stops are the tile pair`);
    assert.match(svg, /fill="#FFFFFF"/, `${f} draws a white glyph`);
  }
  assert.match(read("brand/assets/glyph-white.svg"), /fill="#FFFFFF"/);

  // tile.svg is a 512 box whose corner radius is 22% of it: 112.64.
  assert.match(read("brand/assets/tile.svg"), /viewBox="0 0 512 512"/);
  assert.match(read("brand/assets/tile.svg"), /C0,50\.69,50\.69,0,112\.64,0/, "tile.svg corner radius is 22% of 512");
});

test("tokens.css defines the accent pair and both themes, and agrees with tokens.json", () => {
  const { dark, light } = themes();
  const t = JSON.parse(read("brand/tokens.json"));

  // The accent token, the one a drift would hit first.
  assert.equal(dark["--accent"].toUpperCase(), "#FF6B5E");
  assert.equal(dark["--accent-2"].toUpperCase(), "#E04E8B");
  assert.equal(dark["--on-accent"].toUpperCase(), "#0B0B0D");
  assert.equal(dark["--tile-a"].toUpperCase(), t.color.tile[0]);
  assert.equal(dark["--tile-b"].toUpperCase(), t.color.tile[1]);

  // Both themes: every surface and text token matches the JSON mirror.
  for (const [theme, set] of [["dark", dark], ["light", light]]) {
    for (const key of ["bg", "surface", "line", "text", "muted", "link"]) {
      assert.equal(set[`--${key}`].toUpperCase(), t.color[theme][key].toUpperCase(), `${theme} ${key}`);
    }
  }
  // Signals are shared across the fleet and identical in both themes.
  for (const key of ["success", "warning", "danger", "info"]) {
    assert.equal(dark[`--${key}`].toUpperCase(), t.color.signal[key].toUpperCase(), `signal ${key}`);
    assert.equal(light[`--${key}`], dark[`--${key}`], `signal ${key} must not change with the theme`);
  }
  // The whole accent scale.
  for (const [step, hex] of Object.entries(t.color["accent-scale"])) {
    assert.equal(dark[`--accent-${step}`].toUpperCase(), hex.toUpperCase(), `accent-${step}`);
  }
  // Motion honours reduced motion, and stays inside 160 to 320 ms.
  assert.match(read("brand/tokens.css"), /prefers-reduced-motion:\s*reduce/);
  const ms = ["fast", "base", "slow"].map((k) => parseInt(dark[`--motion-${k}`], 10));
  assert.deepEqual(ms, [160, 240, 320]);
});

test("the token pairs the brand uses meet WCAG: 4.5:1 for text, 3:1 for UI", () => {
  const { dark, light } = themes();
  const need = [];
  for (const [theme, set] of [["dark", dark], ["light", light]]) {
    for (const surface of ["--bg", "--surface"]) {
      for (const fg of ["--text", "--muted", "--link"]) need.push([theme, fg, surface, set, 4.5]);
    }
  }
  need.push(["dark", "--accent", "--bg", dark, 3]);               // focus rings, outlines, chart marks
  need.push(["dark", "--on-accent", "--accent", dark, 4.5]);      // primary button label
  need.push(["light", "--on-accent", "--accent", light, 4.5]);

  for (const [theme, fg, bg, set, min] of need) {
    const ratio = contrast(set[fg], set[bg]);
    assert.ok(ratio >= min, `${theme}: ${fg} ${set[fg]} on ${bg} ${set[bg]} is ${ratio.toFixed(2)}:1, needs ${min}:1`);
  }
});

test("the demo harness chart palette is the dark-theme token set, and reads on its background", () => {
  const src = read("tools/demo/harness.cpp");
  const { dark } = themes();

  const ROW = /const float (k\w+)\[3\] = \{c8\(0x(\w\w)\), c8\(0x(\w\w)\), c8\(0x(\w\w)\)\};\s*\/\/\s*(--[\w-]+)\s+(#[0-9A-Fa-f]{6})/g;
  const palette = [...src.matchAll(ROW)].map((m) => ({
    name: m[1], hex: `#${m[2]}${m[3]}${m[4]}`.toUpperCase(), token: m[5], said: m[6].toUpperCase(),
  }));
  const byName = Object.fromEntries(palette.map((p) => [p.name, p]));

  for (const need of ["kBg", "kLine", "kMuted", "kInk", "kAccent", "kAccent2", "kInfo", "kSuccess"]) {
    assert.ok(byName[need], `harness palette is missing ${need}`);
  }
  for (const p of palette) {
    assert.equal(p.hex, p.said, `${p.name}: the c8() bytes and the comment disagree`);
    assert.ok(dark[p.token], `${p.name}: ${p.token} is not a token`);
    assert.equal(p.hex, dark[p.token].toUpperCase(), `${p.name} drifted from ${p.token}`);
  }

  // Chart canvases are filled with the bg token, never the old blue-black literal.
  assert.doesNotMatch(src, /0\.05f,\s*0\.06f,\s*0\.09f/, "an old hard-coded chart background is back");
  assert.match(src, /fill\(kBg\[0\], kBg\[1\], kBg\[2\]\)/);

  // Anything that carries data must clear 3:1 against that background. The grid (kLine) is
  // decoration under the data, and is the one palette entry exempt from this.
  for (const name of ["kInk", "kMuted", "kAccent", "kAccent2", "kAccentDeep", "kInfo", "kSuccess"]) {
    if (!byName[name]) continue;
    const ratio = contrast(byName[name].hex, byName.kBg.hex);
    assert.ok(ratio >= 3, `${name} ${byName[name].hex} on bg is ${ratio.toFixed(2)}:1, needs 3:1`);
  }
});

test("README wears the tile and its Brand section points at DESIGN.md", () => {
  const readme = read("README.md");
  const tile = readme.match(/<img src="(brand\/assets\/[\w.-]+\.png)"/);
  assert.ok(tile, "README should open with the tile image");
  assert.ok(existsSync(path.join(ROOT, tile[1])), `README tile ${tile[1]} does not exist`);
  assert.ok(readme.indexOf(tile[0]) < readme.indexOf("\n# "), "the tile sits above the title");

  const section = readme.split(/^## /m).find((s) => s.startsWith("Brand\n"));
  assert.ok(section, "README has no '## Brand' section");
  for (const ref of ["brand/DESIGN.md", "brand/tokens.css", "brand/assets/", "test/brand.test.mjs"]) {
    assert.ok(section.includes(ref), `README Brand section should mention ${ref}`);
  }
});

test("FEATURE-MAP has the brand rows and they name a test that exists", () => {
  const map = read("FEATURE-MAP.md");
  const rows = map.split("\n").filter((l) => /^\|.*\*\*(Brand system|Demo harness chart palette)/.test(l));
  assert.equal(rows.length, 2, "FEATURE-MAP should have a Brand system row and a Demo harness chart palette row");
  for (const r of rows) assert.match(r, /test\/brand\.test\.mjs/);
  assert.ok(statSync(path.join(ROOT, "test/brand.test.mjs")).isFile());
});
