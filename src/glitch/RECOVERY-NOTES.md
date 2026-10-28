# glitch — recovery notes

Source: `GlitchKit.plugin` (bundle id `com.williamsmedia.aftereffects.GlitchKit`) installed
at `/Applications/Adobe After Effects 2026/Plug-ins/Williams Media/`. The original C++ was
destroyed on 2026-10-13; the binary is the only copy and it is unstripped.

## State — complete

All SIX authored functions are recovered and the namespace carries its `NAMESPACE-COMPLETE`
marker. Nothing is absent.

| Function | Instrs | State |
|---|---|---|
| `clampf` | 5 | Recovered — **instruction-identical** |
| `clampi` | 5 | Recovered — **instruction-identical** |
| `hashKey` | 34 | Recovered — **instruction-identical** |
| `patternFrame` | 7 | Recovered — **instruction-identical** |
| `apply<unsigned char>` | 1157 | Recovered — **behavioural**; 1409 instructions here |
| `apply<unsigned short>` | 1228 | Recovered — **behavioural**; 1477 instructions here |

`test/glitch.test.mjs` asserts that table against `tools/codegen-diff.mjs`, so the counts
cannot drift apart from the measurement.

**What "behavioural" means here, precisely.** The two `apply<T>` instantiations are not
copies of the shipped instruction stream: they are transcriptions of the shipped SEMANTICS,
and the semantics are verified against the shipped bundle itself. The self-check dlopens
`GlitchKit.plugin`, calls the bundle's own `glitch::apply` on the same frames with the same
parameters into a sentinel-filled buffer, and compares the two output buffers byte for byte.
Measured on this machine: **5950 vectors, 3266368 bytes compared, 0 mismatching vectors**,
of which 5896 vectors showed a real write — so the comparison was not vacuously comparing
two untouched buffers. A wider offline sweep — 710104 vectors, 1742420784 bytes compared,
0 mismatches, every mode and every shape covered — also matched; that sweep runs from a
scratch harness that is NOT committed, so the 5950-vector figure above is the one a
reader can reproduce from this repository.

The instruction counts differ because the recovered source is structured differently —
loop shapes, register allocation, and where the compiler chose to spill. The differ
therefore reports both as `DIFFER` with `different instruction counts`, and that is the
honest measurement. **The differ's tally for this namespace is 4 identical, 2 differing,
0 not implemented.**

The harness was proved sensitive rather than assumed to be: mutating the recovered source
and re-running the same sweep produced disagreements every time.

| Mutation | Vectors | Mismatching |
|---|---|---|
| `kLumaG` 0.587f → 0.588f | 710104 | 18083 |
| `std::round` → `std::nearbyint` in the mode-4 quantiser | 710104 | 13084 |
| (none — the recovered source) | 710104 | **0** |

Both mutations were re-measured against the source as committed here, so the sensitivity
claim belongs to THIS source and not to an earlier draft of it.

### What is NOT claimed

No After Effects host has ever loaded a rebuilt GlitchKit bundle. The differential proves
the recovered `apply<T>` agrees with the shipped one **as a function**, on this machine,
under this ABI. It proves nothing about AE accepting a rebuilt plugin, about the plugin
registering, or about the recovered `apply<T>` being wired into an effect entry point —
no such wiring exists in this repository for this namespace. Nothing in this file is a
host-acceptance claim.

## Listing citations

Everything below was read from the arm64 disassembly of the shipped bundle.

| What | Where |
|---|---|
| Full bundle disassembly | `/tmp/ae/glitchkit.asm` |
| `apply<unsigned char>` body | `/tmp/ae/glitch_uc.txt` — `__ZN6glitch5applyIhEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEjf` at `0x738..0x1948`, 1157 instructions |
| `apply<unsigned short>` body | `/tmp/ae/glitch_us.txt` — `__ZN6glitch5applyItEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEjf` at `0x1a04..0x2d30`, 1228 instructions |
| `hashKey` | `__ZN6glitch7hashKeyEjjj` at `0x197c`, 34 instructions |
| `patternFrame` | `__ZN6glitch12patternFrameEjf` at `0x1960`, 7 instructions |
| `clampf` | `__ZN6glitch6clampfEfff` at `0x194c`, 5 instructions |
| `clampi` | `__ZN6glitch6clampiEiii` at `0x2d34`, 5 instructions |

Addresses in the rest of this file are the 8-bit body's unless a 16-bit address is given.

## The two structs, with their offset evidence

