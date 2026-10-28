# noisefield — recovery notes

Source: `NoiseField.plugin` (bundle id `com.williamsmedia.aftereffects.NoiseField`)
installed at `/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/`. The
original C++ was destroyed on 2026-10-13; the binary is the only copy and is
unstripped.

## State

**All ten** authored functions are recovered, plus the gradient table. Three are
instruction-identical; the other seven are verified against the SHIPPED bundle by a
bit-for-bit differential, or — for the noise core — against independently written
oracles.

| Function | Instrs (shipped / here) | State |
|---|---|---|
| `clampf` | 5 / 5 | Recovered — **instruction-identical** |
| `clampi` | 5 / 5 | Recovered — **instruction-identical** |
| `fbmAmplitudeSum` | 15 / 15 | Recovered — **instruction-identical** |
| `kGrad3` (data, 12 × 3 int) | 144 bytes | Recovered — **verified against the bundle's bytes** |
| `noise3` | 183 / 183 | Recovered — **verified against a textbook oracle**; same count, different register schedule |
| `fbm3` | 51 / 51 | Recovered — verified against the oracle; same count, different register schedule |
| `PermTable::build` | 109 / 63 | Recovered — verified against a shuffle oracle; clang expands the 256-byte copy differently |
| `fieldAt` | 226 / 226 | Recovered — **behavioural** (same count, different register schedule); differential-clean |
| `curlField` | 163 / 163 | Recovered — **behavioural** (same count, different register schedule); differential-clean |
| `apply<unsigned char>` | 408 / 363 | Recovered — **behavioural**; differential-clean |
| `apply<unsigned short>` | 412 / 367 | Recovered — **behavioural**; differential-clean |

`node tools/codegen-diff.mjs noisefield` reports **3 identical, 7 differing, 0 not
implemented** — every `noisefield::` symbol in the bundle is transcribed, so
`src/noisefield/noisefield.h` carries the `NAMESPACE-COMPLETE` marker.

`test/noisefield.test.mjs` asserts that table against `tools/codegen-diff.mjs`.

"Behavioural" means: the semantics are pinned by the listing AND by a differential
against the shipped bundle, but the instruction STREAM is not the bundle's. For
`fieldAt` and `curlField` the instruction COUNT matches and only the register
schedule differs; for the two `apply<T>` instantiations the counts differ too
(363 against 408, and 367 against 412).

### The differential, and what it measured

`src/noisefield/differential.cpp` dlopens the INSTALLED bundle, resolves the shipped
functions by their mangled names, checks each resolved address against the offset
the bundle's own symbol table gives (the image base comes from
`_dyld_get_image_name`), and compares the WHOLE destination buffer, byte for byte,
against the recovered implementation. The shipped code is called in the
loader-mapped bundle at its own address — not a `MAP_JIT` copy, which faulted
intermittently in the chromakey lane.

Measured on this machine:

```
SCALAR_MISMATCHES 0                 (PermTable::build, noise3, fbm3, fieldAt)
APPLY_U8_VECTORS  1548000   APPLY_U8_MISMATCH_VECTORS 0   APPLY_U8_DIFFERING_BYTES 0
APPLY_U16_VECTORS 1548000   APPLY_U16_MISMATCH_VECTORS 0  APPLY_U16_DIFFERING_BYTES 0
VECTORS 3096000             MISMATCH_VECTORS 0            DIFFERING_BYTES 0
```

The grid crosses widths 1..7 against heights 1..5, every mode, edge mode and
displacement mode, octave counts 0..5, zero / negative / tiny / huge amounts and
scales, three different `time` values, several seeds and padded strides; plus a
randomised sweep of 12,000 wider vectors (up to 34×34) per pixel type. The
comparison is on the raw bytes of the destination buffer.

