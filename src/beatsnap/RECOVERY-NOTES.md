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
| `computeFlux` | 416 | **Identified only** |
| `renderValues` | 190 | **Identified only** |
| `pickOnsets` | 136 | **Identified only** |
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

- `computeFlux` (416) — the STFT/spectral-flux core. The largest function here, and
  the one that would need `OnsetConfig`'s remaining fields and the `FluxTrack`
  layout. `spectra::analyse` is already recovered and covers the same shape of
  work, so it is a transcription-plus-difference job rather than a blank page.
- `pickOnsets` (136) — peak picking over a flux track against a threshold track.
  Needs `FluxTrack` and its own return type.
- `renderValues` (190) — takes `const vector<double>&`, an `int` and a
  `RenderConfig`; the "snap these values to beats" payoff. Needs `RenderConfig`.
- The host integration (`EffectMain`) — no `plugins/BeatSnap/` exists, and none
  should be invented: the PiPL and parameter table are recovered
  (`specs/BeatSnap.json`, `PARAMETERS.md`) but a rebuilt effect has to meet the
  `eGL2` obligations this bundle claims (`SUPPORTS_SMART_RENDER`,
  `FLOAT_COLOR_AWARE`, `SUPPORTS_THREADED_RENDERING` — see `CAPABILITIES.md`).
