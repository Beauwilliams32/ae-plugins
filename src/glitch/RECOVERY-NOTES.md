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
| `apply<unsigned char>` | 1157 | Recovered — **behavioural**; 1410 instructions here |
| `apply<unsigned short>` | 1228 | Recovered — **behavioural**; 1478 instructions here |

`test/glitch.test.mjs` asserts that table against `tools/codegen-diff.mjs`, so the counts
cannot drift apart from the measurement.

**What "behavioural" means here, precisely.** The two `apply<T>` instantiations are not
copies of the shipped instruction stream: they are transcriptions of the shipped SEMANTICS,
and the semantics are verified against the shipped bundle itself. The self-check dlopens
`GlitchKit.plugin`, calls the bundle's own `glitch::apply` on the same frames with the same
parameters into a sentinel-filled buffer, and compares the two output buffers byte for byte.
Measured on this machine: **6202 vectors, 3395728 bytes compared, 0 mismatching vectors**,
of which 6148 vectors showed a real write — so the comparison was not vacuously comparing
two untouched buffers. A wider offline sweep — 710104 vectors, 1742420784 bytes compared,
0 mismatches, every mode and every shape covered — also matched; that sweep runs from a
scratch harness that is NOT committed, so the 6202-vector figure above is the one a
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

## What each of the five mode bodies does

Each body is a separate region of the listing, and each was read independently in the
8-bit and the 16-bit instantiation. The two depths carry the SAME structure with
different registers and different addresses, so a fact is only claimed below when it
was found in BOTH. Line numbers are the `L` column of the extractor output — the
listing lines of `/tmp/ae/glitch_uc.txt` for the 8-bit body and of
`/tmp/ae/glitch_us.txt` for the 16-bit body.

### Mode 1 — Block Displace

8-bit region `0x1248..0x156c` (L708..L909); 16-bit region `0x2554..0x28a4`
(L724..L931).

The frame is tiled into `blockSize` squares. Each block takes two displacements from
its own hash and copies its region out of the SOURCE, displaced. **The source is
displaced, not the destination.**

* The block size is read at `0x1248` L708 (`ldr w10, [x2, #0x8]`) and clamped to
  2..4096 at `0x124c..0x1260` L709..L714 with the `cmp`/`csel` pair. The block counts
  are two signed divisions, `sdiv w9, w10, w2` at `0x126c` L717 (16-bit: `0x2578`
  L733).
* The maximum displacement is `(amount * block) * 2`: `ucvtf s6, w2` at `0x12b8` L736
  followed by `fadd s6, s6, s6` at `0x12c0` L738 (16-bit: `0x25d0` L755 and `0x25d8`
  L757). It is a multiply and a doubling, NOT an fma.
* The block's own hash is mixed at `0x1348` L772 (`madd w10, w10, w9, w14`), and the
  salt is folded immediately afterwards — see the salt section below. 16-bit: `0x26b8`
  L813.
* The luminance sample is taken at the block's CENTRE, and only when bit 0 of the
  `luma` byte is set: `tbz w8, #0x0, 0x142c` at `0x13ac` L797 skips it. The centre
  coordinates are `add w10, w24, w22` at `0x13b0` L798 and `add w16, w25, w20` at
  `0x13d0` L806; the address is formed by `madd x16, x28, x16, x3` at `0x13fc` L817
  and the three colour bytes are loaded at `0x1404`, `0x140c` and `0x141c` L819..L826,
  with the final `fdiv s18, s18, s0` at `0x1428` L828. 16-bit: the branch is `0x2718`
  L837 and the three halfwords are loaded at `0x2738`, `0x2740` and `0x2750`
  L845..L851.
* **One draw decides whether the block moves at all.** The xorshift step starts at
  `0x142c` L829 (`eor w10, w26, w26, lsl #13`), the draw is `lsr w16, w10, #8` at
  `0x1438` L832 and `ucvtf s19, w16, #0x18` at `0x143c` L833, and it is compared
  against `amount * luma` — `fmul s20, s2, s18` at `0x1440` L834, `fcmp s19, s20` at
  `0x1444` L835, `b.pl 0x14a4` at `0x1448` L836. Two further draws give the two
  displacements, converted by `fcvtas` at `0x146c` L845 and `0x1494` L855 (16-bit:
  `0x27a0` L871 and `0x27c8` L881).