The harness is also shown to be able to FAIL: a COPY of the source with one constant
perturbed — `curlField`'s X divisor, `0.02f` → `0.0201f` — reports **929,167
mismatching vectors** and **44,706,647 differing bytes**. The real source is never
modified; the test stages a copy, perturbs the copy, and asserts the source is
byte-identical afterwards.

No After Effects host has ever loaded a rebuilt bundle. None of this is a claim of
host acceptance.

## Why `noise3` is verified by an oracle rather than by the differ

`noise3` and `fbm3` compile to **the same instruction counts as the bundle** — 183 and
51 — but with a different register schedule, so 152 of 183 positions differ textually.
The differ cannot distinguish "the same algorithm, allocated differently" from "a
different algorithm", and for this function that distinction is the whole question.

So the self-check contains an **independently written implementation** of Perlin's
improved noise — written in `double`, from the recovered description, not by calling
the function under test — and the implementation is compared against it:

- 4000 points agree to **7.8e-07**, which is `float`-against-`double` rounding. A wrong
  corner is off by ~1; a missing `0.9` by ~0.06.
- The noise is **exactly 0 at every integer lattice point**, over a 7×7×7 block. That
  falls out of the fade weights and would break immediately under a transposed index.
- The output **never exceeds 0.9** in magnitude over 20000 samples.

Three deliberate slips were built and each failed the checks that claim to catch it:

| Deliberate change | What failed | Worst error |
|---|---|---|
| Drop the `* 0.9f` | the oracle comparison, and every `fbm3` case | 0.0636 — the scale change exactly |
| Give one corner the wrong offset (`x1` where `fx` belongs) | the oracle comparison, the 0.9 bound, and the seed-difference check | 0.871 |
| Use `h % 16` instead of `h % 12` | everything, at once | 2.46e+08 |

The third is worth keeping in mind: `kGrad3` has exactly twelve entries, so `h % 16`
**reads past the end of the table**. The `% 12` is not a detail of the maths, it is
what keeps the lookup in bounds, and the check demonstrates that by producing a
garbage value rather than a slightly wrong one.

## `PermTable::build` — a permutation written twice, and why

Fisher-Yates descending (i = 255 down to 1), driven by xorshift32 (shifts 13, 17, 5)
whose state advances **once per iteration**, with `j = seed % (i + 1)`. A seed of 0 is
replaced by `0x9e3779b9`, the golden-ratio constant, so that 0 is a usable seed rather
than a degenerate one.

Two details from the instructions that a rewrite would lose:

- The identity permutation is **copied out of `__TEXT,__const`**, where the bytes
  `00 01 02 ... ff` are stored literally. The first transcription filled `tmp[i] = i`
  in a loop, which compiled to 24 fewer instructions; the source had a constant table.
  (Even with the literal, this build emits 63 instructions against the bundle's 109:
  clang expands the 256-byte copy differently in each. The behaviour is what is
  verified — the shuffle oracle and the differential.)
- The output is **512 bytes: the permutation written twice**. `noise3` indexes with
  `p[AA + 1]`, where `AA = p[A] + zi` can already be 510, so the second copy is what
  makes the upper z-slice's lookup legal without a modulo.

## `fbm3` — which parameter is which

The signature is `(table, x, y, z, octaves, freqMult, ampGain)`, and the last two are
easy to read the wrong way round. They are settled by the two multiplies at the bottom
of the loop: the SIXTH parameter (the first float after the count) multiplies the
**frequency** and the SEVENTH multiplies the **amplitude**. So a caller passing them
in the other order gets a field that still looks like noise and has the wrong
roughness.

The arm64 listing (`__ZN10noisefield4fbm3ERKNS_9PermTableEfffiff`, 0x1c34 in
`/tmp/ae/noisefield.asm`) says so directly. The two float arguments arrive in `v3` and
`v4` — `x0` is the table, `v0..v2` are x/y/z, `w1` is the octave count — and the
prologue keeps them in `v9` and `v8`:

