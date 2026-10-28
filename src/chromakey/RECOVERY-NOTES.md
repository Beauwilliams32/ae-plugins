# chromakey — recovery notes

Source: `ChromaKeyAssist.plugin` (bundle id
`com.williamsmedia.aftereffects.ChromaKeyAssist`) installed at
`/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/`. The original C++
was destroyed on 2026-10-13; the binary is the only copy and is unstripped.

## State

Fourteen of the fifteen authored functions are recovered, 1951 instructions in
total. Seven of the fourteen are **instruction-identical** to the shipped
binary. The other seven are recovered with the right semantics but a different
instruction schedule. One symbol is still outstanding —
`analyzeFrame<unsigned short>`, the 16-bit instantiation of the template whose
8-bit sibling was recovered on 2026-10-28. `StoreReadout` is deliberately absent
from `src/`, and it is not a `chromakey::` symbol the differ tracks.

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
| `analyzeFrame<unsigned char>` | 752 | Recovered — **behavioural**; 703 here, verified bit-for-bit against the shipped bundle |
| `analyzeFrame<unsigned short>` | 770 | **Identified only** — the 16-bit instantiation of the same template, body 0x19b4-0x25bc in the listing; not yet transcribed |
| `renderView<unsigned char>` | 347 | Recovered — **behavioural**; 368 here, verified bit-for-bit against the shipped bundle |
| `renderView<unsigned short>` | 350 | Recovered — **behavioural**; 363 here, verified bit-for-bit against the shipped bundle |
| `spillScore` | 82 | Recovered — 80 here; clang elides a lower clamp it can prove (see below) |
| `uniformityScore` | 203 | Recovered — 203 here; 3 of 203 positions differ (see below) |
| `StoreReadout` | 70 | **Identified only, and must stay out of `src/`** — it takes a `PF_InData*` |

`test/chromakey.test.mjs` asserts that table against `tools/codegen-diff.mjs`, and
that every function in the last column of "Recovered" is named here.

## Why the eight DIFFERs are not transcription errors

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

`renderView<T>` is behavioural rather than instruction-identical: the arithmetic
and the schedule both came from the listing, but the compiler here allocates
registers differently and emits 12 (8-bit) / 13 (16-bit) more instructions than
the bundle does. The OUTPUT is exact — see "How `renderView` was verified" below.

`analyzeFrame<unsigned char>` is behavioural for the same reason and one more: it
is a long function (752 instructions in the bundle), and the compiler here emits
49 fewer. The OUTPUT is exact — see "How `analyzeFrame` was verified" below.

## How `renderView` was verified

`renderView` is not exported from the bundle by name in a way `dlsym` can use
directly, but the bundle is unstripped and the function's own address is known from
the listing, so the verification harness finds the bundle's load base with
`_dyld_get_image_name` and calls the function at `base + 0x25bc` (8-bit) and
`base + 0x2be4` (16-bit) directly.

**Measured:** 40 240 vectors — 20 000 randomised (random widths, heights, row
strides, mismatched source/destination widths, padded strides, 1x1 frames, scales
of 1 / 255 / 65535 / random, sensitivities -20..150, mixes -2..2, modes -1..6,
null and non-null `alpha` and `spill`) plus ~240 structured edge cases — compared
**byte-for-byte over the entire destination buffer**, including the row padding
that the kernel does not write. Result: `vectors=40240 mism8=0 mism16=0`.

Two things had to be got right for that number to mean anything:

- **Both destination buffers must be pre-filled with the same pattern.** The
  shipped kernel writes only the pixels, not the row padding, so if the two
  buffers start out different the untouched padding bytes differ and the
  comparison reports false mismatches. The harness fills both with the same
  bytes first.
- **The harness must be able to fail.** Changing one constant in the recovered
  source — the mode-2 spill threshold from `0.02f` to `0.03f` — makes the same
  harness report `mism8=35 mism16=14`. Reverting the constant returns it to
  `mism8=0 mism16=0`. The zero is therefore a measurement, not a stub.

This is a *measurement of the shipped binary*, not a host test: no After Effects
host has ever loaded a rebuilt bundle, and nothing here claims otherwise.

### What `renderView` actually does

Read from the arm64 listing (`/tmp/ae/chromakeyassist.asm`):

- The frame is rendered over `H = min(dst.height, src.height)` rows and
  `W = min(dst.width, src.width)` columns (0x25bc..0x25c8, 0x2644..0x2650).
- The sensitivity threshold is the inlined `sensitivityToThreshold`
  (0x25cc..0x2604) and the blend is `clampf(mix, 0, 1)` (0x2608..0x261c).
