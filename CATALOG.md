# Fleet catalog

16 after-effects effects, recovered from compiled bundles.

| Effect | AE menu name | Symbols | Source files | Params | Messages | Archs |
|---|---|---|---|---|---|---|
| [AutoFlickerFix](specs/AutoFlickerFix.md) | `WM_AutoFlickerFix` | 22 | 0 | 8 | 2 | x86_64+arm64 |
| [BeatGridRuler](specs/BeatGridRuler.md) | `WM_BeatGridRuler` | 38 | 0 | 10 | 5 | x86_64+arm64 |
| [BeatSnap](specs/BeatSnap.md) | `WM_BeatSnap` | 39 | 0 | 17 | 5 | x86_64+arm64 |
| [ChromaKeyAssist](specs/ChromaKeyAssist.md) | `WM_ChromaKeyAssist` | 31 | 1 | 7 | 1 | x86_64+arm64 |
| [ColorPulse](specs/ColorPulse.md) | `WM_ColorPulse` | 34 | 0 | 12 | 3 | x86_64+arm64 |
| [EchoTrails](specs/EchoTrails.md) | `WM_EchoTrails` | 16 | 0 | 9 | 2 | x86_64+arm64 |
| [GlitchKit](specs/GlitchKit.md) | `WM_GlitchKit` | 15 | 0 | 10 | 2 | x86_64+arm64 |
| [LUTBlend](specs/LUTBlend.md) | `WM_LUTBlend` | 76 | 1 | 6 | 2 | x86_64+arm64 |
| [MaskFlow](specs/MaskFlow.md) | `WM_MaskFlow` | 22 | 0 | 9 | 2 | x86_64+arm64 |
| [MotionCurveX](specs/MotionCurveX.md) | `WM_MotionCurveX` | 41 | 0 | 10 | 5 | x86_64+arm64 |
| [NoiseField](specs/NoiseField.md) | `WM_NoiseField` | 20 | 0 | 5 | 1 | x86_64+arm64 |
| [PathScribe](specs/PathScribe.md) | `WM_PathScribe` | 42 | 1 | 16 | 2 | x86_64+arm64 |
| [ShakeCapture](specs/ShakeCapture.md) | `WM_ShakeCapture` | 33 | 0 | 10 | 4 | x86_64+arm64 |
| [SpectraKeys](specs/SpectraKeys.md) | `WMH SpectraKeys` | 51 | 1 | 23 | 3 | x86_64+arm64 |
| [SpeedRampCurve](specs/SpeedRampCurve.md) | `WM_SpeedRampCurve` | 18 | 0 | 12 | 2 | x86_64+arm64 |
| [TypeRiser](specs/TypeRiser.md) | `WM_TypeRiser` | 77 | 1 | 11 | 2 | x86_64+arm64 |

## Totals

- 16 effects
- 575 named symbols recovered
- 175 parameter-label candidates
- 43 behavioural message strings
- all 16 are universal binaries (Intel + Apple silicon)

## Findings worth acting on

**SpectraKeys breaks the fleet's menu-name convention.** Fifteen effects register as `WM_<Name>`; SpectraKeys alone registers as `WMH SpectraKeys`, and it is also the only effect whose name contains a space. It was the last effect added to the fleet (absent from the 2026-09-25 backup set, present in the 2026-09-28 one). Worth normalising.

**Every effect is pinned to the same version and SDK.** All 16 report effect version `0x00080001` and SDK `0x000d001d`, so the fleet was built from one SDK drop in a single pass.

**The fleet carries real, factorable DSP.** Several effects expose proper domain namespaces rather than flat C, which makes the math recoverable: see `spectra::` in SpectraKeys (radix-2 FFT, Hann window, dB conversion, spectrogram band analysis) and the audio helpers in PathScribe.

## The fleet vendors one DSP module into four effects

Every effect namespaces its own code, and the audio/analysis helpers appear under four of
those namespaces:

| Function | Carriers | Instructions | Verdict |
|---|---|---|---|
| `fftRadix2` | SpectraKeys, PathScribe, BeatSnap | 100 | identical |
| `hannWindow` | SpectraKeys, PathScribe, BeatSnap | 112 | identical |
| `downmixToMono` | + BeatGridRuler | 116 | identical |
| `analyse` | SpectraKeys, PathScribe | 341 | identical |
| `Spectrogram` (dtor) | SpectraKeys, PathScribe | 29 | identical |
| `linearToDb` | SpectraKeys, PathScribe | 12 | identical |
| `isPowerOfTwo` | + BeatGridRuler | 6 | identical |
| `clampf` | + BeatGridRuler | 5 | identical |
| `dbToLinear` | SpectraKeys, PathScribe | 3 | identical |

Verified by comparing disassembly with relocations and the namespace rename normalised
out (`tools/dsp-duplication.mjs`, also a test). **No drift: the copies are in sync.**

Two caveats worth recording, because both produced a false verdict during recovery:

- The comparison is only valid once `adrp` page numbers, the trailing address comments
  *and* the namespace inside mangled call targets are all stripped. Skipping any one of
  them yields a spurious "these differ".
- An early attempt reported functions "identical" while comparing two empty extractions.
  The tool now refuses to compare anything under a minimum instruction count.

**Implication:** recovering this module once yields correct maths for four effects. The
per-effect namespaces, and `bandValues` (SpectraKeys) versus `barLevels` (PathScribe),
are the only genuine differences.
