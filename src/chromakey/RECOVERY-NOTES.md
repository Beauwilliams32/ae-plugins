# chromakey — recovery notes

Source: `ChromaKeyAssist.plugin` (bundle id
`com.williamsmedia.aftereffects.ChromaKeyAssist`) installed at
`/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/`. The original C++
was destroyed on 2026-10-13; the binary is the only copy and is unstripped.

## State

Nine of the fifteen authored functions are recovered, 217 instructions in total.
Six of the nine are **instruction-identical** to the shipped binary.

| Function | Instrs | State |
|---|---|---|
| `clampf` | 5 | Recovered — **instruction-identical** |
| `chromaDistance` | 8 | Recovered — **instruction-identical** |
| `rgbToYCbCr` | 25 | Recovered — **instruction-identical** |
| `sensitivityToThreshold` | 15 | Recovered — **instruction-identical** |
| `hueAngle` | 15 | Recovered — **instruction-identical** |
| `angularDiff` | 18 | Recovered — **instruction-identical** |
| `percentileUnsorted` | 53 | Recovered — 52 here; the same instructions with one register allocated differently |
| `gradientMagnitude` | 47 | Recovered — 44 here; a clamp pair emitted more compactly |
| `percentile` | 31 | Recovered — 28 here; the same arithmetic, scheduled differently |
| `analyzeFrame<unsigned char>` | 752 | **Identified only** |
| `analyzeFrame<unsigned short>` | 770 | **Identified only** |
| `renderView<unsigned char>` | 347 | **Identified only** |
| `renderView<unsigned short>` | 350 | **Identified only** |
| `spillScore` | 82 | **Identified only** |
| `uniformityScore` | 203 | **Identified only** |
| `StoreReadout` | 70 | **Identified only, and must stay out of `src/`** — it takes a `PF_InData*` |

`test/chromakey.test.mjs` asserts that table against `tools/codegen-diff.mjs`, and
that every function in the last column of "Recovered" is named here.

## Why the three DIFFERs are not transcription errors

Each is the same arithmetic with a different instruction schedule, and the three
were each chased once:

- `percentile` and `percentileUnsorted` both differ at their **first instruction**:
  the shipped code materialises its `0.0` constant into a register that leaves the
  incoming argument in `s0`, and this build does the reverse (`mov.16b v1, v0`).
  That choice follows from register pressure, not from the source — writing the
  single shared early-out that the shipped `movi.2d v1, #0` plus two `cbz` branches
  to one target implies moved this build from 28 to 28 and from 48 to 52
  instructions respectively, which is as close as the expression can get it.
- `gradientMagnitude` differs by three instructions, in the coordinate-clamp pairs
  (`csinc` forms) rather than in the arithmetic: the loads, the two divides, the
  `fmadd` and the `fsqrt` all line up.

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
- **`gradientMagnitude`'s kernel is anchored top-left, not centred.** All four samples
  come from rows `ym1` and `yp1`: the horizontal difference is taken on the row
  *above* and the vertical difference on the centre column. On the self-check's grid a
  centred kernel returns 0 where this returns 5, and that is exactly what the check
  reports under a deliberately centred build.
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