```
mov.16b v8, v4      ; v8 = the SEVENTH argument
mov.16b v9, v3      ; v9 = the SIXTH argument
...
fmul s15, s9, s15   ; FREQUENCY *= the SIXTH argument
fmul s14, s8, s14   ; AMPLITUDE *= the SEVENTH argument
```

and the normalisation loop multiplies its term by `s8` as well (`fmul s1, s8, s1`), so
the amplitude sum `1 + g + g² + ...` is built from the SEVENTH argument.

`curlField` and `fieldAt` corroborate it from the other side: they inline the same loop
out of `Params`, where `ldp s12, s11, [x1, #0x14]` puts `Params::freqMult` in `s12` and
`Params::gain` in `s11`, and the loop multiplies the frequency by `s12` and the
amplitude by `s11`.

And it was checked against the SHIPPED bundle: the shipped
`fbm3(table, 1.25, 2.5, 3.75, 3, 2.0f, 0.5f)` returns **-0.0124268103**, which is
exactly the model with frequency 2.0 and amplitude 0.5, and NOT the model with the two
the other way round (**0.14601557**).

**This caught a real defect in the earlier work.** The transcription that was in the
tree before this pass had the two the other way round, and the self-check's oracle had
been written to agree with it — so the oracle verified the wrong behaviour instead of
catching it. Both are fixed now: the implementation takes the sixth argument as the
frequency multiplier (it already did, as of `4fcf6e0`) and the oracle models the
listing.

The normalisation — `1 + gain + gain² + ...`, with the 1e-6f guard — is the arithmetic
of `fbmAmplitudeSum`, which the compiler inlined into `fbm3`. The transcription calls
the function instead, which is bit-identical and keeps one definition of the
normalisation; the differ sees a call where the bundle has inline code, and that is
recorded here rather than hidden.

## A trap in verifying this by coordinate

The oracle comparison started with a sweep to 257 and reported a worst error of
1.8e-05, which looked like a transcription error. It was not: the implementation is
`float` and the oracle is `double`, and a coordinate of 257.3 leaves seven significant
digits, so the two disagree on the *fraction* by more than the algorithm's error. The
sweep now stays inside ±8 and compares with the same `float`-rounded inputs the
implementation receives, which brought the worst error to 7.8e-07.

## `fieldAt` — the Params layout, and the four layers above it

`fieldAt` is 226 instructions with a mode dispatch, and it is recovered in full. Its
PROLOGUE is unambiguous, and what it establishes is the `Params` layout the rest of the
effect is written against:

| Offset | Type | Evidence |
|---|---|---|
| `+0x00` | `int` | `ldr w8, [x1]`, then `cmp w8, #2` / `cmp w8, #3` — the mode selector, and the branches are what give the values |
| `+0x04` | `float` | `ldr s2, [x1, #0x4]` then `fmaxnm s2, s2, 0.001f` and `fdiv` — a SCALE, floored at 0.001f (0x3a83126f) so it can never divide by zero, with the sample divided by it |
| `+0x0c` | `float` | `ldr s13, [x1, #0xc]`, passed as the third coordinate to `curlField` and as the z of the noise |
| `+0x10` | `int` | `ldr w8, [x1, #0x10]`, compared against 1 |
| `+0x14`, `+0x18` | `float` ×2 | `ldp s3, s12, [x1, #0x14]`, loaded as a pair |
| `+0x1c`, `+0x20`, `+0x24` | `uint`, `int`, `int` | the seed, the displacement mode and the edge mode |

The mode dispatch:

- **modes 0 and 1** take the CURL path directly: `curlField(table, px, py, z, params)`.
  The dispatch is two compares and two branches, and the GRADIENT is tested
  **first**:

  ```
  0xf48  cmp  w8, #0x3      ; Params::mode
  0xf4c  b.eq 0xff0        ; -> the gradient mode
  0xf50  cmp  w8, #0x2
  0xf54  b.ne 0x124c       ; -> the curl
  ```

  so `mode == 3` is the gradient, `mode == 2` is the advection, and **everything
  else — 0 and 1 alike — falls through to the same curl**. There is no separate
  mode-1 branch anywhere in the listing, which is why modes 0 and 1 are the same
  function and the self-check asserts they agree.
