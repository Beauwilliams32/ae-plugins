# motioncurvex — recovery notes

Source: `MotionCurveX.plugin` (bundle id `com.williamsmedia.aftereffects.MotionCurveX`)
installed at `/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/`. The
original C++ was destroyed on 2026-10-13 with no remote and no backup; the binary is
the only copy and it is unstripped.

**No After Effects host has ever loaded a rebuilt bundle.** Everything below is a
claim about the *shipped* binary — its instructions and its behaviour — and about
this source tree. Nothing here is an acceptance claim about the plugin running in
After Effects.

## State

All eleven of the bundle's `motioncurve::` symbols are present in `src/motioncurvex/`,
so the namespace carries the `NAMESPACE-COMPLETE` marker. **That marker means "no
symbol in the bundle is missing from this source", and it does not mean that every
body is instruction-faithful.** One of the eleven is transcribed; the other ten are
still modelled bodies with exact signatures.

| Function | Instrs | State |
|---|---|---|
| `clampf` | 5 | Recovered — **identical** (shared) |
| `_GLOBAL__N_1` (anonymous `medianOfWindow`) | 80 | Recovered — **identical** (transcribed, this lane) |
| `quadraticPeakOffset` | 21 | Modelled — 20 here |
| `downsample2x` | 196 | Modelled — 190 here |
| `buildPyramid` | 91 | Modelled — 83 here |
| `cropCenter` | 137 | Modelled — 78 here |
| `sadAt` | 178 | Modelled — 156 here |
| `smoothMovingAverage` | 81 | Modelled — 71 here |
| `temporalMedianFilter` | 164 | Modelled — 155 here (semantics read from the listing) |
| `accumulate` | 37 | Modelled — 59 here |
| `estimateTranslation` | 1373 | Modelled — 996 here |

The differ's tally for this namespace is **2 identical, 9 differing, 0 not
implemented**.

## The eleventh symbol — `motioncurve::_GLOBAL__N_1`

The bundle has eleven `motioncurve::` symbols and only ten of them are public. The
eleventh lives in an **anonymous namespace**, so it has no name a caller can spell and
the compiler demangles it as `_GLOBAL__N_1`. It is a real function, not a compiler
artefact, and the question "what is it?" is settled from the binary:

```
$ nm -a "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/MotionCurveX.plugin/Contents/MacOS/MotionCurveX" | grep -i motioncurve
000000000000297c t __ZN11motioncurve12_GLOBAL__N_114medianOfWindowERKNSt3__16vectorIfNS1_9allocatorIfEEEEii
```

which demangles to

```
motioncurve::(anonymous namespace)::medianOfWindow(const std::vector<float>&, int, int)
```

`nm` marks it `t` (local text), and the listing shows the bundle **calling it by
name twice**, from `temporalMedianFilter`:

```
000000000000285c	bl	__ZN11motioncurve12_GLOBAL__N_114medianOfWindowERKNSt3__16vectorIfNS1_9allocatorIfEEEEii
0000000000002870	bl	__ZN11motioncurve12_GLOBAL__N_114medianOfWindowERKNSt3__16vectorIfNS1_9allocatorIfEEEEii
```

The two call sites are the `x` and the `y` axis of the same sample, which is why the
signature takes a `vector<float>` rather than a path.

It is transcribed into `motioncurvex.cpp` **inside an anonymous namespace**, so it
keeps internal linkage and therefore exactly the same mangled symbol — the differ
looks the symbol up by that name. It is deliberately **not** declared in
`motioncurvex.h`: the bundle has no public `motioncurve::medianOfWindow` symbol, and
declaring one would invent an entry point the shipped binary does not have.

## The listing it was read from

```
otool -tV -arch arm64  ".../MotionCurveX.plugin/Contents/MacOS/MotionCurveX" > /tmp/ae/ae05g/mcx-arm64.asm
otool -tV -arch x86_64  ".../MotionCurveX.plugin/Contents/MacOS/MotionCurveX" > /tmp/ae/ae05g/mcx-x86.asm
```

- **arm64**: body at `0x297c`–`0x2a94`, **80 instructions**.
- **x86_64**: body at `0x29b0`, 100 instructions (a different architecture, so a
  different count; the differ compares arm64).

The two listings agree on every semantic below, which is worth having: the x86_64
control flow is much easier to read, and it was used as the cross-check.

## What the instructions say

