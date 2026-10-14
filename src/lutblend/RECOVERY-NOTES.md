# LUTBlend recovery notes

Per-function derivation records for `lutblend::`. The three recovered functions
carry their own commentary inline in `lutblend.cpp`; this file holds what is
*known about the one function that is not recovered yet*, so the next pass does
not have to rediscover it.

## Status

| Function | Instructions | State |
|---|---|---|
| `clampf` | 5 | recovered |
| `lerpf` | 12 | recovered |
| `luma709` | 12 | recovered |
| `CubeLUT::valid` | 29 | recovered |
| `sampleLUT` | — | recovered |
| `applyLUTBlend` | 112 | recovered |
| `makeIdentity` | 269 | recovered |
| `parseCube` | **1924** | **identified only — not implemented** |

`parseCube` is the largest function in the fleet, roughly four times the size of
TypeRiser's `parseSrt`. Nothing is stubbed for it. What follows is a specification
derived from its string literals and call graph, which is enough to write it, with
the parts that still need disassembly reading marked as such.

## `parseCube` — recovered facts

Symbol: `__ZN8lutblend9parseCubeERKNSt3__112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEPNS_7CubeLUTEPS6_`

Signature: `bool parseCube(const std::string& text, CubeLUT* out, std::string* error)`

The return type is not inspectable from the symbol, but the message inventory
below plus the `std::string*` third argument make the bool-plus-error-string shape
the only coherent reading.

### The file grammar

Seven recognised directives, in the order the keyword dispatch tests them
(8-byte literal compares; the chain is legible at 0x1234 onward, where
`0x4c544954`+`0x45` is the compare for `TITLE`):

| Directive | Arity |
|---|---|
| `TITLE` | free text |
| `LUT_3D_SIZE` | exactly one value, an integer in `[2, 256]` |
| `LUT_1D_SIZE` | exactly one value, an integer in `[2, 256]` |
| `DOMAIN_MIN` | three numeric values |
| `DOMAIN_MAX` | three numeric values |
| `LUT_3D_INPUT_RANGE` | two numeric values |
| `LUT_1D_INPUT_RANGE` | two numeric values |

Anything else that is not a data row is rejected. Data rows are three numeric
values each.

### The exact error strings

Recovered by resolving every string literal `parseCube` references, so this list
is complete rather than assembled from the interesting-looking ones:

```
line <N>: duplicate LUT size declaration
line <N>: <keyword> needs 2 values
line <N>: <keyword> needs 3 values
line <N>: <keyword> needs exactly one value
line <N>: <keyword> is not an integer
line <N>: <keyword> has a non-numeric value
line <N>: LUT size <N> out of range [2,256]
line <N>: data before LUT_1D_SIZE/LUT_3D_SIZE
line <N>: non-numeric data value '<token>'
line <N>: unrecognised keyword or malformed data row '<token>'
expected <N> data rows, got <N>
missing LUT_1D_SIZE/LUT_3D_SIZE
degenerate domain: DOMAIN_MAX must exceed DOMAIN_MIN
```

Two of these carry no `line <N>:` prefix — the data-row count check and the
missing-size check — because they are whole-file checks made after the loop.

The line prefix is built with `to_string(int)` and `string::insert`, and there are
exactly twelve `to_string(int)` call sites against twelve line-prefixed messages,
which is a useful cross-check that the inventory is complete.

### Validation rules these establish

* **A LUT size must be an integer in `[2, 256]`.** The upper bound of 256 is a
  recovered fact, not an assumption, and it is a rule `CubeLUT::valid()` alone
  does not express — `valid()` only checks `size >= 2`. This is the first place
  the 256 limit appears.
* `DOMAIN_MAX` must be **strictly greater** than `DOMAIN_MIN` on each axis;
  equal bounds are rejected as degenerate.
* Declaring `LUT_3D_SIZE` or `LUT_1D_SIZE` twice is an error, not a last-wins
  overwrite.
* A data row appearing before any size declaration is an error.
* Data rows are counted and compared against the size (`expected N data rows,
  got M`), so a truncated or over-long file is rejected rather than silently
  padded or ignored.
* `TITLE` is accepted and has no arity requirement, consistent with it being the
  only directive whose content is free text.

### Call graph

```
49 x operator delete          string temporaries, one per line and per error path
21 x string::append(const char*)
14 x string::operator=
12 x to_string(int)           the line-number prefixes
12 x string::insert(size_t, const char*)   same
10 x string::append(const char*, size_t)
 9 x strtod                   every numeric field; 3 data values + domains + ranges
 4 x locale::~locale
 4 x isspace/ctype facet      tokenising
 3 x vector<float>::push_back the data payload, pushed one float at a time
 2 x istringstream ctor
 2 x operator>> (basic_istream extractor)
 1 x getline
 1 x string::erase
 1 x memchr                    likely the whitespace scan for tokenising
```

