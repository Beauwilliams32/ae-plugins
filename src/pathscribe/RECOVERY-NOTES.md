# pathscribe — recovery notes

Source: `PathScribe.plugin` (bundle id `com.williamsmedia.aftereffects.PathScribe`)
installed at `/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/`. The
original C++ was destroyed on 2026-10-13; the binary is the only copy and it is
unstripped.

## State

Seventeen of the eighteen symbols are recovered. Eight of them are
**instruction-identical** to this bundle.

| Function | Instrs | State |
|---|---|---|
| `clampf` | 5 | Recovered — **identical** (shared) |
| `isPowerOfTwo` | 6 | Recovered — **identical** (shared) |
| `linearToDb` | 12 | Recovered — **identical** (shared) |
| `dbToLinear` | 3 | Recovered — **identical** (shared) |
| `hannCoherentGain` | 2 | Recovered — **identical** (shared) |
| `Spectrogram::~Spectrogram` | 29 | Recovered — **identical** (shared) |
| `binEdgeHz` | 19 | Recovered — **identical** (own) |
| `spanCoverage` | 16 | Recovered — **identical** (own) |
| `hannWindow` | 112 | Recovered — 116 here (shared) |
| `fftRadix2` | 100 | Recovered — 4 of 100 (shared) |
| `downmixToMono` | 116 | Recovered — 2 of 116 (shared) |
| `analyse` | 341 | Recovered — 328 here (shared) |
| `smoothStep` | 25 | Recovered — 25 here, allocation differs (own) |
| `barLevels` | 112 | Recovered — 120 here (own) |
| `waveformColumns` | 80 | Recovered — 83 here (own) |
| `smoothedLevelsAtFrame` | 179 | Recovered — 177 here (own) |
| `rasterizeRibbon` | 147 | Recovered — 151 here (own) |
| `rasterizeBars` | 173 | Identified only |

## Ten of the eighteen symbols are SpectraKeys', and that is the finding

`tools/dsp-duplication.mjs` compares every copy of the fleet's shared DSP across every
installed bundle. It reports PathScribe as a carrier of nine of them — `clampf`,
`isPowerOfTwo`, `linearToDb`, `dbToLinear`, `hannWindow`, `hannCoherentGain`,
`fftRadix2`, `downmixToMono` and `analyse` — plus a `Spectrogram` destructor that
matches SpectraKeys' instruction for instruction. **One recovery covers four effects**:
SpectraKeys, PathScribe, BeatSnap and BeatGridRuler.

So the analysis layer here is a copy, and a copy is only legitimate if the copying is
exact. `MATCH`/`DIFFER` cannot show that on its own, because four of the ten do *not*
match: `hannWindow` is 116 instructions here against 112 shipped, `fftRadix2` differs in
4 of 100 positions, `downmixToMono` in 2 of 116, and `analyse` is 328 against 341. What
does show it is **divergence parity**, in `test/pathscribe.test.mjs`: the same source,
compared against two bundles built from it, must diverge in exactly the same way against
both. All ten do, including all four that differ.

## PathScribe's own layer

### `spanCoverage` — and a `min` read as a `max`

The overlap of the frame `[frameIndex, frameIndex + 1)` with the interval between the
last two arguments, as a fraction of one frame, clamped to `[0, 1]`. The last two
arguments are two EDGES and the body takes their min and max, so their order does not
matter.

The first transcription read that final select backwards and produced `max` where the
bundle has `min`. It compiled to the same 16 instructions and was wrong by 0.25 on
every partially-covered frame — and the differ caught it at **1 of 16 positions**.
That is worth recording because a self-consistent self-check would have agreed with it:
the expectation would have been written from the same misreading. The differ is the
first gate here, not the second.

The parameter names were wrong for the same reason. Reading the second argument as a
*length* and the third as a *start* is what suggested `max`; they are two endpoints, and
the header now says so.

### `smoothStep` — not a smoothstep