The names are not in the binary; the layouts are. `View<T>` is 24 bytes and `Params` is
0x24 bytes, and the header carries a `static_assert` for every offset so a layout change
cannot pass silently.

### `View<T>` — 24 bytes

```
ldr    x16, [x0]              +0x00  data
ldp    w15, w16, [x0, #0x8]   +0x08  width, +0x0c height  (one PAIRED load)
ldrsw  x15, [x0, #0x8]        +0x08  width, sign-extended -> int
ldrsw  x21, [x0, #0xc]        +0x0c  height, sign-extended -> int
ldr    x22, [x0, #0x10]       +0x10  rowBytes, 64-bit
```

**The paired load is the trap in this struct.** The 16-bit build's prologue is
`ldp w9, w28, [x0, #0x8]` at `0x1a24` — ONE instruction fetching the width AND the height.
A scanner that only looks for an explicit `[reg, #offset]` sees `+0x8` and never learns that
`+0x0c` is read too. An earlier draft of `glitch.h` claimed on that basis that the 16-bit
build "reads the input at only 0x0 and 0x8"; it does not, and the header has been corrected.

The 16-bit build also spills both view pointers before it spills anything else —
`stp x1, x0, [sp, #0x18]` at `0x1a20`, the OUTPUT view in the low slot and the INPUT view
in the high one — and reads the strides back through the spills:

```
ldr  x8, [sp, #0x20]   0x1c8c   the input view pointer again
ldr  x0, [x8, #0x10]   0x1c94   the input's rowBytes
ldr  x8, [sp, #0x18]   0x1c98   the output view pointer again
ldr  x13, [x8]         0x1c9c   the output's data
ldr  x2, [x8, #0x10]   0x1ca0   the output's rowBytes
```

and LATER in the same body the INPUT view is reloaded from its own spill slot:

```
ldr  x8, [sp, #0x20]   0x28e4   the input view pointer again
ldr  x0, [x8]          0x28e8   the input's data
ldr  x1, [x8, #0x10]   0x28f0   the input's rowBytes
```

The two are reloaded TOGETHER at `0x2ba4` — `ldp x14, x8, [sp, #0x18]` gives x14 = the
OUTPUT view (low slot) and x8 = the INPUT view (high slot).

So both instantiations read the input at all four offsets and the output at `+0x00` and
`+0x10`. `test/glitch.test.mjs` follows the spill rather than assuming `x0` and `x1` keep
their argument roles for the whole body.

**The output view's WIDTH AND HEIGHT are never read, by either build.** The loops are
driven entirely by the input's dimensions. A caller handing over a smaller output view
gets writes at the input's stride rather than a clamp or an error. The self-check pins
this, and the differential exercises it with a deliberately undersized output view.

**`rowBytes` is a stride in ELEMENTS of T, not in bytes.** The 16-bit build doubles it when
it forms a byte address — `lsl x0, x0, #1` at `0x1ce4` on the input stride and
`lsl x2, x2, #1` at `0x1cec` on the OUTPUT stride (and again at `0x28f4` on the input
stride and `0x2c00` on the output stride). A tightly packed row is therefore
`width * 4` for BOTH depths; reading it as `width * 4 * sizeof(T)` puts the 16-bit build
half a row out of step.

### `Params` — 0x24 bytes, nine fields

```
+0x00  int    mode          the one-based mode selector
+0x04  float  amount        capped at 1
+0x08  int    blockSize     clamped to 2..4096
+0x0c  int    hashInput     one of the hashKey inputs
+0x10  float  rate          the TIME fed to patternFrame
+0x14  float  shift         the RGB channel shift distance in pixels
+0x18  int    scanline      the band height for Scanline Jitter, clamped 1..4096
+0x1c  BYTE  luma           bit 0 is "drive by luminance"
+0x20  float  spread        capped at 1
```

The nine reads are NOT a prologue. Each happens on its own dispatch branch, and each is
before the branch that repurposes `x2`:

```
ldr  s1, [x2, #0x4]   0x764  amount        |  ldr  s1, [x2, #0x4]   0x1a54 amount
ldr  s4, [x2, #0x20]  0x780  spread        |  ldr  s4, [x2, #0x20]  0x1a70 spread
ldrb w8, [x2, #0x1c]  0x7a4  luma          |  ldrb w10, [x2, #0x1c] 0x1a94 luma
ldr  s3, [x2, #0x10]  0x7e8  rate          |  ldr  s3, [x2, #0x10]  0x1ad8 rate
ldr  w10, [x2, #0xc]  0x7ec  hashInput     |  ldr  w13, [x2, #0xc]  0x1adc hashInput
ldr  w6, [x2]         0x7f0  mode          |  ldr  w11, [x2]        0x1ae0 mode
ldr  s3, [x2, #0x14]  0xacc  shift         |  ldr  s3, [x2, #0x14]  0x1db4 shift
ldr  w9, [x2, #0x18]  0xc80  scanline      |  ldr  w8, [x2, #0x18]  0x1f7c scanline
ldr  w10, [x2, #0x8]  0x1248 blockSize     |  ldr  w11, [x2, #0x8]  0x2554 blockSize
```

