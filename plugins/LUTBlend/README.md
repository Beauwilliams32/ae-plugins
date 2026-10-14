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

### Two limits that remain

* `"Global Luma"` computes the frame mean by subsampling on a stride of 16 rather
  than over every pixel. Representative, cheap, and not what the original
  necessarily did.
* Only the 8-bit iterate path exists, while the plugin declares float and
  deep-colour awareness.

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