* The source index is built from the DISPLACEMENT: `add w3, w28, w30` at `0x14dc`
  L873 for the row and `add w9, w26, w10` at `0x1504` L883 for the column, each
  followed by signed clamps against the frame's extent. 16-bit: `add w0, w3, w1` at
  `0x2858` L917 and `add x8, x16, w8, sxtw #3` at `0x286c` L922.

### Mode 2 — RGB Channel Shift

8-bit region `0xac8..0xc7c` (L228..L337); 16-bit region `0x1db0..0x1f78`
(L235..L348).

Every pixel is written; there is no prior copy of the frame. R is taken from the
LEFT of the pixel, B from the RIGHT, and A and G from the pixel itself.

* The salt is folded at `0xb04` L243 (`sub w9, w9, #0x202`); 16-bit `0x1df4` L252.
* The shift magnitude is `(shift * amount) * jitter` — `fmul s2, s3, s2` at `0xb60`
  L266. The jitter comes from the hash, and the hash is guarded against zero at
  `0xb38` L256.
* The displacement is converted once, `fcvtas x14, s6` at `0xbd8` L296 (16-bit:
  `0x1edc` L310), and then used for BOTH directions: `sub w16, w9, x14` at `0xbe0`
  L298 walks LEFT and `add w14, w14, x9` at `0xbec` L301 walks RIGHT. **Both are
  32-bit operations and both wrap**, and the comparisons that follow are signed
  32-bit ones, so a wrapped index lands on column zero.

### Mode 3 — Scanline Jitter

8-bit region `0xc7c..0xeb8` (L337..L480); 16-bit region `0x1f78..0x21c4`
(L349..L495).

The frame is cut into `scanline`-tall bands. Each band takes ONE sideways
displacement and slides its rows. The source row is the row itself — only the
column moves.

* The displacement amplitude is `(amount * width) * 0.25`: `ucvtf s3, w15` at
  `0xc98` L344 and `fmul s3, s3, s4` at `0xcb0` L350 (16-bit: `0x1f94` L356 and
  `0x1fa0` L359).
* The salt is folded at `0xd28` L380 (`sub w9, w9, #0x101`); 16-bit `0x2050` L403.
* The luminance sample is taken half a band below the band's first row, in the
  middle column, and is likewise guarded by bit 0 of the `luma` byte.
* The displacement is converted by `fcvtas x9, s17` at `0xe00` L434 (16-bit: `0x211c`
  L454) and then SIGN-EXTENDED, so the sum with the column is formed in 64 bits
  while the upper bound is tested against the low 32.

### Mode 4 — Bad Signal

8-bit region `0xeb8..0x1248` (L480..L708); 16-bit region `0x21c4..0x2554`
(L496..L723).

A rolling band of displaced pixels, quantised to a coarse step. **The alpha channel
is never quantised.**

* The band half-width is `((amount * 0.2 + 0.05) * height) * 0.5`, floored at 1:
  `fmadd s3, s2, s4, s3` at `0xed4` L487, `fmul s3, s3, s4` at `0xedc` L489,
  `fmul s4, s3, s4` at `0xee4` L491 and `fmaxnm s4, s4, s3` at `0xeec` L493. The
  two constants are 0.05f (`0x3d4ccccd`) and 0.2f (`0x3e4ccccd`). 16-bit:
  `fmaxnm s4, s4, s3` at `0x21f8` L509.
* The magnitude scale is `(amount * width) * 0.12f`: `ucvtf s5, w15` at `0xef0`
  L494, `fmul s5, s2, s5` at `0xef4` L495 and `fmul s5, s5, s6` at `0xf04` L499,
  with 0.12f materialised as `0x3df5c28f` at `0xef8`..`0xf00` L496..L498.
