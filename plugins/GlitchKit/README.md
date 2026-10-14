# plugins/GlitchKit — host integration for the recovered GlitchKit DSP

This directory holds **new code**, not recovered code. It is a mixture by design, and
the split is enforced by directory so it cannot blur.

## What is recovered, and what is new

| Part | Where | Status |
|---|---|---|
| `glitch::hashKey`, `patternFrame`, `clampf`, `clampi` | `src/glitch/` | **Recovered, and verified INSTRUCTION-IDENTICAL** — all four match the shipped binary exactly, including all 34 instructions and all 16 constants of `hashKey` (`tools/codegen-diff.mjs`) |
| The parameter table (9 params: ids, types, names, bounds, mode names) | `GlitchKitParams.h` | **Recovered** from `EffectMain`'s `PF_ADD_PARAM` blocks (`tools/param-table.mjs`, `PARAMETERS.md`) |
| Version words, flags, ABOUT wording, mode choice string | `GlitchKitParams.h` | **Recovered** from the PiPL and the bundle's string table |
| The PiPL resource | `../packaging/GlitchKitPiPL.r` | **Recovered**, byte-verified against the shipped bundle |
| The five render modes | `GlitchRender.cpp` | **NEW** — see below. Split out of the host integration so they can be EXECUTED without AE |

## Why the recovered part matters more than it looks

The plugin describes itself as *"Five deterministic, seeded glitch modes. Same seed,
same frame, same image."* That promise rests entirely on `hashKey` and `patternFrame`.

Those ARE the recovered functions, verified instruction-for-instruction. So a render
driven by them is seeded from **the same generator as the shipped plugin** — the patterns
are characteristic of this effect rather than of something invented alongside it. What is
new is the per-mode pixel arithmetic. The randomness is not.

The shipped engine is `glitch::apply<T>(const View<const T>&, View<T>&, const Params&,
unsigned, float)` — 1157 and 1228 instructions for the `unsigned char` and
`unsigned short` instantiations — plus the `View<T>` and `Params` types. **None of that is
recovered**, so the five modes here are newly written from the recovered parameter names
and mode choices. The same `View<T>` + `apply<T>` shape appears in `noisefield::apply<T>`
and `chromakey::renderView<T>`, so it is a fleet-wide idiom for satisfying
`DEEP_COLOR_AWARE` rather than a GlitchKit peculiarity.

## Building

```bash
./tools/build-plugin.sh GlitchKit
```

Output lands in `build/GlitchKit.plugin` (gitignored).

## What is verified

`test/build.test.mjs` runs the real build and asserts the bundle has the parts a host
looks for, exports `EffectMain` as a defined text symbol (anchored matching — a substring
test once passed for a plugin with no usable entry point), is universal (`x86_64` +
`arm64`) as the shipped bundles are, carries a PiPL
payload byte-identical to the shipped bundle's, and links the recovered DSP symbols.

`test/codegen.test.mjs` separately proves the four recovered functions compile to the
same instructions as the shipped binary. **GlitchKit is the only namespace where that
holds for every recovered function** (4/4, versus 3/6 for lutblend and 1/10 for
typeriser), which makes its DSP layer the best-verified source in the repo.

## The modes are now executed, not just compiled

The engine lives in `GlitchRender.cpp` and takes plain buffers and plain numbers, so
`test/glitchrender.test.mjs` can run it on synthetic frames. That turns several claims
from aspirational into checked:

* **determinism** — same seed and same frame give a pixel-identical result, for all five
  modes. This is the plugin's headline promise and the reason the recovered
  `hashKey`/`patternFrame` matter;
* a different frame gives a *different* pattern, so the modes are actually animated;
* all five modes change the frame at full amount, and are pairwise distinct at the same
  seed (a mode that silently aliased another would be caught);
* `amount = 0` leaves the frame untouched, and Drive by Luma leaves a **black** frame
  alone — both easy to get backwards;
* **the 8-bit and 16-bit paths pick the same source pixel for every output pixel.**
  This is the check that catches a 16-bit path reading at the wrong stride, which is
  exactly the bug that was written once already in LUTBlend's luma pre-pass;
* 1x1, zero-area, `blockSize = 0` and `scanlineHeight = 0` all terminate without
  dividing by zero or reading out of bounds.

Three negative controls confirm the test can fail: a time-dependent seed, a 16-bit
stride forced to the 8-bit value, and `amount = 0` forced to still apply. Each fails
with the expected message.

## What is still NOT verified

**No After Effects host has ever loaded this bundle.** The modes now execute, but on
synthetic buffers driven directly, not through AE. Still untested:

* whether AE accepts the plugin at launch;
* whether the recovered `eGLO`/`eGL2` mean what this code assumes;
* whether the five modes look good. They are new code, and "runs deterministically and
  distinctly" is not the same claim as "is worth looking at".

Do not install this over the shipped plugin.