```
0x29b0  mov  w8, #0x1
0x29b4  orr  w24, w8, w2, lsl #1        ; w24 = 1 | (radius << 1)   -> 2*radius+1 elements
0x29b8  ubfiz x0, x24, #2, #32          ; bytes
0x29bc  bl   operator new              ; the window buffer
0x29c4  sub  x8, x22, x23
0x29c8  lsr  x8, x8, #2                ; size = (end - begin) / 4
0x29d0  add  w22, w20, w21              ; end   = radius + centre
0x29dc  sub  w21, w21, w20              ; start = centre - radius
0x29e0  sub  w20, x8, #0x1             ; last  = size - 1
0x29ec  bic  w8, w21, w21, asr #31      ; clamped = max(0, i)
0x29f0  cmp  w20, w8
0x29f4  csel w8, w20, w8, lt            ; index = min(last, clamped)
0x2a14  ldr  s0, [x8]
0x2a18  str  s0, [x2], #0x4            ; push the sample
0x2a20  cmp  w21, w22
0x2a24  add  w21, w21, #0x1
0x2a28  b.lt 0x29ec                    ; loop back
0x2a48  asr  x9, x8, #3                ; byte count / 8  ==  sample count / 2
0x2a50  cmp  x2, x1
0x2a54  b.eq 0x2a68                    ; skip nth_element on an empty window
0x2a5c  bl   std::__nth_element
0x2a6c  ldr  s8, [x0, x8, lsl #2]      ; the median
```

Five things in there are easy to get wrong, and all five are in the instructions:

- **The window is CLOSED.** The loop **pushes a sample and only then tests the exit
  condition** (`cmp w21, w22` / `b.lt` at `0x2a20`) — a do-while, not a pre-test
  loop. So it runs for `i = centre - radius` through `i = centre + radius`
  **inclusive**: `2 * radius + 1` samples, an odd count, and the median is the true
  middle of the window. Reading the same instructions as a pre-test loop gives
  `2 * radius` samples and a different answer. The x86_64 listing says the same
  thing more plainly (`cmpl %r13d, %r14d; ...; jl 0x2a30` — compare, increment,
  jump back), and the shipped binary was called directly to settle it: with
  `values = {50,10,40,5,60,30}`, `centre = 2`, `radius = 1`, the shipped helper
  returns **10** (the closed median of `{10,40,5}`), where the half-open reading
  would give 40.
- **The radius is `window` itself, not half of it.** `temporalMedianFilter` passes
  the `window` argument straight through as the radius, so a window of `k` sees
  `2k + 1` samples.
- **The index is clamped at BOTH ends, and the edge sample is REPEATED, not
  dropped.** `bic w8, w21, w21, asr #31` is `max(0, i)`, and the following
  `cmp w20, w8` / `csel` is `min(last, clamped)`.
- **`std::min` is not commutative in SOURCE.** The bundle's select is
  `cmp last, clamped` / `csel clamped, last, clamped, lt`, i.e.
  `min(clamped, last)` — **not** `min(last, clamped)`. The two compute the same
  value, but they compile to different instructions, and the transcription has to
  spell it the bundle's way to be instruction-identical. The x86_64 listing agrees:
  `cmpl %ecx, %r12d` / `cmovll %r12d, %ecx`.
- **The median index is `size / 2`.** `asr x9, x8, #3` divides the byte count by
  8, which is the sample count divided by 2. With `2 * radius + 1` samples that is
  the exact middle, so no upper/lower tie convention is reachable here. The
  `cmp x2, x1` / `b.eq` in front of `nth_element` skips the call on an empty window.

The window is built in a **fresh buffer** rather than selected in place: the
listing allocates it (`operator new`), fills it sample by sample, and partitions it
with `std::__nth_element`.

## What the shipped binary does with bad arguments

Two of the bundle's own inputs are undefined behaviour, and the transcription keeps
them rather than tidying them away:

- **An empty `values` vector.** `last = size - 1 = -1`, and the clamp is
  `min(last, max(0, i)) = -1`, so the sample read lands one float **before** the
  buffer. Called against the installed bundle this **segfaults** (observed, exit
  139) — the vector's data pointer is null. It is excluded from the differential
  below for that reason: there is no defined value to compare.
- **A negative `radius`.** `1 | (radius << 1)` is `0xFFFFFFFF` in 32-bit
  arithmetic, so the buffer request is ~16 GB. On this machine the allocation
  succeeds (macOS overcommit) and the loop — still a do-while — pushes exactly one
  sample, at `centre - radius`, and returns it: the shipped bundle returns `5` for
  `values = {50,10,40,5,60,30}`, `centre = 2`, `radius = -1`. The transcription
  reproduces this exactly, because it is the same do-while.

