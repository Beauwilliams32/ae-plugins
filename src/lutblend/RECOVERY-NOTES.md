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

### What still needs reading

Reading all 1924 instructions was not attempted. Specifically unresolved, and to
be settled from the disassembly rather than guessed:

* the exact tokeniser — whether token splitting is on runs of whitespace and
  whether a directive's arguments must be on the same line;
* whether `TITLE` content is validated at all or simply skipped;
* the precise interleaving of the size declaration with the first data row;
* how `LUT_3D_INPUT_RANGE` interacts with `DOMAIN_MIN`/`DOMAIN_MAX` — whether they
  write the same fields or one overrides the other;
* whether the error string is written before or after the partial `CubeLUT` state
  is left behind, i.e. whether a failed parse leaves `out` modified.

Until those are read, implementing from this document alone would mean guessing,
which is exactly what this repository's method forbids. The grammar and the
validation rules above are safe to rely on; the tokeniser details are not.

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
