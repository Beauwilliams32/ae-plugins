# MotionCurveX

Recovered by static analysis of the compiled bundle only. The original C++ source was destroyed on 2026-10-13 and had no git remote, so **everything below was reconstructed from the binary**, not copied from source.

## Effect identity (from the PiPL resource)

| Field | Value |
|---|---|
| Effect name | `MotionCurveX` |
| Menu name in After Effects | `WM_MotionCurveX` |
| Category | `Williams Media` |
| PiPL kind | `eFKT` |
| Effect version | `0x00080001` |
| SDK version | `0x000d001d` — AE SDK 25.6 class (0x000d001d) |
| Entry points | `EffectMain` (Intel) / `EffectMain` (ARM64) |
| Globals | `0x02000000` / `0x08001400` |

## Binary provenance

| Field | Value |
|---|---|
| Bundle identifier | `com.williamsmedia.aftereffects.MotionCurveX` |
| Bundle version | `1.0.0` |
| Minimum macOS | `11.0` |
| Architectures | x86_64, arm64 |
| Binary size | 138,592 bytes |
| Symbols recovered | 41 |
| Stripped | no — full symbol table present |
| MD5 | `e4a7920db48fc07a105516d58cafed76` |
| SHA-256 | `c196153253a41fcf146db208eb8933746886609404bb3c7ee241bc49a60dab1c` |

## Recovered architecture

**Plugin-facing functions** — these take Adobe SDK types, so they are the effect's own API surface (parameter handling, rendering, UI hooks):

_none recovered_

**Domain code** — namespaced logic, i.e. the actual signal/image processing: names and signatures are exact, because the binaries were built without stripping.

```cpp
motioncurve::(anonymous namespace)::medianOfWindow(std::__1::vector<float, std::__1::allocator<float>> const&, int, int)
motioncurve::Image* std::__1::vector<motioncurve::Image, std::__1::allocator<motioncurve::Image>>::__emplace_back_slow_path<motioncurve::Image const&>(motioncurve::Image const&)
motioncurve::Image* std::__1::vector<motioncurve::Image, std::__1::allocator<motioncurve::Image>>::__emplace_back_slow_path<motioncurve::Image>(motioncurve::Image&&)
motioncurve::accumulate(std::__1::vector<motioncurve::Vec2, std::__1::allocator<motioncurve::Vec2>> const&)
motioncurve::buildPyramid(motioncurve::Image const&, int)
motioncurve::clampf(float, float, float)
motioncurve::cropCenter(motioncurve::Image const&, float)
motioncurve::downsample2x(motioncurve::Image const&)
motioncurve::estimateTranslation(motioncurve::Image const&, motioncurve::Image const&, float)
motioncurve::quadraticPeakOffset(float, float, float)
motioncurve::sadAt(motioncurve::Image const&, motioncurve::Image const&, int, int)
motioncurve::smoothMovingAverage(std::__1::vector<motioncurve::Vec2, std::__1::allocator<motioncurve::Vec2>> const&, int)
motioncurve::temporalMedianFilter(std::__1::vector<motioncurve::Vec2, std::__1::allocator<motioncurve::Vec2>> const&, int)
```

**Other named symbols:**

```cpp
(anonymous namespace)::LumaFromWorld8(PF_Pixel const*, unsigned int, int, int, bool)
EffectMain
__dyld_private
float& std::__1::vector<float, std::__1::allocator<float>>::emplace_back<float const&>(float const&)::'lambda0'()::operator()() const
void std::__1::__if_likely_else[abi:nqe220106]<motioncurve::Image& std::__1::vector<motioncurve::Image, std::__1::allocator<motioncurve::Image>>::emplace_back<motioncurve::Image const&>(motioncurve::Image const&)::'lambda'(), motioncurve::Image& std::__1::vector<motioncurve::Image, std::__1::allocator<motioncurve::Image>>::emplace_back<motioncurve::Image const&>(motioncurve::Image const&)::'lambda0'()>(bool, motioncurve::Image& std::__1::vector<motioncurve::Image, std::__1::allocator<motioncurve::Image>>::emplace_back<motioncurve::Image const&>(motioncurve::Image const&)::'lambda'(), motioncurve::Image& std::__1::vector<motioncurve::Image, std::__1::allocator<motioncurve::Image>>::emplace_back<motioncurve::Image const&>(motioncurve::Image const&)::'lambda0'())
void std::__1::__if_likely_else[abi:nqe220106]<motioncurve::Image& std::__1::vector<motioncurve::Image, std::__1::allocator<motioncurve::Image>>::emplace_back<motioncurve::Image const&>(motioncurve::Image const&)::'lambda'(), motioncurve::Image& std::__1::vector<motioncurve::Image, std::__1::allocator<motioncurve::Image>>::emplace_back<motioncurve::Image const&>(motioncurve::Image const&)::'lambda0'()>(bool, motioncurve::Image& std::__1::vector<motioncurve::Image, std::__1::allocator<motioncurve::Image>>::emplace_back<motioncurve::Image const&>(motioncurve::Image const&)::'lambda'(), motioncurve::Image& std::__1::vector<motioncurve::Image, std::__1::allocator<motioncurve::Image>>::emplace_back<motioncurve::Image const&>(motioncurve::Image const&)::'lambda0'()) (.cold.1)
void std::__1::__nth_element[abi:nqe220106]<std::__1::_ClassicAlgPolicy, std::__1::__less<void, void>&, std::__1::__wrap_iter<float*>>(std::__1::__wrap_iter<float*>, std::__1::__wrap_iter<float*>, std::__1::__wrap_iter<float*>, std::__1::__less<void, void>&)
```

## Recovered parameters and UI labels

Parameter labels are registered at runtime rather than stored in the PiPL, so these were recovered from the binary's string table. Treat them as a **high-confidence draft of the UI**, to be confirmed against the effect when it runs:

- `Analysis Region`
- `Bake Motion`
- `Max Displacement (px)`
- `Motion X`
- `Motion XI`
- `Motion Y`
- `Motion YI`
- `Smoothing (frames)`
- `Source LH`
- `Source Layer`

**Likely menu / mode values:**

- `Keyframes`
- `Output`
- `Scale`
- `Tazu`
- `Tezu`

## Behavioural messages

User-visible strings document what the effect actually does, including its guard conditions. These are the closest thing to recovered requirements:

- MotionCurveX: choose a Source Layer to track, then Bake Motion again.
- MotionCurveX: could not read frames from the Source Layer. Pick a different layer and bake again.
- MotionCurveX: the work area is too short to track motion. Widen it and bake again.
- Pick a Source Layer, set Max Displacement and Smoothing, then Bake Motion. Pick-whip Motion X / Motion Y on any other layer.
- Track a layer's motion and bake it into pick-whippable keyframes.

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
