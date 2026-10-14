# MaskFlow

Recovered by static analysis of the compiled bundle only. The original C++ source was destroyed on 2026-10-13 and had no git remote, so **everything below was reconstructed from the binary**, not copied from source.

## Effect identity (from the PiPL resource)

| Field | Value |
|---|---|
| Effect name | `MaskFlow` |
| Menu name in After Effects | `WM_MaskFlow` |
| Category | `Williams Media` |
| PiPL kind | `eFKT` |
| Effect version | `0x00080001` |
| SDK version | `0x000d001d` — AE SDK 25.6 class (0x000d001d) |
| Entry points | `EffectMain` (Intel) / `EffectMain` (ARM64) |
| Globals | `0x02000000` / `0x08000000` |

## Binary provenance

| Field | Value |
|---|---|
| Bundle identifier | `com.williamsmedia.aftereffects.MaskFlow` |
| Bundle version | `1.0.0` |
| Minimum macOS | `11.0` |
| Architectures | x86_64, arm64 |
| Binary size | 136,208 bytes |
| Symbols recovered | 22 |
| Stripped | no — full symbol table present |
| MD5 | `f5efa9410a061960941ebaa1243b4763` |
| SHA-256 | `70e083a3bc21d28903cb11a19006b0ef677a22d71206bbaef873ee3776a58982` |

## Recovered architecture

**Plugin-facing functions** — these take Adobe SDK types, so they are the effect's own API surface (parameter handling, rendering, UI hooks):

_none recovered_

**Domain code** — namespaced logic, i.e. the actual signal/image processing: names and signatures are exact, because the binaries were built without stripping.

```cpp
maskflow::buildMatte(maskflow::ArrivalField const&, float, float, bool)
maskflow::clampf(float, float, float)
maskflow::computeArrivalField(maskflow::Image const&, float, float, float, bool)
maskflow::computeArrivalField(maskflow::Image const&, float, float, float, bool)::dx8
maskflow::computeArrivalField(maskflow::Image const&, float, float, float, bool)::dy8
maskflow::computeDownsampleDims(int, int, int)
maskflow::computeMatteFullRes(maskflow::Image const&, maskflow::Params const&)
maskflow::downsampleBoxFilter(maskflow::Image const&, int, int)
maskflow::edgeCostScaleFromTolerance(float)
maskflow::kInfinity
maskflow::smoothstep(float, float, float)
maskflow::upsampleBilinear(std::__1::vector<float, std::__1::allocator<float>> const&, int, int, int, int)
```

**Other named symbols:**

```cpp
EffectMain
__dyld_private
void std::__1::__pop_heap[abi:nqe220106]<std::__1::_ClassicAlgPolicy, std::__1::greater<std::__1::pair<float, unsigned long>>, std::__1::__wrap_iter<std::__1::pair<float, unsigned long>*>>(std::__1::__wrap_iter<std::__1::pair<float, unsigned long>*>, std::__1::__wrap_iter<std::__1::pair<float, unsigned long>*>, std::__1::greater<std::__1::pair<float, unsigned long>>&, std::__1::iterator_traits<std::__1::__wrap_iter<std::__1::pair<float, unsigned long>*>>::difference_type)
```

## Recovered parameters and UI labels

Parameter labels are registered at runtime rather than stored in the PiPL, so these were recovered from the binary's string table. Treat them as a **high-confidence draft of the UI**, to be confirmed against the effect when it runs:

- `Edge SofI`
- `Edge Softness`
- `Growth (%)`
- `Growth (I`
- `Hold EdgI`
- `Hold Edges`
- `Seed PoiH`
- `Seed Point`
- `Tolerance (%)`

**Likely menu / mode values:**

- `Invert`
- `Output`

## Behavioural messages

User-visible strings document what the effect actually does, including its guard conditions. These are the closest thing to recovered requirements:

- Content-aware animated reveal: grows a matte from a seed point along real color edges.
- Drag the Seed Point onto your footage, then animate Growth from 0 to 100 to reveal outward along real color edges instead of a plain geometric wipe.

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
