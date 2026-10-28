# chromakey — recovery notes

Source: `ChromaKeyAssist.plugin` (bundle id
`com.williamsmedia.aftereffects.ChromaKeyAssist`) installed at
`/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/`. The original C++
was destroyed on 2026-10-13; the binary is the only copy and is unstripped.

## State

Eleven of the fifteen authored functions are recovered, 502 instructions in total.
Seven of the eleven are **instruction-identical** to the shipped binary. Four are
recovered with the right semantics but a different instruction schedule. Four are
not recovered at all, and `StoreReadout` is deliberately absent from `src/`.

| Function | Instrs | State |
|---|---|---|
| `clampf` | 5 | Recovered — **instruction-identical** |
| `chromaDistance` | 8 | Recovered — **instruction-identical** |
| `rgbToYCbCr` | 25 | Recovered — **instruction-identical** |
| `sensitivityToThreshold` | 15 | Recovered — **instruction-identical** |
| `hueAngle` | 15 | Recovered — **instruction-identical** |
| `angularDiff` | 18 | Recovered — **instruction-identical** |
| `percentileUnsorted` | 53 | Recovered — 52 here; the same instructions with one register allocated differently |
| `gradientMagnitude` | 47 | Recovered — **instruction-identical** (corrected 2026-10-28; see below) |
| `percentile` | 31 | Recovered — 28 here; the same arithmetic, scheduled differently |
| `analyzeFrame<unsigned char>` | 752 | **Identified only** |
| `analyzeFrame<unsigned short>` | 770 | **Identified only** |
| `renderView<unsigned char>` | 347 | **Identified only** |
| `renderView<unsigned short>` | 350 | **Identified only** |
| `spillScore` | 82 | Recovered — 80 here; clang elides a lower clamp it can prove (see below) |
| `uniformityScore` | 203 | Recovered — 203 here; 3 of 203 positions differ (see below) |
| `StoreReadout` | 70 | **Identified only, and must stay out of `src/`** — it takes a `PF_InData*` |

`test/chromakey.test.mjs` asserts that table against `tools/codegen-diff.mjs`, and
that every function in the last column of "Recovered" is named here.

## Why the four DIFFERs are not transcription errors

Each is the same arithmetic with a different instruction schedule, and each was
chased once:

- `percentile` and `percentileUnsorted` both differ at their **first instruction**:
  the shipped code materialises its `0.0` constant into a register that leaves the
  incoming argument in `s0`, and this build does the reverse (`mov.16b v1, v0`).
  That choice follows from register pressure, not from the source — writing the
  single shared early-out that the shipped `movi.2d v1, #0` plus two `cbz` branches
  to one target implies moved this build from 28 to 28 and from 48 to 52
  instructions respectively, which is as close as the expression can get it.
- `spillScore` is 80 instructions here against the bundle's 82. The difference is
  the LOWER clamp on the chroma term: the shipped build kept it, and clang in this
  build proves the value non-negative (it is the sum of two square roots) and
  elides it. The outputs are identical, and the self-check pins them against a
  model written from the disassembly rather than against the compiler.
- `uniformityScore` is 203 instructions on both sides, with 3 of 203 positions
  differing — a register-allocation difference in the vectorised prologue
  (`movi.2d v0, #0x0` against `movi.2d v1, #0x0`), not a difference in the
  arithmetic.

`gradientMagnitude` used to be in this list and is not any more: it was wrong, and
is now instruction-identical. See the section below.

## `gradientMagnitude` was wrong, and the error had spread

An earlier revision of this file transcribed the shipped kernel's horizontal
difference onto row `ym1` instead of row `y`, and then described the result as a
kernel "anchored top-left, not centred". The self-check pinned that reading, the
header repeated it, and this file argued from it — a transcription error that had
propagated into three places and was being defended as a shipped quirk.

The arm64 listing (`/tmp/ae/chromakeyassist.asm`, symbol
`__ZN9chromakey17gradientMagnitudeEPKfiilii`, body 0x2b28–0x2bdc) says otherwise:

```
sxtw x12, w5            ; x12 = (int64) y
mul  x12, x3, x12       ; x12 = rowStride * y
add  x12, x0, x12, lsl #2 ; base = field + rowStride*y*4   (row Y, the CENTRE row)
ldr  s0, [x12, w10, uxtw #2]  ; s0 = field[stride*y   + xm1]
ldr  s1, [x12, w9, sxtw #2]   ; s1 = field[stride*y   + xp1]
mul  x12, x3, x11       ; x11 = ym1
add  x12, x0, x12, lsl #2
ldr  s2, [x12, w4, sxtw #2]   ; s2 = field[stride*ym1 + x]  (above)
csinc w8, w8, w5, lt    ; w8 = yp1
ldr  s3, [x12, w4, sxtw #2]   ; s3 = field[stride*yp1 + x]  (below)
```

The two horizontal loads read from the row base `field + rowStride*y*4` — the
CENTRE row — and the two vertical loads read column `x` of rows `ym1` and `yp1`.
So the kernel is a **cross**: the horizontal difference is taken on the centre row
`y`, and the vertical difference on the centre column `x`.

The corrected source is **instruction-identical** to the bundle: the differ reports
`MATCH chromakey::gradientMagnitude 47 instructions, identical`, where it had
reported `DIFFER shipped=47 mine=44`.

Two details the corrected reading makes visible:

- **The denominators are floored at 1, by CSINC's else arm adding 1.** The
  instruction is `csinc w9, w9, wzr, gt` (listing 0x2ba4) — `CSINC` selects `Rn`
  when the condition holds and `Rm + 1` when it does not, so the false arm yields
  1 and not 0. Reading `CSINC` as a plain two-way select would floor the divisor at
  0 and divide by zero on a one-wide field.
