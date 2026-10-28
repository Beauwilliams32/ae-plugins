# shake — recovery notes

Source: `ShakeCapture.plugin` (bundle id `com.williamsmedia.aftereffects.ShakeCapture`)
installed at `/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/`. The original
C++ was destroyed on 2026-10-13; the binary is the only copy and it is unstripped.

## State

Eleven authored functions. **All eleven recovered** — five compile instruction-identical to
the bundle, and six are behavioural recoveries whose arithmetic was checked against the
shipped binary itself.

| Function | Instrs | State |
|---|---|---|
| `clampf` | 5 | Recovered — **instruction-identical** (the fleet's shared copy) |
| `median` | 43 | Recovered — behavioural: 43 here, 12 of 43 positions differ (register allocation) |
| `trackTranslation` | 960 | Recovered — behavioural: 952 here (see below) |
| `accumulate` | 135 | Recovered — **instruction-identical** |
| `ShakeProfile::~ShakeProfile` | 16 | Recovered — **instruction-identical** |
| `movingAverage` | 155 | Recovered — behavioural: 156 here |
| `highPass` | 105 | Recovered — behavioural: 106 here |
| `extractJitter` | 36 | Recovered — **instruction-identical** |
| `normalizeUnitAmplitude` | 125 | Recovered — **instruction-identical** |
| `sampleLooped` | 92 | Recovered — behavioural: 83 here |
| `resampleProfile` | 50 | Recovered — behavioural: 51 here |

The differ's tally is **5 identical, 6 differing, 0 not implemented**. Before
`trackTranslation` landed it was 5 identical, 5 differing, 1 not implemented.
`test/shake.test.mjs` asserts that table against `tools/codegen-diff.mjs`, so recovering
another function fails the test until this file is updated.

## The listing

Everything below was read from `/tmp/ae/shakecapture.asm` — the arm64 listing of
`ShakeCapture.plugin`, regenerated with

```
otool -tV -arch arm64 "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/ShakeCapture.plugin/Contents/MacOS/ShakeCapture"
```

Line numbers are lines of that file; addresses are the function's own.

| Function | Listing lines | Address |
|---|---|---|
| `clampf` | 3–8 | 0x738 |
| `median` | 9–52 | 0x74c |
| `trackTranslation` | 53–1013 | 0x7f8 |
| `accumulate` | 1014–1298 | 0x16f8 |
| `ShakeProfile::~ShakeProfile` | 1299–1315 | 0x1b64 |
| `movingAverage` | 1316–1471 | 0x1ba4 |
| `highPass` | 1472–1577 | 0x1e10 |
| `extractJitter` | 1578–1614 | 0x1fb4 |
| `normalizeUnitAmplitude` | 1615–1740 | 0x2044 |
| `sampleLooped` | 1741–1833 | 0x2238 |
| `resampleProfile` | 1834–1884 | 0x23a8 |

Each range starts at the function's label line and ends at its last line, exception-path
tails included; the next line is either the next label or a blank one.

## `trackTranslation` — the tracker, recovered 2026-10-28

960 instructions, the largest function in the bundle and the effect's whole point: a
block-matching search for how far the picture in `b` has moved relative to the picture in
`a`. This is a **behavioural recovery** — the arithmetic is right, the instruction stream is
not the shipped one.

### The five guards (listing 0x830–0x864)

```
0x830  cbz  x0, 0x14ec      ; a == null
0x834  cbz  x1, 0x14ec      ; b == null
0x838  cmp  w4, #0x4
0x83c  b.lt 0x14ec          ; block < 4
0x840  subs w8, w5, #0x1
0x848  b.lt 0x14ec          ; radius < 1
0x850  add  w8, w4, w5, lsl #1      ; w8 = block + 2*radius
0x858  cmp  w2, w8
0x85c  b.lt 0x1624          ; width  < block + 2*radius
0x860  cmp  w3, w8
0x864  b.lt 0x1624          ; height < block + 2*radius
```

All five answer `{0, 0}`. The first three branch to 0x14ec, which zeroes the result
registers (0x14ec) and branches to the return (0x1624); the width and height tests branch
straight to 0x1624, where both result registers are already zero (0x828 and 0x854).

### The block grid (0x900–0x9a4)

The band row starts at `radius` and steps by `block` while `bandRow + block <= height -
radius` (0x918 adds `block`, 0x94c–0x954 compares against `height - radius`, computed at
0x87c); the column starts at `radius` and steps by `block` while `col + block <= width -
radius` (0x98c adds `block`, 0x99c–0x9a4 compares against `width - radius`, computed at
0x888). So the template block is taken from `a` at `(bandRow, col)` and the search is
centred on it.

### The texture test (0x9c0–0xa10)

```
0x9c0  movi.2d v0, #0      ; the sum
0x9c8  movi.2d v1, #0      ; the sum of squares
0x9d4  ldr  s2, [x11], #0x4
0x9d8  fcvt d2, s2         ; every sample widened to DOUBLE
0x9dc  fadd d0, d0, d2     ; sum    += v
0x9e0  fmadd d1, d2, d2, d1; sumsq += v*v
0x9fc  fdiv d0, d0, d8     ; mean    = sum / (block*block)
0xa00  fdiv d1, d1, d8     ; meansq  = sumsq / (block*block)
0xa04  fmsub d0, d0, d0, d1 ; variance = meansq - mean*mean   (ONE rounding: a fused subtract)
0xa08  fcvt s0, d0         ; ... narrowed to FLOAT
0xa0c  fcmp s0, s9         ; s9 = 0x358637bd = 1e-6f
0xa10  b.mi 0x98c          ; variance < 1e-6 -> SKIP the block
```

`d8` is `block * block` as a double (0x890–0x894), and `s9` is the constant `0x358637bd`
(0x8dc–0x8e4), which decodes to 9.99999997e-07. A block whose variance is below it carries
no position information and is skipped entirely — it contributes to neither median.

### The search (0xa24–0xa90)

```
0xa34  fmov s0, #-1.00000000    ; best = -1.0f
0xa60  fcmp s0, #0.0
0xa64  cset w9, mi              ; w9 = (best < 0)
0xa68  fcmp s1, s0              ; compare the candidate cost with best
0xa6c  csinc w9, w9, wzr, pl    ; w9 = (cost >= best) ? w9 : 1
0xa70  cmp w9, #0x0
0xa74  csel w11, x16, w11, ne   ; bestDx = dx
0xa78  csel w10, x14, w10, ne   ; bestDy = dy
0xa7c  fcsel s0, s1, s0, ne     ; best = cost
```

So the update condition is `best < 0 || cost < best`: before any candidate has been seen
`best` is negative and the first candidate always wins, and after that a candidate has to be
**strictly** cheaper. **A tie therefore keeps the earlier candidate** — the search walks `dy`
from `-radius` upwards and `dx` from `-radius` upwards (the dx counter is seeded at 0x89c
with `-radius` and incremented at 0xa80), so on a tie the answer is the first offset tried,
not the smallest one.

The cost itself is a row-major, sequential **float** sum of the absolute differences
(0xbe8–0xc00: `fabd` then `fadd`), which is why a near-tie resolves the same way.

### The sub-pixel refinement (0xc08–0xfa8, 0xfac–0x114c)

The horizontal refinement runs only when `-radius < bestDx < radius` (0xc10 `cmp w11, x7`
against `-radius`, 0xc14–0xc18 `ccmp`/`b.lt` against `radius`); the vertical one only when
`-radius < bestDy < radius` (0xfb0–0xfb4). Each refinement pays for two more block costs —
the block one column to the left and to the right (0xc38 and 0xdd8), the block one row above
and below (0xfc0 and 0x1160) — and then fits a parabola through the three costs:

```
0xf68  fmov  s4, w0            ; w0 = 0x2b8cbccc = 9.99999996e-13
0xf6c  fcmp  s0, s4
0xf74  b.ls  0xfa8             ; best <= 1e-12 -> no offset
0xf78  fmadd s4, s0, s10, s1   ; denominator = c1 + best*(-2)   (s10 = -2.0f)
0xf7c  fadd  s4, s4, s2        ;              = (c1 - 2*best) + c2
0xf84  fcmp  s4, s5
0xf88  b.ls  0xfa8             ; denominator <= 1e-12 -> no offset
0xf8c  fsub  s1, s1, s2        ; c1 - c2
0xf90  fmul  s1, s1, s11       ; * 0.5f   (s11 = 0.5f)
0xf94  fdiv  s1, s1, s4        ; t
0xf9c  fcsel s2, s11, s1, gt   ; clamp to  0.5f
0xfa4  fcsel s3, s12, s2, mi   ; clamp to -0.5f  (s12 = -0.5f)
0xfa8  fadd  s13, s3, s13      ; the answer is bestDx + t
```

The denominator is built as `(c1 - 2*best) + c2` — a fused multiply-add and then a separate
add — because float addition is not associative and the order is the shipped one.

### The medians and the return (0x135c, 0x1498, 0x14f4–0x1624)

The two refined offsets are pushed into two `std::vector<float>`s (0x135c and 0x1498), and
the answer is the **median of each**, taken with the very same `shake::median` — the listing
calls it twice, over a copy of each vector (0x14f4 onward). The return at 0x1624 loads the
two medians into the `Vec2`.

## Two corrections made after the first transcription (both 2026-10-28)

Both were caught by **calling the shipped bundle**, not by re-reading the listing, and both
are now asserted in `src/shake/selfcheck.cpp`.

### `median` — the even case IS the true median

The shipped code's second `__nth_element` call was read as covering a **one-element** range,
`[begin + mid - 1, begin + mid)`, which would make it a no-op and leave `data[mid - 1]` to
be read straight out of a *partitioned, not sorted*, array — an arbitrary element of the
lower half. **That reading was wrong**, and the first transcription was built on it.

The listing's own argument registers say otherwise:

```
0x79c  ldp  x0, x8, [x19]     ; x0 = begin
0x7ac  add  x2, x21, x0       ; x2 = begin + mid
0x7b0  sub  x1, x2, #0x4      ; x1 = begin + mid - 1
0x7b4  add  x3, sp, #0xf      ; x3 = the comparator
0x7bc  bl   __nth_element     ; __nth_element(begin, begin + mid - 1, begin + mid, less)
0x7c8  ldur s0, [x8, #-0x4]   ; lower = data[mid - 1]
0x7cc  fadd s0, s8, s0        ; lower + upper
0x7d4  fmul s0, s0, s1        ; * 0.5
```

The call's **range** is `begin .. begin + mid` — the lower partition — and its **nth** is
that partition's last element. It is not a one-element range and not a no-op: it selects the
largest of the `mid` smallest, the true lower middle, and leaves it at `data[mid - 1]`. The
first transcription dropped the call and read `data[mid - 1]` out of the partition, whose
order libc++ leaves unspecified — so the answer depended on the input order.

**Measured:** the shipped bundle's exported `shake::median` was called on 8260 vectors (every
length from 0 to 80 with heavy duplicates, 3000 large random ones, and sorted and reversed
inputs) and on 8000 more (every length from 0 to 39) in the differential run, and returned
the exact median every time — the middle element on an odd count, the **average** of the two
middle elements on an even count. The first transcription's version disagrees with the true
median on **10530 of 20000** random even-count vectors. This build now makes the same
selection the shipped code makes and reproduces the shipped arithmetic,
`(lower + upper) * 0.5f`; the two agree bit for bit.

