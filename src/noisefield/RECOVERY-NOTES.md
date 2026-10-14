# noisefield — recovery notes

Source: `NoiseField.plugin` (bundle id `com.williamsmedia.aftereffects.NoiseField`)
installed at `/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/`. The
original C++ was destroyed on 2026-10-13; the binary is the only copy and is
unstripped.

## State

Six of the ten authored functions are recovered, plus the gradient table. Three are
instruction-identical; the noise core is verified against an independent oracle.

| Function | Instrs | State |
|---|---|---|
| `clampf` | 5 | Recovered — **instruction-identical** |
| `clampi` | 5 | Recovered — **instruction-identical** |
| `fbmAmplitudeSum` | 15 | Recovered — **instruction-identical** |
| `kGrad3` (data, 12 × 3 int) | 144 bytes | Recovered — **verified against the bundle's bytes** |
| `noise3` | 183 | Recovered — **verified against a textbook oracle** (183 here, 183 shipped) |
| `fbm3` | 51 | Recovered — verified against the oracle (51 here, 51 shipped) |
| `PermTable::build` | 109 | Recovered — verified against a shuffle oracle (63 here) |
| `fieldAt` | 226 | Identified only |
| `curlField` | 163 | Identified only |
| `apply<unsigned char>` | 408 | Identified only |
| `apply<unsigned short>` | 412 | Identified only |

`test/noisefield.test.mjs` asserts that table against `tools/codegen-diff.mjs`.

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
  verified.)
- The output is **512 bytes: the permutation written twice**. `noise3` indexes with
  `p[AA + 1]`, where `AA = p[A] + zi` can already be 510, so the second copy is what
  makes the upper z-slice's lookup legal without a modulo.

## `fbm3` — which parameter is which

The signature is `(table, x, y, z, octaves, gain, lacunarity)`, and the last two are
easy to read the wrong way round. They are settled by the two multiplies at the bottom
of the loop: the SIXTH parameter (the first float after the count) multiplies the
**frequency** and the SEVENTH multiplies the **amplitude**. So a caller passing them
in the other order gets a field that still looks like noise and has the wrong
roughness.

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

## What is next

`fieldAt` (226) and `curlField` (163) are the effect's own layers over this core, then
the two `apply<T>` per-pixel loops (408/412). `fieldAt` is the one that turns the
normalised noise into the effect's output range, so it is the next target.