Despite the name there is no polynomial and no clamping of a progress value. It is an
**exponential approach**:

```
span = (to >= from) ? spanRising : spanFalling
if (!(x > 0) || !(span > 1e-5f)) return to
return from + (to - from) * (1 - exp(-x / span))
```

Two things a rewrite would get wrong:

- The direction of the move picks the span, which is an attack/release pair rather than
  one time constant.
- **The guard returns `to`, the destination**, not `from`. Read as "x <= 0 means we are
  still at `from`" it is wrong on every call with a zero or negative time. The guard
  constant is `1e-5f` (`0x3727c5ac`), not `1e-7f`, and a span at or below it takes the
  same path.

It is the one function here that does not match the bundle's schedule — 25 instructions
either way, but 22 of 25 positions differ, because the shipped code spills two registers
across the `expf` call and this build does not. Behaviour is verified in the self-check
against the closed form.

### `binEdgeHz` — geometric band edges

```
if (count < 1) return minHz          // the branch target is a bare `ret`, so the
                                     // untouched third argument is what comes back
lo = max(minHz, 1.0)                 // a 1 Hz floor, so the ratio never starts at zero
hi = max(maxHz, lo)
return lo * pow(hi / lo, (double)index / (double)count)
```

Equal *ratios* apart, which is what makes a spectrum analysis look even across the
octaves, and the exponent divides by `count` rather than `count - 1`, so edge `count`
lands exactly on `hi` and edge 0 exactly on `lo`. Two source details were needed to make
it instruction-identical: the exponent is formed *before* the ratio (the shipped code
does `scvtf`, `ucvtf`, `fdiv`, `fdiv`), and the count guard returns `minHz` unchanged
rather than zero.

### `smoothedLevelsAtFrame` — the time constant changes with the data

The band's own value is approached exponentially, and **which** constant does the
approaching is decided by the comparison, not fixed:

```
base = (src >= dst) ? b : c
out  = (a <= 0 || base <= 1e-5) ? src : dst + (1 - expf(-a / base)) * (src - dst)
```

Three things a rewrite gets wrong:

- **The base is selected by `src >= dst`.** When the incoming sample is at least the
  value carried so far, the base is `b`; when it is smaller, the base is `c`. The bundle
  does this with a single `fcsel` (scalar) or `blendvps` (vector) on that mask, so the
  time constant changes direction with the data.
- **The 1e-5 floor is on the BASE, not on the value.** `base <= 1e-5` (the constant is
  0x3727c5ac) takes the raw sample rather than dividing by a base that is nearly zero,
  and `a <= 0` takes the same branch. The bundle builds both into one mask: the
  loop-invariant `a <= 0` test is made once with `fcmp`/`cset` and broadcast as a
  halfword mask, and OR-ed into the per-element comparison.
- **`last` is clamped to band count - 1 and the loop is INCLUSIVE of it**, so a frame
  index past the end walks every band and stops there. The bundle computes the count
  with the divide-by-three magic and subtracts one — the `- 1` is inside the magic
  multiply (`madd` with -1), which is how the instruction stream shows it.

The output is sized from the **first** band and zero-filled, and each band is walked
only as far as the shorter of the band and the output.

It is recovered at 177 instructions against the bundle's 179. The difference is not
arithmetic: the two streams compute the same thing, and the vectorised path is
structurally the same — the same `fdiv`, the same four `expf` calls, the same
`fsub`/`fmla`, the same two `bsl` selects. What differs is how the "skip" mask is
materialised: the bundle builds it scalar-then-halfword (`fcmp`/`cset`, `dup.4h`,
`ushll`/`shl`/`cmlt`) and this build builds it two-wide (`fcmge.2s`, `dup.2s`,
`sshll`). The behaviour is pinned by the self-check instead.

### `rasterizeRibbon` — the drawing layer, and a struct no symbol describes

