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

## `upsampleBilinear` runs the scale BACKWARDS, and that is the whole trap

`downsampleBoxFilter` (126) is an integer box average — every output pixel is the mean of a
source rect. `upsampleBilinear` (168) is not its mirror: it samples the source at fractional
positions, and the two use scale factors that point in opposite directions and would be easy
to write the same way out of habit.

| | factor | direction |
|---|---|---|
| `computeDownsampleDims` | `scale = maxDim / longest` | multiplies a source size into an output size |
| `downsampleBoxFilter` | none — integer division per pixel | — |
| `upsampleBilinear` | `sx = (srcW-1) / max(dstW-1, 1)` | a **source**-pixel step per **output** column |

The `-1` on both sides is what makes the endpoints exact: at `x = dstW-1` the product is
`srcW-1` by construction, so the last output column lands on the last source column rather
than one short of it. The `max(..., 1)` guards the degenerate `dstW == 1`, where the step is
never used anyway — the loop then starts and ends at `x0 = 0`.

Read against the listing, the per-pixel body is:

```
ucvtf s3, w16 / fmul s3, s1, s3        ; fx = sx * x
fcvtzs w1, s3 / fcvtzs s4, s3 / scvtf  ; x0 = trunc(fx), wx = fx - x0
add w2, w1, #1 / cmp w10, w2 / csinc   ; x1 = min(x0 + 1, srcW - 1)
ldr s4..s7                             ; a,b = row y0 at x0,x1 ; c,d = row y1 at x0,x1
fmadd s4, s5, s3, s4                   ; top = a + (b - a) * wx
fmadd s3, s5, s3, s6                   ; bot = c + (d - c) * wx
fsub s3, s3, s4 / fmadd s3, s3, s2, s4 ; out = top + (bot - top) * wy
```

Three details a tidy rewrite gets wrong:

- **`x0` is not clamped, only `x0 + 1` is.** The `csinc` is on the neighbour. `x0` needs no
  clamp because the scale puts the last column exactly on `srcW-1`.
- **The truncation is `fcvtzs`** — toward zero, not `lround`. For non-negative positions
  that is a floor, so it agrees with `(int)` but not with the rounding used in
  `computeDownsampleDims`.
- **A 1x1 SOURCE is a branch of its own** — every output pixel becomes `src[0]`, broadcast in
  a vectorised fill. The correct reading of that guard is the SOURCE being 1x1, not the
  destination: a 1-wide-but-tall source falls through to the general loop, where the zero
  scale collapses `x0`, `wx` and `x1` and the horizontal blend becomes a copy.

Every guard returns `dstW*dstH` ZEROS rather than an empty vector — a non-positive dimension
(which makes that product empty in turn), or a `srcW*srcH` larger than the buffer handed in.
The vector is allocated and zeroed *before* the guards run, which is why an invalid call still
reports the requested shape.

## What is next

Both geometric primitives are now recovered, so the remaining three functions are the
effect's algorithm rather than its plumbing:

```
buildMatte(const ArrivalField&, float, float, bool)         169 instrs
computeArrivalField(const Image&, float, float, float, bool) 290 instrs
computeMatteFullRes(const Image&, const Params&)            151 instrs
```

`computeArrivalField` builds an arrival-time field from the image (it has two file-scope
tables, `dx8` and `dy8`, at 0x3f64 and 0x3f84 — eight-entry int tables, so eight
neighbour directions rather than nine); `buildMatte` thresholds that field into a matte;
`computeMatteFullRes` is the entry point that runs the downsample → field → matte → upsample
chain. Recovering them means modelling `Params` and `ArrivalField` first, which is why they
are absent rather than stubbed.
