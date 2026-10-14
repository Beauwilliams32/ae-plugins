# BeatGridRuler

Recovered by static analysis of the compiled bundle only. The original C++ source was destroyed on 2026-10-13 and had no git remote, so **everything below was reconstructed from the binary**, not copied from source.

## Effect identity (from the PiPL resource)

| Field | Value |
|---|---|
| Effect name | `BeatGridRuler` |
| Menu name in After Effects | `WM_BeatGridRuler` |
| Category | `Williams Media` |
| PiPL kind | `eFKT` |
| Effect version | `0x00080001` |
| SDK version | `0x000d001d` — AE SDK 25.6 class (0x000d001d) |
| Entry points | `EffectMain` (Intel) / `EffectMain` (ARM64) |
| Globals | `0x06000000` / `0x08001400` |

## Binary provenance

| Field | Value |
|---|---|
| Bundle identifier | `com.williamsmedia.aftereffects.BeatGridRuler` |
| Bundle version | `1.0.0` |
| Minimum macOS | `11.0` |
| Architectures | x86_64, arm64 |
| Binary size | 172,160 bytes |
| Symbols recovered | 38 |
| Stripped | no — full symbol table present |
| MD5 | `c84d54cc8373fb3e5ca7159ffce264cc` |
| SHA-256 | `73550db77aeb286ea442f07f19f90342f8fb326ba545c03a4a2a1654fe649a33` |

## Recovered architecture

**Plugin-facing functions** — these take Adobe SDK types, so they are the effect's own API surface (parameter handling, rendering, UI hooks):

_none recovered_

**Domain code** — namespaced logic, i.e. the actual signal/image processing: names and signatures are exact, because the binaries were built without stripping.

```cpp
beatgrid::BeatMarker& std::__1::vector<beatgrid::BeatMarker, std::__1::allocator<beatgrid::BeatMarker>>::emplace_back<beatgrid::BeatMarker const&>(beatgrid::BeatMarker const&)::'lambda0'()::operator()() const
beatgrid::OnsetEnvelope::frameTimeSeconds(int) const
beatgrid::analyzeBeatGrid(float const*, unsigned long, double, float, float, float, int, int)
beatgrid::clampf(float, float, float)
beatgrid::computeOnsetEnvelope(float const*, unsigned long, double, int, int)
beatgrid::downmixToMono(float const*, unsigned long, int)
beatgrid::estimatePhaseSeconds(beatgrid::OnsetEnvelope const&, float)
beatgrid::estimateTempoBpm(beatgrid::OnsetEnvelope const&, float, float, float)
beatgrid::estimateTempoBpm(beatgrid::OnsetEnvelope const&, float, float, float)::Peak& std::__1::vector<beatgrid::estimateTempoBpm(beatgrid::OnsetEnvelope const&, float, float, float)::Peak, std::__1::allocator<beatgrid::estimateTempoBpm(beatgrid::OnsetEnvelope const&, float, float, float)::Peak>>::emplace_back<beatgrid::estimateTempoBpm(beatgrid::OnsetEnvelope const&, float, float, float)::Peak>(beatgrid::estimateTempoBpm(beatgrid::OnsetEnvelope const&, float, float, float)::Peak&&)::'lambda0'()::operator()() const
beatgrid::generateBeatGrid(double, double, beatgrid::Subdivision, double, double)
beatgrid::isPowerOfTwo(int)
beatgrid::phaseInLayerTime(double, double, double, double)
beatgrid::phaseSawtooth(double, double, beatgrid::Subdivision, double)
beatgrid::subdivisionFactor(beatgrid::Subdivision)
```

**Other named symbols:**

```cpp
EffectMain
__dyld_private
double& std::__1::vector<double, std::__1::allocator<double>>::emplace_back<double>(double&&)::'lambda0'()::operator()() const
unsigned short& std::__1::vector<unsigned short, std::__1::allocator<unsigned short>>::emplace_back<unsigned short>(unsigned short&&)::'lambda0'()::operator()() const
void std::__1::__tree_balance_after_insert[abi:nqe220106]<std::__1::__tree_node_base<void*>*>(std::__1::__tree_node_base<void*>*, std::__1::__tree_node_base<void*>*)
```

## Recovered parameters

Parameters are registered at runtime rather than stored in the PiPL, so these were recovered by walking `EffectMain`'s string literals **in address order** — which is the order they appear in the Effect Controls panel. This is the effect's control surface:

 1. `Audio Layer`
 2. `Analysis`
 3. `BPM Min (BPM)`
 4. `BPM Max (BPM)`
 5. `Manual BPM (BPM; 0 = Auto)`
 6. `Phase Offset (ms)`
 7. `Subdivision`
 8. `Detected BPM (read-only)`
 9. `Analyze & Place Markers`
10. `Bake Beat Phase`
11. `Beat Phase Output`

### Dropdown option lists

Popup parameters ship their own option list, joined with `|`, so these sets are recovered exactly rather than inferred. **The owning parameter is not asserted** — the list is recorded next to the parameter whose name precedes it, and adjacency is the only evidence available. Where the options plainly do not match that name, treat the pairing as unresolved:

| List appears after | Options |
|---|---|
| `Subdivision` | `Whole Beat (1/1)`, `Half Beat (1/2)`, `Quarter Beat (1/4)`, `Eighth Beat (1/8)`, `Bars of 4` |

## Behavioural messages

User-visible strings document what the effect actually does, including its guard conditions. These are the closest thing to recovered requirements:

- BeatGridRuler: %d layer and %d comp beat marker(s) were not placed because another marker already sits on that frame. Those markers were left as they were.
- BeatGridRuler: no audio found and no Manual BPM set. Pick a layer with audio in the Audio Layer menu, or set Manual BPM, then try again.
- BeatGridRuler: no steady beat between %.0f and %.0f BPM in the audio layer. Widen BPM Min/Max or set Manual BPM, then try again.
- Detect the tempo of an audio layer and lay a snap-to-beat marker grid across the timeline.
- Pick an audio layer, set the BPM search range, then Analyze & Place Markers. Bake Beat Phase turns the grid into a keyframed 0..1 sawtooth you can pick-whip anything to.

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
