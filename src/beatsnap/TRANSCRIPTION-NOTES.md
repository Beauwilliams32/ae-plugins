# beatsnap — transcription notes for the four outstanding functions

This file is a HANDOVER artefact: the four onset-detection functions below are
declared in `beatsnap.h` with recovered layouts but had no body at the time this
file was written. Everything here is read off the shipped bundle's instruction
stream and was verified line by line against `/tmp/ae/bs-arm64.asm` (the
`otool -tvV` dump of
`/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/BeatSnap.plugin/Contents/MacOS/BeatSnap`).

Instruction ranges in that dump:

| Function | Lines | Instrs |
|---|---|---|
| `computeFlux` | 475–1064 | 416 |
| `adaptiveThreshold` | 1065–1143 | 78 |
| `pickOnsets` | 1144–1280 | 136 |
| `detectOnsets` | 1281–1328 | 47 |
| `renderValues` | 1329–1519 | 190 |

Mangled symbols:

```
__ZN8beatsnap11computeFluxEPKfmRKNS_11OnsetConfigE
__ZN8beatsnap10pickOnsetsERKNS_9FluxTrackERKNSt3__16vectorIfNS3_9allocatorIfEEEEf
__ZN8beatsnap12detectOnsetsEPKfmRKNS_11OnsetConfigE
__ZN8beatsnap12renderValuesERKNSt3__16vectorIdNS0_9allocatorIdEEEEiRKNS_12RenderConfigE
```

Note that `pickOnsets` and `renderValues` take their vector arguments by
`const&`, and that `computeFlux`/`detectOnsets`/`renderValues` are sret
functions (the returned object is written through `x8`).

## The three structs, with their offset evidence

### `OnsetConfig` — 0x20 bytes

```
ldr  d8, [x2]             +0x00  sampleRate    (double)
ldr  w28, [x2, #0x8]      +0x08  fftSize       (int)
ldp  s1, s0, [x22, #0xc]  +0x0c  bandLoHz, +0x10 bandHiHz   (floats)
ldr  s0, [x20, #0x14]     +0x14  thresholdScale (float)
ldr  s0, [x20, #0x18]     +0x18  pickScale      (float)
ldr  w1, [x20, #0x1c]     +0x1c  medianWindow  (int)
```

`detectOnsets` reads the last three in the order medianWindow → thresholdScale →
pickScale, which is the order the fields are declared in.

### `FluxTrack` — 0x28 bytes

```
str  xzr, [x8, #0x20]     ; the sret is zeroed across 0x00..0x28
movi.2d v0, #0
stp  q0, q0, [x8]
stp  d0, d1, [x8, #0x18]  ; hopSeconds, windowSeconds stored TOGETHER
ldp  d1, d2, [x21, #0x18]
```

The vector is the FIRST member: `computeFlux` calls
`__ZNSt3__16vectorIfNS_9allocatorIfEEE6assignEmRKf` with the sret pointer ITSELF
as the vector's `this`, so the flux vector lives at +0x00 and the two doubles
follow it at +0x18 and +0x20.

### `RenderConfig` — 0x20 bytes

```
ldr  w8, [x22]            +0x00  mode        (int; 1 = discrete, else curve)
ldr  d8, [x22, #0x8]      +0x08  timeScale   (double)
ldp  s2, s3, [x22, #0x10] +0x10  attackMs, +0x14 decayMs   (floats, ms)
ldp  s12, s1, [x22, #0x18]+0x18  fillValue, +0x1c peakValue (floats)
```

The peak value is read two ways — `ldr s0, [x22, #0x1c]` on the discrete path and
`ldp s12, s1, [x22, #0x18]` on the curve path — and it is the SAME field.

## `computeFlux` — 416 instructions

### Prologue and guards

```
str  xzr, [x8, #0x20] / movi.2d v0, #0 / stp q0, q0, [x8]   ; sret zeroed
cbz  x0, ret            ; samples == nullptr
cbz  x1, ret            ; n == 0
ldr  d8, [x2] / fcmp d8, #0.0 / b.ls ret                    ; sampleRate <= 0
ldr  w28, [x2, #0x8]
subs w8, w28, #0x1
and  w8, w28, w8
ccmp w8, #0x0, #0x0, ge
ccmp w28, #0x2, #0x0, eq
ccmp w28, #0x4, #0x0, hs
b.hs  continue
```

