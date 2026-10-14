# Feature map

Per the workspace rule that every feature has a row here and a test.

Because this repository is a **recovery** rather than a product, the "features"
are recovered capabilities. `State` distinguishes what has been proven from what
is merely identified.

| Feature | Where | State | Test |
|---|---|---|---|
| Fleet discovery — find installed bundles, read Info.plist + PiPL | `tools/recover.mjs` | **Working** | `test/spectra.test.mjs` (reproducibility) |
| PiPL decoding — effect name, AE menu name, category, versions, flags | `tools/recover.mjs` `parsePipl()` | **Working** — exact for all 16 | `test/spectra.test.mjs` (reproducibility) |
| Symbol recovery — demangled signatures, namespaces, class names | `tools/recover.mjs` `classifySymbols()` | **Working** — 575 symbols | `test/spectra.test.mjs` (reproducibility) |
| Source-file discovery — filenames from `__GLOBAL__sub_I_*` | `tools/recover.mjs` | **Working** — 5 files | `test/spectra.test.mjs` (reproducibility) |
| **Ordered parameter recovery** (walk `EffectMain` literals in address order) | `tools/recover.mjs` `extractParamOrder()` | **Recovered** — 137 params across 16 effects, in panel order | `test/spectra.test.mjs` (reproducibility) |
| **Popup option lists** (recovered exactly, `|`-joined) | `tools/recover.mjs` `extractParamOrder()` | **Recovered** — 26 lists | `test/spectra.test.mjs` (reproducibility) |
| Popup-to-parameter ownership | — | **Unresolved** — adjacency only; not asserted in output | n/a |
| Parameter-label recovery via string shape | `tools/recover.mjs` `categoriseStrings()` | **Superseded** by ordered recovery; retained for cross-checking only | n/a |
| Spec rendering to Markdown | `tools/render-specs.mjs` | **Working** | `test/spectra.test.mjs` (reproducibility) |
| Deterministic output (re-running reproduces committed specs) | `tools/recover.mjs` | **Working** | `test/spectra.test.mjs` |
| `spectra::clampf` | `src/spectra/spectra.cpp` | **Recovered + verified** | `selfcheck.cpp` via `test/spectra.test.mjs` |
| `spectra::isPowerOfTwo` | `src/spectra/spectra.cpp` | **Recovered + verified** | same |
| `spectra::linearToDb` (with 1e-9f log guard) | `src/spectra/spectra.cpp` | **Recovered + verified** | same |
| `spectra::dbToLinear` | `src/spectra/spectra.cpp` | **Recovered + verified** | same |
| `spectra::hannWindow` (periodic Hann) | `src/spectra/spectra.cpp` | **Recovered + verified** | same |
| `spectra::hannCoherentGain` | `src/spectra/spectra.cpp` | **Recovered + verified** | same, incl. cross-check vs `hannWindow` mean |
| Disassembly constant decoding (0x3089705f = 1e-9f, 0x40c90fdb = 2π) | `test/spectra.test.mjs` | **Working** | direct assertion |
| `spectra::fftRadix2` (guard, bit-reversal, stages, butterfly) | `src/spectra/spectra.cpp` | **Recovered + verified** — covers 3 effects via shared code | `selfcheck.cpp` vs naive DFT |
| Twiddle-recurrence error characteristic at N up to 8192 | `src/spectra/RECOVERY-NOTES.md` | **Measured** — 2.5e-05 relative at max size, not a defect | `selfcheck.cpp` tolerance |
| `pathscribe::fftRadix2` / `beatsnap::fftRadix2` identical copies | `tools/dsp-duplication.mjs` | **Verified identical** — one implementation serves all three | `test/spectra.test.mjs` |
| `spectra::analyse` (STFT: centred frames, zero pad, Hann, magnitude) | `src/spectra/spectra.cpp` | **Recovered + verified** — covers 2 effects | `selfcheck.cpp` (Hann fingerprint) |
| `AnalysisConfig` layout (sampleRate/hopDivisor) | `src/spectra/spectra.h` | **Recovered**; @0x10's unit deliberately left open | `selfcheck.cpp` |
| SpectraKeys chain: downmix→window→fft→magnitude→band→envelope | `src/spectra/` | **Complete, end to end** | 98 compiled checks |
| `spectra::analyseBands` (one transform, per-band rows) | `src/spectra/spectra.cpp` | **Recovered + verified** | `selfcheck.cpp` |
| `AnalysisConfig.hopRateHz` is a hop RATE (settled by two uses) | `src/spectra/spectra.h` | **Recovered** | `selfcheck.cpp` |
| `Spectrogram` zero-initialisation (required by analyse's guards) | `src/spectra/spectra.h` | **Recovered** from analyse's zeroing prologue | `selfcheck.cpp` (would crash without it) |
| `spectra::downmixToMono` | `specs/SpectraKeys.md` | **Identified only** | none yet |
| `spectra::Spectrogram::hzToBin` | `src/spectra/spectra.cpp` | **Recovered + verified** | `selfcheck.cpp` via `test/spectra.test.mjs` |
| `Spectrogram` field layout (binCount @0, hzPerBin @8) | `src/spectra/spectra.h` | **Recovered** from `hzToBin`'s loads | `selfcheck.cpp` |
| `spectra::Spectrogram::~Spectrogram` | `src/spectra/spectra.cpp` | **Recovered** (defaulted; matches the binary's vector teardown) | compile/link in `test/spectra.test.mjs` |
| **Shared-DSP duplication proof** (machine-code compare, namespace-agnostic) | `tools/dsp-duplication.mjs` | **Working** — 9 functions, 26 copies, no drift | `test/spectra.test.mjs` |
| Drift gate on the copied DSP | `tools/dsp-duplication.mjs` | **Working** — non-zero exit if any carrier diverges | `test/spectra.test.mjs` |
| Per-effect namespace inventory (16 namespaces) | `specs/*.json` → `symbols.byScope.domain` | **Recovered** | `test/spectra.test.mjs` (reproducibility) |
| **`lutblend::clampf` / `lerpf` / `luma709`** | `src/lutblend/lutblend.cpp` | **Recovered + verified** | `test/lutblend.test.mjs` |
| **`lutblend::CubeLUT::valid`** + full `CubeLUT` layout | `src/lutblend/lutblend.{h,cpp}` | **Recovered + verified** | `test/lutblend.test.mjs` |
| Cross-bundle check: `lutblend::clampf` vs `spectra::clampf` | `test/lutblend.test.mjs` | **Verified** — superseded by the fleet-wide `test/clampf.test.mjs`, kept as a narrower check | own test |
| **`lutblend::sampleLUT`** (trilinear 3D + per-channel 1D, domain clamp) | `src/lutblend/lutblend.cpp` | **Recovered + verified** | `test/lutblend.test.mjs` (identity LUT) |
| `.cube` data ordering: red fastest (index = b·size² + g·size + r) | `src/lutblend/lutblend.cpp` | **Recovered** | identity round-trip |
| **`lutblend::makeIdentity`** (269 instrs) — zeroes the range, fills the identity ramp in .cube order (B outer, G middle, R inner, triples stored (r,g,b)); clears then RESERVEs so capacity is reused | `src/lutblend/lutblend.cpp` | **Recovered + verified** | `test/lutblend.test.mjs` |
| **`lutblend::applyLUTBlend`** (112 instrs) — `out = lerp(rgb, lerp(A, B, clamp(blend)), clamp(intensity))`, with an optional Rec.709 luma-preserving rescale guarded by a 1e-5 epsilon | `src/lutblend/lutblend.cpp` | **Recovered + verified** | `test/lutblend.test.mjs` |
| `lutblend::parseCube` (1924 instrs — the largest function in the fleet) | `src/lutblend/RECOVERY-NOTES.md` | **Identified only** — recovered: the file grammar (7 directives with arities), all 13 error strings, the validation rules (incl. the `[2, 256]` LUT-size bound that `CubeLUT::valid()` does NOT express), the full line/tokeniser loop, and that `TITLE` is ignored entirely. The handler BODIES and the 4 unexplained `strtod` sites are explicitly listed as unread | none yet |
| **`CubeLUT` default member initialisers** | `src/lutblend/lutblend.h` | **Recovered + corrected** — parseCube's prologue initialises a default-constructed `CubeLUT` field by field at 0xd70..0xd98, giving `is3D=true, min=0, max=1`. The header said `max=0` and `is3D=false`; both were wrong and are fixed, with `static_assert`s on the size and the `data` offset | `test/lutblend.test.mjs` |
| **`typeriser::clampf` / `karaokeWordAt` / `wordProgress` / `cueIndexAt`** | `src/typeriser/typeriser.cpp` | **Recovered + verified** | `test/typeriser.test.mjs` |
| **`typeriser::Cue` layout** — 48 bytes: `int index`@0x0, `start`@0x8, `end`@0x10, `std::string text`@0x18 | `src/typeriser/typeriser.h` | **Recovered + verified** — `cueIndexAt` pinned the interval, `parseSrt` pinned the other two; four `static_assert`s hold the offsets | `test/typeriser.test.mjs` |
| **`clampf` copies — 5 recovered, from 15 of the 16 bundles** | `src/*/` | **Verified** — `test/clampf.test.mjs` compares ALL FIVE against each other, including on inverted ranges. The per-namespace tests each covered a subset, so two copies could have drifted unnoticed | `test/clampf.test.mjs` |
| **`typeriser::ease`** + `Easing` enum (7 cases) | `src/typeriser/typeriser.cpp` | **Recovered + verified** | `test/typeriser.test.mjs` |
| **`typeriser::trim` / `parseTimestamp`** (SRT time -> ms, with validation) | `src/typeriser/typeriser.cpp` | **Recovered + verified** | `test/typeriser.test.mjs` |
| **`typeriser::splitWords`** (whitespace split, empty runs collapsed) | `src/typeriser/typeriser.cpp` | **Recovered + verified** | `test/typeriser.test.mjs` |
| **`typeriser::parseTimingLine`** (`"START --> END"` → two ms positions; strict `end > start`; left parsed raw, right trimmed then cut at first space) | `src/typeriser/typeriser.cpp` | **Recovered + verified** | `test/typeriser.test.mjs` (JS oracle, 24 cases) |
| **`typeriser::parseSrt`** (512 instrs) — BOM strip, CR/CRLF/CR-tolerant line splitter, positional block walk, `\n`-joined text, stable sort by `start` | `src/typeriser/typeriser.cpp` | **Recovered + verified** | `test/typeriser.test.mjs` (JS oracle over 20 documents) |
| TypeRiser namespace | `src/typeriser/` | **All 12 authored functions recovered — the namespace is complete.** The whole SRT path plus the timing core: `clampf`, `trim`, `splitWords`, `parseTimestamp`, `parseTimingLine`, `parseSrt`, `ease`, `karaokeWordAt`, `wordProgress`, `cueIndexAt`, `wrapWords`, `wordStyle` | `test/typeriser.test.mjs` |
| **`typeriser::wrapWords`** (52 instrs) — greedy line breaking; `running = sep + running + w[i]`, break on strictly `> maxWidth`, reset to `w[i]` alone | `src/typeriser/typeriser.cpp` | **Recovered + verified** | `test/typeriser.test.mjs` (naive-reference sweep, 400 shapes) |
| **`typeriser::wordStyle`** (164 instrs) — `(Anim, Easing, start, end, now, wordIndex, wordCount, stagger, animDuration, offsetDistance)` → `WordStyle{alpha, scale, offset, highlight}`. Karaoke derives a word index from elapsed time; the other modes ease a 0..1 progress | `src/typeriser/typeriser.cpp` | **Recovered + verified** | `test/typeriser.test.mjs` |
| `typeriser::Anim` enum (1=Pop, 2=Fade, 3=Rise, 4=Karaoke) | `src/typeriser/typeriser.h` | **Selectors recovered**; the names are matched to the plugin's own description string ("pops, fades, rises, or karaoke-highlights") after decoding the switch, not taken from a symbol table | `test/typeriser.test.mjs` |
| `typeriser::WordStyle` layout (16 bytes: 3 floats + int, in x0:x1) | `src/typeriser/typeriser.h` | **Recovered** — field 0 confirmed as alpha by the caller's `clampf`→`CGContextSetAlpha`, field 3 as a colour-select bool by `tst x1, #0x100000000` | `test/typeriser.test.mjs` |
| **`glitch::` namespace** — `clampf`, `clampi`, `hashKey`, `patternFrame` | `src/glitch/` | **Recovered + verified INSTRUCTION-IDENTICAL** (4/4 functions match the shipped binary exactly, 34/34 instructions for `hashKey`) | `test/codegen.test.mjs` (instruction-identical) + `test/glitch.test.mjs` (25 assertions) |
| `glitch::apply<T>` / `View<T>` / `Params` (1157 + 1228 instrs) | `specs/GlitchKit.md` | **Identified only** — the effect's actual render engine. Same `View<T>`+`apply<T>` shape as `noisefield::apply<T>` and `chromakey::renderView<T>`, so it is a fleet-wide idiom | none yet |
| **Fleet parameter tables** — types, IDs, names, bounds, union immediates for every effect | `PARAMETERS.md` (generated by `tools/param-table.mjs`) | **Recovered + verified** — 164 parameters across 16 effects, every one with a decoded type and ID; 90.5% of nameable names decode. `LUTBlend`'s six match the table derived BY HAND before the tool existed | `test/params.test.mjs` |
| **`speedramp::` namespace** — `easeValue`, `easeIntegral`, `speedAt`, `mapTime`, `computeBlend` | `src/speedramp/` | **Recovered + verified** (261 instructions, all 5 functions). `computeBlend` is INSTRUCTION-IDENTICAL; the other four are verified by an antiderivative/integral relationship asserted numerically | `test/speedramp.test.mjs`, `test/codegen.test.mjs` |
| **`echo::` namespace (partial)** — `clampf`, `blendPixel`, `compositeOver`, plus `ImageF`/`BlendMode` layouts | `src/echo/` | **Recovered + verified** — `clampf` is INSTRUCTION-IDENTICAL (6th copy of the idiom); the blend maths is verified against closed-form expectations per mode | `test/echo.test.mjs` |
| `echo::buildLadder` (91) / `sampleBilinear` (120) / `transformBilinear` (122) | `src/echo/echo.h` | **Identified only** — signatures and sizes known, bodies unread. Absent rather than stubbed, and a test asserts they stay absent | `test/echo.test.mjs` |
| The other 13 effects' function bodies | `specs/*.md` | **Identified only** — architecture recovered, bodies not | none yet |
| **PiPL resources reconstructed from the recovered specs** — **all 16 effects** | `packaging/<Plugin>PiPL.r` (generated by `tools/render-pipl.mjs`) | **Recovered + verified** — every 576-byte PiPL payload rezzed from these sources is byte-identical to the shipped `.rsrc` | `test/pipl.test.mjs` (negative-controlled: one flipped flag bit fails) |
| **The exact AE SDK behind the fleet** | `packaging/README.md` | **Verified** — SDK 25.6 declares `PF_PLUG_IN_VERSION 13` / `SUBVERS 29` = `0x000d001d`, the spec version all 16 bundles report; its samples compile clean for arm64 | `test/pipl.test.mjs` |
| **Fleet capability contract** — every effect's `eGLO`/`eGL2` decoded into named `PF_OutFlag`s | `CAPABILITIES.md` (generated by `tools/decode-flags.mjs`) | **Recovered + verified** against the SDK header. 16/16 need a 16-bit path, 16/16 declare threaded rendering, 4 declare float + smart-render. Every bit accounted for, none left over | `test/capabilities.test.mjs` (3 negative controls) |
| SpectraKeys match-name inconsistency (`WMH SpectraKeys` vs the fleet's `WM_<Name>`) | `packaging/README.md` | **Identified only** — a deliberate rename changes AE's plugin identity, so it is left as found rather than silently "fixed" | none |
| **LUTBlend parameter table** (6 params: types, IDs, names, bounds, Drive choices) | `plugins/LUTBlend/LUTBlendParams.h` | **Recovered** from `EffectMain`'s `PF_Cmd_PARAMS_SETUP` blocks | `test/build.test.mjs` |
| **`EffectMain` + param registration + render callbacks** (LUTBlend) | `plugins/LUTBlend/LUTBlend.cpp` | **NEW CODE, not recovered** — the original body is lost. Reproduces the recovered parameter contract and calls the recovered DSP | `test/build.test.mjs` |
| **A buildable `.plugin` bundle** — LUTBlend AND GlitchKit, arm64 | `tools/build-plugin.sh` → `build/LUTBlend.plugin` | **Assembled + structurally verified** — exports `EffectMain`, PiPL payload byte-identical to the shipped bundle, DSP symbols present in the link. **NOT loaded by AE; host acceptance untested** | `test/build.test.mjs` |
| **Capability words** — `eGLO` = `DEEP_COLOR_AWARE`, `eGL2` = `SUPPORTS_THREADED_RENDERING` | `plugins/LUTBlend/LUTBlendParams.h` | **Recovered + enforced** — decoded against the SDK, assigned verbatim, and asserted from source so they cannot drift. The plugin had invented three flags and missed one | `test/build.test.mjs` (3 negative controls) |
| 16-bit render path (`BlendPixel16`, depth dispatch on `PF_WORLD_IS_DEEP`) | `plugins/LUTBlend/LUTBlend.cpp` | **NEW CODE** — required by the recovered `DEEP_COLOR_AWARE`, and mirroring the shipped plugin's own halfword-reading path | `test/build.test.mjs` |
| **LUT discovery + `.cube` loading** — folder scan, popup menus, parsed-LUT cache | `plugins/LUTBlend/LutLibrary.cpp` | **NEW CODE** implementing the recovered GRAMMAR (7 directives, arities, `[2,256]` bound, finite-only numerics, 12 of 13 error strings). The `LB_*` discovery layer is unrecovered; `parseCube` remains only partly read | `test/lutlib.test.mjs` (negative-controlled) |
| "`<keyword>` is not an integer" (13th recovered message) | `src/lutblend/RECOVERY-NOTES.md` | **Identified only** — the branch reaching it is not located, so the loader deliberately does not emit it rather than invent a trigger | none |
| **GlitchKit `EffectMain`** + the five glitch modes (engine in `GlitchRender.cpp`, host-free) | `plugins/GlitchKit/` | **NEW CODE** driving the RECOVERED `hashKey`/`patternFrame`, so the patterns come from the shipped plugin's own generator | `test/build.test.mjs`, `test/codegen.test.mjs` |
| **GlitchKit render core EXECUTED** — determinism, mode distinctness, 8/16-bit agreement, degenerate frames | `plugins/GlitchKit/GlitchRender.cpp` | **Verified by execution** on synthetic frames (3 negative controls). Not a host test | `test/glitchrender.test.mjs` |
| **LUTBlend render core EXECUTED** — the three Drive modes, strength as a lerp, `preserveLuma`, null-LUT identity fallback, **depth-aware `meanLuma` as a regression test** (control reproduces the round-23 bug: 56.4 vs 0.44) | `plugins/LUTBlend/LutRender.cpp` | **Verified by execution** on synthetic frames (3 negative controls). Not a host test | `test/lutrender.test.mjs` |
| `EffectMain` for the other 14 effects | — | **Not started** | none |

## Gate

```bash
npm run check && npm test
```

`npm test` runs thirty-three tests: it asserts the disassembly constants decode as
claimed, compiles each recovered namespace with `clang++ -Werror` and runs its
internal property checks (spectra 107, lutblend 102, typeriser 202 — 411 in
total), diffs the recovered DSP against all 26 vendored machine-code copies,
cross-checks the three recovered `clampf` copies against each other, drives
`parseTimingLine` through an independent JS oracle, and re-runs the extractor to
prove the committed specs are reproducible. The compile tests **skip** (rather
than fail) where `clang++` is absent, so the specs stay readable on any machine.