The three `push_back` calls against the ~9 `strtod` sites fit the shape: the
extractor sites parse directive arguments, and data rows go through the
`push_back` path into `CubeLUT::data`.

### The reading loop and tokeniser — RECOVERED

The whole of Phase 1 (0xe48..0xf80) is now read, which closes two of the open
questions outright:

```
getloc / use_facet<ctype> / widen('\n')      ; the delimiter, i.e. plain getline
getline(is, line, widened)                    ; line buffer at sp+0x220
<stream state test> -> exit loop
line ends with 0x0d ? drop the last byte      ; CRLF, both the SSO and heap paths
line.find('#') != npos ? line.erase(pos,npos) ; '#' starts a comment, to end of line
++lineNo                                       ; w21, incremented AFTER the strips
istringstream ls(line, ios::in)                ; w2 = 8 == ios::in
while (ls >> tok) tokens.push_back(tok)        ; vector<string> at sp+0xd0
```

So tokenising is whitespace-delimited `operator>>` on the stripped line, which
means a directive's arguments must be on the SAME line as the keyword, and
trailing `# comments` are removed before tokenising. The CR strip happens BEFORE
the comment strip, so a `\r` after a comment is still removed with the comment.

**`TITLE` is ignored entirely.** Its dispatch arm is length 5, matching `TITLE`,
and it branches to the same block that an empty token list branches to — a
`mov w24, #2` followed by a jump to the loop's continue point. There is no arity
check and no validation of its content. That is a closed question, not a guess.

An empty token list (a blank line, or a line that was only a comment) also jumps
straight to "next line" without dispatching.

### Directive dispatch — RECOVERED

The dispatch compares the FIRST token's length, then its bytes, and clang emits
the chain twice: once reading the SSO buffer inline, once via the heap data
pointer. Lengths and match strings:

| Length | Token | Entry |
|---|---|---|
| 5 | `TITLE` | ignored (next line) |
| 10 | `DOMAIN_MIN` / `DOMAIN_MAX` | 0x1528 / 0x142c / 0x187c |
| 11 | `LUT_3D_SIZE` | 0x1270 |
| 11 | `LUT_1D_SIZE` | 0x12e8 |
| 18 | `LUT_3D_INPUT_RANGE` | 0x13ec |
| 18 | `LUT_1D_INPUT_RANGE` | 0x139c |
| other | — | error path |

The three length-10 entries are NOT three handlers. They are three different ways
into ONE decision sub-tree, which is why the SSO and heap copies of the outer
chain appear to disagree. The sub-tree:

```
0x189c  ldr  x8, [x9] / rev x8, x8      ; bytes 0-7, byte-swapped
0x18a4  x10 = 0x444f4d41494e5f4d       ; "DOMAIN_M" in big-endian byte order
0x18b4  cmp  x8, x10 / b.ne 0x19b4      ; not DOMAIN_M at all
0x18bc  ldrh w8, [x9, #0x8] / rev / lsr #16   ; bytes 8-9
0x18c8  cmp  w8, #0x494e                ; "IN"
0x18d4  mov  w8, #0x0 / b 0x1c4c        ; MIN  -> mode 0
0x1c3c  cmp  x8, #0x494e / mov w8,#-1 / cneg w8,w8,hs   ; MAX -> mode != 0
0x1c4c  cmp  w8, #0x0
0x1c50  ldp  x9, x8, [sp, #0x10]        ; x9 = &minR, x8 = &maxR
0x1c54  csel x19, x9, x8, eq            ; x19 = mode==0 ? &minR : &maxR
```

So `x19` is a SELECTED base pointer, and the handler that follows parses the three
value tokens into `x19[0..2]` — `minR/minG/minB` for `DOMAIN_MIN`, `maxR/maxG/maxB`
for `DOMAIN_MAX`. There is no inconsistency and no bug; the previous revision of
this file recorded it as an unresolved defect, which was wrong.

Worth recording HOW that wrong conclusion was reached, because the trap is
general: `&minR` and `&maxR` are stored adjacent at `sp+0x10` and `sp+0x18`, and
they are read back with a single `ldp x9, x8, [sp, #0x10]`. A grep for
`ldr x\?, [sp, #0x10]` finds nothing and makes it look as though `&minR` is never
used. Grepping for one load width when the compiler emits `ldp` will silently
invert a conclusion — check for the pair load.