- **Verbatim is `(mode == 5) || (alpha == nullptr)`** (0x2654..0x265c). A null
  `alpha` buffer means "no matte", and the frame is copied through untouched —
  it does not fall back to the default mode.
- The alpha channel is always copied verbatim first (0x2790..0x2794), whatever
  the mode.
- The dispatch (0x279c) is: 0 or out of range -> default, 1 -> ramp, 2 -> despill,
  3 -> gradient, 4 -> matte, 5 -> verbatim.
- **The fallback colour is `(src / scale) * 0.35`** (0x289c, 0x29bc) — it is used
  by the ramp, despill and gradient paths whenever their test fails.
- Mode 1 (ramp): if `alpha <= threshold` then `t = clampf(alpha / threshold, 0, 1)`
  and the ramp is looked up at `t`; otherwise the fallback.
- Mode 2 (despill): if `spill == nullptr` -> fallback. If `spill <= 0.02` ->
  fallback. Otherwise `R = clampf(0.3 + spill * 0.7, 0, 1)`,
  `G = (G / scale) * 0.25` (**not clamped**), `B = clampf(0.3 + spill * 0.6, 0, 1)`.
- Mode 3 (gradient): if `|alpha - threshold| >= threshold * 0.5` -> fallback.
  Otherwise the alpha buffer's gradient magnitude is taken and
  `t = 1 - min(mag / (2 * threshold), 1)`, and the ramp is looked up at `t`.
  The gradient is the same **cross** kernel `gradientMagnitude` uses: the
  horizontal difference on row `y`, the vertical difference on column `x`.
- Mode 4 (matte): `v = clampf((alpha / threshold) * 0.5, 0, 1)`, written to all
  three colour channels.
- The tail (0x2a74..0x2b08) is
  `out = fma(computed - src/scale, blend, src/scale)`, then `clampf(out, 0, 1)`,
  then `out = fma(out, scale, 0.5)`, then stored as `(T)(int)out` — the
  conversion truncates toward zero and the low 8 or 16 bits are kept.
- The ramp table is `.rodata` at 0x5b18, four stops on a sixteen-byte stride
  `{pos, r, g, b}`:
  `stop0 00000000 3dcccccd 3e4ccccd 3f666666` /
  `stop1 3eb33333 3dcccccd 3f4ccccd 3e99999a` /
  `stop2 3f333333 3f733333 3f666666 3dcccccd` /
  `stop3 3f800000 3f733333 3dcccccd 3dcccccd`.
  The segment is `(t <= 0.35) ? 0 : (t <= 0.70) ? 1 : 2`, and the interpolation
  is `c = fma(c1 - c0, frac, c0)` with `frac = (t - pos0) / (pos1 - pos0)`.
- `rowBytes` is in ELEMENTS of `T` (the 16-bit instantiation doubles it itself at
  0x2cac), and the pixel layout is `[A, R, G, B]` for both widths.
- The alpha and spill buffers are `W` floats per row with no padding.

### The spill threshold is 0.02, not 0.25

An earlier probe of this function reported the mode-2 spill threshold as `0.25`.
That was wrong, and it came from reading the wrong register: `fmov s19, #0.25`
at 0x26b8 is the **mode-2 green multiplier**, a different quantity.

The threshold itself is 0.02 (`0x3CA3D70A`), materialised at:

- 8-bit: `mov w19, #0xd70a` / `movk w19, #0x3ca3` at **0x26b0-0x26b4**, compared
  and branched at `fmov s21, w19` / `fcmp s23, s21` / `b.le 0x289c` at
  **0x27bc-0x27c4** — so the fallback is taken when `spill <= 0.02`.
- 16-bit: the same constant in `w20` at **0x2ce8-0x2cec**, compared and
  branched at **0x2dec-0x2df4**.

The `<=` matters: `spill == 0.02` exactly takes the fallback. The oracle probe
confirms it — `spill = 0.02` and `spill = 0.0` give the same output `(89, 0, 0)`,
while `spill = 0.1` gives `(94, 0, 92)` and `spill = 0.9` gives `(237, 0, 214)`.

### Two hazards in the shipped code

- **The shipped loop does not terminate when `dst.width <= 0`.** `W` is zero and
  the loop is a do-while, so it never exits. The recovered source skips the frame
  instead. This is the one place where the recovered code deliberately differs in
  CONTROL FLOW from the bundle, and it is a difference that can only turn a hang
  into a no-op.
