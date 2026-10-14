# shake — recovery notes

Source: `ShakeCapture.plugin` (bundle id `com.williamsmedia.aftereffects.ShakeCapture`)
installed at `/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/`. The original
C++ was destroyed on 2026-10-13; the binary is the only copy and it is unstripped.

## State

Two of the ten authored functions are recovered. `trackTranslation` alone is 960
instructions and is the effect's whole point, so this namespace is at its beginning.

| Function | Instrs | State |
|---|---|---|
| `clampf` | 5 | Recovered — **identical** (shared) |
| `median` | 43 | Recovered — 32 here (see below) |
| `ShakeProfile::~ShakeProfile` | 16 | Identified only |
| `extractJitter` | 36 | Identified only |
| `resampleProfile` | 50 | Identified only |
| `sampleLooped` | 92 | Identified only |
| `highPass` | 105 | Identified only |
| `normalizeUnitAmplitude` | 125 | Identified only |
| `accumulate` | 135 | Identified only |
| `movingAverage` | 155 | Identified only |
| `trackTranslation` | 960 | Identified only |

Only `clampf` and `median` need no recovered type: everything else takes `Vec2`,
`ShakeProfile` or `LoopMode`. `trackTranslation(float const*, float const*, int, int, int,
int)` takes six scalars and is therefore reachable without them, but at 960 instructions it
is a block of work rather than a step.

## `median` — the even case is NOT the true median

`median` takes its vector **by value**, so it reorders its own copy with `std::nth_element`
— partial selection, not a sort — and the caller's vector is untouched. That much is
verified.

The even case is where the shipped code goes wrong, and it is worth stating precisely
because the function looks correct:

```
nth_element(begin, begin + mid, end)      ; pins values[mid]; leaves the rest unordered
upper = values[mid]                       ; guaranteed: the (mid)th order statistic
if (odd) return upper
nth_element(begin, begin + mid - 1, begin + mid)   ; ONE ELEMENT — pins nothing
lower = values[mid - 1]                   ; whatever the first call left here
return (lower + upper) * 0.5
```

The second call's range is `[begin + mid - 1, begin + mid)`, a single element, so it is a
no-op. `values[mid - 1]` is therefore *any* value from the lower half — the first call only
guarantees that everything before `mid` is `<= values[mid]`, not where each one sits.

**Measured:** on a permutation of 0..999 the function returns **469.5**, where the true
median is 499.5. The odd case is exact, and the small even cases are exact because libc++
fully orders ranges that short — which is why the defect is invisible in any test that uses
four or six values.

This is recorded rather than fixed: correcting the range to `end` would change the effect's
output for every even-length measurement window, which is a change of behaviour, not a
recovery. The self-check asserts the property that does hold (the result lies within the
inputs' range) and the upper middle, and does not assert the arbitrary value, because that
would pin libc++'s partial ordering rather than this source.

## `median`'s codegen difference is the compiler's

The shipped bundle is 43 instructions and this transcription is 32, and the missing
eleven are exactly that second `nth_element` block: libc++ defines `__nth_element` inline,
so this build proves the one-element call is nothing and deletes it, while the shipped build
kept the call. Same arithmetic, same result, different inlining.

## Two medians in one fleet that disagree

`beatsnap::medianOfWindow` returns the **upper** middle for an even count and says so in its
own notes. `shake::median` returns the **average** of the pair — when it can. A rewrite that
substituted one for the other would change every even-length case, in opposite directions
depending on which way the substitution went.

## What is next

`extractJitter` (36) and `resampleProfile` (50) are the smallest outstanding functions and
both need one type each: `ShakeProfile` and `LoopMode`. `movingAverage` (155) and `highPass`
(105) are the jitter filter chain, and `trackTranslation` (960) is the motion estimate
everything else is downstream of.
