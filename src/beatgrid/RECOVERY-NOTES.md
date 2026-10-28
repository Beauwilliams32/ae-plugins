# beatgrid — recovery notes

Source: `BeatGridRuler.plugin` (bundle id `com.williamsmedia.aftereffects.BeatGridRuler`)
installed at `/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/`. The
original C++ was destroyed on 2026-10-13; the binary is the only copy and it is
unstripped.

## State

Ten of the twelve `beatgrid::` symbols are recovered; four are instruction-identical.

| Function | Instrs | State |
|---|---|---|
| `clampf` | 5 | Recovered — **identical** (shared) |
| `isPowerOfTwo` | 6 | Recovered — **identical** (shared) |
| `downmixToMono` | 116 | Recovered — 116 here, 2 of 116 positions (shared) |
| `subdivisionFactor` | 8 | Recovered — 8 here; the only difference is that this build prints the switch table's address symbolically |
| `phaseInLayerTime` | 24 | Recovered — 24 here, one condition-code alias |
| `phaseSawtooth` | 31 | Recovered — 31 here |
| `OnsetEnvelope::frameTimeSeconds` | 13 | Recovered — **identical** (see below) |
| `computeOnsetEnvelope` | 436 | Recovered — **[partial]**, 434 here |
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
if (sampleRate <= 0.0) return 0.0;
return (frame * hopSize + windowSize / 2.0) / sampleRate;
```

which is the **frame-centre** time: the frame's start plus half the analysis window.

It reads `hopInterval` (+0x18), `startFrameHalf` (+0x20) and `step` (+0x24) — but those
three names were themselves a guess, and `computeOnsetEnvelope` has since shown the
struct's real layout: `+0x18` is the **sample rate**, `+0x20` the **window size** and
`+0x24` the **hop size**. The arithmetic below is therefore restated in the recovered
names, and the header now declares the whole struct.

The window size counts HALF frames: the compiler emits `scvtf d3, w8, #0x1`, a
fixed-point convert with one fractional bit, so the halving of the window is folded into
the convert rather than written as a separate divide.

This function is also the reason a real defect in the differ was found. It is a CONST
member, mangled `__ZNK…`, and `parseNested` required `__ZN…`: **every const member in the
fleet was invisible to the tool** — not reported as absent, not reported at all, so a
completeness marker was measured against a list with holes in it. Fixing the parser made
this function appear (and it is now **instruction-identical** — the shipped code folds
the halving into the convert and so does this build) and exposed two more that had
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

## `computeOnsetEnvelope` (436 instructions) — [partial]

The spectral-flux onset envelope. Read in full from `/tmp/ae/beatgrid.asm` lines 216–652.

### The `OnsetEnvelope` layout, corrected

The header used to declare only three fields, with the first `0x18` bytes opaque:

```
+0x00  unknown[0x18]
+0x18  hopInterval   (double)
+0x20  startFrameHalf(int)
+0x24  step          (int)
```

That was wrong, and the listing says so. The function's very first act is to zero the
whole 0x30-byte result (`stp q1, q1, [x8, #0x10]` / `str q1, [x8]`), and then it writes:

```
str  d0, [x8, #0x18]      ; the sample rate
stp  w9, w3, [x8, #0x20]  ; the window size, then the hop size
str  d0, [x8, #0x28]      ; the frame rate: sampleRate / hopSize
```

and, before that, `vector<float>::assign(numFrames, 0.0f)` fills `+0x00`. So the real
layout is:

```
+0x00  std::vector<float> values        (24 bytes: three words)
+0x18  double             sampleRate
+0x20  int                windowSize
+0x24  int32_t            hopSize
+0x28  double             framesPerSecond
sizeof == 0x30
```

Every one of those offsets is pinned by a `static_assert(offsetof(...))` in
`beatgrid.h`, whose message quotes the store that establishes it. The two old field
names are gone — `hopInterval` was the sample rate and `step` was the hop size, so
keeping them would have been worse than useless.

### The algorithm

* **Guards**, in the listing's own order: no samples (`cbz x0`), no frames (`cbz x1`),
  a non-positive sample rate (`fcmp d0, #0.0`), a window below one
  (`ccmp w9, #0x1, #0x8, hi` / `b.lt`), a hop below one (`cmp w3, #0x1` / `b.lt`), a
  window below two (`cmp w9, #0x2` / `b.lo`), and a window that is **not a power of
  two** (`sub w9, w10, #0x1` / `and w9, w10, w9` / `cbnz w9`). Every one of them returns
  the **zeroed** envelope — empty vector and zeroed scalars — because the zeroing
  happens before the guards.
* The four scalars are stored **before** the frame count is worked out, so even the
  frame-count overflow return (`cmp w9, #0x7ffffffe` / `b.hi`) keeps them. The self-check
  pins that ordering.
* The frame count is `(frames - window) / hop + 1`, and is **one** when the signal is
  shorter than the window (`subs x9, x21, x20` / `b.ge`).
* The window is a **periodic** Hann: `0.5f * (1 - cosf(i * 2pi / N))`, dividing by the
  window SIZE and not by size - 1 (`fdiv s0, s0, s1`). `2*pi` is the float literal
  `0x40c90fdb`.
* The transform is an **in-place radix-2 decimation-in-time** FFT: an incremental
  bit-reversal permutation (`tst`/`eor`/`b.ne` at 0xdf0 — the reversal counter is XORed at
  every level and the loop stops at the first level whose bit was clear), then
  butterflies of length 2, 4, ... N whose twiddle advances by a **recurrence**
  (`nextRe = wr*stepRe - wi*stepIm`) rather than being recomputed from an angle.
* The flux of a frame is the sum of the **positive** differences between this frame's
  half-spectrum magnitudes and the previous frame's, accumulated in **double**
  (`fcvt d2, s1`) and only then narrowed to a float. The **first** frame has no
  predecessor and is zero (`cbz x23` at 0xf48), and the two magnitude buffers alternate,
  so the previous frame's magnitudes are always the other buffer.

### Why it is `[partial]` and not `[exact]`: 434 instructions here against the bundle's
### 436.

The two are a stack-frame size and a register spill, not a behavioural difference: the
shipped code allocates `0x140` bytes of frame and this build allocates `0x150`, which
shifts the `stp d11, d10` / `stp d9, d8` spills by one slot. The instruction bodies are
otherwise the same — same guards in the same order, same periodic window, same
bit-reversal shape, same recurrence-stepped butterflies, same double-accumulated
positive-difference flux.

Two source-level details were needed to get within two instructions at all, and they are
worth recording because they are the kind of thing a paraphrase loses:

* the window loop's index must be an **`int`**, not a `size_t` — with a `size_t` index
  this build splits the loop into two four-wide lanes and adds about eighteen
  instructions;
* the bit-reversal must be written as an infinite `for(;;)` with a `carry` boolean, so
  that the XOR happens on every level and the shift only on the continue path, which is
  the shape the shipped `tst`/`eor`/`b.ne` has.

## What is next

`estimateTempoBpm` (578) is the biggest outstanding function and the tempo analysis
proper; `analyzeBeatGrid` (57) is the entry point that ties these together. `generateBeatGrid`
(94), `estimatePhaseSeconds` (138) and `computeOnsetEnvelope` (436) are recovered.