- **mode 2** is the ADVECTION mode: it runs the curl field and advects the sample
  point **FOUR times** with a step of **0.35f** (0x3eb33333), accumulating the
  displacement and then subtracting the starting point. The listing contains exactly
  four calls to `curlField` (at 0xf6c, 0xf98, 0xfb8, 0xfd8) — an earlier reading of
  this file said five, and it was wrong.
- **mode 3** is the GRADIENT mode: four fBm samples at ±0.01f around the point, each
  normalised on its own, with the two differences divided by **+0.02f** for BOTH
  components (0x3ca3d70a). The curl divides its Y component by **-0.02f** instead
  (0x3c20 in `__TEXT,__const` holds `{+0.02f, -0.02f}`), because a curl is a
  perpendicular derivative and a gradient is not.

  The four gradient samples, each an fBm block inlined exactly as in `curlField`
  and each calling `noise3` once per octave:

  | Sample | Point | Where the offset is built | Where `noise3` is called |
  |---|---|---|---|
  | a | `(px + 0.01f, py, z)` | `0x1000  fadd s8, s9, s0` | `0x1038` |
  | b | `(px - 0.01f, py, z)` | `0x1088  fadd s0, s1, s0` | `0x10bc` |
  | c | `(px, py + 0.01f, z)` | `0x1110  fadd s0, s14, s0` | `0x1140` |
  | d | `(px, py - 0.01f, z)` | `0x1184  fadd s0, s2, s0` (saved at `0x1188`) | `0x11b4` |

  The divisor is built **once** and used for both components:

  ```
  0x1224  mov  w8, #0xd70a
  0x1228  movk w8, #0x3ca3, lsl #16   ; 0x3ca3d70a = +0.02f
  0x122c  fmov s5, w8
  0x1230  fdiv s0, s0, s5              ; the X difference / +0.02f
  0x1244  fdiv s1, s1, s5              ; the Y difference / +0.02f — the SAME s5
  ```

  `s5` is not reloaded between the two divisions, which is the instruction-level
  proof that the gradient has no -0.02f in it. The curl's own divisor is a
  *pair* loaded from `__TEXT,__const`, which is where the asymmetry comes from.

`curlField` (163 instructions) is the CURL of the field: four samples, each
normalised by its own denominator, `out.x = (s1 - s2) / +0.02f` and
`out.y = (s3 - s4) / -0.02f`. It is arm64 **0x1d00..0x1f88** in
`/tmp/ae/noisefield.asm`, and every step of it is readable off the listing:

```
0x1d24  mov.16b v8, v2      ; z
0x1d28  mov.16b v9, v1      ; y
0x1d2c  mov.16b v10, v0     ; x
0x1d34  mov  w8, #0xd70a
0x1d38  movk w8, #0x3c23, lsl #16   ; 0x3c23d70a = +0.01f, the epsilon
0x1d40  fadd s13, s1, s0    ; y + eps
0x1d44  ldr  w8, [x1, #0x10]        ; Params::octaves
0x1d48  ldp  s12, s11, [x1, #0x14]  ; Params::freqMult, Params::gain
0x1d4c  cmp  w8, #0x1
0x1d50  csinc w20, w8, wzr, gt      ; count = octaves > 1 ? octaves : 1
```

The four samples, in the order the listing takes them. Each is an fBm block
inlined: `fmul` the three coordinates by the frequency, `bl noise3`, then
`fmadd s1, s0, s14, s1` to accumulate — a **fused** multiply-add — then
`fmul s15, s12, s15` (frequency *= freqMult) and `fmul s14, s11, s14`
(amplitude *= gain). The amplitude sum `1 + gain + gain² + ...` is then rebuilt
inline in a second loop and the pair is divided.

