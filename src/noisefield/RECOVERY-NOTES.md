# noisefield — recovery notes

Source: `NoiseField.plugin` (bundle id `com.williamsmedia.aftereffects.NoiseField`)
installed at `/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/`. The
original C++ was destroyed on 2026-10-13; the binary is the only copy and is
unstripped.

## State

Three of the ten authored functions are recovered, plus the gradient table. All three
functions are **instruction-identical** to the shipped binary.

| Function | Instrs | State |
|---|---|---|
| `clampf` | 5 | Recovered — **instruction-identical** |
| `clampi` | 5 | Recovered — **instruction-identical** |
| `fbmAmplitudeSum` | 15 | Recovered — **instruction-identical** |
| `kGrad3` (data, 12 × 3 int) | 144 bytes | Recovered — **verified against the bundle's bytes** |
| `PermTable::build` | 109 | Identified only |
| `noise3` | 183 | Identified only |
| `fbm3` | 51 | Identified only |
| `fieldAt` | 226 | Identified only |
| `curlField` | 163 | Identified only |
| `apply<unsigned char>` | 408 | Identified only |
| `apply<unsigned short>` | 412 | Identified only |

`test/noisefield.test.mjs` asserts this table against `tools/codegen-diff.mjs`.

## The gradient table is recovered before the noise, deliberately

`kGrad3` is the twelve edge gradients of Perlin's improved noise, and a noise built
from a different gradient set is a *different effect with the same function names*.
So the table was recovered and verified first, and `noise3` — which is next — has to
agree with it.

It was read from the arm64 slice's `__TEXT,__const` at `0x3c28` as 36 32-bit words,
each holding one small signed integer:

```
(1,1,0) (-1,1,0) (1,-1,0) (-1,-1,0) (1,0,1) (-1,0,1)
(1,0,-1) (-1,0,-1) (0,1,1) (0,-1,1) (0,1,-1) (0,-1,-1)
```

That matches the canonical table both as a set and in order, every entry has squared
length 2, and all twelve are distinct. The test reads the twelve triples out of the
binary and compares them to the source, so the claim is against the artefact rather
than against a transcription.

Two things about getting there are worth recording, because both looked like findings
and were mistakes:

- Reading the section by *file offset* produced 40 zero floats. The binary is
  universal, and `otool -l` without an architecture prints both slices' sections, so
  the section address and the offset came from different slices. `otool -s` prints the
  section's words with their addresses and needs no offset arithmetic at all.
- The first decode was as floats, which produced `nan` and denormals — because the
  values are **integers**, not floats. The word pattern `00000001 00000001 00000000
  ffffffff` is `(1, 1, 0)` then `(-1, 1, 0)` read as signed 32-bit, not as an
  encoding of floats.

**One deliberate deviation from the original.** In the bundle `kGrad3` is a *local*
symbol (`nm -S` reports `s`), inside an anonymous namespace, so nothing outside its
translation unit can reference it. It is declared in `noisefield.h` instead, so the
self-check can assert its contents; the only way to verify a table is to look at it.
That means a caller linking against this header sees a symbol the shipped bundle does
not export, and it is recorded here rather than glossed over.

## `fbmAmplitudeSum` — the guard is reachable, the clamp is not

The function is `1 + gain + gain^2 + ...`, then `sum > 1e-6f ? sum : 1.0f`, with the
octave count clamped to at least one.

The 1e-6f is a divide-by-zero guard, and it is **reachable — but only through a
negative gain**: the first term is always 1, so the only way the sum reaches zero is
for the later terms to cancel it, which `gain = -1` does exactly. The self-check
pins that (`fbmAmplitudeSum(2, -1.0f) == 1.0f`) alongside `gain = -0.5` giving 0.5, so
the guard cannot be mistaken for "always returns 1".

The clamped count, by contrast, is **not observable**: a count of 0 or less skips the
loop and leaves the sum at 0, which the guard then turns into 1.0 — the same answer
one octave would give. So the `csinc` is redundant given the guard. It is transcribed
because it is in the binary, and recorded as unobservable because claiming a
behaviour for it would be a claim the binary cannot support.

## Two instruction-level deductions that made functions identical

Both came from reading the schedule rather than the maths, the same way
`rgbToYCbCr` and `percentileUnsorted` were settled in `chromakey`:

- The shipped code builds `1.0f` **before** `0.0f` (`fmov s2, #1.0` then
  `movi.2d v1, #0`), which is declaration order, not semantics.
- It accumulates as `fadd s1, s2, s1` — that is `amplitude + sum`, not `sum +
  amplitude`. Writing the addition the other way round left 3 of 15 positions
  differing.

## What is next

`noise3` (183 instructions) is the core and the next target, then `fbm3` (51, which
calls it), then `PermTable::build` (109). `fieldAt`, `curlField` and the two
`apply<T>` loops follow. The recovered `kGrad3` is the anchor `noise3` will be
verified against, and `fbmAmplitudeSum` is the normalisation `fbm3` divides by.