- **A row is skipped when `src.width <= 0`** (0x26f4), which is not the same
  condition as `dst.width <= 0`.

## How `analyzeFrame` was verified

`analyzeFrame<unsigned char>` is the largest body in the namespace: 752
instructions (`__ZN9chromakey12analyzeFrameIhEENS_5StatsERKNS_4ViewIKT_EEfRKNS_6ParamsEPfSB_`,
body 0x7d8-0x1398, the hot path returning at 0x12f8 and the cold exception
tail after it), read from the arm64 listing `/tmp/ae/chromakeyassist.asm`. Its
16-bit sibling (`It`, body 0x19b4-0x25bc, 770 instructions) is the same
algorithm with a wider pixel load and is still outstanding.

The verification is the same dlopen differential as `renderView`: the installed
bundle is loaded, its load base is found through `_dyld_get_image_name`, and the
shipped function's own machine code is called **in place** at `base + 0x7d8`.
No `MAP_JIT` page is involved — the shipped code runs where the loader put it.

Its `Stats` comes back through a hidden pointer in `x8` rather than in registers:
five floats is not a homogeneous aggregate of at most four members, so AAPCS64
returns it indirectly. The harness's `ShippedStats` therefore carries a
non-trivial destructor on purpose, so that the harness's own call uses the same
indirect-return convention and the result pointer really lands in `x8`, while the
other arguments keep `x0` (the View), `x1` (the Params), `v0` (the scale) and
`x2`/`x3` (the two output buffers). The shipped body zeroes twenty bytes through
that pointer and then stores uniformity at +0x0, the mean spill at +0x4, p5 and
p95 as one eight-byte store at +0x8, and the keyed fraction at +0x10.

**Measured:** 8100 vectors — 8000 randomised (widths and heights 1..9,
padded strides, five pixel patterns including all-grey and all-key-colour,
scales of 1 / 255 / random, sensitivities -20..150, random key colours, a null
output buffer in one case in four, and the key normalised and un-normalised) plus
100 structured edge cases (1x1, single row, single column, zero-size, every-pixel-
key-colour, grey-under-coloured-key, coloured-under-grey-key, both-grey, and the
sensitivity and scale extremes) — compared **byte-for-byte over both output
buffers and over the returned `Stats`**:

```
an_vectors=8100 an_mism8=0 an_statmism8=0
```

**The harness can fail.** Two independent mutations of the recovered source, each
applied to a copy in a temporary directory, both make the same harness report
mismatches:

| Mutation | 8-bit |
|---|---|
| the 5th-percentile fraction `0.05f` -> `0.06f` | 1110 mismatches |
| the achromatic shortcut `||` -> `&&` | 962 mismatches |

Reverting either constant returns the harness to zero. The zeros are therefore
measurements, not stubs.

This is a *measurement of the shipped binary*, not a host test: no After Effects
host has ever loaded a rebuilt bundle, and nothing here claims otherwise.

### What `analyzeFrame` actually does

Read from the arm64 listing, in the order the listing runs it:

- The returned `Stats` is zeroed first (0x808/0x80c for the 8-bit body,
  0x19e4/0x19e8 for the 16-bit one), and a width or height below 1 returns it as
  it stands **without touching either output buffer** (0x810..0x828 -> the epilogue
  at 0x12cc, `ret` at 0x12f8).
- The threshold is `sensitivityToThreshold` (0x840..0x874), and the pixel count is
  `height * width`, multiplied in 32 bits and widened (0x87c; `umull x25, w9, w19`
  at 0x1a58 in the 16-bit body).
- **The KEY colour is NOT divided by the scale** (0x8ac..0x900), while **every
  PIXEL channel is** (0x988/0x98c/0x998). In the 16-bit body the same three
  divisions are at 0x1b68/0x1b6c/0x1b78, each `fdiv` against the scale in `s8`.
  The asymmetry is easy to miss and it is not a mistake in the transcription: it
  is what the shipped code does, and normalising the key is the caller's job.
- The pixel is four components, `[A, R, G, B]`, for both widths. In the 16-bit
  body the loads are `ldur h6, [x13, #-0x2]` (R, at byte +2), `ldr h7, [x13]`
  (G, at byte +4) and `ldr h16, [x13, #0x2]` (B, at byte +6), each followed by
  `ucvtf` — so the 16-bit pixel is eight bytes wide and the channels are
  unsigned.
- `rowBytes` is in ELEMENTS of `T` for both widths: the 16-bit body scales it
  itself at 0x1b40 (`lsl x11, x11, #1`), having loaded it at 0x1b38.
