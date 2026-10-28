# beatsnap — recovery notes

Source: `BeatSnap.plugin` (bundle id `com.williamsmedia.aftereffects.BeatSnap`,
version 1.0.0) installed at
`/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/`. The original
C++ was destroyed on 2026-10-13; the binary is the only copy and is unstripped.

## State

Eleven authored functions. **Seven recovered**, four identified only.

| Function | Instrs | State |
|---|---|---|
| `clampf` | 5 | Recovered — **instruction-identical** |
| `isPowerOfTwo` | 6 | Recovered — **instruction-identical** |
| `medianOfWindow` | 58 | Recovered — **instruction-identical** |
| `hannWindow` | 112 | Recovered — differs, 116 here (see below) |
| `downmixToMono` | 116 | Recovered — differs in 2 of 116 positions, both reordered |
| `fftRadix2` | 100 | Recovered — differs in 4 of 100 positions |
| `adaptiveThreshold` | 78 | Recovered — 91 here; a loop-structure difference, not a maths one |
| `computeFlux` | 416 | Recovered — differs, 421 here (see below) |
| `renderValues` | 190 | **Identified only** |
| `pickOnsets` | 136 | Recovered — differs in 37 of 136 positions (see below) |
| `detectOnsets` | 47 | **Identified only** — but fully read; see below |

`test/beatsnap.test.mjs` asserts that table against `tools/codegen-diff.mjs`, so
recovering another function fails the test until this file is updated.

## The five shared-DSP copies are not a shortcut, and are checked

`hannWindow`, `fftRadix2`, `downmixToMono`, `isPowerOfTwo` and `clampf` are
machine-code-identical across the fleet — `tools/dsp-duplication.mjs` compares every
copy in every installed bundle — so the source here was transcribed from the
`spectra::` copy and the namespace changed.

That is only legitimate if the copying is exact, and `MATCH`/`DIFFER` alone cannot
show it: four of the five do **not** match instruction for instruction, so a
transcription error would look exactly like the known codegen variance. The check
that separates them is comparative, in `test/beatsnap.test.mjs`: the same source,
compared against two bundles built from it, must diverge *in the same way* against
both. All five do — `hannWindow` 112/116 against SpectraKeys and 112/116 against
BeatSnap, `fftRadix2` 4-of-100 both times, `downmixToMono` 2-of-116 both times.

The residual differences are clang emission, not transcription: the twiddle
recurrence in `fftRadix2` is reassociated, and `downmixToMono` emits two independent
instructions in the other order.

## `detectOnsets` — the architecture, recovered without transcribing it

47 instructions, read in full. It is a three-call pipeline and it is the whole
architecture of this effect:

```
detectOnsets(const float* samples, size_t count, const OnsetConfig& cfg):   [sret in x8]
    FluxTrack flux = computeFlux(samples, count, cfg);          // local at sp+0x18
    vector<float> thresholds = adaptiveThreshold(
        flux, cfg[+0x1c] /*int*/, cfg[+0x14] /*float*/);        // local at sp
    return pickOnsets(flux, thresholds, cfg[+0x18] /*float*/);  // sret straight out
```

Readings this gives, none of which required transcribing the callees:

- **The three `OnsetConfig` fields the pipeline uses**, from the call sites:
  `+0x14` float → `adaptiveThreshold`'s scale; `+0x18` float → `pickOnsets`' third
  argument; `+0x1c` int → `adaptiveThreshold`'s window. The rest of the struct
  stays unclaimed.
- **`pickOnsets`' result goes straight into the return slot** (`mov x8, x19`), so
  `detectOnsets` returns `pickOnsets`' type unchanged — no copy, no conversion.
- Both temporaries are freed through the normal vector destructor, with an unwind
  path: the locals are a `FluxTrack` and a `std::vector<float>`, so `FluxTrack`
  owns a vector, which is consistent with `computeFlux` being handed an sret.

This is why the function is listed as identified-only: transcribing it would
reference `computeFlux`, `pickOnsets` and the `FluxTrack` layout, none of which are
recovered, and a stub of any of them would be invention.

## `adaptiveThreshold` — what it actually computes

Worth stating plainly, because the name suggests something simpler than what the
disassembly does:

1. One pass takes the **maximum of the whole track**, starting from **zero** rather
   than from the first sample. An all-negative track therefore has a maximum of 0,
   and a `max_element`-style implementation would floor at its largest negative
   sample instead. The self-check pins this.
2. `scale` saturates at 100 (one-sided — no lower clamp), then
   `multiplier = 5.0 + (scale / -100.0) * 3.6`, which is `5.0` at `scale = 0` falling
   to `1.4` at `scale = 100`. **A higher `scale` means a LOWER threshold**, so the
   ramp runs opposite to the name: this is a sensitivity knob, not a strength one.
   A negative `scale` skips the ramp entirely and uses `5.0`.
3. Each sample's threshold is `multiplier * median(window)` **floored at 5% of the
   track maximum**, which is what stops a quiet passage from reading its own noise
   floor as a run of onsets.

The self-check reads that ramp back out of a single-sample track
(`10 * 5.0 = 50.0`, `10 * 1.4 = 14.0`), so the formula is verified rather than
asserted.

## `computeFlux` — the STFT core, and the ordering that is easy to get wrong

416 instructions, read in full. It is the largest function in the bundle and the
one with the most behaviour pinned to the ORDER of the listing rather than to
the arithmetic.

The four guards come first, in this order — `samples == nullptr`, `n == 0`,
`sampleRate <= 0`, and then a FUSED compare chain that rejects a non-power-of-two
fft size and an fft size below four:

```
subs w8, w28, #0x1 / and w8, w28, w8
ccmp w8, #0x0, #0x0, ge     ; fft >= 1
ccmp w28, #0x2, #0x0, eq    ; fft != 2
ccmp w28, #0x4, #0x0, hs    ; fft >= 4
```

so the source is one condition, `!isPowerOfTwo(fft) || fft < 4`. `fft == 2` is
excluded by the `fft < 4` arm, which is LAST because it is the final `b.hs`.

**The ordering that matters.** The hop and window times are computed and stored
BEFORE the "is there even one frame" test:

```
ucvtf d0, w21 / fdiv d0, d0, d8          ; hopSeconds   = hopSize / rate
fmov d1, #0.5 / fmul d1, d9, d1 / fdiv d1, d1, d8
                                         ; windowSeconds = (fft * 0.5) / rate
stp d0, d1, [x8, #0x18]                  ; BOTH ARE STORED HERE
cmp w27, #0x1 / b.lt ret                 ; hops < 1 -> empty flux, but the two
                                         ;   doubles above are ALREADY SET
```

So a signal too short to yield a single frame comes back with an EMPTY flux and
its hop and window times FILLED IN. Only the four guards leave them at zero.
The self-check pins both halves of that.

The rest of the function, in the listing's order:

1. `track.flux.assign(hops, 0.0f)` — a real libc++ call with the sret pointer
   ITSELF as the vector's `this`, which is what puts the flux vector at +0x00.
2. The window is built INLINE, not by calling `hannWindow`: the listing contains
   no call to it, only `__Znwm` + `_bzero` (what `std::vector<float> v(n)`
   emits) and four `_cosf` calls per group of four samples. The formula is the
   same PERIODIC Hann, `0.5f * (1 - cosf(j * 2pi / fft))`.
3. The band is min/max'd, converted to bins with `fcvtas` (an `lround`), clamped,
   and then SORTED, so an upside-down band behaves like an upright one.
   The clamp is asymmetric in the listing — the upper bound is an UNSIGNED min
   while the lower bound is a SIGNED floor applied to the RAW value — so the net
   rule is `raw < 0 ? 0 : min(raw, bins - 1)`, which is not the namespace's own
   `clampf`.
4. `fftRadix2` on two local vectors, magnitudes `sqrt(re^2 + im^2)` into two
   ping-ponged buffers, and the flux is the sum over the band of the POSITIVE
   difference between consecutive frames, in float arithmetic.
   `flux[0]` is a literal zero — the first frame has no predecessor.

### Why it differs from the bundle

`shipped=416 mine=421`, and the differ's first divergence is in the PROLOGUE:
the two spills the callee-saved vector registers to different slots. That is
register allocation, not semantics — it is the same class of difference the
shared-DSP copies show, and it is why the differ reports a differing
instruction COUNT rather than a position count.