The fused `ccmp` chain is: continue iff `fft >= 1` AND `(fft & (fft-1)) == 0`
(a power of two) AND `fft != 2` AND `fft >= 4`. The `fft < 4` test is LAST
because it is the final `b.hs`, so the source shape is

```cpp
if (!isPowerOfTwo(config.fftSize) || config.fftSize < 4) return track;
```

with `isPowerOfTwo` inlined. `fft == 2` is rejected by the `fft < 4` test, not
by a separate one.

### Sizes

```
lsr  x21, x28, #2        ; hopSize = fft >> 2
ucvtf d9, w28            ; (double)fft
lsr  x19, x28, #1        ; bins   = fft >> 1
```

### Hop count

```
subs x8, x1, x28         ; n - fft
udiv x8, x8, x21         ; (n - fft) / hopSize
csinc w27, wzr, w8, lo   ; hops = (n < fft) ? 0 : that + 1
```

### The two doubles are stored BEFORE the `hops < 1` check

```
ucvtf d0, w21 / fdiv d0, d0, d8          ; hopSeconds   = hopSize / rate
fmov d1, #0.5 / fmul d1, d9, d1 / fdiv d1, d1, d8
                                       ; windowSeconds = (fft * 0.5) / rate
stp  d0, d1, [x8, #0x18]
cmp  w27, #0x1 / b.lt ret               ; hops < 1 -> empty flux, but the
                                       ;   two doubles ARE already set
```

This ordering is the single most easily-missed detail in the function: a signal
too short to produce a single frame still comes back with its hop and window
times filled in.

### Flux allocation

```
mov  x1, x27 / bl __ZNSt3__16vectorIfNS_9allocatorIfEEE6assignEmRKf
```

A real libc++ call with the sret as `this`, so the source is
`track.flux.assign(hops, 0.0f)`.

### The window

```
mov  x0, x25 / bl __Znwm        ; operator new(fft * 4)
bl  _bzero
```

`operator new` + `_bzero` is what `std::vector<float> v(n)` compiles to (value
initialisation), so the window is a `std::vector<float>` of `fft` samples.

The formula, from the vectorised body:

```
movi.4s v1, #0x3f, lsl #24          ; 0x5f000000? no: 0x3F000000 = 0.5f
mov   w8, #0xfdb / movk w8, #0x40c9 ; 0x40c90fdb = 6.28318548f (2*pi)
fmul.4s v0, v0, v2                  ; index * 2pi
fdiv.4s v0, v0, v1                  ; / (float)fft
```

with four scalar `_cosf` calls per group of four and a scalar tail from
`fft & ~3`:

```
fsub.4s v0, v0, v1                  ; 1 - cos
fmul.4s v0, v0, v1                  ; * 0.5f
```

so `w[j] = 0.5f * (1.0f - cosf(j * 2pi / fft))` — the same PERIODIC Hann the
namespace's `hannWindow` produces, but built inline rather than by calling it
(the listing contains no call to `beatsnap::hannWindow`).

### The band, in bins

```
ldp  s1, s0, [x22, #0xc]   ; s1 = bandLo, s0 = bandHi
fcmp s0, s1
fcsel s2, s0, s1, mi        ; s2 = min(bandLo, bandHi)
fcmp s1, s0
fcsel s0, s0, s1, mi        ; s0 = max(bandLo, bandHi)
fdiv d8, d8, d9             ; binHz = rate / fft
fcvt d1, s2 / fdiv d1, d1, d8 / fcvtas x8, d1     ; round(loHz / binHz)
fcvt d0, s0 / fdiv d0, d0, d8 / fcvtas x11, d0    ; round(hiHz / binHz)
```

`fcvtas` is `lround` — round half away from zero.

### The clamp — the awkward one