* The quantisation step is `fma(amount, -250, 256) - 1`, floored at 1, and the grid
  size is `maxValue / step`: `mov w9, #0x43800000` at `0xf08` L500 is 256.0f,
  `mov w9, #-0x3c860000` at `0xf10` L502 is 0xc37a0000 = **-250.0f**, `fmadd s7, s2,
  s7, s6` at `0xf18` L504 multiplies and adds, `fadd s7, s7, s6` at `0xf20` L506
  subtracts 1.0f, `fmaxnm s7, s7, s3` at `0xf24` L507 floors it at 1, and `fdiv s7,
  s0, s7` at `0xf28` L508 divides. 16-bit: `0x2214` L516, `0x221c` L518, `0x2230`
  L523, `0x2234` L524. So at amount = 1 the step is 5 and the grid is 51 steps on
  8-bit, and 13107 steps on 16-bit.
* Each row's hash is keyed by the row XOR a constant: `mov w5, #0xb5` at `0xfd4` L551
  and `movk w5, #0xb16b, lsl #16` at `0xfd8` L552 give 0xb16b00b5, used at `eor w9,
  w2, w5` at `0x1058` L584 (16-bit: `mov w22, #0xb5` at `0x2354` L596 and `eor w8,
  w2, w22` at `0x23a4` L616).
* The quantisation is applied only when the amount is STRICTLY greater than 0.001f.
  The threshold is materialised as 0x3a83126f = 0.001f at `0x100c` L565 and
  `0x1010` L566, and tested per pixel at `fcmp s2, s22` / `b.le 0x1164` — `0x11c0`
  L674 and `0x11c4` L675 (16-bit: `0x2348` L593, `0x234c` L594). Below the
  threshold the three colour channels are copied straight through.
* Alpha is loaded raw and stored raw: `ldrb w9, [x15]` at `0x11b4` L671.
* The displacement is converted by `fcvtas x19, s23` at `0x1150` L646 and
  sign-extended by `sxtw x20, w19` at `0x1158` L648 (16-bit: `0x246c` L666).

### Mode 5 — Datamosh Smear

8-bit region `0x156c..0x1784` (L909..L1043); 16-bit region `0x28a4..0x2b9c`
(L936..L1126).

The frame is copied FIRST (`0x888..0xab8` in the 8-bit body, `0x1b6c..0x1c84` in the
16-bit body), and then rows are smeared rightward: one pixel is stretched over a run
of columns.

* The maximum smear length is `(amount * 5)` truncated: `fmul s5, s2, s5` at `0x158c`
  L917 and `fcvtzs w9, s5` at `0x1590` L918. The divisor is that length plus one,
  `add w2, w9, #0x1` at `0x15cc` L933. The maximum magnitude is halved, `fmul s3,
  s3, s4` at `0x1594` L919.
* The salt is folded at `0x1608` L948 (`add w9, w9, #0x101`); 16-bit `0x2968` L985.
* The hash is guarded against zero at `0x163c` L961 (`cmp w9, #0x0` / `csel w9, w13,
  w9, eq`).
* **The first draw skips the row entirely**: `fcmp s7, s2` at `0x1658` L968 and
  `b.ge 0x15d8` at `0x165c` L969 branch past the smear when the draw is not below
  the amount.
* The column index inside the run comes from an UNSIGNED division and its
  remainder: `udiv w9, w5, w2` at `0x166c` L973 and `msub w3, w9, w2, w5` at `0x1670`
  L974, and the remainder's sign bit is tested with `tbnz w3, #0x1f, 0x15d8` at
  `0x1674` L975. 16-bit: `udiv w8, w21, w3` at `0x29d4` L1012, `msub w22, w8, w3,
  w21` at `0x29d8` L1013 and `tbnz w22, #0x1f, 0x2928` at `0x29dc` L1014.

### The catch-all and the spread blend

An unknown mode — including mode 0 — lands on the plain copy: `0x1784` L1043 in the
8-bit body and `0x2b9c` L1126 in the 16-bit body.

The spread blend that mixes the mode's output back with the original input is at
`0x189c` L1113..`0x192c` L1149 in the 8-bit body and `0x2cb4` L1196 in the 16-bit
body. It computes `fma(in, 1 - spread, spread * full)`.

## The salt table: the folded 0x27d4eb2f constant

`hashKey`'s third argument `c` is the salt, and in every inlined copy inside
`apply<T>` the compiler FOLDED it into the constant that `hashKey` adds at the end.

