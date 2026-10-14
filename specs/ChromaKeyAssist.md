# ChromaKeyAssist

Recovered by static analysis of the compiled bundle only. The original C++ source was destroyed on 2026-10-13 and had no git remote, so **everything below was reconstructed from the binary**, not copied from source.

## Effect identity (from the PiPL resource)

| Field | Value |
|---|---|
| Effect name | `ChromaKeyAssist` |
| Menu name in After Effects | `WM_ChromaKeyAssist` |
| Category | `Williams Media` |
| PiPL kind | `eFKT` |
| Effect version | `0x00080001` |
| SDK version | `0x000d001d` — AE SDK 25.6 class (0x000d001d) |
| Entry points | `EffectMain` (Intel) / `EffectMain` (ARM64) |
| Globals | `0x02000000` / `0x08000000` |

## Binary provenance

| Field | Value |
|---|---|
| Bundle identifier | `com.williamsmedia.aftereffects.ChromaKeyAssist` |
| Bundle version | `1.0.0` |
| Minimum macOS | `11.0` |
| Architectures | x86_64, arm64 |
| Binary size | 137,872 bytes |
| Symbols recovered | 31 |
| Stripped | no — full symbol table present |
| MD5 | `abc0c88639a8f557c51442154bd52543` |
| SHA-256 | `38cf581cbb0c0d52e8452b704a348e21b37bcf23e60e297e3707f2617e76585b` |

## Original source files (named in the binary)

These filenames survive in the symbol table and reveal how the effect was split up:

- `ChromaKeyAssist.cpp`

## Recovered architecture

**Plugin-facing functions** — these take Adobe SDK types, so they are the effect's own API surface (parameter handling, rendering, UI hooks):

_none recovered_

**Domain code** — namespaced logic, i.e. the actual signal/image processing: names and signatures are exact, because the binaries were built without stripping.

```cpp
chromakey::(anonymous namespace)::colorRamp(float, float&, float&, float&)::stops
chromakey::Stats chromakey::analyzeFrame<unsigned char>(chromakey::View<unsigned char const> const&, float, chromakey::Params const&, float*, float*)
chromakey::Stats chromakey::analyzeFrame<unsigned short>(chromakey::View<unsigned short const> const&, float, chromakey::Params const&, float*, float*)
chromakey::angularDiff(float, float)
chromakey::chromaDistance(chromakey::YCbCr const&, chromakey::YCbCr const&)
chromakey::clampf(float, float, float)
chromakey::gradientMagnitude(float const*, int, int, long, int, int)
chromakey::hueAngle(float, float)
chromakey::percentile(float const*, unsigned long, float)
chromakey::percentileUnsorted(std::__1::vector<float, std::__1::allocator<float>>, float)
chromakey::rgbToYCbCr(chromakey::RGB const&)
chromakey::sensitivityToThreshold(float)
chromakey::spillScore(chromakey::YCbCr const&, chromakey::YCbCr const&, float)
chromakey::uniformityScore(float const*, unsigned long)
void chromakey::renderView<unsigned char>(chromakey::View<unsigned char const> const&, chromakey::View<unsigned char>&, float, chromakey::Params const&, chromakey::ViewMode, float, float const*, float const*)
void chromakey::renderView<unsigned short>(chromakey::View<unsigned short const> const&, chromakey::View<unsigned short>&, float, chromakey::Params const&, chromakey::ViewMode, float, float const*, float const*)
```

**Other named symbols:**

```cpp
(anonymous namespace)::g_readoutMutex
(anonymous namespace)::g_readouts
EffectMain
__dyld_private
void std::__1::__tree_balance_after_insert[abi:nqe220106]<std::__1::__tree_node_base<void*>*>(std::__1::__tree_node_base<void*>*, std::__1::__tree_node_base<void*>*)
void std::__1::__tree_remove[abi:nqe220106]<std::__1::__tree_node_base<void*>*>(std::__1::__tree_node_base<void*>*, std::__1::__tree_node_base<void*>*)
```

## Recovered parameters

Parameters are registered at runtime rather than stored in the PiPL, so these were recovered by walking `EffectMain`'s string literals **in address order** — which is the order they appear in the Effect Controls panel. This is the effect's control surface:

 1. `Key Color`
 2. `Sensitivity (%)`
 3. `Suggested Clip Black (%)`
 4. `Suggested Clip White (%)`
 5. `Heatmap Opacity (%)`

### Dropdown option lists

Popup parameters ship their own option list, joined with `|`, so these sets are recovered exactly rather than inferred. **The owning parameter is not asserted** — the list is recorded next to the parameter whose name precedes it, and adjacency is the only evidence available. Where the options plainly do not match that name, treat the pairing as unresolved:

| List appears after | Options |
|---|---|
| `Key Color` | `Uniformity Heatmap`, `Spill Map`, `Edge Quality`, `Distance Matte`, `Original` |

## Behavioural messages

User-visible strings document what the effect actually does, including its guard conditions. These are the closest thing to recovered requirements:

- Diagnostic overlay that scores greenscreen quality before you key. Shows where the key will struggle and suggests Clip Black/White starting points. Assists Keylight, doesn't replace it.

## Recovery status

| Aspect | State | Basis |
|---|---|---|
| Effect identity | **Recovered** | read from the PiPL resource |
| Function names + signatures | **Recovered** | un-stripped symbol table |
| Source file layout | **Recovered** | filenames embedded in symbols |
| Parameter list | Draft | string-table heuristic |
| Function bodies (DSP math) | **Not yet** | needs disassembly per function |
| Type definitions | Partial | recoverable from signatures and vtable layout |

Recovered by `tools/recover.mjs`; see the repository README for method and limits.
