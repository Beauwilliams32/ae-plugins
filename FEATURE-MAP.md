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
| Cross-bundle check: the two `clampf` copies agree | `test/lutblend.test.mjs` | **Verified** | own test |
| **`lutblend::sampleLUT`** (trilinear 3D + per-channel 1D, domain clamp) | `src/lutblend/lutblend.cpp` | **Recovered + verified** | `test/lutblend.test.mjs` (identity LUT) |
| `.cube` data ordering: red fastest (index = b·size² + g·size + r) | `src/lutblend/lutblend.cpp` | **Recovered** | identity round-trip |
| `lutblend::parseCube` / `applyLUTBlend` / `makeIdentity` | `specs/LUTBlend.md` | **Identified only** | none yet |
| **`typeriser::clampf` / `karaokeWordAt` / `wordProgress` / `cueIndexAt`** | `src/typeriser/typeriser.cpp` | **Recovered + verified** | `test/typeriser.test.mjs` |
| `typeriser::Cue` layout (48 bytes, start@0x8, end@0x10) | `src/typeriser/typeriser.h` | **Recovered** from `cueIndexAt` | `test/typeriser.test.mjs` |
| Third `clampf` copy; all three agree | `test/typeriser.test.mjs` | **Verified** across 3 bundles/namespaces | own test |
| **`typeriser::ease`** + `Easing` enum (7 cases) | `src/typeriser/typeriser.cpp` | **Recovered + verified** | `test/typeriser.test.mjs` |
| **`typeriser::trim` / `parseTimestamp`** (SRT time -> ms, with validation) | `src/typeriser/typeriser.cpp` | **Recovered + verified** | `test/typeriser.test.mjs` |
| `typeriser::parseSrt` (512 instrs) / `splitWords` | `specs/TypeRiser.md` | **Identified only** | none yet |
| The other 13 effects' function bodies | `specs/*.md` | **Identified only** — architecture recovered, bodies not | none yet |
| Buildable plugin bundles (AE SDK at `0x000d001d`) | — | **Not started** | none |

## Gate

```bash
npm run check && npm test
```

`npm test` does three things: asserts the disassembly constants decode as
claimed, compiles `src/spectra/*.cpp` with `clang++ -Werror` and runs its 30
internal property checks, and re-runs the extractor to prove the committed specs
are reproducible. The compile test **skips** (rather than fails) where `clang++`
is absent, so the specs stay readable on any machine.
