# beatgrid — recovery notes

Source: `BeatGridRuler.plugin` (bundle id `com.williamsmedia.aftereffects.BeatGridRuler`)
installed at `/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/`. The
original C++ was destroyed on 2026-10-13; the binary is the only copy and it is
unstripped.

## State

Nine of the twelve `beatgrid::` symbols are recovered; three are instruction-identical.

| Function | Instrs | State |
|---|---|---|
| `clampf` | 5 | Recovered — **identical** (shared) |
| `isPowerOfTwo` | 6 | Recovered — **identical** (shared) |
| `downmixToMono` | 116 | Recovered — 116 here, 2 of 116 positions (shared) |
| `subdivisionFactor` | 8 | Recovered — 8 here; the only difference is that this build prints the switch table's address symbolically |
| `phaseInLayerTime` | 24 | Recovered — 24 here, one condition-code alias |
| `phaseSawtooth` | 31 | Recovered — 31 here |
| `OnsetEnvelope::frameTimeSeconds` | 13 | Recovered — 14 here (see below) |
| `computeOnsetEnvelope` | 436 | Identified only |
| `estimateTempoBpm` | 578 | Identified only |
| `estimatePhaseSeconds` | 138 | Recovered — **[partial]**, 135 here |
| `generateBeatGrid` | 94 | Recovered — **[partial]**, 94 here |
| `analyzeBeatGrid` | 57 | Identified only |

`specs/BeatGridRuler.json` lists fourteen domain entries, of which two are `std::vector`
member instantiations taking beatgrid types; the differ counts those under `std`, which is
why the namespace is twelve symbols and not fourteen.

`(anonymous namespace)::WriteMarkerGrid` (811 instructions) is **excluded on purpose**:
it takes `AEGP_SuiteHandler&` and `_AEGPp_Stream**`, which are Adobe SDK types, so it
belongs with the other host integrations and not in `src/`.

## The subdivision table, read rather than guessed

`subdivisionFactor` is eight instructions: a range check on `value - 2` computed
UNSIGNED, then a load from a four-entry double table, with 1.0 as the default.

```
value 2 -> 0.5     value 3 -> 0.25     value 4 -> 0.125     value 5 -> 4.0
anything else, including 0, 1 and negatives -> 1.0
```

The table sits at `0x77e0` in `__TEXT,__const` and its four doubles are
`0x3fe0000000000000`, `0x3fd0000000000000`, `0x3fc0000000000000` and
`0x4010000000000000`. **The fourth is the one worth having read**: the first three halve
in turn, so a guess would make the fourth a sixteenth — it is four beats.

Getting there took a correction. The `adrp` prints its immediate as `5` and its resolved
target as `0x7000`, and the first reading added `0x7e0` to `0x5000` and decoded
instruction words as doubles. The address to use is the one the annotation gives.

## Two functions that return different things under similar names

`phaseSawtooth` and `phaseInLayerTime` are both "where are we in the beat", and they
disagree in two ways that a rewrite would flatten:

- `phaseSawtooth` **divides by its period and returns a `float`** in [0, 1);
  `phaseInLayerTime` **does not divide and returns a `double`** measured in seconds.
- `phaseSawtooth` scales the beat by `subdivisionFactor`; `phaseInLayerTime` takes no
  subdivision at all.

The return types are asserted at compile time in the self-check, because mangling carries
parameter types and never the return type: `fcvt s0, d0` is the only evidence that
`phaseSawtooth` narrows.

Both wrap a negative remainder UP, and `phaseInLayerTime`'s final comparison cannot fire
at all — `fmod` never returns its divisor — but it is transcribed because it is in the
binary.

## `frameTimeSeconds`, and the differ hole it was hiding in

```
if (hopInterval <= 0.0) return 0.0;
return (frame * step + startFrameHalf / 2.0) / hopInterval;
```

Three of the four `OnsetEnvelope` fields are read here and nothing else recovered touches
the struct, so only those three are declared and everything else is opaque bytes with
`static_assert`s on the offsets.