**Consequence outside this namespace.** `tools/demo/harness.cpp` has a panel that shows
`shake::median` disagreeing with the true median under reordering, and `test/demo.test.mjs`
gates on that disagreement being greater than zero ("the recorded defect"). That defect was
the first transcription's, not the bundle's: the shipped median is order-independent. Now
that the recovery is correct, the panel has nothing to show and that gate fails. Both files
are outside this namespace's scope, so they are reported rather than edited here — **the demo
panel and its gate need to be retired or rewritten by whoever owns the demo harness.**

### `sampleLooped` — Clamp answers 0 outside the range, it does not hold the end sample

The listing sends **both** out-of-range tests to the **same** zero return:

```
0x229c  fcmp d1, #0.0
0x22a0  b.mi 0x238c      ; t < 0     -> 0.0f
0x22a8  fcmp d1, d2       ; d2 = (double)(n - 1)
0x22ac  b.gt 0x238c      ; t > last  -> 0.0f
0x22b0  fcmp d1, #0.0
0x22b4  b.ls 0x2388      ; t <= 0    -> values[0]
0x22b8  fcmp d1, d2
0x22bc  b.ge 0x23a0      ; t >= last -> values[last]
```

The end samples are returned only **at the endpoints**; anywhere outside the range the answer
is `0.0f`. The first transcription read the `t > last` branch as "hold the last sample", and
the self-check asserted that. **Measured** against the shipped bundle: `sampleLooped({0,10,20,30},
3.001, Clamp)` is `0.0f` and `sampleLooped({0,10,20,30}, 3.0, Clamp)` is `30.0f`. Corrected.

