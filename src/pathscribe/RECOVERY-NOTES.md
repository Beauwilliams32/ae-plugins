# pathscribe — recovery notes

Source: `PathScribe.plugin` (bundle id `com.williamsmedia.aftereffects.PathScribe`)
installed at `/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/`. The
original C++ was destroyed on 2026-10-13; the binary is the only copy and it is
unstripped.

## State

Thirteen of the eighteen symbols are recovered. Eight of them are
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
| `barLevels` | 112 | Identified only |
| `smoothedLevelsAtFrame` | 179 | Identified only |
| `waveformColumns` | 80 | Identified only |
| `rasterizeBars` | 173 | Identified only |
| `rasterizeRibbon` | 147 | Identified only |

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

## What is next

`barLevels` (112) and `waveformColumns` (80) are the analysis-to-drawing step, then
`smoothedLevelsAtFrame` (179) and the two rasterisers (173 + 147) that turn levels into
the ribbon and bar geometry this effect is named for.
