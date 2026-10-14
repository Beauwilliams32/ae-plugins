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

## What is *not* recovered

Be clear-eyed about this:

- **Most function bodies.** Eleven namespaces are recovered (see below); the other
  effects' DSP is still machine code. Names and signatures are free, behaviour needs
  per-function disassembly, and that is the bulk of the remaining work.
- **`lutblend::parseCube`** (1924 instructions) is identified but unread, along with
  `echo`'s host integration and 12 effects' bodies — BeatSnap is the most recent
  namespace opened, and four of its eleven functions are still unread. Where a body is unread it is
  **absent rather than stubbed**, and a test asserts it stays absent, so a stub cannot
  quietly make a namespace look complete.
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
eleven recovered source namespaces, and two effects that build into loadable bundles —
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

Eleven recovered namespaces, all transcribed from the disassembly with per-function commentary
citing the instructions that establish each behaviour:

| Module | Contents | State |
|---|---|---|
| `typeriser::` | The whole SRT karaoke engine: `parseSrt`, `parseTimingLine`, `parseTimestamp`, `trim`, `splitWords`, `wrapWords`, `wordStyle`, `ease`, `karaokeWordAt`, `wordProgress`, `cueIndexAt`, plus `Cue`, `WordStyle` and `Anim` | **complete — all 12 authored functions** |
| `glitch::` | `clampf`, `clampi`, `hashKey`, `patternFrame` | **complete for the scalar layer, and INSTRUCTION-IDENTICAL** (4/4). The render engine `glitch::apply<T>` is unrecovered, but its `View<T>`/`Params` layouts, its mode field and its one-based five-way dispatch ARE recovered — see `test/glitch.test.mjs`, which re-derives the offsets from the bundle |
| `speedramp::` | `easeValue`, `easeIntegral`, `speedAt`, `mapTime`, `computeBlend`, plus `RampParams` and `FrameBlend` | **complete — all 5 functions, 261 instructions** |
| `lutblend::` | `clampf`, `lerpf`, `luma709`, `CubeLUT::valid`, `sampleLUT`, `makeIdentity`, `applyLUTBlend`, plus the `CubeLUT` layout | **7 of 8** (`parseCube` pending — 1924 instructions, the largest function in the fleet) |
| `echo::` | `clampf`, `blendPixel`, `compositeOver`, `buildLadder`, `sampleBilinear`, `transformBilinear`, plus the corrected `ImageF` layout and `BlendMode` | **complete — all 6 functions**. Recovering `transformBilinear` also CORRECTED `ImageF`: `data` had been modelled as a raw `float*` on the strength of a read that a `std::vector`'s begin pointer produces identically. The host integration is unread |
| `colorpulse::` | The whole analyser: `clampf`, `smoothScalar`, `luma709`, `smoothColor`, `saturationBoost`, `regionBounds`, `averageColor`, `brightestRegionColor`, `dominantColor`, `sampleColor`, plus the `RGB`/`Rect` layouts and the `Region`/`Mode` selectors | **complete — all 10 functions, 1123 instructions**, and five are INSTRUCTION-IDENTICAL. The host integration is unread |
| `aff::` | The AutoFlickerFix DSP: `clampf`, `dbToGainClamp`, `computeGain`, `centerRegion`, `meanOf`, `medianOf`, `trimmedMean` (both overloads), `medianFilter`, `temporalTarget`, plus the `GainClamp` and `RegionBounds` layouts | **complete — all 10 functions, 719 instructions, 7 of them INSTRUCTION-IDENTICAL**. The host integration (`EffectMain`, `SampleWorldStat`, `DrawStatsBar`) is unread |
| `spectra::` | FFT, Hann window, STFT, band analysis, falloff envelope, `downmixToMono`, plus `Spectrogram` | **complete — all 13 functions, shared by 4 effects**, and 7 of them INSTRUCTION-IDENTICAL. The host integration is unread |
| `beatsnap::` | The onset-detection core: `clampf`, `isPowerOfTwo`, `medianOfWindow`, `adaptiveThreshold`, `hannWindow`, `fftRadix2`, `downmixToMono` | **7 of 11** — `computeFlux` (416), `renderValues` (190), `pickOnsets` (136) and `detectOnsets` (47) are outstanding. `medianOfWindow` is **instruction-identical** (58/58), and the five shared-DSP copies are checked by divergence parity against the `spectra::` originals rather than by assuming a copy of a copy is faithful |
| `chromakey::` | The colour and statistics layer: `clampf`, `rgbToYCbCr` (Rec.601), `sensitivityToThreshold`, `chromaDistance`, `hueAngle`, `angularDiff`, `percentile`, `percentileUnsorted`, `gradientMagnitude` | **9 of 15** namespace symbols — the denominator is the differ's own count, which sees one symbol per template instantiation, so `analyzeFrame<T>` and `renderView<T>` account for two each. Outstanding: `spillScore` (82), `uniformityScore` (203) and those four instantiations. `StoreReadout` is outside the namespace and takes a `PF_InData*`, so it stays out of `src/`. Six of the nine are **instruction-identical**, `rgbToYCbCr` among them at 25 of 25 |
| `noisefield::` | The noise core: `clampf`, `clampi`, `fbmAmplitudeSum`, `PermTable::build`, `noise3`, `fbm3`, and `kGrad3` — the twelve edge gradients of Perlin's improved noise, read out of the bundle's `__TEXT,__const` | **6 of 10** — three instruction-identical (`clampf`, `clampi`, `fbmAmplitudeSum`); `noise3` (183) and `fbm3` (51) compile to the bundle's instruction COUNT with a different register schedule, and are verified instead against an independently written textbook oracle — 4000 points agreeing to 7.8e-07. `PermTable::build` is Fisher-Yates over xorshift32 into a permutation written TWICE, which is what keeps `p[AA + 1]` in range. `fieldAt` (226), `curlField` (163) and the two `apply<T>` loops (408/412) are outstanding |

Three fleet-wide duplications are recorded rather than silently deduplicated, because
each was verified rather than assumed:

* `clampf` is the fleet's most duplicated idiom: **15 of the 16 bundles** carry one, in
  eight different bundles, and the nine that are recovered here are instruction for
  instruction identical. (Measured by listing `src/*/` for a `clampf` definition, which is
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

## Provenance and licence

These are Williams Media's own effects. The recovered specifications describe
Williams Media software and are not a redistribution of any third-party code. The
Adobe After Effects SDK itself is **not** included and remains governed by Adobe's
licence — the recovered declarations reference SDK types by name only.
