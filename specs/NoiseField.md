# NoiseField

Recovered by static analysis of the compiled bundle only. The original C++ source was destroyed on 2026-10-13 and had no git remote, so **everything below was reconstructed from the binary**, not copied from source.

## Effect identity (from the PiPL resource)

| Field | Value |
|---|---|
| Effect name | `NoiseField` |
| Menu name in After Effects | `WM_NoiseField` |
| Category | `Williams Media` |
| PiPL kind | `eFKT` |
| Effect version | `0x00080001` |
| SDK version | `0x000d001d` — AE SDK 25.6 class (0x000d001d) |
| Entry points | `EffectMain` (Intel) / `EffectMain` (ARM64) |
| Globals | `0x02000000` / `0x08000000` |

## Binary provenance

| Field | Value |
|---|---|
| Bundle identifier | `com.williamsmedia.aftereffects.NoiseField` |
| Bundle version | `1.0.0` |
| Minimum macOS | `11.0` |
| Architectures | x86_64, arm64 |
| Binary size | 134,400 bytes |
| Symbols recovered | 20 |
| Stripped | no — full symbol table present |
| MD5 | `4ead9b560b34c4daebb844165f4b92a9` |
| SHA-256 | `5a862f2ff602b6e34e6d25cd70cb0c71e09b2b7441734e0d83e3a17c967da1b3` |

## Recovered architecture

**Plugin-facing functions** — these take Adobe SDK types, so they are the effect's own API surface (parameter handling, rendering, UI hooks):

_none recovered_

**Domain code** — namespaced logic, i.e. the actual signal/image processing: names and signatures are exact, because the binaries were built without stripping.

```cpp
noisefield::(anonymous namespace)::curlField(noisefield::PermTable const&, float, float, float, noisefield::Params const&)
noisefield::(anonymous namespace)::kGrad3
noisefield::PermTable::build(unsigned int)
noisefield::clampf(float, float, float)
noisefield::clampi(int, int, int)
noisefield::fbm3(noisefield::PermTable const&, float, float, float, int, float, float)
noisefield::fbmAmplitudeSum(int, float)
noisefield::fieldAt(noisefield::PermTable const&, float, float, noisefield::Params const&)
noisefield::noise3(noisefield::PermTable const&, float, float, float)
```

**Other named symbols:**

```cpp
EffectMain
__dyld_private
void noisefield::apply<unsigned char>(noisefield::View<unsigned char const> const&, noisefield::View<unsigned char>&, noisefield::Params const&, float)
void noisefield::apply<unsigned short>(noisefield::View<unsigned short const> const&, noisefield::View<unsigned short>&, noisefield::Params const&, float)
```

## Recovered parameters and UI labels

Parameter labels are registered at runtime rather than stored in the PiPL, so these were recovered from the binary's string table. Treat them as a **high-confidence draft of the UI**, to be confirmed against the effect when it runs:

- ` !"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_`abcdefghijklmnopqrstuvwxyz{|}~`
- `Edge BehI`
- `Edge Behavior`
- `Field TyH`
- `Field Type`

**Likely menu / mode values:**

- `Amount`
- `Displacement`
- `Evolution`
- `Gain`
- `Lacunarity`
- `Octaves`
- `Scale`
- `Seed`

## Behavioural messages

User-visible strings document what the effect actually does, including its guard conditions. These are the closest thing to recovered requirements:

- Procedural displacement by curl noise / flow fields. Organic motion, seeded and deterministic.

## Recovery status

| Aspect | State | Basis |
|---|---|---|
| Effect identity | **Recovered** | read from the PiPL resource |
| Function names + signatures | **Recovered** | un-stripped symbol table |
| Source file layout | Not recoverable | filenames embedded in symbols |
| Parameter list | Draft | string-table heuristic |
| Function bodies (DSP math) | **Not yet** | needs disassembly per function |
| Type definitions | Partial | recoverable from signatures and vtable layout |

Recovered by `tools/recover.mjs`; see the repository README for method and limits.