PathScribe's drawing layer takes a `pathscribe::VizConfig` by const reference. **No
symbol in the bundle mentions that type** - it is never in a mangled name - so this is the
first thing in this namespace whose layout cannot simply be read off the symbol table. It
is recovered from the two rasterisers' loads, and its MEANING from the effect's own
parameter names in `specs/PathScribe.json`:

| Offset | Type | Reads | Meaning |
|---|---|---|---|
| 0x00 | int | neither | not read by either rasteriser |
| 0x04 | int | both | column count |
| 0x08 | int | both | row count |
| 0x0c | float | both | baseline position, as a fraction of the height |
| 0x10 | float | both | amplitude - the vertical scale AND the profile's lower bound |
| 0x14 | float | ribbon | thickness in pixels |
| 0x18 | float | bars only | read by `rasterizeBars` alone |

The layout is measured. The names are inference, and the header says so.

What the ribbon draws, per column `i` of `levels`:

```
origin = (baseline < 0 ? 1 : 1 - min(baseline, 1)) * rows
scale  = min(amplitude, 4) * rows * 0.5
half   = max(thickness, 0.5) * 0.5
ya = origin - clamp(levels[|min(n-1, i)|],   -1, 1) * scale
yb = origin - clamp(levels[|min(n-1, i+1)|], -1, 1) * scale
yc = origin - clamp(levels[|min(n-1, i-1)|], -1, 1) * scale
len   = sqrt(((yb - yc) * 0.5)^2 + 1)
reach = half * len + 1
rowLo = |trunc(ya - reach)|;  rowHi = rint(ya + reach), clamped to rows-1
for row j in rowLo .. rowHi:
    v = (half + 0.5) - |j + 0.5 - ya| / len          ; the profile falls off with distance
    v = v > 1 ? 1 : (v < 0 ? amplitude : v)
    if (v <= 0) skip
    outBars[columns * j + i]   = max(outBars[...],   v)
    outRibbon[columns * j + i] = max(outRibbon[...], |level|)
```

Three things a rewrite gets wrong, and all three are in the instructions:

- **The "previous" sample index is `|min(n-1, i-1)|`, not a clamp.** At `i = 0` that is
  `|-1| = 1`, so the first column's slope is taken from `levels[1]` and not from
  `levels[0]`. With a single level it reads `levels[1]` on a one-element vector: an
  out-of-bounds read in the shipped binary, preserved here rather than tidied away.
- **`rowLo` is the ABSOLUTE value of a truncated coordinate.** A column whose band would
  start above row 0 has its band start at row `|row|` instead, which puts it at the
  BOTTOM of the strip.
- **`amplitude` does double duty**: it is the vertical scale AND the profile's lower
  bound, so a negative amplitude flips the mapping and holds the drawn band at the
  amplitude rather than at zero.

The bundle clamps a sample with two SEPARATE tests on the ORIGINAL value - `if (x > 1)
y = 1; if (x < -1) y = -1;` - not a clamp of a clamp, so a value above 1 is not
re-tested against -1 afterwards.

**Measurement.** 151 instructions here against the bundle's 147, and DIFFER rather than
MATCH. The two streams are the same function; what differs is the compiler's choice of
idiom in two places: the bundle takes the absolute value of an int with `bic` where this
build uses `cmp`/`cneg`, and it rounds to nearest with `fcvtps` where this build uses
`frintx`/`fcvtzs`. Both are the same value. The fleet was built with a different
compiler than the one on this machine, which is the same reason `analyse` and
`hannWindow` differ.

The behaviour is pinned by the self-check: 21 new assertions, with literal anchors for
the pixels that are drawn (1.0, 0.2977531, 0.7659177, 0.4393398) and for the magnitudes
the ribbon carries.


## What is next

`rasterizeBars` (173) is the last one: the same `VizConfig`, the same grid, the
other profile. Its `cfg[0x18]` field - the one the ribbon does not read - is the only
part of `VizConfig` this recovery does not yet explain.