### The working value is a local CubeLUT

`parseCube` does not write through `out` as it goes. It builds a local `CubeLUT`
at `x29-0xc8`, and the prologue initialises it field by field
(0xd70..0xd98) — which is also where the struct's DEFAULT MEMBER INITIALISERS
were recovered from:

```
is3D = true, size = 0, min = (0,0,0), max = (1,1,1), data empty
```

The offsets land exactly on `CubeLUT`'s layout, so this is a default-constructed
`CubeLUT`, not a hand-filled aggregate. `CubeLUT` in the header had `max = 0.0f`
and `is3D = false`; both were corrected to match. `max = 0` would have made a
default-constructed LUT read as degenerate under `valid()`.

This also means a failed parse can leave the caller's `out` untouched, since
nothing is written through the pointer until the local is complete — but that is
inferred from the shape, not read, and is listed below.

### Value parsing — RECOVERED

The LUT-size range check is confirmed, and it confirms the message literally:

```
fcvtzs w28, s0            ; truncate the parsed double to int
sub    w8, w28, #0x2
cmp    w8, #0xff          ; UNSIGNED
b.hs   <"out of range [2,256]">
str    w28, [x29, #-0xc4] ; lut.size = size
```

`(unsigned)(size - 2) < 255` is taken as valid, i.e. `size - 2 <= 254`, i.e.
**`2 <= size <= 256`** — exactly the bound the message names. Size is truncated
with `fcvtzs` rather than rounded, so `LUT_3D_SIZE 33.9` would be 33; whether that
is intended or a latent bug is not recoverable from the code.

Every numeric field goes through the same three-part acceptance test, visible in
the data-row path at 0x1ec8:

```
strtod(tok, &endptr)
endptr == tok               -> reject   ; nothing was consumed at all
*endptr != '\0'            -> reject   ; trailing garbage after the number
(|d| < +inf) == false       -> reject   ; inf and NaN are rejected explicitly
```

The finiteness test is `and x8, x8, #0x7fffffffffffffff` then a compare against
`0x7ff0000000000000`, so it clears the sign bit and tests for infinity — a
standard `isfinite`. Non-finite LUT values are therefore refused, not stored.

Data rows accumulate three values at a time. The handler parses one token into a
three-float scratch buffer (`x29-0x90`, `-0x8c`, `-0x88`), and once the row is
complete pushes all three into `lut->data` with three consecutive
`vector<float>::push_back` calls (0x1f18, 0x1f28, 0x1f38), then resets the
per-line status to 0. A completion flag at `[sp+0x24]` bit 0 gates the push.

### What still needs reading

* the precise interleaving of the size declaration with the first data row, and
  what the per-line status code in `w24` means at each of its values.

Until that is read, implementing from this document alone would mean guessing,
which is exactly what this repository's method forbids. The grammar, the
tokeniser, the dispatch lengths and the validation rules above are safe to rely
on; that one handler body is not.

### RESOLVED since the first pass

Four of the five open items above are now read. Recorded with their evidence,
because two of them changed what was previously an INFERENCE in shipping code.

**1. `LUT_*_INPUT_RANGE` writes the SAME six floats `DOMAIN_MIN`/`DOMAIN_MAX` write,
so the two OVERWRITE each other and the LAST directive in the file wins.**

There is exactly ONE write site for the local CubeLUT's domain fields — its base is
`x29-0xc8`, so minR/minG/minB are at `-0xc0`/`-0xbc`/`-0xb8` and maxR/maxG/maxB at
`-0xb4`/`-0xb0`/`-0xac`:

```
0x1d94   stp s1, s1, [x29, #-0xc0]      ; minR = minG = s1
0x1d98   stp s1, s0, [x29, #-0xb8]      ; minB = s1, maxR = s0
0x1d9c   stp s0, s0, [x29, #-0xb0]      ; maxG = maxB = s0
0x1da0   sub x28, x29, #0xc8            ; back to the local CubeLUT
0x1da4   b   0x21fc                     ; and on to the next line
```

The shape is a BROADCAST — one lo to all three minima and one hi to all three maxima —
and there is no other store into those offsets anywhere in the function. An exhaustive
search for `stp`/`str`/`stur` into `[xN, #-0x[abc]...]` returns the initialiser, this
site, and nothing else. So a file containing both directives gets whichever comes last.

`plugins/LUTBlend/LutLibrary.cpp` had to CHOOSE an interpretation here before this was
read, and chose this one. The choice was right; `test/lutlib.test.mjs` now asserts it in
both orders, with a negative control that makes the loader keep the first value and
watches the suite fail.