| Sample | Point | Where the coordinate is built | Where `noise3` is called |
|---|---|---|---|
| s1 | `(x, y + eps, z)` | `0x1d40` (`fadd s13, s1, s0`) | `0x1d78` |
| s2 | `(x, y - eps, z)` | `0x1dc4` (`fadd s13, s9, s0`, eps = 0xbc23d70a = -0.01f) | `0x1dec` |
| s3 | `(x + eps, y, z)` | `0x1e38` (`fadd s13, s10, s0`) | `0x1e60` |
| s4 | `(x - eps, y, z)` | `0x1eac` (`fadd s10, s10, s0`, eps = -0.01f) | `0x1ed4` |

The epsilon is added to the coordinate **before** the frequency multiplies it,
which is why it is `±0.01f` in *world* units rather than in noise units.

Each sample is divided by **its own** denominator, and the guard is the same one
`fbmAmplitudeSum` uses — `(den > 1e-6f) ? den : 1.0f`, with 1e-6f built as
0x358637bd at `0x1f14`/`0x1f18` and applied as the vectorised
`fcmgt.2s` / `bsl.8b` pair at `0x1f24`..`0x1f30`:

```
0x1f08  ldp  q4, q3, [sp, #0x10]    ; the denominators
0x1f24  fcmgt.2s v0, v3, v1         ; (den > 1e-6f)
0x1f30  bsl.8b  v0, v3, v2          ; v2 = 1.0, so: den or 1.0
0x1f34  ldr  q3, [sp, #0x60]        ; the sums
0x1f40  fdiv.2s v0, v3, v0          ; {s1/den1, s3/den3}
0x1f50  fdiv.2s v1, v2, v1          ; {s2/den2, s4/den4}
0x1f54  fsub.2s v0, v0, v1          ; the two differences
0x1f5c  ldr  d1, [x8, #0xc20]       ; __TEXT,__const 0x3c20 = {+0.02f, -0.02f}
0x1f60  fdiv.2s v0, v0, v1          ; divided by that PAIR
```

So the X component divides by **+0.02f** and the Y component by **-0.02f**. That
asymmetry IS the curl: the Y derivative is taken with the opposite sign, which
is what turns a gradient into a perpendicular one. `fieldAt`'s gradient mode does
not have it — it divides both components by +0.02f (0x3ca3d70a) — and the
self-check asserts the two readings differ at a point where they can.

## `apply<T>` — the renderer, and the transparency trap

`apply<unsigned char>` (408 instructions, arm64 0x06e8..0x0d48) and
`apply<unsigned short>` (412, 0x1284..0x18f4) displace one pixel at a time; the two
differ only in the width of the pixel.

### The prologue

```
0x728  ldr  w8, [x0, #0x8]     ; src.width
0x72c  cmp  w8, #0x1
0x730  b.lt 0xcfc               ; -> return, the destination is NOT touched
0x738  ldr  w9, [x0, #0xc]     ; src.height
0x73c  cmp  w9, #0x1
0x740  b.lt 0xcfc               ; -> return
0x74c  ldr  s0, [x2, #0x8]     ; Params::amount  -- overwrites s0
0x750  fcmp s0, #0.0
0x754  b.eq 0xbe4               ; -> the copy path
```

The `ldr s0, [x2, #8]` at **0x74c** is why the fourth argument (`time`) is dead:
`time` arrives in `s0`, and the very first thing the function does with that
register is overwrite it with `params.amount`. Nothing ever reads the argument
again. The copy path itself is the `memmove` expansion at **0xbe4..0xcf8**, whose
byte count is `lsl x10, x8, #2` at **0xbe8** — the width times four elements.

The parts that are easy to get wrong:

- **`params.amount == 0.0f` takes a COPY path** — a per-row `memmove` of
  `width * 4 * sizeof(T)` bytes. The fourth argument (`time`) is **DEAD**: the
  instruction stream overwrites `s0` with `params.amount` before it is read. The
  differential varies `time` across the grid precisely so that "it is dead" is a
  measured claim.
- **`width < 2` skips the X block entirely** and forces `sx = 0` with
  `inside = true`; `height < 2` skips the Y block and forces `sy = 0` with no Y test
  at all.
- **Edge mode 3 is "outside is transparent"**, and the X and Y tests are the SAME
  rule, written with different instructions. Both accept a coordinate of exactly
  `extent-1` and both reject a negative one; the listing simply writes the X test
  and the Y test differently:

  ```
  X:  0x9b4  fcmp    s10, s14          ; s10 = sx, s14 = xTop
      0x9b8  fccmp   s10, s12, #0x8, le ; if sx <= xTop compare sx with 0.0,
                                           ; otherwise force NZCV = 0x8
      0x9bc  cset    w28, pl            ; inside = (sx <= xTop) && (sx >= 0)
  Y:  0xa28  fcmp    s9, #0.0
      0xa2c  fccmp   s9, s13, #0x0, pl  ; if sy >= 0 compare sy with yTop,
                                          ; otherwise force NZCV = 0x0
      0xa30  b.gt    0x908              ; sy > yTop, or sy < 0 (N == V == 0), is outside
  ```

  `pl` is N == 0, which holds for zero as well as for a positive value, so the X
  test accepts `sx == 0`; the Y test's fallback `#0x0` leaves Z clear and N clear,
  so `b.gt` (Z == 0 && N == V) fires for a negative `sy`. Both land on the same
  condition: **inside iff `0 <= v <= extent-1`**.

  An earlier transcription of this file had the Y test as a **NO-OP** — it never
  rejected anything — and that was worth **46,768 mismatching vectors out of
  463,800** in the first differential run. The fix is commit `6c8484a`. (An
  intermediate revision of THIS file described the two tests as asymmetric; they
  are not, and the self-check now asserts the rule directly, against both axes.)
- **Edge mode 2 wraps**: `fmodf(v, extent)`, then `if (f < 0) f += extent`, then the
  SAME upper clamp as modes 0 and 1. Edges 0 and 1 clamp to `[0, extent-1]`.
- The sample is bilinear between four neighbours; `i1`/`j1` wrap to 0 in edge mode 2
  and are clamped otherwise.
- The conversion is `fcvtas` — round to nearest, ties away from zero — and then the
  LOW byte (or halfword) is stored (`fcvtas x12, s2` / `strb w12, [x24]` at
  0xb18/0xb1c for 8-bit, `fcvtas` / `strh` at 0x16c4/0x16c8 for 16-bit), so the
  STORE WRAPS rather than clamps: the instruction sequence would turn 300 into 44
  in 8-bit and 70000 into 4464 in 16-bit. That wrap is **not reachable through
  `apply`**, because the sample is a convex combination of the source values and
  so never leaves the source's range; it is a property of the conversion, not an
  observable behaviour of the renderer, and no self-check claims to observe it.
- The stride is counted in **ELEMENTS** of `T`, not bytes.

### The per-pixel loop

The inner counter is the COLUMN and the outer one is the ROW, and the listing is
explicit about it:

```
0x920  ucvtf  s10, w23         ; the column, as a float
0x928  mov.16b v0, s10         ; v0 = x
0x92c  mov.16b v1, s8          ; v1 = y
0x934  bl     fieldAt
0x938  ldr    s2, [x21, #0x8]  ; Params::amount
0x93c  fmul   s0, s0, s2       ; dx
0x940  fmul   s1, s1, s2       ; dy
0x944  ldp    w8, w26, [x21, #0x20]   ; Params::dispMode, Params::edgeMode
0x948  cmp    w8, #0x2
0x950  fcsel  s13, v9, s1, eq  ; dispMode 2 -> dy = 0
0x954  cmp    w8, #0x3
0x958  fcsel  s0, v9, s0, eq   ; dispMode 3 -> dx = 0
0x95c  cmp    w25, #0x2        ; the width
0x960  b.ge   0x970
0x964  mov    w28, #0x1        ; inside = true
0x968  movi.2d v10, #0        ; sx = 0
0x96c  b      0x9d8            ; -> the Y block only
0x970  fadd   s10, s0, s10     ; sx = dx + x
```