`hashKey` itself ends with `add w8, w8, w10` where `w10` holds 0x27d4eb2f
(`0x19c4`..`0x19cc` in the shipped listing). When the compiler inlines the function
into `apply<T>` it cannot keep that add in place — the salt has to be folded into the
`madd` that carries `c` — so it materialises a DIFFERENT constant and then folds the
difference back with a single `sub` or `add` immediate.

The base constant the mode bodies use is **0x27d4ef33** (`mov w14, #0xef33` /
`movk w14, #0x27d4, lsl #16`), which is exactly `0x27d4eb2f + 0x404`. So:

* salt `0x101` (mode 1) — `sub w10, w10, #0x303` at `0x1350` L774, because
  0x404 - 0x303 = 0x101. 16-bit: `sub w12, w12, #0x303` at `0x26c0` L815.
* salt `0x202` (mode 2) — `sub w9, w9, #0x202` at `0xb04` L243, because
  0x404 - 0x202 = 0x202. 16-bit: `sub w8, w8, #0x202` at `0x1df4` L252.
* salt `0x303` (mode 3) — `sub w9, w9, #0x101` at `0xd28` L380, because
  0x404 - 0x101 = 0x303. 16-bit: `sub w8, w8, #0x101` at `0x2050` L403.
* salt `0x404` (mode 4) — **no adjustment at all**: the base constant already is
  0x27d4eb2f + 0x404, so `madd w10, w10, w12, w14` at `0xf58` L520 uses w14 =
  0x27d4ef33 unchanged (16-bit: `0x227c` L542 and `0x23cc` L626).
* salt `0x505` (mode 5) — `add w9, w9, #0x101` at `0x1608` L948, because
  0x404 + 0x101 = 0x505. 16-bit: `add w8, w8, #0x101` at `0x2968` L985.

So the mode salts are simply **the mode number times 0x101**: 0x101, 0x202, 0x303,
0x404, 0x505 for modes 1..5.

**Two readings of this table are wrong, and both were wrong in an earlier draft of
this file.**

1. The bare immediates `0x101`, `0x202` and `0x303` in the listing are NOT the
    salts. They are the FOLDED DELTAS, and reading them as salts gives mode 1 the
    salt 0x303, mode 2 the salt 0x202 and mode 3 the salt 0x101 — mode 1 and mode 3
    swapped, and mode 2 right only by coincidence. The salt is what is LEFT AFTER
    the fold, which is the section above.
2. The `sub w10, w10, #0x404` at `0x844` L67 in the TOP hash is NOT a salt at all.
    The top hash is `hashKey(hashInput, patternFrame(...), params.mode)`, and its
    third argument is the RAW `params.mode` value — 1, 2, 3, 4 or 5 — carried by
    `madd w10, w10, w7, w6` at `0x830` L62, where w6 is the mode read from
    `Params + 0x00` at `0x7f0` L46. The `0x404` there is only the constant-fold
    correction that turns 0x27d4ef33 back into 0x27d4eb2f, exactly as above. The
    16-bit build does the same at `0x1b24` L72. It is tempting to read "0x404" as
    "mode 4's salt"; it is not.

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

### A zero width ALSO RETURNS EARLY — the `ccmp` does not compare

```
ldp    w15, w16, [x0, #0x8]   0x738
cmp    w15, #0x1              0x73c
ccmp   w16, #0x1, #0x8, ge    0x740
b.lt   0x1948                 0x744
```

The `ccmp` only compares the HEIGHT when the width is already >= 1; otherwise it does NOT
compare, and installs the immediate `#0x8` as the WHOLE NZCV byte — N = 1, V = 0, Z = 0.
`b.lt` branches when N != V, and 1 != 0, so the branch IS TAKEN and the shipped code
returns EARLY for a zero width as well as for a zero height.

**An earlier draft of this file claimed the opposite** — that a zero width FALLS THROUGH
the guard into the body. That was wrong. It read `#0x8` as "greater-than", which is what
the immediate means for the `cmp` it stands in for, but `ccmp` installs the immediate as
the entire flags byte, and `b.lt` then reads N and V out of it. The claim was re-checked by
assembling the exact four-instruction sequence on this machine and running it: a zero
width, a zero height and a zero-by-zero frame all take the early return, and a 5x5 and a
1x1 frame do not.