```
sub  w9, w19, #0x1        ; bins - 1
cmp  w8, w9
csel w10, w8, w9, lo      ; UNSIGNED min(raw, bins-1)
cmp  w8, #0x0
csel w8, wzr, w10, lt     ; SIGNED floor on the ORIGINAL raw
```

The `csel` for the upper bound is an UNSIGNED comparison while the floor is a
SIGNED one, and both look at the ORIGINAL `raw`, not at the already-clamped
value. The net effect is

```
raw < 0 ? 0 : min(raw, bins - 1)
```

which is what `clampf` would NOT give (that one tests lo last), so the source
here is an explicit expression rather than a call to the namespace's `clampf`.

### The two bins are then SORTED

```
cmp  w9, w8
csel w22, w9, w8, lo      ; w22 = min(loBin, hiBin)   -> saved as binLo
cmp  w10, w22
csel w10, w10, w22, hi    ; w10 = max(...)
cmp  w9, w8
csel w24, w10, w9, lo     ; w24 = max(loBin, hiBin)   -> saved as binHi
```

so a band given upside down is handled: the smaller bin index is used first.
The band count is `binHi - binLo + 1`.

### The FFT

Two `std::vector<float>` locals (the `re`/`im` vector objects live at
`[x29-0x90]` and `[x29-0xa8]`, each with its begin/end/cap triple):

```
sub x0, x29, #0x90 / sub x1, x29, #0xa8 / bl beatsnap::fftRadix2
```

The copy loop, vectorised 8-wide with a scalar tail:

```
ldr s0, [x8], #0x4       ; samples[start + k]
ldr s1, [x9], #0x4       ; window[k]
fmul s0, s0, s1
str s0, [x10], #0x4      ; re[k]
str wzr, [x11], #0x4     ; im[k] = 0
```

### Magnitudes

Two raw heap buffers of `bins * 4` bytes each (`__Znwm` + `_bzero`, freed with
`__ZdlPv` — a `delete[]` with the cookie elided for a trivial destructor), plus
two more of the same size for the ping-pong. The pointer rotation is a
three-move dance:

```
loop top:  mov x26, x25 / mov x25, x27
loop end:  mov x27, x26
```

The magnitude formula:

```
fmul  s1, s1, s1
fmadd s0, s0, s0, s1
fsqrt s0, s0            ; sqrt(re^2 + im^2)
```

### The flux accumulation

```
str wzr, [x8]           ; flux[0] = 0
ldr s1, [x8], #0x4      ; magCur[k]
ldr s2, [x9], #0x4      ; magPrev[k]
fsub s1, s1, s2
fadd s2, s0, s1
fcmp s1, #0.0
fcsel s0, s2, s0, gt    ; acc += (d > 0) ? d : 0
str s0, [x8, x22, lsl #2]
```

The accumulator is a FLOAT (`fadd s2, s0, s1` on single-precision registers),
not a double. `flux[0]` is a literal zero — the first frame has no predecessor
and is not compared with anything.

## `pickOnsets` — 136 instructions

### Guards

```
ldp  x9, x8, [x0] / sub x8, x8, x9 / asr x9, x8, #2   ; flux.size()
cmp  x9, #0x3 / b.lo ret                              ; size < 3 -> empty
ldp  x11, x10, [x1] / sub x10, x10, x11
cmp  x10, x8 / b.ne ret                               ; thresholds.size() must match
```

The sizes are compared as BYTE counts, which is the same test.

### The peak test — asymmetric

```
ldr s0, [x11, #0x4]     ; flux[i]
ldr s1, [x10, #0x4]     ; thresholds[i]
fcmp s0, s1 / b.ls skip ; flux[i] > thresholds[i]        STRICT
ldr s1, [x9, x8, lsl #2] ; flux[i-1]
fcmp s0, s1 / b.mi skip ; flux[i] >= flux[i-1]           NOT strict
ldr s1, [x8, #0x8]      ; flux[i+1]
fcmp s0, s1 / b.ls skip ; flux[i] > flux[i+1]            STRICT
```

The left neighbour is `>=` and the right neighbour is `>`. A plateau therefore
picks its LAST sample, not its first.

### The time

