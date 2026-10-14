# ShakeCapture

Recovered by static analysis of the compiled bundle only. The original C++ source was destroyed on 2026-10-13 and had no git remote, so **everything below was reconstructed from the binary**, not copied from source.

## Effect identity (from the PiPL resource)

| Field | Value |
|---|---|
| Effect name | `ShakeCapture` |
| Menu name in After Effects | `WM_ShakeCapture` |
| Category | `Williams Media` |
| PiPL kind | `eFKT` |
| Effect version | `0x00080001` |
| SDK version | `0x000d001d` — AE SDK 25.6 class (0x000d001d) |
| Entry points | `EffectMain` (Intel) / `EffectMain` (ARM64) |
| Globals | `0x02000020` / `0x08001400` |

## Binary provenance

| Field | Value |
|---|---|
| Bundle identifier | `com.williamsmedia.aftereffects.ShakeCapture` |
| Bundle version | `1.0.0` |
| Minimum macOS | `11.0` |
| Architectures | x86_64, arm64 |
| Binary size | 137,280 bytes |
| Symbols recovered | 33 |
| Stripped | no — full symbol table present |
| MD5 | `23f0cb80f4a637a951b41e41c6cbc2cb` |
| SHA-256 | `c404ccb310ddf44d83efb8692cef3d2c16a22b6d3dcc5f70d73e5cc341905857` |

## Recovered architecture

**Plugin-facing functions** — these take Adobe SDK types, so they are the effect's own API surface (parameter handling, rendering, UI hooks):

_none recovered_

**Domain code** — namespaced logic, i.e. the actual signal/image processing: names and signatures are exact, because the binaries were built without stripping.

```cpp
shake::ShakeProfile::~ShakeProfile()
shake::Vec2& std::__1::vector<shake::Vec2, std::__1::allocator<shake::Vec2>>::emplace_back<shake::Vec2 const&>(shake::Vec2 const&)::'lambda0'()::operator()() const
shake::accumulate(std::__1::vector<shake::Vec2, std::__1::allocator<shake::Vec2>> const&)
shake::clampf(float, float, float)
shake::extractJitter(shake::ShakeProfile const&, int)
shake::highPass(std::__1::vector<float, std::__1::allocator<float>> const&, int)
shake::median(std::__1::vector<float, std::__1::allocator<float>>)
shake::movingAverage(std::__1::vector<float, std::__1::allocator<float>> const&, int)
shake::normalizeUnitAmplitude(shake::ShakeProfile&)
shake::resampleProfile(std::__1::vector<float, std::__1::allocator<float>> const&, double, int, shake::LoopMode)
shake::sampleLooped(std::__1::vector<float, std::__1::allocator<float>> const&, double, shake::LoopMode)
shake::trackTranslation(float const*, float const*, int, int, int, int)
```

**Other named symbols:**

```cpp
EffectMain
__dyld_private
void std::__1::__nth_element[abi:nqe220106]<std::__1::_ClassicAlgPolicy, std::__1::__less<void, void>&, std::__1::__wrap_iter<float*>>(std::__1::__wrap_iter<float*>, std::__1::__wrap_iter<float*>, std::__1::__wrap_iter<float*>, std::__1::__less<void, void>&)
```

## Recovered parameters and UI labels

Parameter labels are registered at runtime rather than stored in the PiPL, so these were recovered from the binary's string table. Treat them as a **high-confidence draft of the UI**, to be confirmed against the effect when it runs:

- `&ffff.`
- `Amplitude (px)`
- `Capture & Bake`
- `Frequency Scale`
- `Shake X`
- `Shake Y`
- `ShakeCapture: Capture & Bake`
- `Smoothing Window (frames)`
- `Source LH`
- `Source Layer`

**Likely menu / mode values:**

- `Keyframes`
- `Loop`

## Behavioural messages

User-visible strings document what the effect actually does, including its guard conditions. These are the closest thing to recovered requirements:

- Capture the shake profile of real footage and re-apply it anywhere.
- Pick a Source Layer (defaults to the layer this effect sits on), press Capture & Bake, then pick-whip Shake X / Shake Y to anything.
- no trackable motion found in the source layer.
- source layer is too short to capture (need 2+ frames).

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
