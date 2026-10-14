# GlitchKit

Recovered by static analysis of the compiled bundle only. The original C++ source was destroyed on 2026-10-13 and had no git remote, so **everything below was reconstructed from the binary**, not copied from source.

## Effect identity (from the PiPL resource)

| Field | Value |
|---|---|
| Effect name | `GlitchKit` |
| Menu name in After Effects | `WM_GlitchKit` |
| Category | `Williams Media` |
| PiPL kind | `eFKT` |
| Effect version | `0x00080001` |
| SDK version | `0x000d001d` — AE SDK 25.6 class (0x000d001d) |
| Entry points | `EffectMain` (Intel) / `EffectMain` (ARM64) |
| Globals | `0x02000004` / `0x08000000` |

## Binary provenance

| Field | Value |
|---|---|
| Bundle identifier | `com.williamsmedia.aftereffects.GlitchKit` |
| Bundle version | `1.0.0` |
| Minimum macOS | `11.0` |
| Architectures | x86_64, arm64 |
| Binary size | 133,824 bytes |
| Symbols recovered | 15 |
| Stripped | no — full symbol table present |
| MD5 | `12c20e28d6f01ab79e9be1863e332875` |
| SHA-256 | `f008768dc711c1d5244d8d7c7ddcfbad09b17af2e450ecde90193b0aa1fc0456` |

## Recovered architecture

**Plugin-facing functions** — these take Adobe SDK types, so they are the effect's own API surface (parameter handling, rendering, UI hooks):

_none recovered_

**Domain code** — namespaced logic, i.e. the actual signal/image processing: names and signatures are exact, because the binaries were built without stripping.

```cpp
glitch::clampf(float, float, float)
glitch::clampi(int, int, int)
glitch::hashKey(unsigned int, unsigned int, unsigned int)
glitch::patternFrame(unsigned int, float)
```

**Other named symbols:**

```cpp
EffectMain
__dyld_private
void glitch::apply<unsigned char>(glitch::View<unsigned char const> const&, glitch::View<unsigned char>&, glitch::Params const&, unsigned int, float)
void glitch::apply<unsigned short>(glitch::View<unsigned short const> const&, glitch::View<unsigned short>&, glitch::Params const&, unsigned int, float)
```

## Recovered parameters and UI labels

Parameter labels are registered at runtime rather than stored in the PiPL, so these were recovered from the binary's string table. Treat them as a **high-confidence draft of the UI**, to be confirmed against the effect when it runs:

- `&ffff.`
- `Amount (%)`
- `Amount (I`
- `Block SiI`
- `Block Size`
- `Channel Shift (px)`
- `Drive by Luma`
- `Drive byI`
- `Mix (%)`
- `Scanline Height`

**Likely menu / mode values:**

- `Mode`
- `Seed`
- `Spee`
- `Speed`

## Behavioural messages

User-visible strings document what the effect actually does, including its guard conditions. These are the closest thing to recovered requirements:

- Five deterministic, seeded glitch modes. Same seed, same frame, same image.
- Renders are deterministic: keyframe Amount freely, scrub anywhere, and every frame comes back pixel-identical.

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