So `fieldAt` is asked for **(column, row)**, not (row, column), and the
displacement modes are exactly two `fcsel`s: mode 2 zeroes the Y displacement and
mode 3 zeroes the X one. The self-check renders a non-symmetric image and asserts
that the transposed reading does NOT reproduce it.

Edge mode 2, the wrap, is `fmodf` and two selects:

```
0xa0c  bl     _fmodf
0xa10  fadd   s1, s0, s11      ; f + extent
0xa14  fcmp   s0, #0.0
0xa18  fcsel  s0, s1, s0, mi   ; if (f < 0) f += extent
0xa1c  fcmp   s0, s13          ; the top edge
0xa20  fcsel  s9, s13, s0, gt  ; then the SAME upper clamp as modes 0 and 1
```

The neighbour indices and the bilinear sample:

```
0xa4c  fcvtms w8, s10          ; i0 = (int)floor(sx)
0xa58  csel  w10, w8, w9, lt   ; clamped against width-1
0xa60  csel  w8, wzr, w10, lt  ; and against 0
0xa64  fcvtms w10, s9          ; j0 = (int)floor(sy)
0xa7c  add   w12, w8, #0x1     ; i0 + 1
0xa84  cmp   w26, #0x2
0xa88  csel  w9, wzr, w9, eq   ; edge mode 2 -> the neighbour wraps to 0
0xa90  cmp   w12, w25
0xa94  csinc w9, w9, w8, ge    ; otherwise i1 = i0+1 or width-1
0xaa0  scvtf s0, w8            ; fxx = sx - i0
0xaa4  scvtf s2, w10           ; fyy = sy - j0
...
0xae0  ldr   b2, [x10]         ; p00
0xaf4  fmadd s2, s1, s3, s2    ; top = fma(fxx, p10 - p00, p00)
0xb0c  fmadd s3, s1, s4, s3    ; bot = fma(fxx, p11 - p01, p01)
0xb10  fsub  s3, s3, s2
0xb14  fmadd s2, s0, s3, s2    ; out = fma(fyy, bot - top, top)
0xb18  fcvtas x12, s2
0xb1c  sturb w12, [x24, #-0x1]
```

The row pointer is built as `data + stride * row` with `madd x11, x14, x10, x13`
at **0xabc** — `x14` is the stride — which is why the stride counts ELEMENTS of
`T`, not bytes.

The transparent pixel is written as a single zero store of the pixel's width:
`stur wzr, [x24, #-0x1]` at **0x908** for 8-bit (four zero bytes) and
`str xzr, [x25]` at **0x14ac** for 16-bit (eight zero bytes). The 16-bit
conversion is `fcvtas x12, s2` / `strh w12, [x25]` at **0x16c4**/**0x16c8**.

## What is not proven

- **No After Effects host has ever loaded a rebuilt bundle.** Everything above is
  measured against the shipped binary and against oracles, never against the host.
- The instruction streams of the seven non-identical functions are NOT the bundle's.
  For `fieldAt`, `curlField`, `noise3` and `fbm3` the counts match and only the
  register schedule differs; for `PermTable::build` and the two `apply<T>`s the
  counts differ too. The SEMANTICS are what is verified.
- `curlField` is an internal-linkage symbol in the bundle (`_GLOBAL__N_1`, local, `t`
  in `nm`), so it cannot be reached by `dlsym`. It is verified through `fieldAt` and
  through the renderer, not by calling it directly.
