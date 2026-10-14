# Williams Media — After Effects plugin fleet

Sixteen After Effects effects (`com.williamsmedia.aftereffects.*`), **recovered by
static analysis of the compiled bundles**.

## Why this repository is recovered rather than written

The original C++ source was destroyed on **2026-10-13** by an unsupervised
`rm -rf` during a workspace cleanup. The repository had **no git remote**, so there was
no clone to fall back on, and no backup existed anywhere on the machine — not in
Time Machine, not in an APFS snapshot, not in the `~/Drive-archive` recovery stages.

What survived were three generations of **built** bundles:

| Generation | Bundles |
|---|---|
| Installed in After Effects | 16 |
| Backup set 2026-09-25 (root-owned) | 15 |
| Backup set 2026-09-28 (root-owned) | 16 |

Those binaries are the source of everything in this repository. Nothing here was
copied from the lost source, because none of it exists.

The bundles were built **without stripping**, which is why this recovery is
worthwhile: the full C++ symbol table survived, so function names, namespaces and
exact signatures come back intact.

## What was recovered, and how

| Recovered | Method | Confidence |
|---|---|---|
| Effect name, menu name, category, effect/SDK version, flags | parsed from the `PiPL` resource (16-byte `8BIM` chunks) | **exact** |
| Function names, namespaces, classes, full signatures | un-stripped Mach-O symbol table, demangled | **exact** |
| Original source-file layout | filenames embedded in `__GLOBAL__sub_I_*.cpp` symbols | **exact** |
| **Parameters, in panel order, with units** | `EffectMain` string literals walked in address order | **recovered** — 137 across the fleet |
| **Dropdown option lists** | the popup's own `\|`-joined string | **recovered** — 26 lists |
| Popup-to-parameter ownership | adjacency only | **unresolved** — deliberately not asserted |
| User-facing messages and guard conditions | message strings | **exact text**, context inferred |
| Binary identity (arch, size, MD5, SHA-256) | direct | **exact** |

Run it yourself:

```bash
node tools/recover.mjs          # extract specs/<Effect>.json from the installed bundles
node tools/render-specs.mjs     # render specs/<Effect>.md + CATALOG.md
```

`tools/recover.mjs` is **read-only** with respect to the installed bundles. It never
modifies, moves or copies them.

## What is *not* recovered

Be clear-eyed about this:

- **Function bodies.** The DSP math inside each function is machine code. Names and
  signatures are free; behaviour needs per-function disassembly. Not done yet.
- **The build system.** No Makefile, no CMake, no `RELEASE-MATRIX.md`. The SDK
  version is known (`0x000d001d`); the flags are not.
- **The DSP test suite.** Gone.
- **Anything not referenced by a symbol or string.** Templates instantiated and
  inlined everywhere leave no trace.

So this repository currently contains a **complete, exact architectural
specification** of the fleet, not a buildable product.

## The fleet

See **[CATALOG.md](CATALOG.md)** for the full table and recovered findings. Current
totals: **575 named symbols**, **175 parameter-label candidates**, **43 behavioural
message strings**, across 16 universal binaries (Intel + Apple silicon).

Two findings already worth acting on:

- **SpectraKeys breaks the naming convention.** Fifteen effects register as
  `WM_<Name>`; SpectraKeys registers as `WMH SpectraKeys` and is the only name with a
  space in it.
- **The fleet is a single build pass.** Every effect reports version `0x00080001`
  against SDK `0x000d001d`.

## Domain engines recovered so far

Beyond the shared `spectra::` audio module, two effects carry their own
self-contained engines. LUTBlend's is partially recovered:

| Module | Contents | State |
|---|---|---|
| `spectra::` | FFT, Hann window, STFT, band analysis, falloff envelope | **recovered + verified** (4 effects) |
| `lutblend::` | `clampf`, `lerpf`, `luma709`, `CubeLUT::valid` + layout | **partially recovered** (`parseCube`, `sampleLUT`, `applyLUTBlend` pending) |
| `typeriser::` | `clampf`, `karaokeWordAt`, `wordProgress`, `cueIndexAt` + `Cue` layout | **partially recovered** (`parseSrt`, `splitWords`, `parseTimestamp`, `ease` pending) |

One cross-bundle detail worth recording: `spectra::clampf` and `lutblend::clampf`
were recovered from two different plugins in two different namespaces and turned
out **instruction-for-instruction identical**, down to the `fcsel` ordering that
makes the low bound win on an inverted range. A test asserts the two still agree.

## Roadmap

1. **Recover the DSP.** Disassemble per effect, starting with the effects that expose
   real domain namespaces (`spectra::` in SpectraKeys is the richest: radix-2 FFT,
   Hann window and coherent gain, mono downmix, spectrogram band analysis, dB
   conversion). Recovered headers make this tractable.
2. **Reconstruct a buildable tree.** Rebuild `src/<Effect>/` against the AE SDK
   pinned at `0x000d001d`, driven by the recovered parameter tables.
3. **Improve.** Normalise the SpectraKeys naming, then address the quality gaps the
   recovered messages expose.
4. **Re-sign and qualify.** The last shipped artefacts were ad-hoc signed, so nothing
   should be installed or sold until the AE host-acceptance matrix is re-recorded on
   rebuilt binaries.

## Verifying a recovery

Every spec records the MD5 and SHA-256 of the binary it came from, so any claim here
can be checked against the artefact:

```bash
md5 "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/SpectraKeys.plugin/Contents/MacOS/SpectraKeys"
jq -r .binary.md5 specs/SpectraKeys.json
```

## Provenance and licence

These are Williams Media's own effects. The recovered specifications describe
Williams Media software and are not a redistribution of any third-party code. The
Adobe After Effects SDK itself is **not** included and remains governed by Adobe's
licence — the recovered declarations reference SDK types by name only.