- **The `uxtw`/`sxtw` split on the loads is not a discrepancy.** The `xm1` load is
  unsigned because `xm1` is never negative (`xm1 = x > 1 ? x - 1 : 0`), and the
  others are signed because `xp1`, `y` and the row bases are.

The self-check now pins the shipped reading on grids where the two readings
disagree — a horizontal step in the row above the centre reads 0, the same step in
the centre row reads 5, and a vertical step in the centre column reads 5 while the
same step in the next column reads 0.

### What is measured, and how

The shipped bundle is unstripped, and `chromakey::gradientMagnitude` is one of its
EXPORTED symbols (arm64 export offset 0x2b28), so its own machine code can be run
and compared against this build. `test/chromakey.test.mjs` extracts the arm64 slice
of the installed bundle, maps the function's bytes into an executable page, and
calls them alongside the recovered source over randomised fields, widths, heights,
strides and coordinates.

**Measured:** the shipped `chromakey::gradientMagnitude` was called on 4000
randomised cases — random widths 1..24, heights 1..24, row strides of width + 0..8
elements, and coordinates one outside the frame on either side so the edge clamps
are exercised — and the recovered source returned the identical float in every
one. 4000 cases, **0 mismatches**.

This is a *measurement of the shipped binary*, not a host test: no After Effects
host has ever loaded a rebuilt bundle, and nothing here claims otherwise.

Two source-shape deductions *did* land as instruction-identical results, and both
came from reading the schedule rather than the maths:

- `rgbToYCbCr` was 10 of 25 positions off until all three channels were read into
  locals up front — the shipped code's first two instructions are `ldp s2, s0, [x0]`
  and `ldr s1, [x0, #0x8]`, before any constant is built. Hoisting them made it
  25 of 25 identical.
- `percentileUnsorted` checks its input for emptiness **twice**, once before the sort
  and once after: the shipped code re-reads `begin`/`end` following the call
  (`ldp x8, x9, [x19]` then `cmp x8, #0` / `ccmp x9, x8, #0x4, ne`). Reproducing the
  second check took it from 48 to 52 of the 53 shipped instructions.

## What the recovered functions establish about the effect

- **The luma is Rec.601**, and the coefficients are exact: the immediates decode to
  0.587, 0.299, 0.114, and the chroma scales to the six-decimal literals 0.564334 and
  0.713267. Those are *not* the divisions `1/1.772` and `1/1.402`, which are three
  ulps away — the self-check pins the literals bit-exactly and asserts the division
  value is **not** what the function returns.
- **`chromaDistance` ignores luma entirely.** It reads `+0x4` and `+0x8` of each
  argument and never the base offset, so two colours with the same chroma are at
  distance 0 however far apart their brightness is. `test/chromakey.test.mjs`
  re-derives the offsets from the bundle's own two loads and asserts that neither
  argument is read from offset 0.
- **`hueAngle(a, b)` is `atan2f(b, a)`** — the first parameter is the denominator.
  Reading it the other way round swaps 0 and pi/2, and the self-check asserts both.
- **`angularDiff` wraps past pi** (strictly greater, so exactly pi is not wrapped) and
  takes the difference as a magnitude.
- **`sensitivityToThreshold` runs 0.03 to 0.45**, saturating one-sidedly at 100 and
  short-circuiting a negative input to 0.03.
- **`gradientMagnitude`'s kernel is a cross.** The horizontal difference is taken on
  the CENTRE row `y` and the vertical difference on the CENTRE column `x` — the four
  samples are `field[y][xm1]`, `field[y][xp1]`, `field[ym1][x]` and `field[yp1][x]`.
  A step in the row above the centre therefore reads 0, and the same step in the
  centre row reads 5; a vertical step in the centre column reads 5 and the same
  step one column to the right reads 0. An earlier revision of this file said the
  opposite — see the section above, and the correction is the reason
  `gradientMagnitude` is now instruction-identical rather than a DIFFER.
- **Its row parameter counts elements, not bytes.** The code scales it by four to
  reach a row, so a caller passing a byte stride gets the wrong row. The self-check
  uses a stride of 6 elements over a 3-wide, 3-tall grid, where a byte interpretation
  returns 0 instead of 2.

## Two `percentile` variants that differ only in how they survive a negative `p`

Both return the minimum, by different routes, and the difference is worth stating
because it is invisible in the output:

- `percentile` lets the negative `p` into the index arithmetic unclamped
  (`fcsel s0, s0, s1, mi` selects `p` itself), and only the **saturating** float-to-
  unsigned conversions keep the index in range: `fcvtmu` and `fcvtpu` both land on 0.
- `percentileUnsorted` clamps to a fraction of 0 first.

So the shipped `percentile` is one refactor away from an out-of-bounds read — if the
index conversions ever became non-saturating, a negative percentile would index
before the array. That is an improvement candidate recorded here rather than fixed,
because changing it would change recovered behaviour.

## Improvement candidates

Both are behavioural-identical changes, which is the only kind this repository makes
to recovered code without changing the effect:

1. **`percentileUnsorted` copies the whole vector to sort it**, and it is called
   per-analysis rather than per-pixel, so the cost is small; but the *same* function's
   by-value signature means a caller cannot avoid the copy. An overload taking the
   vector by rvalue reference would remove it where the caller is done with the data.
   Pinned by the self-check: the caller's vector must stay unsorted.
2. **`gradientMagnitude`'s element-count stride is a trap for a caller.** A byte-count
   overload that divides by `sizeof(float)` and delegates preserves every recovered
   number while removing the one way this function can be misused.