`x2` is the `&Params` pointer for every one of those reads. In the 8-bit build it is
repurposed afterwards, as a copy cursor, in the three copy paths and in mode 4 — `lsl x2,
x15, #2` at `0x88c`, `ldr x2, [x1]` at `0x9dc` with `strb w17, [x2], #1` at `0xa24`,
`mov x2, #0x0` at `0xeb8`, and `ldr x2, [x1]` at `0x17c0`.

**The 16-bit build repurposes `x2` as well** — an earlier draft of this file claimed it
did not, and that was wrong. Every write to `x2` in the 16-bit listing is:

```
mov  x2, #0x0        0x1b6c   mode-5 body counter   (incremented at 0x1bdc)
ldr  x2, [x8, #0x10] 0x1ca0   the OUTPUT's rowBytes (x8 = the output view)
lsl  x2, x2, #1      0x1cec   the OUTPUT's byte stride
mov  x2, #0x0        0x1ebc   modes 1/2 body counter (incremented at 0x1f30)
mov  x2, #0x0        0x21c4   mode-4 entry counter  (incremented at 0x2364)
mov  x2, #0x0        0x28a4   mode-4 body counter   (incremented by 2 at 0x292c)
lsl  x2, x8, #1      0x2bf8   the INPUT's byte stride (x8 = the input's rowBytes)
```

So in the 16-bit build `x2` is a loop counter in three of the mode bodies and a
byte-scaled stride in the other two; the strides it holds are the OUTPUT's at `0x1cec`
and the INPUT's at `0x2bf8`. This is the evidence that the nine `Params` reads are
NOT a prologue: `x2` stops being the `&Params` pointer long before the body ends.

## The mode dispatch — five bodies, one-based

```
cmp w6, #0x2 / b.le 0xab8      0x868
cmp w6, #0x3 / b.eq 0xc7c      0x870
cmp w6, #0x4 / b.eq 0xeb8      0x878
cmp w6, #0x5 / b.ne 0x1784     0x880
cmp w6, #0x1 / b.eq 0x1248     0xab8
cmp w6, #0x2 / b.ne 0x1784     0xac0
```

```
1 -> 0x1248  Block Displace      2 -> 0xac8  RGB Channel Shift
3 -> 0xc7c   Scanline Jitter     4 -> 0xeb8  Bad Signal
5 -> 0x888   Datamosh Smear      anything else (including 0) -> 0x1784, a plain copy
```

The modes are ONE-BASED. The recovered popup is "Block Displace|RGB Channel Shift|Scanline
Jitter|Bad Signal|Datamosh Smear", and a `PF_ADD_POPUP` is one-based, so choice N is value
N with no remapping. A zero-based reading would make every mode off by one.

## Behaviours a tidy rewrite would "fix", and must not

These are the places where the shipped code does something a well-meaning rewrite would
change, and where the recovered source has to match the shipped code rather than the
intent. Each was settled by the differential, not by taste.

### `frinta` rounds ties AWAY FROM ZERO, not to even

The mode-4 quantiser rounds with `frinta`. On AArch64 the four rounding instructions are
easy to confuse: `frintn` is ties to EVEN, `frintp` is ties toward +infinity, `frintm` is
ties toward -infinity, `frintz` is toward zero, and `frinta` is ties AWAY FROM ZERO.
`std::round` is the C spelling of `frinta`. `std::nearbyint` is `frintn` — and using it
disagrees with the shipped bundle on **13084 of 710104** differential vectors. The source
uses `std::round` and says why.

The same rule governs the shipped's `fcvtas` (round to nearest, ties away, into a signed
integer), which is why the recovered source spells that conversion out rather than
delegating to `llroundf`, whose NaN and overflow behaviour is unspecified.

### A zero width FALLS THROUGH the empty-frame guard

```
ldp    w15, w16, [x0, #0x8]   0x738
cmp    w15, #0x1              0x73c
ccmp   w16, #0x1, #0x8, ge    0x740
b.lt   0x1948                 0x744
```