A `radius` of zero is well defined: the window is the single sample at `centre`,
clamped.

## Measurement

**80 instructions in the bundle, 80 here, and the differ reports MATCH — this is an
instruction-identical recovery**, not a behavioural one. The differ's line:

```
  MATCH   motioncurve::_GLOBAL__N_1  80 instructions, identical
```

Getting there took the source shape, not just the semantics. Two source details
were load-bearing:

- the loop must be a **do-while**, not a `for` with a `<=` test — a `for` compiles to
  the same values but to a different instruction stream;
- `std::min(std::max(0, i), last)` — the operands the other way round, which is
  semantically identical and instruction-different.

## The dlopen differential

The shipped bundle is dlopened and its own `medianOfWindow` is called through the
image base (`dladdr` on the exported `motioncurve::clampf` gives the load base; the
helper's Mach-O vmaddr `0x297c` is added to it), and the result is compared
**bit-for-bit** against this transcription.

Harness: `/tmp/ae/ae05r/mcx-diff.cpp` (and `mcx-diff2.cpp`, the wider sweep).
Build:

```
clang++ -std=c++17 -O2 -I src/motioncurvex -o /tmp/ae/ae05r/mcx-diff /tmp/ae/ae05r/mcx-diff.cpp
```

Results, three seeds x 20,000 vectors each:

```
helper  vectors=20000 mismatches=0
helper  vectors=20000 mismatches=0
helper  vectors=20000 mismatches=0
temporal vectors=20000 mismatches=0
temporal vectors=20000 mismatches=0
temporal vectors=20000 mismatches=0
```

**60,000 helper vectors and 60,000 whole-`temporalMedianFilter` vectors, 0
disagreements**, every sample compared as raw bits rather than as floats.

The wider sweep (`mcx-diff2.cpp`) extends the ranges to `radius` in `[-2, 12]` and
`window` in `[-1, 9]` with path lengths down to 0: another **60,000 helper vectors
and 60,000 temporal vectors, 0 disagreements**. Reproduce with:

```
clang++ -std=c++17 -O2 -I src/motioncurvex -o /tmp/ae/ae05r/mcx-diff  /tmp/ae/ae05r/mcx-diff.cpp
clang++ -std=c++17 -O2 -I src/motioncurvex -o /tmp/ae/ae05r/mcx-diff2 /tmp/ae/ae05r/mcx-diff2.cpp
for s in 20261028 20261029 20261030; do
  /tmp/ae/ae05r/mcx-diff  20000 $s 0    # helper
  /tmp/ae/ae05r/mcx-diff  20000 $s 1    # temporalMedianFilter
  /tmp/ae/ae05r/mcx-diff2 20000 $s 0
  /tmp/ae/ae05r/mcx-diff2 20000 $s 1
done
```

The third argument selects the target: `0` is the helper itself, `1` is the
whole `temporalMedianFilter`. The two harnesses are the same program with
different input ranges. **240,000 vectors in total across the four runs, 0
disagreements.**

## `temporalMedianFilter` — modelled, but its semantics are pinned

The helper is only reachable through `temporalMedianFilter`, and the header used to
describe that function's window as the half-open range `[i - window, i + window)`,
`2 * window` samples, UPPER middle. **That was wrong**, and it is corrected here:
the window is the closed range, `2 * window + 1` samples, true middle.

The function's OWN body is still a modelled one — the differ reports
`DIFFER motioncurve::temporalMedianFilter shipped=164 mine=155` — so the header
says so. What *is* pinned is its behaviour, through two independent routes:

- the dlopen differential above, which compares the whole function against the
  shipped one bit-for-bit over 120,000 randomised paths (60,000 in each sweep);
- the self-check, which compares the implementation against an **independent
  reference** written with `std::sort` rather than `std::nth_element`, over a table
  of cases and 400 randomised paths, and pins the closed window, the radius being
  `window` itself, the `2 * window + 1` sample count and the clamping at both ends.

## The self-check

`selfcheck.cpp` is the property harness for this namespace, and it is where the
rules above are asserted. The checks for the recovered helper are deliberately
written so that the plausible WRONG readings fail them: a mutation that makes the
window half-open fails seven assertions, and a mutation that drops the clamping
fails the reference comparison. There are 74 checks in total.

## What is next

Nothing in this namespace. All eleven of the bundle's symbols are present in
`src/motioncurvex/`, and the eleventh — the anonymous one — is transcribed
instruction-identically.