```
ucvtf d0, x27                    ; i
ldp  d1, d2, [x21, #0x18]        ; hopSeconds, windowSeconds
fmadd d0, d0, d1, d2             ; i * hopSeconds + windowSeconds
```

### The two different `emplace_back` overloads

```
bl __ZZNSt3__16vectorIdNS_9allocatorIdEEE12emplace_backIJdEEERdDpOT_ENKUlvE0_clEv
```

takes a `double&&` — the CANDIDATE list, built from a temporary expression.

```
bl __ZZNSt3__16vectorIdNS_9allocatorIdEEE12emplace_backIJRKdEEERdDpOT_ENKUlvE0_clEv
```

takes a `double const&` — the RESULT list, built by ranging over a `const`
vector of doubles.

So the source pushes a computed temporary into one vector and a `const double`
lvalue into the other.

### The merge

```
fcvt d0, s8 / fdiv d0, d0, d1     ; tol = scale / 1000.0
movi.2d v1, #0                    ; NOTE: this zeroes the SAME register
fmaxnm d8, d0, d1                 ; tol = max(scale/1000, 0.0)
```

The `movi.2d` comes AFTER the `fdiv` and overwrites the divisor, so the
`fmaxnm` compares against 0.0 — the intended behaviour, but the register reuse
means the divisor register cannot be the same one.

```
fmov d0, x8                       ; last = -1e18  (0xc3abc16d674ec800)
```

The merge keeps a candidate iff `next - last >= tol` (`b.lt` skips only when
STRICTLY less), and `last` is updated ONLY when a candidate is kept — so two
onsets closer together than the tolerance collapse onto the FIRST of them.

## `detectOnsets` — 47 instructions

The whole function is a three-call pipeline:

```
add x8, sp, #0x18 / bl beatsnap::computeFlux        ; FluxTrack local at sp+0x18
ldr w1, [x20, #0x1c]                                ; cfg.medianWindow
ldr s0, [x20, #0x14]                                ; cfg.thresholdScale
mov x8, sp / add x0, sp, #0x18 / bl beatsnap::adaptiveThreshold
ldr s0, [x20, #0x18]                                ; cfg.pickScale
add x0, sp, #0x18 / mov x1, sp / mov x8, x19 / bl beatsnap::pickOnsets
```

`pickOnsets`'s result goes straight into the return slot (`mov x8, x19`), so
the function returns `pickOnsets`'s type with no copy and no conversion.

## `renderValues` — 190 instructions

### Prologue: the vector is zeroed BEFORE the count guard

```
stp xzr, xzr, [x8] / str xzr, [x8, #0x10]   ; sret zeroed
cmp w1, #0x1 / b.lt ret                     ; count < 1 -> empty
```

The frame is set up only after the guard, so the empty case returns through the
shared epilogue with the vector still zeroed.

### The fill

```
ubfiz x24, x21, #2, #32 / bl __Znwm
str x0, [x23]          ; begin
add x8, x0, x24 / str x8, [x23, #0x10]   ; capacity
ldr s0, [x22, #0x18]   ; fillValue
... dup.4s fill ...
str x8, [x23, #0x8]    ; end
```

This is exactly what `std::vector<float> v(count, fillValue)` compiles to
(`__Znwm`, capacity store, vectorised fill, end store). `resize` and `assign`
were both ruled out by probe: they emit a call.

### The timeScale guard comes AFTER the fill

```
ldr d8, [x22, #0x8] / fcmp d8, #0.0 / b.ls ret
```

So a non-positive `timeScale` returns the vector ALREADY FILLED with
`fillValue`, not an empty one.

### Mode 1 — discrete

```
ldr w8, [x22] / cmp w8, #0x1 / b.ne curve
ldr s0, [x22, #0x1c]        ; peakValue
ldr d1, [x8]                ; onset time
fmul d1, d8, d1             ; t * timeScale
fcvtas x10, d1              ; lround
cmp w21, w10 / b.ls skip    ; count <= index -> skip
and x10, x10, #0x7fffffff   ; strip the sign
str s0, [x19, x10, lsl #2]  ; out[index] = peakValue
```