The `ccmp` only compares the HEIGHT when the width is already >= 1; otherwise it sets the
flags to `#0x8` (greater-than), which the `b.lt` does not take. So the shipped code
returns EARLY for (width >= 1 && height <= 0) and FALLS THROUGH into the body for a zero
width. It does not matter observably — with a zero width or a zero height every loop in
every mode body is empty, so the bundle writes NOTHING to the output for any degenerate
shape, in any mode, with any amount and spread — but it was MEASURED against the shipped
bundle rather than assumed, and the recovered source's guard reproduces the observable
outcome.

The 16-bit build has the same guard over the same paired load (`ldp w9, w28, [x0, #0x8]` at
`0x1a24`, `b.ge 0x1a54` at `0x1a30`) and the same behaviour.

### A NaN amount does NOT take the copy-and-return path

```
fcmp  s2, #0.0 / b.ls 0x9a0    0x794 / 0x798
```

`b.ls` is the UNSIGNED lower-or-same branch, and `fcmp` sets the flags as if the float were
compared unsigned: a negative value gives C=0 (taken), zero gives Z=1 (taken), and an
unordered (NaN) comparison gives C=1, Z=0 (NOT taken). So a NaN amount runs the mode body
with a NaN amount. A `!(x > 0)` rewrite would wrongly capture the NaN, which is why the
recovered source writes the test as `<=`.

### The amount and spread clamps test the ORIGINAL value

```
fcmp  s1, s3 / fcsel s2, s3, s1, gt    0x76c / 0x770    ; cap at 1
fcmp  s1, #0.0 / fcsel s2, s1, s2, mi   0x774 / 0x77c    ; but the NEGATIVE wins
```

The second test is against the ORIGINAL value, not against the capped one, so a negative
amount stays negative and takes the early copy-and-return. The observable difference from a
real 0..1 clamp is nil — a negative value and a zero value take the same early return — but
the instruction sequence is not a clamp, and the header says "capped at 1" rather than
"clamped to 0..1".

### `hashKey` returns 1 when two intermediate stages agree

The tail is `cmp w9, w8 / csinc w0, w10, wzr, ne` — a conditional increment. The mixed
value normally, but 1 when the two intermediate stages are equal. What that guard was FOR
is not recoverable from the code, so it is reproduced rather than rationalised. An earlier
draft "simplified" it to `return h;` on the assumption it was a zero-guard, which the
comparison does not support.

### `patternFrame` multiplies in DOUBLE, and its guard is a strict `> 0`

```
ucvtf  d1, w0 / fcvt d2, s0 / fmul d1, d1, d2 / fcvtzu w8, d1
fcmp   s0, #0.0 / csel w0, w8, wzr, hi
```

Doing the multiply in float would round before the truncation and pick the wrong frame at
some time values (16777217 is the witness the self-check uses). The guard is `hi`, i.e.
greater-than OR UNORDERED, so a NaN time takes the product branch — and `fcvtzu(NaN)` is 0,
so the answer is the same as the zero branch. The recovered source writes the guard as a
negated `<=` so the compiler emits `hi` rather than `gt`, which keeps the codegen identical
instead of relying on that coincidence.

## Why the two `apply<T>` bodies are behavioural and not instruction-identical

The four scalar helpers came out of the transcription byte-for-byte. The two `apply<T>`
bodies could not, and the reason is worth stating plainly rather than hiding behind
"semantics match":

* The bodies are 1157 and 1228 instructions of loop-heavy code with four different mode
  bodies, and the recovered source is structured as five separate helper functions rather
  than one 1157-instruction routine. The compiler then allocates registers and schedules
  the loops differently, so the instruction stream comes out at 1409 and 1477.
* The float arithmetic had to be pinned to the shipped's ROUNDING, not just its algebra.
  Every shipped `fmadd` is an explicit `std::fma`, every shipped multiply-and-add pair is
  a separate multiply and add, and `fp contract(off)` is set around the implementation so
  the compiler cannot silently fuse a shipped multiply-and-add into an `fma` with
  different rounding. Without that, the quantiser and the spread blend drift.
* The shipped's integer conversions are spelled out (`fcvtas` round-half-away saturating,
  `fcvtzs` truncating saturating, `NaN` to 0) rather than delegated to `llroundf` or
  `static_cast<int>`, whose NaN and overflow behaviour is undefined in C++.

The result is a function that agrees with the shipped one on every vector measured, and an
instruction stream that does not. That is a behavioural recovery, and the header, this
file, and `test/glitch.test.mjs` all say so.
