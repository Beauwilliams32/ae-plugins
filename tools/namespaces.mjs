// Recovered-namespace naming: three names that are NOT the same thing.
//
// For every namespace recovered before AutoFlickerFix the directory name happened to
// equal the C++ namespace, so tooling was free to treat one as the other. AutoFlickerFix
// breaks that: the bundle is `AutoFlickerFix`, the directory is `autoflickerfix`, and the
// C++ namespace inside it is `aff`. The differ keyed on the directory name and therefore
// found no symbols at all in a namespace whose ten functions were sitting right there.
//
// So the C++ namespace is now READ OUT OF THE HEADER rather than assumed, and the
// bundle mapping stays an explicit table because nothing in the recovered source can
// tell you which `.plugin` it came out of. `specs/<Plugin>.json` records that separately,
// and its `symbols.byScope.domain` entries are the cross-check.

import { existsSync, readFileSync, readdirSync } from "node:fs";
import path from "node:path";

/** Directory under `src/` -> the shipped bundle it was recovered from. */
export const BUNDLES = {
  glitch: "GlitchKit",
  lutblend: "LUTBlend",
  typeriser: "TypeRiser",
  spectra: "SpectraKeys",
  noisefield: "NoiseField",
  chromakey: "ChromaKeyAssist",
  speedramp: "SpeedRampCurve",
  echo: "EchoTrails",
  colorpulse: "ColorPulse",
  autoflickerfix: "AutoFlickerFix",
  beatsnap: "BeatSnap",
  pathscribe: "PathScribe",
  beatgrid: "BeatGridRuler",
  maskflow: "MaskFlow",
  shake: "ShakeCapture",
  motioncurvex: "MotionCurveX",
};

/** Recovered namespace directories, discovered from the tree. */
export function recoveredDirs(root) {
  return readdirSync(path.join(root, "src"), { withFileTypes: true })
    .filter((e) => e.isDirectory())
    .map((e) => e.name)
    .sort();
}

/**
 * The C++ namespace declared by `src/<dir>/<dir>.h`.
 *
 * Returns null when the header is missing or declares no namespace, so a caller can
 * skip rather than silently compare against a guess.
 */
export function namespaceOf(root, dir) {
  const header = path.join(root, "src", dir, `${dir}.h`);
  if (!existsSync(header)) return null;
  const m = readFileSync(header, "utf8").match(/^\s*namespace\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{/m);
  return m ? m[1] : null;
}

/** Find an installed bundle binary, across the AE versions this machine has. */
export function bundlePath(plugin) {
  for (const year of ["2026", "2025"]) {
    const p = `/Applications/Adobe After Effects ${year}/Plug-ins/Williams Media`
      + `/${plugin}.plugin/Contents/MacOS/${plugin}`;
    if (existsSync(p)) return p;
  }
  return null;
}
