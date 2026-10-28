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

`curlField` (163 instructions) is the CURL of the field: four samples, each
normalised by its own denominator, `out.x = (s1 - s2) / +0.02f` and
`out.y = (s3 - s4) / -0.02f`.

## `apply<T>` — the renderer, and the transparency trap

`apply<unsigned char>` (408 instructions, arm64 0x06e8..0x0d48) and
`apply<unsigned short>` (412, 0x1284..0x18f4) displace one pixel at a time; the two
differ only in the width of the pixel.

The parts that are easy to get wrong:

- **`params.amount == 0.0f` takes a COPY path** — a per-row `memmove` of
  `width * 4 * sizeof(T)` bytes. The fourth argument (`time`) is **DEAD**: the
  instruction stream overwrites `s0` with `params.amount` before it is read. The
  differential varies `time` across the grid precisely so that "it is dead" is a
  measured claim.
- **`width < 2` skips the X block entirely** and forces `sx = 0` with
  `inside = true`; `height < 2` skips the Y block and forces `sy = 0` with no Y test
  at all.
- **Edge mode 3 is "outside is transparent"**, and the X and Y tests are NOT
  symmetric: the X test is `inside = sx <= xTop && sx >= 0`, while the Y test REJECTS
  `sy < 0 || sy > yTop`. An earlier transcription of this file claimed the Y test was
  symmetric with the X one — it is not, and the difference was worth **46,768
  mismatching vectors out of 463,800** in the first differential run. The fix is
  commit `6c8484a`; the sequence that settles it is the `fccmp … , pl / b.gt` pair
  in the listing.
- **Edge mode 2 wraps**: `fmodf(v, extent)`, then `if (f < 0) f += extent`, then the
  SAME upper clamp as modes 0 and 1. Edges 0 and 1 clamp to `[0, extent-1]`.
- The sample is bilinear between four neighbours; `i1`/`j1` wrap to 0 in edge mode 2
  and are clamped otherwise.
- The conversion is `fcvtas` — round to nearest, ties away from zero — and then the
  LOW byte (or halfword) is stored, so it WRAPS rather than clamps: 300 becomes 44 in
  8-bit, 70000 becomes 4464 in 16-bit.
- The stride is counted in **ELEMENTS** of `T`, not bytes.

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
