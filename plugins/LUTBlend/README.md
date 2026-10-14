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
| The `out_flags` / `out_flags2` capability words | `LUTBlendParams.h` | **Recovered** — decoded to `PF_OutFlag_DEEP_COLOR_AWARE` and `PF_OutFlag2_SUPPORTS_THREADED_RENDERING`, and nothing else |
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

## The render path is now executed, not just compiled

The engine lives in `LutRender.{h,cpp}` and takes plain buffers, plain numbers and two
`CubeLUT`s, so `test/lutrender.test.mjs` can drive it directly. That turns several claims
from aspirational into checked:

* the three Drive modes are genuinely different, and each reads what it should — Slider
  follows the slider and ignores the mean, Global Luma follows the mean and ignores the
  slider, Per-pixel Luma follows each pixel's own luma;
* `strength` is a lerp, so 0 is a true no-op and 0.5 lands halfway;
* `preserveLuma` changes the result **and** makes the output luma match the input's —
  so the checkbox is provably wired, not decorative;
* both depths map every pixel to the same 8-bit-equivalent value, and alpha is carried
  through;
* a full render is byte-identical run to run; 1x1 and zero-area frames are safe.

**`meanLuma` is depth-aware, and that is a REGRESSION TEST.** Reading a 16-bit frame with
8-bit arithmetic walks at half its real stride and averages the wrong bytes. That bug was
written once already, in this very pre-pass, and the negative control reproduces it: a
depth-blind version reports a mean of **56.4 instead of 0.44**. It is now impossible to
reintroduce silently.

**A second bug was caught before it shipped.** The null-LUT fallback was originally a
default-constructed `CubeLUT` — which has `size = 0` and fails `valid()`, so sampling it
is meaningless. The shipped plugin builds a real identity LUT for that case
(`__ZL11identityLUTv`). The fallback now does too, and a test asserts a null LUT passes
every channel through rather than producing black.

Three negative controls confirm the tests can fail: a depth-blind `meanLuma`, a
default-`CubeLUT` fallback, and an ignored Drive mode.

## What is verified

`test/build.test.mjs` runs the real build in the gate and asserts:

* the bundle has `Contents/MacOS/`, `Info.plist`, `PkgInfo` and `Resources/*.rsrc`;
* the Mach-O **exports `EffectMain` as a defined text symbol** — anchored matching,
  because a substring test also matched `_EffectMainRenamed` and passed against a
  plugin with no usable entry point;
* it is universal (`x86_64` + `arm64`), matching the shipped bundle;
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

## LUT loading — implemented, as new code

`LutLibrary.h`/`.cpp` hold a fresh implementation of the discovery and loading
layer. The shipped plugin's own layer (`LB_ScanLutFolder`, `LB_LutFileList`,
`LB_LutFolderPath`, `LB_BuildPopupMenu`, `LB_LutForPopupValue`) is **not
recovered**, and `parseCube` — 1924 instructions — is only partly read. What IS
recovered is the `.cube` **grammar and validation**, so the loader implements
those rules rather than transcribing the original:

* the seven directives and their arities;
* the `[2, 256]` size bound, tested as `(unsigned)(size - 2) < 255`;
* size truncation rather than rejection of a fractional value (`fcvtzs`);
* the three-part numeric acceptance test — something consumed, nothing trailing,
  and finite (so `inf` and `NaN` are refused rather than stored);
* `DOMAIN_MAX` strictly greater than `DOMAIN_MIN`;
* duplicate size declarations rejected; data before a size rejected;
* data rows counted against the size, reporting both numbers;
* twelve of the thirteen recovered error messages, with the same wording.

The thirteenth, `"<keyword> is not an integer"`, is **deliberately not emitted**.
It exists in the binary, but the branch reaching it has not been located and the
size handler demonstrably truncates instead of rejecting. Inventing a trigger for
it would put behaviour in the plugin that the shipped one may not have.

LUTs are cached in a name → `shared_ptr<const CubeLUT>` map, mirroring the
shipped plugin's `std::map`, so a frame does not re-read or re-parse. A missing or
unparseable LUT falls back to the identity LUT, which is what the shipped plugin
does (`__ZL11identityLUTv`).

`test/lutlib.test.mjs` checks all of this, including a real folder scan against a
fixture `HOME`. Every rejection assertion requires the **specific expected
message**, not merely that parsing failed — see below.

### Capability words, and why they are not cosmetic

`GLOBAL_SETUP` writes the RECOVERED words verbatim, because AE copies them into
the PiPL's `eGLO`/`eGL2` atoms. Decoded against the SDK:

| Atom | Value | Meaning |
|---|---|---|
| `eGLO` | `0x02000000` | `PF_OutFlag_DEEP_COLOR_AWARE` (1<<25), nothing else |
| `eGL2` | `0x08000000` | `PF_OutFlag2_SUPPORTS_THREADED_RENDERING` (1<<27), nothing else |

An earlier revision of this file built those from named constants instead and got
**three of them wrong**: it invented `PF_OutFlag_USE_OUTPUT_EXTENT`,
`PF_OutFlag2_FLOAT_COLOR_AWARE` and `PF_OutFlag2_SUPPORTS_SMART_RENDER`, and missed
`SUPPORTS_THREADED_RENDERING` entirely. That is not cosmetic — claiming
`FLOAT_COLOR_AWARE` makes AE hand over 32-bpc float data, which a render path
reading 8-bit pixels would silently misread.

The two recovered flags then create two obligations, both now met:

* `DEEP_COLOR_AWARE` means AE will supply **16-bit** worlds, so `BlendPixel16`
  exists and the render dispatches on `PF_WORLD_IS_DEEP` — the same
  `world_flags` bit 0 test the shipped plugin performs at its own two render
  passes. There is deliberately **no float path**, because the recovered `eGL2`
  does not claim float capability.
* `SUPPORTS_THREADED_RENDERING` means renders may run concurrently, so the shared
  LUT cache is mutex-guarded.

`test/build.test.mjs` asserts all of this from the source, so the flags cannot
drift from the recovered words again.

### Remaining limits

* `"Global Luma"` computes the frame mean by subsampling on a stride of 16 rather
  than over every pixel. Representative, cheap, and not necessarily what the
  original did.
* Composite/footage over 8K would make the stride-16 pre-pass noticeable.

## Traps worth keeping

**Backticks inside a `String.raw` template literal terminate it.** A C++ comment
in the test's embedded probe contained a backtick, which closed the template and
turned the rest of the probe into a JavaScript syntax error. The probe itself
still compiled, so the failure looked like a build problem rather than a
punctuation one.

**A negative control that cannot fail is not a control.** The first attempt at
disabling the size bound loosened the threshold, which changed nothing — the
unsigned compare still rejects `n = 1` at any large bound. The next attempt
exposed a real weakness in the tests instead: every rejection assertion used only
"did parsing fail?", so a broken rule stayed hidden behind the *next* rule that
rejected the same input. The tests now assert the specific message, and four
negative controls (size bound, row count, degenerate domain, output-write) each
fail as they should.

## Another trap worth keeping

Object files in `build-plugin.sh` are **numbered**, not named after their sources.
On a case-insensitive filesystem — the macOS default — `LUTBlend.cpp.o` and
`lutblend.cpp.o` are the same file, so the plugin's object silently overwrote the
DSP's and the only symptom was a link error claiming `EffectMain` was undefined.
The test now asserts the DSP symbols survive into the link so that class of
collision cannot return unnoticed.
