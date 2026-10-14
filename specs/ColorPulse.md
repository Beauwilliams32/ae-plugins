# ColorPulse

Recovered by static analysis of the compiled bundle only. The original C++ source was destroyed on 2026-10-13 and had no git remote, so **everything below was reconstructed from the binary**, not copied from source.

## Effect identity (from the PiPL resource)

| Field | Value |
|---|---|
| Effect name | `ColorPulse` |
| Menu name in After Effects | `WM_ColorPulse` |
| Category | `Williams Media` |
| PiPL kind | `eFKT` |
| Effect version | `0x00080001` |
| SDK version | `0x000d001d` — AE SDK 25.6 class (0x000d001d) |
| Entry points | `EffectMain` (Intel) / `EffectMain` (ARM64) |
| Globals | `0x02000004` / `0x08000000` |

## Binary provenance

| Field | Value |
|---|---|
| Bundle identifier | `com.williamsmedia.aftereffects.ColorPulse` |
| Bundle version | `1.0.0` |
| Minimum macOS | `11.0` |
| Architectures | x86_64, arm64 |
| Binary size | 137,168 bytes |
| Symbols recovered | 34 |
| Stripped | no — full symbol table present |
| MD5 | `d0208c39af9150ba02a89fd87993f708` |
| SHA-256 | `fc32fd4b1228038d7aec025872c9822a61ed218dd86676dab68a37691c2dc6e1` |

## Recovered architecture

**Plugin-facing functions** — these take Adobe SDK types, so they are the effect's own API surface (parameter handling, rendering, UI hooks):

_none recovered_

**Domain code** — namespaced logic, i.e. the actual signal/image processing: names and signatures are exact, because the binaries were built without stripping.

```cpp
colorpulse::averageColor(colorpulse::RGB const*, unsigned long)
colorpulse::brightestRegionColor(colorpulse::RGB const*, int, int, int)
colorpulse::clampf(float, float, float)
colorpulse::dominantColor(colorpulse::RGB const*, unsigned long, int)
colorpulse::luma709(colorpulse::RGB const&)
colorpulse::regionBounds(colorpulse::Region, int, int)
colorpulse::sampleColor(colorpulse::RGB const*, int, int, colorpulse::Mode)
colorpulse::saturationBoost(colorpulse::RGB const&, float)
colorpulse::smoothColor(colorpulse::RGB const&, colorpulse::RGB const&, float)
colorpulse::smoothScalar(float, float, float)
```

**Other named symbols:**

```cpp
(anonymous namespace)::GridFromWorld8(PF_Pixel const*, unsigned int, int, int, colorpulse::Region, std::__1::vector<colorpulse::RGB, std::__1::allocator<colorpulse::RGB>>*, int*, int*)
EffectMain
__dyld_private
colorpulse::RGB& std::__1::vector<colorpulse::RGB, std::__1::allocator<colorpulse::RGB>>::emplace_back<colorpulse::RGB const&>(colorpulse::RGB const&)::'lambda0'()::operator()() const
float& std::__1::vector<float, std::__1::allocator<float>>::emplace_back<float>(float&&)::'lambda0'()::operator()() const
```

## Recovered parameters

Parameters are registered at runtime rather than stored in the PiPL, so these were recovered by walking `EffectMain`'s string literals **in address order** — which is the order they appear in the Effect Controls panel. This is the effect's control surface:

 1. `Source Layer`
 2. `Sample Region`
 3. `Smoothing (%)`
 4. `Saturation Boost (%)`
 5. `Sampled Color`
 6. `Brightness (%)`
 7. `Show Preview`
 8. `swatch bar across the top of the layer`
 9. `Keyframes`
10. `Bake Keyframes`

### Dropdown option lists

Popup parameters ship their own option list, joined with `|`, so these sets are recovered exactly rather than inferred. **The owning parameter is not asserted** — the list is recorded next to the parameter whose name precedes it, and adjacency is the only evidence available. Where the options plainly do not match that name, treat the pairing as unresolved:

| List appears after | Options |
|---|---|
| `Source Layer` | `Average`, `Dominant`, `Brightest Region` |
| `Sample Region` | `Full`, `Center`, `Top`, `Bottom` |

## Behavioural messages

User-visible strings document what the effect actually does, including its guard conditions. These are the closest thing to recovered requirements:

-  trim it before baking (20000 frame limit).
- Pick a Source Layer, choose Mode + Sample Region, then Bake Keyframes to write the color and brightness onto every frame of the work area. Pick-whip Sampled Color / Brightness to anything.
- Sample a layer's color and expose it as keyframable color + brightness.

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
