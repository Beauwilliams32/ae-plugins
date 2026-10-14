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

| Length | Token | Handler |
|---|---|---|
| 5 | `TITLE` | ignored (next line) |
| 10 | `DOMAIN_MIN` | 0x1528 / 0x142c |
| 10 | `DOMAIN_MAX` | 0x142c / 0x187c |
| 11 | `LUT_3D_SIZE` | 0x1270 |
| 11 | `LUT_1D_SIZE` | 0x12e8 |
| 18 | `LUT_3D_INPUT_RANGE` | 0x13ec |
| 18 | `LUT_1D_INPUT_RANGE` | 0x139c |
| other | — | error path |

NOTE: the two 10-length arms resolve to different handler addresses between the
SSO and heap copies of the chain (MIN -> 0x1528 in one, 0x142c in the other). That
inconsistency has NOT been resolved. Either a handler is shared and entered with
different state, or one of these addresses is a common tail rather than a distinct
handler. It must be settled before `DOMAIN_MIN`/`DOMAIN_MAX` are implemented.

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

### What still needs reading

Specifically unresolved, to be settled from the disassembly rather than guessed:

* the `DOMAIN_MIN` / `DOMAIN_MAX` handler duplication noted above;
* how `LUT_3D_INPUT_RANGE` and `LUT_1D_INPUT_RANGE` interact with
  `DOMAIN_MIN`/`DOMAIN_MAX` — whether they write the same fields or one overrides;
* the precise interleaving of the size declaration with the first data row, and
  what the per-line status code in `w24` means at each of its values;
* whether a failed parse writes through `out` at all, or returns leaving it
  untouched as the local-value shape suggests;
* the four consecutive `strtod` sites at 0x1bdc..0x1d58, which do not obviously
  match the three-values-per-data-row shape and need identifying.

Until those are read, implementing from this document alone would mean guessing,
which is exactly what this repository's method forbids. The grammar, the
tokeniser, the dispatch lengths and the validation rules above are safe to rely
on; the handler bodies are not.

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
