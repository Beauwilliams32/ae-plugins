# BeatSnap

Recovered by static analysis of the compiled bundle only. The original C++ source was destroyed on 2026-10-13 and had no git remote, so **everything below was reconstructed from the binary**, not copied from source.

## Effect identity (from the PiPL resource)

| Field | Value |
|---|---|
| Effect name | `BeatSnap` |
| Menu name in After Effects | `WM_BeatSnap` |
| Category | `Williams Media` |
| PiPL kind | `eFKT` |
| Effect version | `0x00080001` |
| SDK version | `0x000d001d` — AE SDK 25.6 class (0x000d001d) |
| Entry points | `EffectMain` (Intel) / `EffectMain` (ARM64) |
| Globals | `0x02000020` / `0x08001408` |

## Binary provenance

| Field | Value |
|---|---|
| Bundle identifier | `com.williamsmedia.aftereffects.BeatSnap` |
| Bundle version | `1.0.0` |
| Minimum macOS | `11.0` |
| Architectures | x86_64, arm64 |
| Binary size | 155,120 bytes |
| Symbols recovered | 39 |
| Stripped | no — full symbol table present |
| MD5 | `fa6edf74ef5b387fe479185d1406252a` |
| SHA-256 | `6da4c7296d48522a313149740f2152b495af3cc1fdf1f46233fa1963774ee83f` |

## Recovered architecture

**Plugin-facing functions** — these take Adobe SDK types, so they are the effect's own API surface (parameter handling, rendering, UI hooks):

_none recovered_

**Domain code** — namespaced logic, i.e. the actual signal/image processing: names and signatures are exact, because the binaries were built without stripping.

```cpp
beatsnap::adaptiveThreshold(std::__1::vector<float, std::__1::allocator<float>> const&, int, float)
beatsnap::clampf(float, float, float)
beatsnap::computeFlux(float const*, unsigned long, beatsnap::OnsetConfig const&)
beatsnap::detectOnsets(float const*, unsigned long, beatsnap::OnsetConfig const&)
beatsnap::downmixToMono(float const*, unsigned long, int)
beatsnap::fftRadix2(std::__1::vector<float, std::__1::allocator<float>>&, std::__1::vector<float, std::__1::allocator<float>>&)
beatsnap::hannWindow(int)
beatsnap::isPowerOfTwo(int)
beatsnap::medianOfWindow(std::__1::vector<float, std::__1::allocator<float>> const&, int, int)
beatsnap::pickOnsets(beatsnap::FluxTrack const&, std::__1::vector<float, std::__1::allocator<float>> const&, float)
beatsnap::renderValues(std::__1::vector<double, std::__1::allocator<double>> const&, int, beatsnap::RenderConfig const&)
```

**Other named symbols:**

```cpp
(anonymous namespace)::AsciiToUtf16(char const*)
EffectMain
__dyld_private
double& std::__1::vector<double, std::__1::allocator<double>>::emplace_back<double const&>(double const&)::'lambda0'()::operator()() const
double& std::__1::vector<double, std::__1::allocator<double>>::emplace_back<double>(double&&)::'lambda0'()::operator()() const
unsigned short& std::__1::vector<unsigned short, std::__1::allocator<unsigned short>>::emplace_back<unsigned short>(unsigned short&&)::'lambda0'()::operator()() const
void std::__1::__tree_balance_after_insert[abi:nqe220106]<std::__1::__tree_node_base<void*>*>(std::__1::__tree_node_base<void*>*, std::__1::__tree_node_base<void*>*)
```

## Recovered parameters and UI labels

Parameter labels are registered at runtime rather than stored in the PiPL, so these were recovered from the binary's string table. Treat them as a **high-confidence draft of the UI**, to be confirmed against the effect when it runs:

- `Add Markers at Hits`
- `Attack (I`
- `Attack (ms)`
- `Audio LaH`
- `Audio Layer`
- `Bake Keyframes`
- `Band High Freq (Hz)`
- `Band Low Freq (Hz)`
- `Min Gap (ms)`
- `Min Gap I`
- `Output MI`
- `Output Max`
- `Output Min`
- `Output Mode`
- `Release (ms)`
- `Release I`
- `Sensitivity (%)`

**Likely menu / mode values:**

- `Detection`
- `Keyframes`
- `Markers`
- `Output`

## Behavioural messages

User-visible strings document what the effect actually does, including its guard conditions. These are the closest thing to recovered requirements:

- BeatSnap: %d hit marker(s) were not placed because another marker already sits on that frame. Those markers were left as they were.
- BeatSnap: no audio found. Pick a layer that has audio in the Audio Layer menu, then try again.
- BeatSnap: no hits detected with the current Band/Sensitivity/Min Gap settings. Try raising Sensitivity or narrowing the band.
- Detect audio transients and bake them into keyframes or markers.
- Select an audio layer, dial in the band and sensitivity, then Bake Keyframes (writes the Output slider) or Add Markers at Hits (drops comp markers). Pick-whip Output to anything.

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
