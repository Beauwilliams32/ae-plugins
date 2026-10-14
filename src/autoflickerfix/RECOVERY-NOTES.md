# AutoFlickerFix — recovery notes

Recovered from `AutoFlickerFix.plugin` (arm64), Adobe After Effects' Williams Media
plug-in folder. The original C++ was lost on 2026-10-13 with no remote and no backup;
the shipped bundles are the only copies left.

Bundle: 135,904 bytes. `aff::` domain functions: **10 of 10 recovered**, 719 instructions.

## What the effect is

The DSP reads as one coherent idea. `temporalTarget` smooths a brightness measurement
over time (a median filter to kill one-frame spikes, then a double-accumulated mean), and
`computeGain` converts the ratio between that target and the frame's own measurement into
a multiplicative gain, clamped to a symmetric decibel window built by `dbToGainClamp`.
Everything else supports those three.

## Coverage

| Function | Instrs | Codegen vs bundle |
|---|---|---|
| `aff::clampf` | 5 | **identical** |
| `aff::trimmedMean(const float*, size_t, float)` | 230 | differs (mine 147) |
| `aff::trimmedMean(const std::vector<float>&, float)` | 5 | **identical** |
| `aff::medianFilter` | 97 | differs (mine 96) |
| `aff::medianOf` | 28 | **identical** |
| `aff::meanOf` | 92 | **identical** |
| `aff::temporalTarget` | 197 | **identical** |
| `aff::dbToGainClamp` | 13 | **identical** |
| `aff::computeGain` | 23 | **identical** |
| `aff::centerRegion` | 29 | differs (mine 29, register numbering only) |

7 of 10 instruction-identical. The three that differ are semantically verified by
`selfcheck.cpp` (89 checks) and are described at the end of this file.

## Not recovered

The `aff::` namespace is complete. The HOST INTEGRATION is not:

| Symbol | Instrs | Status |
|---|---|---|
| `EffectMain` | 681 | identified only |
| `(anonymous namespace)::SampleWorldStat(PF_LayerDef const*, aff::RegionBounds const&, bool)` | 275 | identified only |
| `(anonymous namespace)::DrawStatsBar(PF_LayerDef*, float, aff::GainClamp const&)` | 191 | identified only |

Nothing is stubbed for these. There is no host-side code in this directory at all, which
is deliberate: `src/` holds transcriptions of shipped binaries, and these three are
unread. There is no `plugins/AutoFlickerFix/` either — unlike LUTBlend and GlitchKit,
this namespace has no reimplemented host layer.

## Recovered layouts

`GainClamp` — `{ float lo; float hi; }`, 8 bytes.

Pinned twice over. `computeGain` ends with `ldp s1, s2, [x0]` and clamps against `s2` with
`gt`, so `s2` is the ceiling and the member order is lo@0, hi@4. And `dbToGainClamp`
RETURNS the struct: its body leaves `1/gain` in `s0` and `gain` in `s1`, and the
`EffectMain` call site stores both with one `stp s0, s1, [sp, #0xb8]` — the same scratch
slot later passed to `computeGain` as `x0`.

`RegionBounds` — `{ int minX, minY, maxX, maxY; }`, 16 bytes, returned in `x0`/`x1`
rather than through an sret pointer:

```
orr  x0, x10, x12, lsl #32     ; minX | (minY << 32)
orr  x1,  x8,  x9, lsl #32     ; maxX | (maxY << 32)
```

## Three findings that came out of the codegen differ, not out of reading

**1. `meanOf` of an empty vector returns 0.0f, and the first transcription returned NaN.**
The guard was left implicit and an empty input fell through to `0.0 / 0.0`. The bundle has
its own `ret` on that path, which showed up as 93 instructions against 92. A self-check
written from the same misreading would have confirmed the wrong behaviour. This is the
clearest evidence in the repository that the instruction-stream comparison and the
behavioural self-check catch different classes of mistake.

**2. `dbToGainClamp` returns `GainClamp`, not `float`.**
The mangling cannot say so — C++ does not encode a non-template function's return type —
and there is no sret pointer to give it away either, because an 8-byte two-float aggregate
comes back in registers. The tell was a `mov.16b v1, v0` that a float-returning function
has no reason to emit, and an `s1` at the call site that is written nowhere in
`EffectMain`. Reading it as a float gave a 12-instruction function for a 13-instruction
one, and got `GainClamp.hi` semantically wrong: the real window is symmetric,
`{10^(-|db|/20), 10^(+|db|/20)}`.

**3. `computeGain`'s first parameter is the frame's own measurement, not the target.**
The body divides `s1 / s0`, and all four `EffectMain` call sites load `v0` from the
per-frame value and `v1` from `temporalTarget(...)`. Naming them the other way produces a
reciprocal gain — an error that inverts the correction and that no self-check written from
the same misreading would notice.

## Two corrections the self-check made to its own expectations

Recording these because they are the same failure mode as the above, one layer out — the
test was wrong, not the code, and only running it showed which.

* `centerRegion(w, h, 0.0f)` does **not** give the top-left pixel. It gives a ZERO-AREA
  rectangle at the CENTRE, because both offsets are `max((dimension - rw) / 2, 0)` and
  `rw` is 0. On 1920x1080 that is `minX == maxX == 960`.
* A single-sample spike does **not** survive `medianFilter(v, 1)`. The window is 3 wide,
  the spike is 1 of 3, and the median is a neighbour. The boundary is a spike of `w + 1`
  samples, which is what the retained checks now assert.

## The three functions that are not instruction-identical

* **`centerRegion` (29 vs 29)** — same instruction stream, same count, differing only in
  which general-purpose registers hold which subexpression, plus one commuted `cmp`/`csel`
  pair in the `min`. Verified by reading both streams position by position, not by the
  summary line, which reports any textual difference as a mismatch.
* **`medianFilter` (97 vs 96)** — the bundle duplicates a `ret` on the window-empty path
  where this transcription branches to a shared epilogue, costing one instruction.
* **`trimmedMean(const float*, size_t, float)` (230 vs 147)** — the bundle's vectoriser
  unrolled the double-accumulator reduction further than it does for this transcription.
  The unreachable whole-array branch discussed in the source is kept because dropping it
  changes the emitted stream; on this narrowing it is not the cause.

None of these three changes a result. All three are covered by `selfcheck.cpp`, and the
trimmed-mean one is additionally pinned by the identity
`trimmedMean(skew, t) == trimmedMean(skew.data(), skew.size(), t)` — the vector overload
being a tail call into the pointer form, which IS instruction-identical.
