# maskflow — recovery notes

Source: `MaskFlow.plugin` (bundle id `com.williamsmedia.aftereffects.MaskFlow`) installed
at `/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/`. The original C++ was
destroyed on 2026-10-13; the binary is the only copy and it is unstripped.

## State

Eight of the nine authored functions are recovered. **Three came out instruction-identical
on the first transcription**; four are one instruction off and `computeArrivalField` three,
which is the closest result short of identity and is recorded rather than chased.

| Function | Instrs | State |
|---|---|---|
| `clampf` | 5 | Recovered — **identical** (shared, and the fifth carrier) |
| `edgeCostScaleFromTolerance` | 12 | Recovered — **identical** |
| `computeDownsampleDims` | 32 | Recovered — **identical** |
| `smoothstep` | 19 | Recovered — 18 here; a branch where the bundle has a select |
| `downsampleBoxFilter` | 126 | Recovered — 125 here (one callee-saved register short) |
| `upsampleBilinear` | 168 | Recovered — 167 here (same cause) |
| `buildMatte` | 169 | Recovered — 168 here; the scalar tail fuses a compare the bundle repeats |
| `computeArrivalField` | 290 | Recovered — 293 here; the index multiply is `mul`+`add` where the bundle has `umaddl` |
| `computeMatteFullRes` | 151 | Identified only — the only one that needs `Params` |

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

## `buildMatte` is an inverted smoothstep with three degenerate paths

Wrapper first, because it is where the surprises are:

```
tol    = min(tolerance, 100) / 100          ; but a NEGATIVE tolerance becomes 100.0f
halfW  = max(halfWidth, 1e-4)               ; floored, so `den` cannot be zero by accident
centre = tol * field.span
lo     = centre - halfW ; hi = centre + halfW ; den = hi - lo
degenerate = tol <= 0 || tol >= 1
```

`tol` is a FRACTION of the field's own `span`, not an absolute arrival time: tolerance 50 on a
span of 100 puts the window at 50, and on a span of 3 it puts it at 1.5. That is the one
relationship a reader of the parameter name gets wrong.

### A negative tolerance is not the bottom of the range — it is past the top

`fcsel s1, s0, s1, mi` selects **100.0f**, not 1.0f, when the argument is negative. Because
`degenerate` tests `tol >= 1`, that lands the call in the degenerate branch and every pixel
takes the fill. So `tolerance = 0` produces an all-ones matte and `tolerance = -1` produces an
all-zeros one — the two ends of the range, from arguments that a clamp would have collapsed
into each other.

### `+inf` and NaN are special. `-inf` is not.

```asm
fcmp s17, s6            ; v against +infinity
cset w12, eq            ; v == +inf
csinc w12, w12, wzr, vc ; ...or the comparison was unordered, i.e. NaN
```

A `-inf` therefore goes through the ramp like any other small value and clamps to the bottom of
the window. The source almost certainly read `!(v < kInfinity)` — the vectorised block computes
it as `mvn(fcmgt(v, inf) | fcmgt(inf, v))`, which is the same predicate — and writing it as
`v == INFINITY || isnan(v)` compiles to a different (also correct) instruction mix a few
instructions longer. The predicate, not the spelling, is what the self-check pins.

### The `invert` arm is `1 - (1 - x)`, and that is not `x`

```asm
fsub s18, s2, s17       ; y = 1 - value
fsub s17, s2, s18       ; and 1 - y
cmp  w19, #0x0
fcsel s17, s17, s18, ne ; invert ? 1 - y : y
```

The double subtraction is in the binary, so it is in the recovered source. It is **not** the
identity: when `value` is small enough that `1 - value` rounds to 1, `1 - (1 - value)` is
exactly 0. A `t` of 1e-5 gives a polynomial of about 3e-10, and the inverted arm renders that as
a hard 0. The self-check asserts the 0.

### `lo == hi` guards cancellation, not division

`halfW` cannot be zero — it is floored at 1e-4 — so `den` is positive by construction and the
step branch at `fcmp s3, s4 / b.ne` looks dead. It is not dead **for a large enough span**: with
`span = 1e30` the centre is 5e29 and `centre ± 1e-4` round to the same float, so `den` really is
zero and the code returns a hard step instead of dividing by it. That needs a span around 1e25
times the half width, which a matte over pixel counts will never reach — but it is what the
branch is for, and the self-check reproduces it.

