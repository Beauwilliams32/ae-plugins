# EchoTrails

Recovered by static analysis of the compiled bundle only. The original C++ source was destroyed on 2026-10-13 and had no git remote, so **everything below was reconstructed from the binary**, not copied from source.

## Effect identity (from the PiPL resource)

| Field | Value |
|---|---|
| Effect name | `EchoTrails` |
| Menu name in After Effects | `WM_EchoTrails` |
| Category | `Williams Media` |
| PiPL kind | `eFKT` |
| Effect version | `0x00080001` |
| SDK version | `0x000d001d` — AE SDK 25.6 class (0x000d001d) |
| Entry points | `EffectMain` (Intel) / `EffectMain` (ARM64) |
| Globals | `0x02000006` / `0x08000000` |

## Binary provenance

| Field | Value |
|---|---|
| Bundle identifier | `com.williamsmedia.aftereffects.EchoTrails` |
| Bundle version | `1.0.0` |
| Minimum macOS | `11.0` |
| Architectures | x86_64, arm64 |
| Binary size | 101,872 bytes |
| Symbols recovered | 16 |
| Stripped | no — full symbol table present |
| MD5 | `0ed0c11d403fad93d6ca4f8b183eef05` |
| SHA-256 | `d8e34a500f228737ee10f5e56b28a908d0b32e2cf90ac471a500bd6312f24f5e` |

## Recovered architecture

**Plugin-facing functions** — these take Adobe SDK types, so they are the effect's own API surface (parameter handling, rendering, UI hooks):

_none recovered_

**Domain code** — namespaced logic, i.e. the actual signal/image processing: names and signatures are exact, because the binaries were built without stripping.

```cpp
echo::blendPixel(float*, float const*, echo::BlendMode, float)
echo::buildLadder(int, float, float, float)
echo::clampf(float, float, float)
echo::compositeOver(echo::ImageF&, echo::ImageF const&, echo::BlendMode, float)
echo::sampleBilinear(echo::ImageF const&, float, float, float*)
echo::transformBilinear(echo::ImageF const&, echo::ImageF&, float, float)
```

**Other named symbols:**

```cpp
EffectMain
__dyld_private
```

## Recovered parameters

Parameters are registered at runtime rather than stored in the PiPL, so these were recovered by walking `EffectMain`'s string literals **in address order** — which is the order they appear in the Effect Controls panel. This is the effect's control surface:

 1. `Time Step (frames)`
 2. `Opacity Decay (%)`
 3. `Scale Step (%)`
 4. `Rotation Step (deg)`
 5. `Blend Mode`
 6. `Start Behind`
 7. `echoes render under the source`

### Dropdown option lists

Popup parameters ship their own option list, joined with `|`, so these sets are recovered exactly rather than inferred. **The owning parameter is not asserted** — the list is recorded next to the parameter whose name precedes it, and adjacency is the only evidence available. Where the options plainly do not match that name, treat the pairing as unresolved:

| List appears after | Options |
|---|---|
| `Blend Mode` | `Normal`, `Add`, `Screen`, `Lighten` |

## Behavioural messages

User-visible strings document what the effect actually does, including its guard conditions. These are the closest thing to recovered requirements:

- Set Copies and Time Step, then art-direct the trail with the decay, scale and rotation ladder. Add / Screen for light trails.
- Time-offset echo copies with per-copy decay, scale, rotation and blend mode.

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
