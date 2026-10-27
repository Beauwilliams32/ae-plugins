<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="brand/assets/lockup-dark.svg">
    <img src="brand/assets/lockup-light.svg" alt="Williams Media AE, by Trident Studio" width="480" height="120">
  </picture>
</p>

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
| **Parameters: type, `uu.id`, name, bounds, defaults** | decoding the `PF_ADD_PARAM` blocks in `EffectMain`'s `PF_Cmd_PARAMS_SETUP` | **recovered** — 164 across the fleet, every one with a type and id. See `PARAMETERS.md` |
| **Dropdown option lists** | the popup's own `\|`-joined string | **recovered** — 26 lists |
| Popup-to-parameter ownership | each popup's own `PF_ADD_PARAM` block | **resolved** — the parameter table gives every popup its choices |
| User-facing messages and guard conditions | message strings | **exact text**, context inferred |
| Binary identity (arch, size, MD5, SHA-256) | direct | **exact** |

If a local AE SDK is present, three more things come out of the same bundles:

| Recovered | Method | Confidence |
|---|---|---|
| **Capability contract** — every `eGLO`/`eGL2` bit named | decoded against the SDK's own `AE_Effect.h` | **exact**. See `CAPABILITIES.md` |
| **The PiPL resource itself** | Rez sources generated from the specs, then byte-compared against the shipped bundle | **exact** — all 16 round-trip byte-identically. See `packaging/` |
| **The parameter table** | `PF_ADD_PARAM` blocks in `EffectMain` | **recovered**. See `PARAMETERS.md` |

Run the whole chain yourself:

```bash
npm run recover                 # specs + Catalog + PiPL sources + capability matrix
npm test                        # the whole gate, including the byte-identity proofs
```

`tools/recover.mjs` is **read-only** with respect to the installed bundles. It never
modifies, moves or copies them.

## Where the fleet stands today

**[RECOVERY-STATUS.md](RECOVERY-STATUS.md)** is the coverage table, and it is **generated**
by `tools/recovery-status.mjs` from the differ's own output — so its numbers are measured,
and `test/status.test.mjs` fails if it ever stops matching a fresh run. At this revision:
**114 of 155 named symbols recovered, 56 of them instruction-identical, 7,500 of the
fleet's 20,883 authored instructions (35.9%)**, across 13,897 lines of recovered source.
Six effects are at 100% instruction coverage and one — MotionCurveX — has no recovered
source at all. The two sentences above are the one copy of these figures that is not
generated; if they disagree with the table, the table wins.

```bash
npm run status                  # regenerate RECOVERY-STATUS.md
```

## What is *not* recovered

Be clear-eyed about this:

- **Most function bodies.** Sixteen namespaces exist under `src/` (see below); the other
  effects' DSP is still machine code. Names and signatures are free, behaviour needs
  per-function disassembly, and that is the bulk of the remaining work.