A NaN arriving in that branch is not a special case of its own: `t` is NaN, both selects pass it
through unchanged, and the polynomial hands the NaN back.

## `computeArrivalField` is a Dijkstra flood, and `span` is what it settled

The field `buildMatte` consumes is built here: one seed pixel, an 8-neighbour flood, and an edge
cost measured in COLOUR.

```
seedRow = lround(clamp(row, 0, height-1))        ; fcvtas — ties AWAY from zero
values.assign(width*height, +infinity) ; values[seed] = 0
colour  = |from.rgb - to.rgb| * (1/sqrt3)        ; normalised, so 1.0 is a full-scale jump
step    = k < 4 ? 1 : sqrt2                      ; the 4-connected offsets come FIRST
cost    = step * (edgeCostScaleFromTolerance(tolerance) * colour + 1)
if (blockAcrossEdges && colour > 0.45) cost = +infinity
candidate = here + cost
```

Both file-scope tables are eight-entry — `dx8` and `dy8` at 0x3f64 and 0x3f84 — and the ORDER of
the first four is load-bearing, because the step length is chosen from the index rather than from
the offsets:

| k | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| dx | +1 | −1 | 0 | 0 | +1 | +1 | −1 | −1 |
| dy | 0 | 0 | +1 | −1 | +1 | −1 | +1 | −1 |
| step | 1 | 1 | 1 | 1 | √2 | √2 | √2 | √2 |

Read out of the listing, that length is two selects — `fcsel s0, sqrt2, 1.0, hi` on `k > 1` then
`fcsel s0, 1.0, s0, eq` on `(k & 6) == 2` — which is why the recovered source tests `k < 4` and
still matches: the two predicates agree on all eight indices.

Four things a rewrite loses:

- **The colour distance is normalised by √3, not left raw.** `|Δrgb|` ranges 0..√3, so dividing by
  √3 puts the edge scale and the 0.45 threshold in the same units: a full black-to-white jump is
  1.0. Without it a three-channel jump costs √3 times what a one-channel jump does *and* the
  barrier threshold means something different depending on how many channels moved.
- **`blockAcrossEdges` REPLACES the cost with infinity**, after the multiply. It is not a big
  number and not a scaled penalty — so a block does not depend on the step length, and a blocked
  diagonal is as blocked as a blocked orthogonal.
- **Infinity and NaN abandon the edge through the same test.** `fcmp s0, s14 / b.eq` handles the
  barrier and `b.vs` handles an unordered product; writing `!(cost < infinity)` covers both, and
  that is what makes a NaN colour (not just a barrier) refuse to propagate.
- **There are TWO staleness guards and both are needed.** `settled[i]` rejects a pixel reached by a
  better route after it was queued; `here > values[i]` rejects a QUEUE ENTRY that a later, cheaper
  push has made stale. A binary heap has no decrease-key, so the second check is the only thing
  keeping a stale entry from expanding.

### `span` is a measurement, not a bound

`span` is the largest arrival the flood actually POPPED, tracked as `s9 = max(s9, s15)` as each
entry is settled — not the largest value in the array. Unreachable pixels keep `+infinity`
forever, so a `max` over the buffer would be infinity and `buildMatte` would compute a window at
infinity. The self-check pins this: a strip cut in half by a colour jump reports `span` 1, not
infinity.

### The seed snaps with `fcvtas`

`lround`, not truncation and not `floor`: column 0.6 seeds at index 1, column 0.4 at 0, and a
0.5 tie goes AWAY from zero to 1. The clamp is on both sides and pulls a position past the far
edge back to the LAST index — a seed of 99 on a 5-pixel strip is pixel 4, not out of range and
not clamped to 0.

## What is next

One function left, and it is the only one in the whole fleet still blocked on a struct nothing
has pinned:

```
computeMatteFullRes(const Image&, const Params&)   151 instrs
```

It is the entry point that runs downsample → field → matte → upsample. `Params` has to be
modelled from its own accesses before it can be written; every other function in MaskFlow now
takes only types that are recovered.
