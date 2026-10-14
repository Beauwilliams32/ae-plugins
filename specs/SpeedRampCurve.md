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

## Recovered parameters and UI labels

Parameter labels are registered at runtime rather than stored in the PiPL, so these were recovered from the binary's string table. Treat them as a **high-confidence draft of the UI**, to be confirmed against the effect when it runs:

- `End (s)`
- `Frame BlI`
- `Frame Blend`
- `Hcx H`
- `Mix adjacent source frames`
- `Ramp End (s)`
- `Ramp EndI`
- `Ramp StaH`
- `Ramp Start (s)`
- `Speed After (%)`
- `Speed Before (%)`
- `Time Offset (s)`

**Likely menu / mode values:**

- `Easing`

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