One contributing difference is known and is a source-shape difference rather
than a behaviour one: the bundle allocates its two magnitude buffers as raw
`new float[bins]()` (`__Znwm` + `_bzero`, freed with `__ZdlPv`, with no vector
triple), whereas the recovered source uses `std::vector<float>`. The two have
identical observable behaviour — same values, same order, same lifetime — and
the difference is confined to allocation and teardown.

## `pickOnsets` — peak picking, and an asymmetric test

136 instructions, read in full. Two guards first: a flux track of fewer than
three samples, and a threshold track whose length does not match the flux
track's.

The peak test is where the function's character is, and it is ASYMMETRIC:

```
fcmp s0, s1 / b.ls skip    ; flux[i] >  thresholds[i]   STRICT
ldr s1, [x9, x8, lsl #2]   ; flux[i-1]
fcmp s0, s1 / b.mi skip    ; flux[i] >= flux[i-1]        NOT strict
ldr s1, [x8, #0x8]         ; flux[i+1]
fcmp s0, s1 / b.ls skip    ; flux[i] >  flux[i+1]        STRICT
```

The left neighbour is compared with `>=` and the right neighbour with `>`, so a
PLATEAU is picked at its LAST sample rather than its first. An implementation
that used `>` on both sides would pick the first sample of the plateau and
report a different onset time; the self-check pins the last one.

The onset's time is the frame index scaled by the hop, plus the window:
`i * hopSeconds + windowSeconds` (`ucvtf` / `ldp d1, d2, [x21, #0x18]` /
`fmadd`).

The listing also shows the two `emplace_back` overloads, which is how the
CANDIDATE list and the RESULT list are told apart: the candidate list is
filled through `emplace_back<double>(double&&)` — a temporary — and the result
list through the `double const&` overload, which is what ranging over a
`const` vector of doubles produces.

### The merge

```
fcvt d0, s8 / fdiv d0, d0, d1     ; scale / 1000.0
movi.2d v1, #0                    ; the divisor register is reused as 0.0
fmaxnm d8, d0, d1                 ; tol = max(scale / 1000, 0.0)
fmov d0, x8                       ; last = -1e18
fsub d2, d1, d0 / fcmp d2, d8 / b.lt skip
```

Two details: the tolerance is clamped at ZERO from below, so a negative scale
merges nothing; and `last` is updated ONLY when a candidate is KEPT, so two
onsets closer together than the tolerance collapse onto the FIRST of them. The
keep test is `>=`, so an onset exactly one tolerance after the last is kept.

### Why it differs from the bundle

`shipped=136 mine=136` — the same instruction count — but 37 of the 136
positions differ. The difference is register allocation and the shape of the
comparisons: the recovered source computes the three tests as separate
`if` statements with early `continue`, and clang folds them into a different
branch structure than the bundle's. The tests themselves are the same three,
in the same order, with the same strictness — which is what the self-check
asserts.

## `medianOfWindow` — an honest improvement target

The shipped implementation allocates a **copy** of the window, `memcpy`s it, runs a
**full sort**, indexes `sorted[count/2]`, and frees it — once per sample of the
track, from `adaptiveThreshold`'s inner loop. That is O(k log k) allocation-free-but-
copying work per sample, so a track of *n* samples with half-width *h* does
*n* sorts of *2h+1* elements.

Two properties make this improvable **without changing a single output value**, and
both are pinned by the self-check: the window is `[max(centre-radius,0),
min(centre+radius,size-1)]`, and the answer is the **upper** middle for an even
count. An incremental selection over a sliding window reproduces both exactly. That
is the first item in the improvement list below — a speed change with a
bit-identical output, which is the only kind of change this repository makes to
recovered behaviour without changing the effect.

## Not recovered, and what would be needed

- `renderValues` (190) — takes `const vector<double>&`, an `int` and a
  `RenderConfig`; the "snap these values to beats" payoff. Needs `RenderConfig`.
- The host integration (`EffectMain`) — no `plugins/BeatSnap/` exists, and none
  should be invented: the PiPL and parameter table are recovered
  (`specs/BeatSnap.json`, `PARAMETERS.md`) but a rebuilt effect has to meet the
  `eGL2` obligations this bundle claims (`SUPPORTS_SMART_RENDER`,
  `FLOAT_COLOR_AWARE`, `SUPPORTS_THREADED_RENDERING` — see `CAPABILITIES.md`).