This propagates: `resampleProfile` (0x2414–0x2424) calls `sampleLooped` with `t = step * i`,
so a step that runs past the end of the series produces zeros at the tail — 299 of 4000
random `resampleProfile` calls disagreed before the fix and 0 disagree after it.

## What was measured, and how

The shipped bundle is unstripped, so its own functions can be called through `dlopen` and
compared against this build on random inputs. The final differential run, over the six
behavioural recoveries:

| Function | Random cases | Mismatches |
|---|---|---|
| `median` | 8000 | 0 |
| `movingAverage` + `highPass` | 10000 | 0 |
| `resampleProfile` | 6000 | 0 |
| `sampleLooped` | 10000 | 0 |
| `extractJitter` | 3000 | 0 |
| `trackTranslation` | 5000 | 0 |

`trackTranslation`'s cases cover the guard paths (`block < 4`, `radius < 1`, frames smaller
than `block + 2*radius`), flat frames, single-block and multi-block grids, and integer shifts
in both axes. A further 28000 randomised cases were run across seven seeds with the same
result.

`src/shake/selfcheck.cpp` runs 89 checks, all of which pass; the tracker's own checks are
anchored to the listing addresses quoted above.

## Two medians in one fleet that disagree

`beatsnap::medianOfWindow` returns the **upper** middle for an even count and says so in its
own notes. `shake::median` returns the **average** of the pair. A rewrite that substituted one
for the other would change every even-length case, in opposite directions depending on which
way the substitution went.

## What is NOT claimed

* **No After Effects host has ever loaded a rebuilt bundle.** Nothing here has been through
  the plugin host; installing one needs administrator rights and is an owner action. The
  verification above is against the *shipped binary's own functions*, not against a running
  effect.
* The six behavioural recoveries are not instruction-identical to the bundle, and the notes
  say which. They agree with the shipped binary **bit for bit** on the inputs above; that is
  a much narrower claim than "identical code".
* The names of `ShakeProfile`'s fields (`horizontal`, `vertical`) and of `Vec2`'s fields
  (`x`, `y`) are **inferred**, not recovered — the binary has only the layouts.