`startFrameHalf` counts HALF frames: the compiler emits `scvtf d3, w8, #0x1`, a
fixed-point convert with one fractional bit. What it counts half-frames *of* is not
established — nothing recovered writes it — and the name describes the arithmetic rather
than being recovered.

This function is also the reason a real defect in the differ was found. It is a CONST
member, mangled `__ZNK…`, and `parseNested` required `__ZN…`: **every const member in the
fleet was invisible to the tool** — not reported as absent, not reported at all, so a
completeness marker was measured against a list with holes in it. Fixing the parser made
this function appear (14 instructions here against the bundle's 13: the shipped code
folds the halving into the convert and this build does not) and exposed two more that had
never been compared: `spectra::Spectrogram::hzToBin` and `lutblend::CubeLUT::valid`.

## `estimatePhaseSeconds` (138 instructions) — [partial]

The phase of the beat, in seconds, within one beat period. Read in full from
`/tmp/ae/beatgrid.asm` lines 1405–1543.

It is a coarse-to-fine search, and both halves of that matter:

* 512 candidates, **half a beat apart** (`t0 = i * period * 0.5`), each scored by
  summing the envelope sampled every `period` seconds;
* the winner is then refined against its two neighbours — `shift = (p - n) / 2 /
  (p - 2c + n)`, clamped to [-1, 1] — which buys a **sub-half-beat** resolution, and
  the result is wrapped into `[0, period)` with `fmod`.

Three details the listing pins and a paraphrase would lose:

* the samples are offset by the **half window** (`(windowSize / 2) / sampleRate`), so
  the score reads the centre of the analysis window and not its start. The half window
  is **zero** when the sample rate is not positive — the listing guards the field at
  `+0x18` before it does any arithmetic;
* the interpolation between frames is **linear** and in **float**:
  `values[f] * (1 - frac) + values[f + 1] * frac`, with `frac` the fractional part of
  the frame position truncated toward zero (`fcvtms`);
* the neighbour indices **wrap**: `511` and `0` are the neighbours of `0`.

**Why it is `[partial]` and not `[exact]`: 135 instructions here against the bundle's
138.** The three are the empty-and-tempo guards: this build folds the two early exits
into a single `cmp`/`ccmp` pair where the shipped code tests them separately. The
BEHAVIOUR is the same — the self-check pins the phase of a known onset to 1e-5 s.

## `generateBeatGrid` (94 instructions) — [partial]

The markers of the beat grid across a region. Read in full from `/tmp/ae/beatgrid.asm`
lines 1611–1705. **94 instructions here against the bundle's 94, and only four
positions differ** — and those four are the switch table's address, which this build
annotates symbolically (`adrp x9, l_switch.table...@PAGE`) where the shipped listing
prints the resolved page and offset. The same difference is already recorded against
`subdivisionFactor`; it is an annotation, not a code difference.

What the listing pins:

* the returned vector is **emptied first** (`stp xzr, xzr, [x8]` / `str xzr, [x8, #0x10]`),
  so every guard — a non-positive bpm, a region of no length, a non-positive period —
  returns an empty grid rather than a partially built one;
* the first marker is rounded **UP** from the origin onto the subdivision period
  (`frintp`), and then advanced while it is still before the region — against
  `regionStart - 1e-9`, so a marker that lands a hair early is not dropped by a
  rounding error;
* the marker number is the beat index from the origin **plus one**;
* the four-beat subdivision numbers its markers in **quarter beats**
  (`nearbyint(index * 0.25)`), and only that subdivision gets the treatment — the
  listing computes it unconditionally and then selects, which is the same answer.

## What is next

`analyzeBeatGrid` (57) is the smallest outstanding function and the entry point that ties
these together; `generateBeatGrid` (94) and `estimatePhaseSeconds` (138) follow. The two
big ones — `computeOnsetEnvelope` (436) and `estimateTempoBpm` (578) — are the tempo
analysis proper.
