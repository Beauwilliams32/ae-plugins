# AutoFlickerFix

Recovered by static analysis of the compiled bundle only. The original C++ source was destroyed on 2026-10-13 and had no git remote, so **everything below was reconstructed from the binary**, not copied from source.

## Effect identity (from the PiPL resource)

| Field | Value |
|---|---|
| Effect name | `AutoFlickerFix` |
| Menu name in After Effects | `WM_AutoFlickerFix` |
| Category | `Williams Media` |
| PiPL kind | `eFKT` |
| Effect version | `0x00080001` |
| SDK version | `0x000d001d` — AE SDK 25.6 class (0x000d001d) |
| Entry points | `EffectMain` (Intel) / `EffectMain` (ARM64) |
| Globals | `0x02000040` / `0x08000000` |

## Binary provenance

| Field | Value |
|---|---|
| Bundle identifier | `com.williamsmedia.aftereffects.AutoFlickerFix` |
| Bundle version | `1.0.0` |
| Minimum macOS | `11.0` |
| Architectures | x86_64, arm64 |
| Binary size | 135,904 bytes |
| Symbols recovered | 22 |
| Stripped | no — full symbol table present |
| MD5 | `79960266bd76621d60213efd98b4ca60` |
| SHA-256 | `cdd175f03b7fcf26fca4fe20d992e74088ae5a23f9b3bbe699630eb9a535c023` |

## Recovered architecture

**Plugin-facing functions** — these take Adobe SDK types, so they are the effect's own API surface (parameter handling, rendering, UI hooks):

_none recovered_

**Domain code** — namespaced logic, i.e. the actual signal/image processing: names and signatures are exact, because the binaries were built without stripping.

```cpp
aff::centerRegion(int, int, float)
aff::clampf(float, float, float)
aff::computeGain(float, float, float, aff::GainClamp const&)
aff::dbToGainClamp(float)
aff::meanOf(std::__1::vector<float, std::__1::allocator<float>> const&)
aff::medianFilter(std::__1::vector<float, std::__1::allocator<float>> const&, int)
aff::medianOf(std::__1::vector<float, std::__1::allocator<float>>)
aff::temporalTarget(std::__1::vector<float, std::__1::allocator<float>> const&, int)
aff::trimmedMean(float const*, unsigned long, float)
aff::trimmedMean(std::__1::vector<float, std::__1::allocator<float>> const&, float)
```

**Other named symbols:**

```cpp
EffectMain
__dyld_private
float& std::__1::vector<float, std::__1::allocator<float>>::emplace_back<float const&>(float const&)::'lambda0'()::operator()() const
```

## Recovered parameters and UI labels

Parameter labels are registered at runtime rather than stored in the PiPL, so these were recovered from the binary's string table. Treat them as a **high-confidence draft of the UI**, to be confirmed against the effect when it runs:

- `Analyze H`
- `Analyze Region`
- `Lco H`
- `Max Correction (dB)`
- `Show StaH`
- `Show Stats`
- `Strength (%)`
- `Window Size (frames)`

**Likely menu / mode values:**

- `Mode`

## Behavioural messages

User-visible strings document what the effect actually does, including its guard conditions. These are the closest thing to recovered requirements:

- Raise Window Size to sample more neighbour frames, and Strength to push the correction harder. Max Correction is a hard dB ceiling Strength cannot exceed.
- Removes exposure and color flicker by normalizing each frame toward a temporally smoothed target.

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
