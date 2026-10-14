# SpectraKeys

Recovered by static analysis of the compiled bundle only. The original C++ source was destroyed on 2026-10-13 and had no git remote, so **everything below was reconstructed from the binary**, not copied from source.

## Effect identity (from the PiPL resource)

| Field | Value |
|---|---|
| Effect name | `SpectraKeys` |
| Menu name in After Effects | `WMH SpectraKeys` |
| Category | `Williams Media` |
| PiPL kind | `eFKT` |
| Effect version | `0x00080001` |
| SDK version | `0x000d001d` — AE SDK 25.6 class (0x000d001d) |
| Entry points | `EffectMain` (Intel) / `EffectMain` (ARM64) |
| Globals | `0x06008064` / `0x08000008` |

## Binary provenance

| Field | Value |
|---|---|
| Bundle identifier | `com.williamsmedia.aftereffects.SpectraKeys` |
| Bundle version | `1.0.0` |
| Minimum macOS | `11.0` |
| Architectures | x86_64, arm64 |
| Binary size | 172,992 bytes |
| Symbols recovered | 51 |
| Stripped | no — full symbol table present |
| MD5 | `c383fbee000c80924dca4ea790bb8323` |
| SHA-256 | `b3aaa007c21671501abcf7314710d255ee0b3c23a8f67f8fb8a28a0fba1d3743` |

## Original source files (named in the binary)

These filenames survive in the symbol table and reveal how the effect was split up:

- `SpectraKeys_UI.cpp`

## Recovered architecture

**Plugin-facing functions** — these take Adobe SDK types, so they are the effect's own API surface (parameter handling, rendering, UI hooks):

```cpp
SK_FftSizeFromParam(int)
SK_PublishOverlaySpectrum(std::__1::vector<float, std::__1::allocator<float>> const&, double)
```

**Domain code** — namespaced logic, i.e. the actual signal/image processing: names and signatures are exact, because the binaries were built without stripping.

```cpp
spectra::Spectrogram::hzToBin(float) const
spectra::Spectrogram::~Spectrogram()
spectra::analyse(float const*, unsigned long, spectra::AnalysisConfig const&)
spectra::analyseBands(float const*, unsigned long, spectra::AnalysisConfig const&, std::__1::vector<spectra::BandSpec, std::__1::allocator<spectra::BandSpec>> const&)
spectra::bandValues(spectra::Spectrogram const&, spectra::BandSpec const&, double)
spectra::clampf(float, float, float)
spectra::dbToLinear(float)
spectra::downmixToMono(float const*, unsigned long, int)
spectra::fftRadix2(std::__1::vector<float, std::__1::allocator<float>>&, std::__1::vector<float, std::__1::allocator<float>>&)
spectra::hannCoherentGain()
spectra::hannWindow(int)
spectra::isPowerOfTwo(int)
spectra::linearToDb(float)
spectra::rawBandLevel(std::__1::vector<float, std::__1::allocator<float>> const&, spectra::Spectrogram const&, spectra::BandSpec const&)
```

**Other named symbols:**

```cpp
(anonymous namespace)::OverlayCache::~OverlayCache()
(anonymous namespace)::g_drag (.0)
(anonymous namespace)::g_drag (.1)
(anonymous namespace)::g_overlay
(anonymous namespace)::g_overlay_mutex
(anonymous namespace)::g_plugin_id (.0)
EffectMain
__dyld_private
void std::__1::vector<float, std::__1::allocator<float>>::__assign_with_size[abi:nqe220106]<std::__1::_ClassicAlgPolicy, float*, float*>(float*, float*, long)
```

## Recovered parameters and UI labels

Parameter labels are registered at runtime rather than stored in the PiPL, so these were recovered from the binary's string table. Treat them as a **high-confidence draft of the UI**, to be confirmed against the effect when it runs:

- `Audio LaH`
- `Audio Layer`
- `Bake Keyframes`
- `Band %s`
- `Band %s Enabled`
- `Ceiling (dB)`
- `Ceiling I`
- `Display I`
- `Display Mode`
- `Display Opacity`
- `Falloff Time (s)`
- `Floor (dB)`
- `Floor (dI`
- `Gain (dB)`
- `Gain (dBI`
- `Output MM`
- `Output Max`
- `Output Min`
- `Selection Color`
- `Show SpeI`
- `Show Spectrum`
- `Spectrum Color`
- `Threshold (dB)`

**Likely menu / mode values:**

- `Analysis`
- `Display`
- `Falloff`
- `Keyframes`
- `Sampling`

## Behavioural messages

User-visible strings document what the effect actually does, including its guard conditions. These are the closest thing to recovered requirements:

- Select an audio layer, drag a box over the part of the spectrum you want, then Bake Keyframes. Pick-whip the Output values to anything.
- SpectraKeys: no audio found. Pick a layer that has audio in the Audio Layer menu, then bake again.
- Turn regions of an audio spectrum into keyframable values.

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
