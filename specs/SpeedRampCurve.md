# SpeedRampCurve

Recovered by static analysis of the compiled bundle only. The original C++ source was destroyed on 2026-10-13 and had no git remote, so **everything below was reconstructed from the binary**, not copied from source.

## Effect identity (from the PiPL resource)

| Field | Value |
|---|---|
| Effect name | `SpeedRampCurve` |
| Menu name in After Effects | `WM_SpeedRampCurve` |
| Category | `Williams Media` |
| PiPL kind | `eFKT` |
| Effect version | `0x00080001` |
| SDK version | `0x000d001d` — AE SDK 25.6 class (0x000d001d) |
| Entry points | `EffectMain` (Intel) / `EffectMain` (ARM64) |
| Globals | `0x02000026` / `0x08000000` |

## Binary provenance

| Field | Value |
|---|---|
| Bundle identifier | `com.williamsmedia.aftereffects.SpeedRampCurve` |
| Bundle version | `1.0.0` |
| Minimum macOS | `11.0` |
| Architectures | x86_64, arm64 |
| Binary size | 101,008 bytes |
| Symbols recovered | 18 |
| Stripped | no — full symbol table present |
| MD5 | `a818f461901c38242483b42a873297b0` |
| SHA-256 | `c8f45660dbc5ce4ffd02784778578cebffd0124fd54342f61d02b40e556a79d4` |

## Recovered architecture

**Plugin-facing functions** — these take Adobe SDK types, so they are the effect's own API surface (parameter handling, rendering, UI hooks):

_none recovered_

**Domain code** — namespaced logic, i.e. the actual signal/image processing: names and signatures are exact, because the binaries were built without stripping.

```cpp
speedramp::computeBlend(double, double)
speedramp::easeIntegral(speedramp::Easing, double)
speedramp::easeValue(speedramp::Easing, double)
speedramp::mapTime(speedramp::RampParams const&, double)
speedramp::speedAt(speedramp::RampParams const&, double)
```

**Other named symbols:**

```cpp
BlendFunc16(void*, int, int, PF_Pixel16*, PF_Pixel16*)
BlendFunc8(void*, int, int, PF_Pixel*, PF_Pixel*)
EffectMain
__dyld_private
```

## Recovered parameters

Parameters are registered at runtime rather than stored in the PiPL, so these were recovered by walking `EffectMain`'s string literals **in address order** — which is the order they appear in the Effect Controls panel. This is the effect's control surface:

 1. `Ramp Start (s)`
 2. `Ramp End (s)`
 3. `Speed Before (%)`
 4. `Speed After (%)`
 5. `Frame Blend`
 6. `Mix adjacent source frames`
 7. `Time Offset (s)`

### Dropdown option lists

Popup parameters ship their own option list, joined with `|`, so these sets are recovered exactly rather than inferred. **The owning parameter is not asserted** — the list is recorded next to the parameter whose name precedes it, and adjacency is the only evidence available. Where the options plainly do not match that name, treat the pairing as unresolved:

| List appears after | Options |
|---|---|
| `Speed After (%)` | `Linear`, `Smooth`, `Smoother`, `Snap` |

## Behavioural messages

User-visible strings document what the effect actually does, including its guard conditions. These are the closest thing to recovered requirements:

- Eased speed ramps without time-remap keyframe wrestling.
- Set Ramp Start / Ramp End, dial in the speed before and after, and pick an easing. The layer retimes smoothly between the two speeds instead of you hand-placing time-remap keyframes.

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
