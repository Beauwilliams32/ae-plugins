# plugins/LUTBlend — host integration for the recovered DSP

This directory holds **new code**, not recovered code. That distinction matters and
is enforced by keeping it in a separate tree from `src/`, which contains only
transcriptions of the shipped binaries.

## What is recovered, and what is new

| Part | Where | Status |
|---|---|---|
| The DSP (`clampf`, `lerpf`, `luma709`, `sampleLUT`, `makeIdentity`, `applyLUTBlend`, `CubeLUT`) | `src/lutblend/` | **Recovered** from the shipped binary, with instruction-level commentary |
| The parameter table (IDs, types, names, bounds, the Drive choice string) | `LUTBlendParams.h` | **Recovered** from `EffectMain`'s `PF_Cmd_PARAMS_SETUP` blocks |
| Version words, match name, category, ABOUT wording | `LUTBlendParams.h` | **Recovered** from the PiPL resource and the bundle's string table |
| The PiPL resource itself | `../packaging/LUTBlendPiPL.r` | **Recovered**, and byte-verified against the shipped bundle |
| `EffectMain` and its handlers | `LUTBlend.cpp` | **NEW** — the original body is lost |

So this is a *reimplementation of the surrounding plugin*, built on a recovered
core and a recovered parameter contract. It is not a transcription, and it is not
presented as one.

## Building

```bash
./tools/build-plugin.sh LUTBlend
```

Requires an AE SDK; set `AE_SDK_DIR` to its `Examples` directory or unpack one
under `~/.ae-sdk/`. Output lands in `build/LUTBlend.plugin` (gitignored).

## What is verified

`test/build.test.mjs` runs the real build in the gate and asserts:

* the bundle has `Contents/MacOS/`, `Info.plist`, `PkgInfo` and `Resources/*.rsrc`;
* the Mach-O **exports `EffectMain` as a defined text symbol** — anchored matching,
  because a substring test also matched `_EffectMainRenamed` and passed against a
  plugin with no usable entry point;
* it is `arm64`;
* the PiPL payload is byte-identical to the shipped bundle's;
* the recovered engine's own symbols (`applyLUTBlend`, `sampleLUT`, `makeIdentity`,
  `luma709`) are present in the link, which catches the object-file collision
  described below.

## What is NOT verified, and this is the important part

**No After Effects host has ever loaded this bundle.** Everything above is
structural. Specifically untested:

* whether AE accepts the plugin at launch at all;
* whether the PiPL's `eGLO`/`eGL2` flag words mean what this code assumes about
  smart render and 32-bit float support;
* whether the render path produces a correct image — the pixel loop has never run;
* the `8-bit only` limitation: `BlendPixel8` is registered but the plugin declares
  float and deep-colour awareness, so a 16/32-bpc project would take a path that
  does not exist yet.

Do not install this over the shipped plugin expecting it to work. It is a
structural proof that the recovered pieces compose, not a finished effect.

## Known gap: LUT loading

The shipped plugin discovers `.cube` files through its own layer
(`LB_ScanLutFolder`, `LB_LutFileList`, `LB_LutFolderPath`, `LB_BuildPopupMenu`,
`LB_LutForPopupValue`) and caches parsed LUTs in a
`std::map<std::string, std::shared_ptr<const CubeLUT>>`. **None of that is
recovered**, so:

* both LUT popups are populated with a single placeholder entry;
* the render path falls back to the identity LUT, which is what the shipped plugin
  does when a LUT is missing or fails to parse (`__ZL11identityLUTv`);
* `Drive`'s "Global Luma" mode uses a hard-coded 0.5 mean luma instead of the
  frame's actual mean.

The effect therefore loads, registers its parameters and renders — but is a no-op
until a LUT can be selected. Closing that gap needs either the LUT layer recovered
or `parseCube` finished and wired up; see `../../src/lutblend/RECOVERY-NOTES.md`.

## A trap worth keeping

Object files in `build-plugin.sh` are **numbered**, not named after their sources.
On a case-insensitive filesystem — the macOS default — `LUTBlend.cpp.o` and
`lutblend.cpp.o` are the same file, so the plugin's object silently overwrote the
DSP's and the only symptom was a link error claiming `EffectMain` was undefined.
The test now asserts the DSP symbols survive into the link so that class of
collision cannot return unnoticed.