- **Several function bodies.** `echo`'s host integration, twelve effects' bodies, and four of
  BeatSnap's eleven functions are still unread. Where a body is unread it is **absent rather than
  stubbed**, and a test asserts it stays absent, so a stub cannot quietly make a namespace look
  complete. `lutblend::parseCube` (the fleet's largest function) was reconstructed from its recovered
  specification on 2026-10-27, and `motioncurve::` is **built from scratch** — the bundle keeps only
  its symbol list, which is why that namespace has no transcription to compare against and is
  verified by property checks instead.
- **The original build system.** No Makefile, no CMake. The SDK version is known and
  verified (`0x000d001d` — AE SDK 25.6); the original flags are not.
- **The original DSP test suite.** Gone.
- **Anything not referenced by a symbol or string.** Templates instantiated and
  inlined everywhere leave no trace.
- **Host acceptance.** No After Effects has ever loaded a rebuilt plugin. The two
  that build are structurally verified and their engines are executed against
  synthetic frames, but whether AE accepts them, and whether the output is any good,
  is untested. See `plugins/*/README.md` for exactly what is and is not claimed.

So this repository contains a **complete architectural specification** of the fleet,
fifteen recovered source namespaces, and two effects that build into loadable bundles —
**not** a finished or verified product.

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

## Recovered source

Sixteen namespaces under `src/`. Fifteen are transcribed from the disassembly with per-function commentary citing the
instructions that establish each behaviour; `motioncurve::` is the exception — built from scratch from its surviving
symbol list, and marked as such in its own row:

| Module | Contents | State |
|---|---|---|
| `typeriser::` | The whole SRT karaoke engine: `parseSrt`, `parseTimingLine`, `parseTimestamp`, `trim`, `splitWords`, `wrapWords`, `wordStyle`, `ease`, `karaokeWordAt`, `wordProgress`, `cueIndexAt`, plus `Cue`, `WordStyle` and `Anim` | **complete — all 12 authored functions** |
| `glitch::` | `clampf`, `clampi`, `hashKey`, `patternFrame` | **complete for the scalar layer, and INSTRUCTION-IDENTICAL** (4/4). The render engine `glitch::apply<T>` is unrecovered, but its `View<T>`/`Params` layouts, its mode field and its one-based five-way dispatch ARE recovered — see `test/glitch.test.mjs`, which re-derives the offsets from the bundle |
| `speedramp::` | `easeValue`, `easeIntegral`, `speedAt`, `mapTime`, `computeBlend`, plus `RampParams` and `FrameBlend` | **complete — all 5 functions, 261 instructions** |
| `lutblend::` | `clampf`, `lerpf`, `luma709`, `CubeLUT::valid`, `sampleLUT`, `makeIdentity`, `applyLUTBlend`, `parseCube`, plus the `CubeLUT` layout | **8 of 8** — `parseCube` (1924 instructions, the largest function in the fleet) landed 2026-10-27 as a reconstruction from the recovered specification, verified by `selfcheck.cpp` against all twelve error messages and every validation rule |
| `echo::` | `clampf`, `blendPixel`, `compositeOver`, `buildLadder`, `sampleBilinear`, `transformBilinear`, plus the corrected `ImageF` layout and `BlendMode` | **complete — all 6 functions**. Recovering `transformBilinear` also CORRECTED `ImageF`: `data` had been modelled as a raw `float*` on the strength of a read that a `std::vector`'s begin pointer produces identically. The host integration is unread |
| `colorpulse::` | The whole analyser: `clampf`, `smoothScalar`, `luma709`, `smoothColor`, `saturationBoost`, `regionBounds`, `averageColor`, `brightestRegionColor`, `dominantColor`, `sampleColor`, plus the `RGB`/`Rect` layouts and the `Region`/`Mode` selectors | **complete — all 10 functions, 1123 instructions**, and five are INSTRUCTION-IDENTICAL. The host integration is unread |
| `aff::` | The AutoFlickerFix DSP: `clampf`, `dbToGainClamp`, `computeGain`, `centerRegion`, `meanOf`, `medianOf`, `trimmedMean` (both overloads), `medianFilter`, `temporalTarget`, plus the `GainClamp` and `RegionBounds` layouts | **complete — all 10 functions, 719 instructions, 7 of them INSTRUCTION-IDENTICAL**. The host integration (`EffectMain`, `SampleWorldStat`, `DrawStatsBar`) is unread |
| `spectra::` | FFT, Hann window, STFT, band analysis, falloff envelope, `downmixToMono`, plus `Spectrogram` | **complete — all 13 functions, shared by 4 effects**, and 7 of them INSTRUCTION-IDENTICAL. The host integration is unread |
| `motioncurve::` | **Built from scratch, not transcribed (2026-10-27, row AE-06):** the eleven signatures the bundle still names — `clampf`, `quadraticPeakOffset`, `medianOfWindow`, `downsample2x`, `buildPyramid`, `sadAt`, `cropCenter`, `accumulate`, `smoothMovingAverage`, `temporalMedianFilter`, `estimateTranslation` — implemented as the translation-only stabilisation pipeline they describe (2x box downsample → SAD search → sub-pixel parabolic peak → coarse-to-fine pyramid walk → path accumulation → moving-average or median smoothing). `Image` and `Vec2` are MODELLED, since no surviving symbol carries their layout | **10 of 11** — 1 identical (`clampf`), 9 differing (expected: the bodies are new, so matching codegen is not the goal). The one absent symbol is `motioncurve::_GLOBAL__N_1`, the bundle's 80-instruction anonymous-namespace static *initialiser*, not a domain function. Verified by `selfcheck.cpp` instead of by the differ: known integer shifts are recovered to within 1 px at four magnitudes, a static scene reports exactly zero, a one-frame outlier is discarded by the median but not by the average, and every documented edge case is asserted |
| `beatsnap::` | The onset-detection core: `clampf`, `isPowerOfTwo`, `medianOfWindow`, `adaptiveThreshold`, `hannWindow`, `fftRadix2`, `downmixToMono` | **7 of 11** — `computeFlux` (416), `renderValues` (190), `pickOnsets` (136) and `detectOnsets` (47) are outstanding. `medianOfWindow` is **instruction-identical** (58/58), and the five shared-DSP copies are checked by divergence parity against the `spectra::` originals rather than by assuming a copy of a copy is faithful |
| `chromakey::` | The colour and statistics layer: `clampf`, `rgbToYCbCr` (Rec.601), `sensitivityToThreshold`, `chromaDistance`, `hueAngle`, `angularDiff`, `percentile`, `percentileUnsorted`, `gradientMagnitude`, `spillScore`, `uniformityScore` | **11 of 15** namespace symbols — the denominator is the differ's own count, which sees one symbol per template instantiation, so `analyzeFrame<T>` and `renderView<T>` account for two each. Outstanding: the four `analyzeFrame<T>`/`renderView<T>` instantiations. `StoreReadout` is outside the namespace and takes a `PF_InData*`, so it stays out of `src/`. Six of the eleven are **instruction-identical**. `spillScore` (82) compiles to 80 with identical outputs (clang elides a provably-redundant lower clamp) and `uniformityScore` (203) compiles to exactly 203 with three register-only differences; both are pinned by self-checks rather than by the differ alone |
| `noisefield::` | The noise core: `clampf`, `clampi`, `fbmAmplitudeSum`, `PermTable::build`, `noise3`, `fbm3`, and `kGrad3` — the twelve edge gradients of Perlin's improved noise, read out of the bundle's `__TEXT,__const` | **6 of 10** — three instruction-identical (`clampf`, `clampi`, `fbmAmplitudeSum`); `noise3` (183) and `fbm3` (51) compile to the bundle's instruction COUNT with a different register schedule, and are verified instead against an independently written textbook oracle — 4000 points agreeing to 7.8e-07. `PermTable::build` is Fisher-Yates over xorshift32 into a permutation written TWICE, which is what keeps `p[AA + 1]` in range. `fieldAt` (226), `curlField` (163) and the two `apply<T>` loops (408/412) are outstanding |
| `pathscribe::` | The analysis pipeline, shared with SpectraKeys: `clampf`, `isPowerOfTwo`, `linearToDb`, `dbToLinear`, `hannWindow`, `hannCoherentGain`, `fftRadix2`, `downmixToMono`, `analyse`, `Spectrogram`, plus its own `spanCoverage`, `binEdgeHz`, `smoothStep` | **13 of 18** — eight INSTRUCTION-IDENTICAL. Ten of the eighteen are SpectraKeys' code, so one recovery covers four effects; the copy is checked by divergence parity rather than by matching, since four of the ten legitimately differ. `barLevels` (112), `smoothedLevelsAtFrame` (179), `waveformColumns` (80) and the two rasterisers (173/147) are outstanding |
| `beatgrid::` | The beat maths: `clampf`, `isPowerOfTwo`, `downmixToMono` (shared), `subdivisionFactor`, `phaseSawtooth`, `phaseInLayerTime`, `OnsetEnvelope::frameTimeSeconds` | **7 of 12** namespace symbols — twelve, not the fourteen entries `specs/BeatGridRuler.json` lists, because two of those are `std::vector` member instantiations that take beatgrid types and the differ counts them under `std`, where they are not this effect's code. The four own functions all compile to the bundle's instruction counts, two functions are instruction-identical, and the subdivision factors (0.5, 0.25, 0.125 and **4.0** — the odd one out) were read out of the literal pool rather than guessed. `computeOnsetEnvelope` (436), `estimateTempoBpm` (578), `estimatePhaseSeconds` (138), `generateBeatGrid` (94) and `analyzeBeatGrid` (57) are outstanding, and `WriteMarkerGrid` (811) stays out of `src/` because it takes Adobe SDK types |
| `maskflow::` | The geometry helpers: `clampf` (shared, and the fifth carrier), `edgeCostScaleFromTolerance`, `computeDownsampleDims`, `smoothstep` | **4 of 9** — three came out INSTRUCTION-IDENTICAL on the first transcription. `smoothstep` extrapolates below its lower edge (a clamp that clamps nothing — almost certainly a typo for 0 in the original, and at x = -1 it returns 5) and returns negative infinity when its edges are equal. `downsampleBoxFilter` (126), `upsampleBilinear` (168), `buildMatte` (169), `computeMatteFullRes` (151) and `computeArrivalField` (290) need `Image` first and are outstanding |
| `shake::` | `clampf` (shared) and `median` | **2 of 11** — `trackTranslation` alone is 960 instructions and is the effect's whole point, and every other function takes `Vec2`, `ShakeProfile` or `LoopMode`, none recovered. `median`'s even case is NOT the true median: its second `nth_element` call covers a ONE-ELEMENT range and pins nothing, so a permutation of 0..999 returns 469.5 against a true median of 499.5 — measured, and recorded rather than fixed because correcting it would change the effect |

Three fleet-wide duplications are recorded rather than silently deduplicated, because
each was verified rather than assumed:

* `clampf` is the fleet's most duplicated idiom: **15 of the 16 bundles** carry one, and the
  ten that are recovered here are instruction for instruction identical. `tools/dsp-duplication.mjs`
  verifies FIVE carriers against each other — 27 copies of nine functions, no drift. (Measured by listing `src/*/` for a `clampf` definition, which is
  what the test below discovers too; chromakey's copy was the ninth.) `test/clampf.test.mjs` compares all of them against each other,
  including on INVERTED ranges, which is the only input where a `min(max(v,lo),hi)`
  rewrite would diverge. The test discovers the copies from the tree, so a newly
  recovered one is covered without editing it.
* `typeriser::wordStyle` re-implements the five easing curves `typeriser::ease` already
  has, inline. Both were recovered independently and a test drives each against the
  other — including the one place they legitimately differ.
* `lutblend::applyLUTBlend` inlines `lutblend::luma709` with the same constants and the
  same `fmadd` order. The transcription calls the function instead, which is
  bit-identical.

## Building and running the effects

Two effects build into real universal (`x86_64` + `arm64`) `.plugin` bundles:

```bash
./tools/build-plugin.sh GlitchKit
./tools/build-plugin.sh LUTBlend
```

Each joins a recovered spec (the PiPL), recovered DSP, and newly written host
integration. The provenance split is enforced by directory: `src/` holds only
transcriptions of the shipped binaries, `plugins/` holds new code.

`test/build.test.mjs` runs those builds in the gate and checks the bundles export
`EffectMain`, are universal, and carry a PiPL payload **byte-identical** to the shipped
bundle's.

It also **compares the rebuilt bundle against the shipped one, field by field**, on
everything a host can see: architectures, `PkgInfo`, both signatures and their
`Identifier`, whether resources are sealed, and every `Info.plist` key the original
has. That comparison is what turned three assumptions into measured facts — the
rebuild carried the creator code `????` against a shipped `FXTC`, its binary was thin
`arm64` against a universal original, and it was never `codesign`ed, so it reported an
`Identifier` of the executable's name where the original reports its bundle id and
seals its `Info.plist` and `Resources`. All three now match, and a regression fails
the build test.

**Their render engines are also executed**, not merely compiled. `GlitchRender.cpp` and
`LutRender.cpp` are split out of the host integration so `test/glitchrender.test.mjs`
and `test/lutrender.test.mjs` can drive them on synthetic frames — checking determinism,
that each Drive/mode branch reads what it should, that the 8-bit and 16-bit paths agree,
and that degenerate frames are safe. Six negative controls confirm those tests can fail;
one of them reproduces a real 16-bit stride bug that was written once in this repo.

```bash
npm test                        # the whole gate
node tools/codegen-diff.mjs glitch   # instruction-level fidelity per namespace
```

## Demonstrating the effects

Compiled code that is never run is a claim, not a demonstration. `tools/demo/` executes
the recovered namespaces on synthetic input and publishes what comes out:

```bash
npm run demo                                     # build tools/demo/harness.cpp, run it
node tools/demo/publish.mjs --out <site-dir>     # turn the panels into a WebP page
```

`tools/demo/harness.cpp` is **new code** (it lives in `tools/`, like every other
harness here) that calls each recovered namespace and writes one PPM panel plus the
numbers per effect into `results.json`. Three things make the output worth trusting:

* **The measured demos are scored, not admired.** Where a synthetic input has a known
  answer the demo reports the error against it: `ChromaKeyAssist` sweeps sensitivity and
  reports the false-positive/false-negative rate against the plate's ground-truth mask,
  and `BeatSnap` detects five of five clicks within a hop of their true times.
* **`shake::median`'s defect is shown on purpose.** The same multiset shuffled 200 times
  returns 28 different answers, because the shipped code's second `nth_element` range is
  one element wide and libc++ elides it. A demo page that only displayed clean output
  would be hiding the most useful thing in that bundle.
* **MotionCurveX is a spec card, not a picture.** It has no recovered source, so it gets
  its symbol list and an architecture paragraph instead of an invented illustration.

Nothing in the demo output is an After Effects render: no host has loaded a rebuilt
bundle yet, so an AE screenshot does not exist and none is imitated. `test/demo.test.mjs`
builds and runs the harness and then publishes from it, so a stat that resolves to
nothing fails the gate instead of reaching a page.

`tools/demo/publish.mjs` writes the static page under `trident-tools`'s
`public/products/after-effects-plugins/demos/`; the site's `fleet-demos` test gates what
actually shipped.

## Differential codegen verification

`tools/codegen-diff.mjs` compiles a recovered namespace and compares each
function's instruction stream against the shipped bundle's. For small routines the
match is exact and unforgeable — `glitch::hashKey` reproduces all 34 instructions
and all 16 constants, and the glitch namespace is 4/4 identical:

```
$ node tools/codegen-diff.mjs glitch
  absent  glitch::apply(IhEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEjf) (not implemented — 1157 instrs)
  MATCH   glitch::clampf  5 instructions, identical
  MATCH   glitch::patternFrame  7 instructions, identical
  MATCH   glitch::hashKey  34 instructions, identical
  absent  glitch::apply(ItEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEjf) (not implemented — 1228 instrs)
  MATCH   glitch::clampi  5 instructions, identical

4 identical, 0 differing, 2 not implemented
```

Two things about that output are worth reading rather than skimming. The comparison unit is
the full MANGLED SYMBOL, not the function name, so `glitch::apply` appears twice — it has
two instantiations, and both are reported as absent rather than one hiding the other. And
the three differ defects listed below were each found by noticing that a function the
bundle and this source clearly agreed on was being reported as differing.

It found a real transcription error: `patternFrame` was emitting `csel ..., gt`
where the binary has `hi`, which pins the original's control structure — a negated
`<=`, not a `>` ternary.

It also had **three defects of its own**, and finding them mattered more than finding
anything in the recovered code, because each one made the tool report differences that
were not there:

* **Calls were compared against a raw address.** otool prints a call through the
  lazy-binding stub as `bl 0x38b8 ; symbol stub for: ___exp10f` while clang prints
  `bl ___exp10f`, so the two sides never matched at any call site. Every function that
  calls something differed by construction.
* **Three-operand branches were never resolved.** `tbz`/`tbnz`/`cbz`/`cbnz` carry a
  register (and a bit index) before the target, and the target was captured with `(\S+)$`,
  which cannot match `tbnz w10, #0x2, 0xc88`. Those branches stayed as label text on one
  side and a numeric address on the other.
* **A label resolved to the END of its basic block.** A label in clang's output sticks to
  every instruction that follows it, and the first-index-wins rule was missing, so the
  fresh side pointed at the last instruction of each block while the shipped side (indexed
  by exact address) pointed at the first. Every branch into a block of more than one
  instruction differed.

Fixing them raised the measured instruction-identical counts for `spectra` (3 → 6),
`colorpulse` (4 → 5) and `speedramp` (1 → 2) **without any transcription changing**, and
turned the previously reported "`saturationBoost` differs only in basic-block order" into
what it always was: a match. The floors in `test/codegen.test.mjs` were raised accordingly
and the reason for each rise is written down there.

A fourth gap was not a false positive but a false silence: keying on the bare function
name collapsed OVERLOADS, so `aff::trimmedMean` was reported once while only one of its two
overloads had ever been compared. The comparison unit is now the mangled symbol.

The check is honest about its own reach. Larger functions differ in codegen
legitimately (the originals inline `std::vector` growth and `luma709`; this source
calls out where that is bit-identical, and register allocation differs). Those are
verified by self-checks and oracles, **not** by this, and `test/codegen.test.mjs`
gates `glitch` on zero differences while gating the rest on not regressing.

## The capability contract

`CAPABILITIES.md` decodes every effect's recovered `eGLO`/`eGL2` PiPL atoms into
named `PF_OutFlag` values. Those atoms are what AE's host reads to decide how to
call an effect, so they are obligations rather than descriptions:

| Obligation | Effects |
|---|---|
| 16-bit render path (`DEEP_COLOR_AWARE`) | **16 / 16** |
| Thread-safe shared state (`SUPPORTS_THREADED_RENDERING`) | **16 / 16** |
| 32-bpc float path (`FLOAT_COLOR_AWARE`) | 4 — BeatGridRuler, BeatSnap, MotionCurveX, ShakeCapture |
| `PF_Cmd_SMART_RENDER` handler (`SUPPORTS_SMART_RENDER`) | the same 4 |

That splits the fleet into two tiers for a rebuild: twelve effects need an 8/16-bit
path and locking, while those four additionally need float handling and a
smart-render entry point.

## Roadmap

1. **Recover the DSP.** Disassemble per effect, starting with the effects that expose
   real domain namespaces (`spectra::` in SpectraKeys is the richest: radix-2 FFT,
   Hann window and coherent gain, mono downmix, spectrogram band analysis, dB
   conversion). Recovered headers make this tractable.
2. **Reconstruct a buildable tree.** In progress. Every effect's PiPL resource is
   now rebuilt from its recovered spec and **byte-compared against the shipped
   bundle** (`packaging/`, `test/pipl.test.mjs`), which proves the recovered
   metadata is complete; all 16 match exactly. The AE SDK 25.6 that built the
   fleet is identified and its toolchain verified. What remains is `EffectMain`,
   parameter registration and the render callbacks — see `packaging/README.md`.
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

## Brand

The fleet wears the Trident Studio brand system (Media and creative family): a coral
to pink tile with a double-diamond glyph, coral `#FF6B5E` and pink `#E04E8B` as the
accent pair, and a dark and a light theme. The tagline in the brand book is a proposal
awaiting review, so it is not used in the copy here.

| What | Where |
|---|---|
| Usage rules, colour, type, voice, motion | `brand/DESIGN.md` |
| Brand book (cover, essence, logo, colour, type, system, applications) | `brand/brand-book.pdf` |
| Design tokens, dark and light | `brand/tokens.css`, `brand/tokens.json` |
| Tile, glyph and the icon set (favicon, touch icon, PWA, maskable, 1024 px app icon) | `brand/assets/` |
| Lockups: tile plus the name and "by Trident Studio", for dark and light surfaces | `brand/assets/lockup-dark.svg`, `brand/assets/lockup-light.svg` |
| Link-preview card, 1200 x 630 (PNG for uploads, SVG source) | `brand/assets/og-card.png`, `brand/assets/og-card.svg` |

What the brand touches in this repository, and what it deliberately does not:

- **The lockup** is the README header above: the light version by default, the dark one
  when the viewer's theme is dark. An After Effects effect has no icon slot: the
  installed bundles carry only their `.rsrc` resource and an `Info.plist` with no icon
  key, so no effect or bundle gets one. Use the tile in `brand/assets/` for any store
  listing, release page or avatar.
- **The link-preview card** (`brand/assets/og-card.png`) has no page to point at: this
  repository ships no web page, so there are no `og:image` or `twitter:image` tags to
  set. It is for the GitHub social preview (repository Settings, Social preview) and for
  any release page or post that wants one. The card carries the tagline the brand book
  marks as a proposal, so it is not published anywhere until that is approved.
- **The tokens** replace the chart palette in `tools/demo/harness.cpp`, the only place
  this repository draws colour. Series run accent, accent-2, then the shared signals.
  The recovered functions read none of it: the harness's results.json is identical
  before and after.
- **Type** is unchanged. Nothing here renders text and the repository self-hosts no
  fonts, so `brand/tokens.css` is kept as the reference and not linked from anything.
- **Effect code, PiPL resources and code signing** are untouched.

`test/brand.test.mjs` fails if the tile, the lockups, the link-preview card, the icon
sizes or the tokens drift or go missing, if a token pair stops meeting contrast, if the
README loses its lockup header, if a web page is added without the card, or if the
harness palette stops matching the tokens. To adopt a reissued tile, lockup or card, copy
the new files into `brand/` and update the hash table in that test in the same commit.

## Provenance and licence

These are Williams Media's own effects. The recovered specifications describe
Williams Media software and are not a redistribution of any third-party code. The
Adobe After Effects SDK itself is **not** included and remains governed by Adobe's
licence — the recovered declarations reference SDK types by name only.