- Pass one computes each pixel's chroma distance and whether it is inside the
  threshold (0x91c..0xa0c), and accumulates the mean luma of the KEYED pixels
  only, in DOUBLE (0x9d8..0x9e8); the mean is that sum over the keyed count, or
  0.5 when nothing is keyed (0xa10..0xa28).
- Pass two scores every pixel that is NOT keyed (0xb38..0xd94). A keyed pixel
  scores zero and its distance joins the keyed set, but it is **not** counted in
  the score sum and **not** counted in the non-keyed count (0xba8 skips
  0xb54/0xb58/0xb5c).
- The keyed fraction is the keyed count over the pixel count (0xd98) and the mean
  spill is the mean score over the pixels that are NOT keyed (0xdac..0xdc4).
- The uniformity is `uniformityScore` over the **KEYED** distances only
  (0xdc8..0x110c), and is 0 when nothing is keyed.
- p5 and p95 are percentiles over **ALL** the distances, over two sorted copies
  of the whole array (0x1110..0x1238). The interpolation is a **fused**
  multiply-add (0x11a4), which is why the shipped `analyzeFrame` cannot reuse the
  exported `percentile` — that one divides `p` by 100 and interpolates with a
  separate multiply and add, and the two differ in the last bit. The recovered
  source therefore has its own local `analyzePercentile`.
- Both percentiles are then divided by sqrt(2), multiplied by 100 and clamped to
  [0, 100] (0x123c..0x1270).
- The two optional output buffers are copied out at the end (0x1274..0x129c),
  each guarded by a null check.

### Two transcription errors the differential caught

Both were found by the differential rather than by reading, and both are worth
recording because each produced a plausible-looking source:

1. **The hue arguments were the wrong way round.** The shipped key hue is
   `atan2f(cr, cb)` — 0xa80 `mov.16b v0, v10` puts `cr` in the first argument
   register and 0xa84 `mov.16b v1, s9` puts `cb` in the second, then 0xa88
   `bl _atan2f` — and the pixel hue is the same (0xc1c/0xc20/0xc34). The repo's
   `hueAngle(a, b)` is `atan2f(b, a)`, so the correct call is
   `hueAngle(cb, cr)`, and the first transcription had it as `hueAngle(cr, cb)`.
   The exported `spillScore` uses `atan2f(cr, cb)` as well (0x14d8 `mov.16b v0,
   s11` for `key.cr`, 0x14dc `mov.16b v1, s9` for `key.cb`), which is the
   cross-check. Swapping the arguments made the mean spill differ on 4509 of the
   8100 8-bit vectors.
2. **The achromatic shortcut is an OR, not an AND.** The shipped sequence is

   ```
   0xc08  movi.2d v0, #0          ; the score the shortcut produces
   0xc0c  fcmp s14, s6             ; s14 = the pixel's chroma magnitude, s6 = 1e-6
   0xc10  ldr s1, [sp, #0x60]      ; s1 = the key's chroma magnitude
   0xc14  fccmp s1, s6, #0x8, pl   ; the key's magnitude is compared only on
                                   ; the path where the pixel's was NOT smaller
   0xc18  b.mi 0xb4c              ; ... and the branch is taken when it is
   ```

   The immediate `0x8` in the `fccmp` is NZCV with **N in bit 3**, so it forces
   N=1, Z=0, C=0, V=0 — "less than" — on the else path. The shortcut therefore
   fires when the pixel's magnitude was below 1e-6 whatever the key's magnitude
   is, and when it was not, when the KEY's magnitude is below 1e-6. That is an
   **OR**, and the exported `spillScore` uses the same OR (its two early returns
   at 0x1490 and 0x14ac), so the two shipped functions agree rather than
   disagree. Reading the `fccmp` as a plain compare gives an AND, and an AND
   sends a grey pixel under a coloured key down the scored path, where the
   shipped code returns a score of exactly 0.

   This was checked three independent ways: the `nzcv` immediate was confirmed by
   assembling and running the four-instruction sequence (the branch is taken for
   pixel<1e-6/key>=1e-6, pixel<1e-6/key<1e-6 and pixel>=1e-6/key<1e-6, and not
   for pixel>=1e-6/key>=1e-6 — three of four, so an OR); the x86 slice of the
   same bundle agrees (`ucomiss %xmm3,%xmm7` / `ja` at 0xd5d and `ucomiss
   -0xcc(%rbp),%xmm7` / `ja` at 0xd69, both jumping to the same place); and the
   oracle shows the shipped score is 0 for a grey pixel under a coloured key,
   where the AND gave 9.1e-10.

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
