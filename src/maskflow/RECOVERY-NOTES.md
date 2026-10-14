# maskflow — recovery notes

Source: `MaskFlow.plugin` (bundle id `com.williamsmedia.aftereffects.MaskFlow`) installed
at `/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/`. The original C++ was
destroyed on 2026-10-13; the binary is the only copy and it is unstripped.

## State

Four of the nine authored functions are recovered, and **three of the four came out
instruction-identical on the first transcription**.

| Function | Instrs | State |
|---|---|---|
| `clampf` | 5 | Recovered — **identical** (shared, and the fifth carrier) |
| `edgeCostScaleFromTolerance` | 12 | Recovered — **identical** |
| `computeDownsampleDims` | 32 | Recovered — **identical** |
| `smoothstep` | 19 | Recovered — 18 here; a branch where the bundle has a select |
| `downsampleBoxFilter` | 126 | Identified only |
| `upsampleBilinear` | 168 | Identified only |
| `buildMatte` | 169 | Identified only |
| `computeMatteFullRes` | 151 | Identified only |
| `computeArrivalField` | 290 | Identified only |

The five outstanding functions all take `Image`, `Params` or `ArrivalField`, none of which
is recovered, which is why they are absent rather than stubbed. The four here take nothing
but ints and floats.

## Mapping this namespace closed a hole in the duplication check

`tools/dsp-duplication.mjs` has a HARDCODED carrier list, deliberately: it compares
BUNDLES, so a bundle missing from the list is a copy nobody is checking. MaskFlow was
missing, because it had no recovered namespace for the list to map through — so its
`clampf`, a fifth copy, had been outside that comparison for the tool's whole life.
Adding it took the check from 26 copies across four bundles to **27 copies across five**,
and MaskFlow's copy is identical to the others.

## Two behaviours a tidy rewrite would "fix", and must not

### `smoothstep` extrapolates below its lower edge

The clamp on `t = (x - edge0) / (edge1 - edge0)` is ONE-SIDED. The instructions are:

```
fcmp  s3, #1.0 / fcsel s5, s4, s3, gt   ; cap at 1
fcmp  s3, #0.0 / fcsel s5, s3, s5, mi   ; and below zero ... return t, unchanged
```

So the lower branch returns `t` itself — a clamp that clamps nothing, which is almost
certainly a typo for `0` in the original. The consequence is real: at `x = -1` with edges
0 and 1, `t = -1` and the polynomial `t*t*(3 - 2t)` is **5**. The self-check pins that. A
`std::clamp(t, 0, 1)` rewrite returns 0 and is a different function.

### And its degenerate case returns negative infinity

With `edge0 == edge1` the division is by zero and the function returns the raw ratio:

- `x` below the edge → `t = -inf`, and `-inf` is what comes back. It propagates into
  whatever the caller does with it, and the self-check asserts the value is a negative
  infinity rather than pretending it is a clamp.
- `x` above the edge → exactly `1.0`.

There is no guard on the equality for the *division*; the only use of the equality is to
choose between the polynomial and that ratio, which is `fcsel s0, s5, s2, ne`.

## `computeDownsampleDims` — and the float that rides in x1

The return is a twelve-byte struct: the tail is

```
mov  w8, w8
orr  x0, x8, x9, lsl #32     ; x0 = width | (height << 32)
ret
```

with the float written separately as `fmov w1, s0` — so the two ints leave in x0 and the
SCALE leaves in x1. Only the shape is recovered; the name is not in the binary.

Three details the self-check pins:

- The test is STRICTLY greater: a longest side exactly equal to `maxDim` is left alone,
  with a scale of exactly 1.
- The rounding is `fcvtas` — nearest, **ties away from zero** — which is `lround`, not
  banker's rounding.
- Both dimensions are floored at 1 afterwards, so a very small `maxDim` still names a real
  image rather than a zero-pixel one.

## `edgeCostScaleFromTolerance` runs the other way

`tolerance` 0 gives 40, 100 gives 0, and the mapping is `(100 - min(tolerance, 100)) / 100
* 40`. A NEGATIVE tolerance short-circuits to 40.0 — the same value as 0 — rather than
extrapolating past it, which is the one place this function clamps where `smoothstep`
above does not.

## What is next

`downsampleBoxFilter` (126) and `upsampleBilinear` (168) are the two geometric primitives
the matte is built on, and recovering them means recovering `Image` first. `buildMatte`
(169) and `computeArrivalField` (290) are the effect's actual algorithm: an arrival-time
field propagated from the mask edges and thresholded into a matte.