There is ONE unsigned bound check, and the `and` with 0x7fffffff is what makes
a negative index safe: `lround` of a negative time gives a negative index,
which fails `cmp w21, w10` (unsigned) and is skipped anyway.

### Mode != 1 — curve

```
fcvt d2, s2 / fdiv d9, d2, d0        ; span  = max(attackMs / 1000, 0.0)
fmaxnm d10, d9, d2
fcvt d2, s3 / fdiv d0, d2, d0
fmaxnm d0, d0, d1                   ; max(decayMs / 1000, 1e-4)
fmov d1, #3.0 / fdiv d0, d0, d1     ; sigma = that / 3.0
ldp s12, s1, [x22, #0x18]          ; fillValue, peakValue
fsub s13, s1, s12                  ; amp = peakValue - fillValue
```

`1e-4` is the constant `0x3f1a36e2eb1c432d`.

The per-sample loop:

```
ucvtf d1, w23 / fdiv d1, d1, d8     ; target = j / timeScale
```

Onsets are consumed while `times[k] <= target`; the LAST one consumed becomes
the reference. The inner loop is unrolled by two, which is why the listing
jumps between `x8 += 2` and `x8 += 1`.

```
tbz w9, #0x0, notStarted            ; the `started` flag
mov.16b v11, v0                    ; keep the reference in the same register
fsub d0, d1, d11                   ; delta = target - reference
fcmp d0, d10 / b.ls ramp            ; delta <= span ?
fsub d0, d0, d10 / fneg d0, d0 / fdiv d0, d0, d1 / bl _exp
                                   ; exp(-(delta - span) / sigma)
fcmp d9, #0.0 / b.le noRamp
fdiv d0, d0, d9                    ; delta / span
fmov s1, #1.00000000               ; span <= 0 -> 1.0
```

Two branches, not one: `delta <= span` takes the ramp, and the ramp divides by
`span` only if `span > 0`, otherwise it yields 1.0.

The clamp, HI FIRST and LO LAST:

```
fcmp s1, s14 / fcsel s2, s14, s1, gt    ; s2 = (v > 1.0) ? 1.0 : v
fcmp s1, #0.0 / fcsel s1, v15, s2, mi   ; s1 = (v < 0.0) ? 0.0 : s2
fmadd s1, s1, s13, s12                  ; clamped * amp + fillValue
```

The second `fcsel` compares the ORIGINAL value against 0, not the
already-clamped-to-1 value, so this is
`(v > 1.0f) ? 1.0f : ((v < 0.0f) ? 0.0f : v)` — the low bound wins on an
inverted range, the same quirk the namespace's `clampf` has.

The `started` flag: if no onset has been consumed yet, the value is 0 and the
output is `fillValue`:

```
mov w9, #0 / movi.2d v1, #0
```

The shipped code KEEPS the `last = -1e18` initialisation in a register even
though the not-started path does not read it, so the source should keep
`double last = -1e18;` and `bool started = false;` as written locals.

## Codegen facts established by probe

- `std::vector<float> v(n)` -> `__Znwm` + `_bzero` (value initialisation).
- `std::vector<float> v(n, fill)` -> `__Znwm`, capacity store, vectorised
  `dup.4s` fill, end store.
- `v.resize(n, fill)` and `v.assign(n, fill)` -> a call to the corresponding
  libc++ method, which the listing does NOT contain.
- `std::lround(d)` -> `fcvtas x0, d0`; `(long)std::round(d)` -> the same.
- `std::fmax(a, b)` -> `fmaxnm` (NaN-propagating), not `fmaxnm`-vs-`maxnm`.

## What is still open

- The exact source shape of the band-bin clamp and of `renderValues`'s
  `vector(count, fill)` are inferred from the emitted code rather than read
  directly, so the source may differ cosmetically from the original.
- `computeFlux`'s magnitude buffers are raw `new float[bins]()` rather than
  vectors: the listing allocates them with `__Znwm` + `_bzero` and frees them
  with `__ZdlPv`, with no vector triple, which is `new float[n]()`, not a
  `std::vector<float>`.