The 16-bit build has the same guard over the same paired load (`ldp w9, w28, [x0, #0x8]` at
`0x1a24`, `ccmp w28, #1, #0x8, ge` at `0x1a2c`, `b.ge 0x1a54` at `0x1a30`) and the same
outcome. There the branch is taken when N == V, so the early-return path is the one where
the `ccmp` did NOT compare — N = 1, V = 0, so N != V and the branch is not taken and
control falls into the epilogue at `0x1a34`. The observable result is the same: nothing is
written for a degenerate frame.

### A NaN amount does NOT take the copy-and-return path

```
fcmp  s2, #0.0 / b.ls 0x9a0    0x794 / 0x798
```

`b.ls` is the UNSIGNED lower-or-same branch, and `fcmp` sets the flags as if the float were
compared unsigned: a negative value gives C=0 (taken), zero gives Z=1 (taken), and an
unordered (NaN) comparison gives C=1, Z=0 (NOT taken). So a NaN amount runs the mode body
with a NaN amount. A `!(x > 0)` rewrite would wrongly capture the NaN, which is why the
recovered source writes the test as `<=`.

### The amount and spread clamps are a REAL 0..1 clamp — and the zero comes out of a register trick

```
ldr    s1, [x2, #0x4]              0x764    ; the amount
fmov   s3, #1.00000000             0x768
fcmp   s1, s3                      0x76c
fcsel  s2, s3, s1, gt              0x770    ; s2 = (amount > 1) ? 1 : amount
fcmp   s1, #0.0                    0x774
movi.2d v1, #0000000000000000      0x778    ; <-- the whole trick
fcsel  s2, s1, s2, mi              0x77c    ; s2 = (amount < 0) ? s1 : s2
```

**The `movi.2d v1, #0` at 0x778 is the instruction that supplies the 0.0f constant, and it
is IN THE SAME REGISTER that held the amount.** A 64-bit vector move zeroes BOTH halves of
V1, so S1 — the amount — is destroyed at 0x778. The `fcsel s2, s1, s2, mi` at 0x77c then
selects that zero, not the original negative amount. So the clamp IS a 0..1 clamp:
negative becomes 0.0f.

The spread is clamped the same way at 0x780..0x790 (`ldr s4, [x2, #0x20]` / `fcmp s4, s3` /
`fcsel s3, s3, s4, gt` / `fcmp s4, #0.0` / `fcsel s1, s1, s3, mi`), reusing the same zeroed
V1.

**MEASURED, not assumed.** That exact eight-instruction sequence was assembled and run on
this machine: `clamp(-2.5) = 0`, `clamp(0.25) = 0.25`, `clamp(2.5) = 1`, and
**`clamp(NaN) = NaN`** — because `fcmp` sets NZCV = 0b0011 for an unordered comparison, and
the `mi` condition (N set) is therefore false, so the NaN passes straight through the
lower bound. The upper bound is not tested for unordered either: `fcmp s1, s3` with a NaN
also sets C = 1, Z = 0, so `gt` is false and the NaN survives that `fcsel` too.

**The 16-bit build has the identical pattern**, independently: `ldr s1, [x2, #0x4]` at
0x1a54 and `movi.2d v1, #0` at **0x1a68**, followed by the same `fcsel`. The mode-4 band
clamp does the same thing with its own register: `movi.2d v17, #0` at 0xfe8 feeds the
`fcsel` pair at 0x103c..0x1054, so the band's low side is 0.0f as well.

**An earlier draft of this file claimed the lower test was made against the ORIGINAL
value and that "a negative amount stays negative".** That was wrong, in this file and in
`glitch.h` and in `selfcheck.cpp` alike; all three are corrected now. The OBSERVABLE
behaviour is unchanged either way — a negative amount and a zero amount both take the
same early copy-and-return — so the differential could not have caught it. Only the
reading was wrong.

### The mode-4 band clamp is a 0..1 clamp too

```
fcmp   s2, s22 / fcsel ... / fcsel ...
```

At 0x103c..0x1054 the band value `1 - d / bandHalf` is clamped with the same pair of
`fcsel`s and the same zeroed V17 (`movi.2d v17, #0` at 0xfe8), so the band's low side is
0.0f and not a negative band.

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
  the loops differently, so the instruction stream comes out at 1410 and 1478.
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
