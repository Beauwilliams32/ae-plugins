# PathScribe

Recovered by static analysis of the compiled bundle only. The original C++ source was destroyed on 2026-10-13 and had no git remote, so **everything below was reconstructed from the binary**, not copied from source.

## Effect identity (from the PiPL resource)

| Field | Value |
|---|---|
| Effect name | `PathScribe` |
| Menu name in After Effects | `WM_PathScribe` |
| Category | `Williams Media` |
| PiPL kind | `eFKT` |
| Effect version | `0x00080001` |
| SDK version | `0x000d001d` — AE SDK 25.6 class (0x000d001d) |
| Entry points | `EffectMain` (Intel) / `EffectMain` (ARM64) |
| Globals | `0x02000004` / `0x08000000` |

## Binary provenance

| Field | Value |
|---|---|
| Bundle identifier | `com.williamsmedia.aftereffects.PathScribe` |
| Bundle version | `1.0.0` |
| Minimum macOS | `11.0` |
| Architectures | x86_64, arm64 |
| Binary size | 156,144 bytes |
| Symbols recovered | 42 |
| Stripped | no — full symbol table present |
| MD5 | `3b3dd32b5dcd5b71e0e4486cc5ac1f88` |
| SHA-256 | `362b553fd200f87cec471bf776d5e4e52e792a9c257f7e8e3b6985d8dbd67bde` |

## Original source files (named in the binary)

These filenames survive in the symbol table and reveal how the effect was split up:

- `PathScribe_Audio.cpp`

## Recovered architecture

**Plugin-facing functions** — these take Adobe SDK types, so they are the effect's own API surface (parameter handling, rendering, UI hooks):

_none recovered_

**Domain code** — namespaced logic, i.e. the actual signal/image processing: names and signatures are exact, because the binaries were built without stripping.

```cpp
pathscribe::Spectrogram::~Spectrogram()
pathscribe::analyse(float const*, unsigned long, pathscribe::AnalysisConfig const&)
pathscribe::barLevels(std::__1::vector<float, std::__1::allocator<float>> const&, double, int, double, double)
pathscribe::binEdgeHz(int, int, double, double)
pathscribe::clampf(float, float, float)
pathscribe::dbToLinear(float)
pathscribe::downmixToMono(float const*, unsigned long, int)
pathscribe::fftRadix2(std::__1::vector<float, std::__1::allocator<float>>&, std::__1::vector<float, std::__1::allocator<float>>&)
pathscribe::hannCoherentGain()
pathscribe::hannWindow(int)
pathscribe::isPowerOfTwo(int)
pathscribe::linearToDb(float)
pathscribe::rasterizeBars(std::__1::vector<float, std::__1::allocator<float>> const&, pathscribe::VizConfig const&, std::__1::vector<float, std::__1::allocator<float>>&, std::__1::vector<float, std::__1::allocator<float>>&)
pathscribe::rasterizeRibbon(std::__1::vector<float, std::__1::allocator<float>> const&, pathscribe::VizConfig const&, std::__1::vector<float, std::__1::allocator<float>>&, std::__1::vector<float, std::__1::allocator<float>>&)
pathscribe::smoothStep(float, float, float, float, float)
pathscribe::smoothedLevelsAtFrame(std::__1::vector<std::__1::vector<float, std::__1::allocator<float>>, std::__1::allocator<std::__1::vector<float, std::__1::allocator<float>>>> const&, int, float, float, float)
pathscribe::spanCoverage(float, float, float)
pathscribe::waveformColumns(float const*, unsigned long, long, long, int)
```

**Other named symbols:**

```cpp
(anonymous namespace)::AudioCacheEntry::operator=((anonymous namespace)::AudioCacheEntry&&)
(anonymous namespace)::blendPixel16(PF_Pixel16*, PF_Pixel const&, double)
(anonymous namespace)::g_audioCache
(anonymous namespace)::g_audioCacheMutex
(anonymous namespace)::g_plugin_id (.0)
EffectMain
__dyld_private
void std::__1::__tree_balance_after_insert[abi:nqe220106]<std::__1::__tree_node_base<void*>*>(std::__1::__tree_node_base<void*>*, std::__1::__tree_node_base<void*>*)
void std::__1::__tree_remove[abi:nqe220106]<std::__1::__tree_node_base<void*>*>(std::__1::__tree_node_base<void*>*, std::__1::__tree_node_base<void*>*)
void std::__1::vector<float, std::__1::allocator<float>>::__assign_with_size[abi:nqe220106]<std::__1::_ClassicAlgPolicy, float*, float*>(float*, float*, long)
```

## Recovered parameters

Parameters are registered at runtime rather than stored in the PiPL, so these were recovered by walking `EffectMain`'s string literals **in address order** — which is the order they appear in the Effect Controls panel. This is the effect's control surface:

 1. `Audio Layer`
 2. `Analysis`
 3. `Bar Count`
 4. `Freq Range Low (Hz)`
 5. `Freq Range High (Hz)`
 6. `Attack (s)`
 7. `Release (s)`
 8. `Amplitude (%)`
 9. `Baseline Position (%)`
10. `Thickness (px)`
11. `Color A (Low Amplitude)`
12. `Color B (High Amplitude)`
13. `Background Opacity (%)`

### Dropdown option lists

Popup parameters ship their own option list, joined with `|`, so these sets are recovered exactly rather than inferred. **The owning parameter is not asserted** — the list is recorded next to the parameter whose name precedes it, and adjacency is the only evidence available. Where the options plainly do not match that name, treat the pairing as unresolved:

| List appears after | Options |
|---|---|
| `Audio Layer` | `Bars`, `Ribbon`, `Mirrored Bars` |

## Behavioural messages

User-visible strings document what the effect actually does, including its guard conditions. These are the closest thing to recovered requirements:

- Draw an audio layer's spectrum or waveform as an animated EQ graphic.
- Pick an audio layer, choose Bars / Ribbon / Mirrored Bars, and dial in frequency range, smoothing, and colour.

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
