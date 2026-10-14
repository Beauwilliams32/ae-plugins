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