**2. `DOMAIN_MIN` and `DOMAIN_MAX` share ONE handler, selected by a pointer.**

```
0x0db8   add x9, x28, #0x14      ; x28 = the local CubeLUT, so x9 = &maxR
0x0dbc   add x8, x28, #0x8       ; x8 = &minR
0x0dc0   stp x8, x9, [sp, #0x10] ; both candidate bases saved
...
0x1c54   csel x19, x9, x8, eq    ; x19 = the predicate ? &minR : &maxR
0x1cb8   str  s0, [x19]          ; first value  -> x19[0]
0x1d0c   str  s0, [x19, #0x4]    ; second value -> x19[1]
```

so the directive name is reduced to a boolean that picks a base address, and the
three-value body runs once. The predicate comes from a comparison against `0x494e`
with a `cneg`/`cmp` pair just above, i.e. from a two-way test on the keyword rather
than a four-way switch.

**3. A failed parse does NOT write through `out`.** The domain writes above all target
the LOCAL CubeLUT, and the only copy into the caller's `*out` is at the end
(`ldur q1, [x29, #-0xc8]` / `ldur q2, [x29, #-0xb8]`), after every validation. Already
asserted: `test/lutlib.test.mjs` checks "a failed parse ... leaves the output UNTOUCHED
(parseCube browses a local)".

**4. The `strtod` sites are the per-token parses of the domain and range handlers.**
Nine in total. The region at `0x1c58..0x1da4` accounts for three of them — one per
token, each followed by the same three-part acceptance test (`endptr == tok`,
`*endptr != '\0'`, `isfinite`) — and the per-axis stores at `x19[0]` and `x19[1]` are
tokens 1 and 2, with token 3 arriving in the broadcast at `0x1d94`. The earlier note
that these "do not obviously match the three-values-per-data-row shape" was right to
flag them: they are the SAME three-part test applied by a different handler, not data
rows. `strtod` result register pairing matters here — `mov.16b v8, v0` at `0x1d84`
saves the SECOND token's value into `s8` before the third `strtod` clobbers `s0`, which
is why the broadcast reads `fcvt s1, d8` and `fcvt s0, d0`.

## Cross-cutting findings in this module

* `applyLUTBlend` **inlines** `luma709`: same three Rec.709 constants
  (`0x3f371759`, `0x3e59b3d0`, `0x3d93dd98`), same `fmadd` accumulation order,
  instruction for instruction. The transcription calls `luma709()` instead, which
  is bit-identical because the constants and the association both match.
* `makeIdentity` **writes** `.cube` order (B outer, G middle, R inner, triples
  stored `(r, g, b)`) and `sampleLUT` **reads** `.cube` order. Writer and reader
  were recovered independently and agree, which is stronger evidence for the
  layout than either alone.
* `makeIdentity` with `size == 1` divides by the `size - 1` normaliser and yields
  NaN. Preserved deliberately: `CubeLUT::valid()` rejects `size < 2` and
  `parseCube` rejects `[0, 1]`, so no shipped path reaches it.

## `EffectMain` and the plugin layer — first pass

Not part of `lutblend::`, but recovered from the same bundle and needed by anything
that wants to rebuild the plugin.

`EffectMain` is exported as `_EffectMain` at 0x4408 and is ~658 instructions. It
contains the render logic INLINE (it calls `lutblend::applyLUTBlend` twice,
`lutblend::clampf` six times and `lutblend::luma709` four times directly), so it is
not merely a dispatcher.

### Parameter registration — located

`PF_ADD_PARAM` is a macro over a suite function pointer, so it shows up as `blr`
rather than `bl`. The pattern repeats once per parameter:

```
add  x22, sp, #0x50          ; &def
... build the PF_ParamDef at sp+0x50 ...
ldr  x8, [x21, #0x10]        ; the add-param function pointer
ldr  x0, [x21, #0xb8]        ; in_data
add  x2, sp, #0x50           ; &def
mov  w1, #-0x1               ; append after the last param
blr  x8
cbnz w0, <error handler>     ; a non-zero result aborts
```

Each block re-zeroes the 0x48-byte `PF_ParamDef` before filling it, which is what
makes the individual blocks readable.

### PF_ParamDef offsets — verified against the SDK, not guessed

The stores line up exactly with the struct at `AE_Effect.h:2407`, which is a useful
independent check that the reading is right:

| Offset | Field |
|---|---|
| 0x00 | `uu.id` |
| 0x04 | `ui_flags` |
| 0x08 / 0x0a | `ui_width` / `ui_height` |
| 0x0c | `param_type` |
| 0x10 | `PF_DEF_NAME[PF_MAX_EFFECT_PARAM_NAME_LEN + 1]` = 32 bytes |
| 0x30 | `flags` |
| 0x34 | `unused` |
| 0x38 | `PF_ParamDefUnion u` |

The first block stores `param_type = 7` and the name as the 4 bytes `"LUT "` at
0x10 followed by `'A'` at 0x14 — so the name is `"LUT A"` and `7` is
`PF_Param_POPUP`, which matches the plugin's own description ("they appear in the
LUT A / LUT B menus"). That correspondence is the check that the offset table is
right; a single wrong offset would have made the name garbage.

### The complete parameter table — RECOVERED

All six `PF_ADD_PARAM` blocks are read. otool annotates every string literal, so
the names come straight out of the disassembly rather than being inferred:

| `uu.id` | `param_type` | Name | Recovered settings |
|---|---|---|---|
| 1 | `PF_Param_POPUP` (7) | `LUT A` | |
| 2 | `PF_Param_POPUP` (7) | `LUT B` | |
| 3 | `PF_Param_FLOAT_SLIDER` (10) | `Blend (%)` | `valid_max` = 100.0f, `slider_max` = 100.0f, `precision` = 1 |
| 4 | `PF_Param_POPUP` (7) | `Drive` | choices `"Slider|Global Luma|Per-pixel Luma"` |
| 5 | `PF_Param_FLOAT_SLIDER` (10) | `Strength (%)` | 100.0 values, `precision` = 1 |
| 6 | `PF_Param_CHECKBOX` (4) | `Preserve Luma` | |

Six parameters, registered in that order, and the IDs are contiguous from 1.

The union offsets then confirm themselves against the SDK a second time. For
`PF_FloatSliderDef` with `u` at `def+0x38`: `value` is a `PF_FpLong` = **double**
(`A_FpLong` is `double` in `A.h:40`, so the 8-byte `mov x8, #0x4059000000000000`
in block 5 really is the double 100.0), `value_desc` is 32 bytes
(`PF_MAX_PARAM_DESCRIPTION_LEN` is 31), and `precision` is a `PF_Precision` =
`A_short` — which is exactly why block 3 uses a HALFWORD store for it. Block 3's
stores land on `valid_max` and `slider_max` at `def+0x6c` and `def+0x74`. Three
independent layout details, all predicted by the header and all matching.

Note that `Drive` is the popup that appears AFTER `Blend (%)` in the ECW and
carries the three blend modes, while `Blend (%)` itself is the slider — so
`applyLUTBlend`'s single `blend` float is supplied either from the slider or from
the image, depending on `Drive`. That is why `applyLUTBlend` takes the blend as a
plain float with no knowledge of the mode.

Still unrecovered in this area: the exact `flags` word per parameter, the
`value_desc` strings, and the PATH parameter that the recovered strings imply
(`/Documents/Williams Media LUTs` appears in the spec's parameter strings but no
PATH block was found among the six, so it likely belongs to `LB_LutFolderPath`
rather than to a registered parameter).

### Other recovered structure

* **There is a LUT cache.** The bundle instantiates
  `std::map<std::string, std::shared_ptr<const lutblend::CubeLUT>>`, so parsed LUTs
  are shared and reference-counted rather than re-read per frame. `parseCube` being
  called from a cached path is consistent with the bounds checking it does.
* **`__ZL11identityLUTv`** — a file-static function returning an identity LUT, used
  as a fallback when a LUT is missing or fails to parse.
* **The plugin has its own LUT layer**, separate from `lutblend::`:
  `LB_ScanLutFolder`, `LB_LutFileList`, `LB_LutFolderPath`, `LB_BuildPopupMenu`,
  `LB_LutForPopupValue(int)`. The recovered parameter strings name the folder as
  `/Documents/Williams Media LUTs`. None of these are recovered.
* Reading files goes through `std::ifstream`, so `parseCube` receives file contents
  as a string rather than opening the file itself.

### Parameter names recovered from the spec (not from the blocks)

`specs/LUTBlend.json` has 11 raw parameter strings, of which the ordered ones are
`/Documents/Williams Media LUTs`, `Blend (%)`, `Strength (%)`, `Preserve Luma`, plus
a popup AFTER `Blend (%)` with options `Slider`, `Global Luma`, `Per-pixel Luma`.
So the blend mode is a three-way choice and the "blend" value comes from either the
slider or the image. Types, IDs, defaults and order are NOT yet recovered.
