# LUTBlend

Recovered by static analysis of the compiled bundle only. The original C++ source was destroyed on 2026-10-13 and had no git remote, so **everything below was reconstructed from the binary**, not copied from source.

## Effect identity (from the PiPL resource)

| Field | Value |
|---|---|
| Effect name | `LUTBlend` |
| Menu name in After Effects | `WM_LUTBlend` |
| Category | `Williams Media` |
| PiPL kind | `eFKT` |
| Effect version | `0x00080001` |
| SDK version | `0x000d001d` — AE SDK 25.6 class (0x000d001d) |
| Entry points | `EffectMain` (Intel) / `EffectMain` (ARM64) |
| Globals | `0x02000000` / `0x08000000` |

## Binary provenance

| Field | Value |
|---|---|
| Bundle identifier | `com.williamsmedia.aftereffects.LUTBlend` |
| Bundle version | `1.0.0` |
| Minimum macOS | `11.0` |
| Architectures | x86_64, arm64 |
| Binary size | 256,432 bytes |
| Symbols recovered | 76 |
| Stripped | no — full symbol table present |
| MD5 | `e73a50c56627d986a754a37d6a39c809` |
| SHA-256 | `eb9157729480bc379d8ea1aa0cea9dac48d29d1a9a71fe03490c9eba997ce10a` |

## Original source files (named in the binary)

These filenames survive in the symbol table and reveal how the effect was split up:

- `LUTBlend_LUTs.cpp`

## Recovered architecture

**Plugin-facing functions** — these take Adobe SDK types, so they are the effect's own API surface (parameter handling, rendering, UI hooks):

```cpp
LB_BuildPopupMenu()
LB_LutFileList()
LB_LutFolderPath()
LB_LutForPopupValue(int)
LB_ScanLutFolder()
```

**Domain code** — namespaced logic, i.e. the actual signal/image processing: names and signatures are exact, because the binaries were built without stripping.

```cpp
lutblend::CubeLUT::valid() const
lutblend::applyLUTBlend(lutblend::CubeLUT const&, lutblend::CubeLUT const&, float, float, bool, float const*, float*)
lutblend::clampf(float, float, float)
lutblend::lerpf(float, float, float)
lutblend::luma709(float, float, float)
lutblend::makeIdentity(lutblend::CubeLUT*, int, bool)
lutblend::parseCube(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>> const&, lutblend::CubeLUT*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*)
lutblend::sampleLUT(lutblend::CubeLUT const&, float, float, float, float*)
```

**Other named symbols:**

```cpp
EffectMain
VTT for std::__1::basic_ifstream<char, std::__1::char_traits<char>>
VTT for std::__1::basic_istringstream<char, std::__1::char_traits<char>, std::__1::allocator<char>>
VTT for std::__1::basic_ostringstream<char, std::__1::char_traits<char>, std::__1::allocator<char>>
__dyld_private
bool std::__1::__insertion_sort_incomplete[abi:nqe220106]<std::__1::_ClassicAlgPolicy, std::__1::__less<void, void>&, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*>(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::__less<void, void>&)
bool std::__1::__sort3[abi:nqe220106]<std::__1::_ClassicAlgPolicy, std::__1::__less<void, void>&, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, 0>(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::__less<void, void>&)
construction vtable for std::__1::basic_istream<char, std::__1::char_traits<char>>-in-std::__1::basic_ifstream<char, std::__1::char_traits<char>>
construction vtable for std::__1::basic_istream<char, std::__1::char_traits<char>>-in-std::__1::basic_istringstream<char, std::__1::char_traits<char>, std::__1::allocator<char>>
construction vtable for std::__1::basic_ostream<char, std::__1::char_traits<char>>-in-std::__1::basic_ostringstream<char, std::__1::char_traits<char>, std::__1::allocator<char>>
float& std::__1::vector<float, std::__1::allocator<float>>::emplace_back<float const&>(float const&)::'lambda0'()::operator()() const
g_lutCache
g_lutFiles
g_lutMutex
g_popupMenu
guard variable for identityLUT()::identity
identityLUT()
identityLUT()::$_0::operator()() const
identityLUT()::identity
typeinfo for std::__1::__shared_ptr_emplace<lutblend::CubeLUT const, std::__1::allocator<lutblend::CubeLUT>>
typeinfo for std::__1::__shared_ptr_emplace<lutblend::CubeLUT, std::__1::allocator<lutblend::CubeLUT>>
typeinfo for std::__1::basic_filebuf<char, std::__1::char_traits<char>>
typeinfo for std::__1::basic_ifstream<char, std::__1::char_traits<char>>
typeinfo for std::__1::basic_istringstream<char, std::__1::char_traits<char>, std::__1::allocator<char>>
typeinfo for std::__1::basic_ostringstream<char, std::__1::char_traits<char>, std::__1::allocator<char>>
typeinfo for std::__1::basic_stringbuf<char, std::__1::char_traits<char>, std::__1::allocator<char>>
typeinfo name for std::__1::__shared_ptr_emplace<lutblend::CubeLUT const, std::__1::allocator<lutblend::CubeLUT>>
typeinfo name for std::__1::__shared_ptr_emplace<lutblend::CubeLUT, std::__1::allocator<lutblend::CubeLUT>>
typeinfo name for std::__1::basic_filebuf<char, std::__1::char_traits<char>>
typeinfo name for std::__1::basic_ifstream<char, std::__1::char_traits<char>>
typeinfo name for std::__1::basic_istringstream<char, std::__1::char_traits<char>, std::__1::allocator<char>>
typeinfo name for std::__1::basic_ostringstream<char, std::__1::char_traits<char>, std::__1::allocator<char>>
typeinfo name for std::__1::basic_stringbuf<char, std::__1::char_traits<char>, std::__1::allocator<char>>
virtual thunk to std::__1::basic_ifstream<char, std::__1::char_traits<char>>::~basic_ifstream()
virtual thunk to std::__1::basic_istringstream<char, std::__1::char_traits<char>, std::__1::allocator<char>>::~basic_istringstream()
virtual thunk to std::__1::basic_ostringstream<char, std::__1::char_traits<char>, std::__1::allocator<char>>::~basic_ostringstream()
void std::__1::__insertion_sort[abi:nqe220106]<std::__1::_ClassicAlgPolicy, std::__1::__less<void, void>&, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*>(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::__less<void, void>&)
void std::__1::__insertion_sort_unguarded[abi:nqe220106]<std::__1::_ClassicAlgPolicy, std::__1::__less<void, void>&, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*>(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::__less<void, void>&)
void std::__1::__introsort<std::__1::_ClassicAlgPolicy, std::__1::__less<void, void>&, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, false>(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::__less<void, void>&, std::__1::iterator_traits<std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*>::difference_type, bool)
void std::__1::__sift_down[abi:nqe220106]<std::__1::_ClassicAlgPolicy, false, std::__1::__less<void, void>&, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*>(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::__less<void, void>&, std::__1::iterator_traits<std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*>::difference_type, std::__1::iterator_traits<std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*>::difference_type)
void std::__1::__sift_up[abi:nqe220106]<std::__1::_ClassicAlgPolicy, std::__1::__less<void, void>&, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*>(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::__less<void, void>&, std::__1::iterator_traits<std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*>::difference_type)
void std::__1::__sort4[abi:nqe220106]<std::__1::_ClassicAlgPolicy, std::__1::__less<void, void>&, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, 0>(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::__less<void, void>&)
void std::__1::__sort5[abi:nqe220106]<std::__1::_ClassicAlgPolicy, std::__1::__less<void, void>&, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, 0>(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::__less<void, void>&)
void std::__1::__sort_heap[abi:nqe220106]<std::__1::_ClassicAlgPolicy, std::__1::__less<void, void>&, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*>(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::__less<void, void>&)
void std::__1::__tree_balance_after_insert[abi:nqe220106]<std::__1::__tree_node_base<void*>*>(std::__1::__tree_node_base<void*>*, std::__1::__tree_node_base<void*>*)
vtable for std::__1::__shared_ptr_emplace<lutblend::CubeLUT const, std::__1::allocator<lutblend::CubeLUT>>
vtable for std::__1::__shared_ptr_emplace<lutblend::CubeLUT, std::__1::allocator<lutblend::CubeLUT>>
vtable for std::__1::basic_filebuf<char, std::__1::char_traits<char>>
vtable for std::__1::basic_ifstream<char, std::__1::char_traits<char>>
vtable for std::__1::basic_istringstream<char, std::__1::char_traits<char>, std::__1::allocator<char>>
vtable for std::__1::basic_ostringstream<char, std::__1::char_traits<char>, std::__1::allocator<char>>
vtable for std::__1::basic_stringbuf<char, std::__1::char_traits<char>, std::__1::allocator<char>>
```

## Recovered parameters

Parameters are registered at runtime rather than stored in the PiPL, so these were recovered by walking `EffectMain`'s string literals **in address order** — which is the order they appear in the Effect Controls panel. This is the effect's control surface:

 1. `/Documents/Williams Media LUTs`
 2. `Blend (%)`
 3. `Strength (%)`
 4. `Preserve Luma`

### Dropdown option lists

Popup parameters ship their own option list, joined with `|`, so these sets are recovered exactly rather than inferred. **The owning parameter is not asserted** — the list is recorded next to the parameter whose name precedes it, and adjacency is the only evidence available. Where the options plainly do not match that name, treat the pairing as unresolved:

| List appears after | Options |
|---|---|
| `Blend (%)` | `Slider`, `Global Luma`, `Per-pixel Luma` |

## Behavioural messages

User-visible strings document what the effect actually does, including its guard conditions. These are the closest thing to recovered requirements:

- Blend two .cube LUTs by slider or by the image's own luma.
- Drop .cube files into ~%s and they appear in the LUT A / LUT B menus. Blend between them with the slider, or let the image's luma drive the mix.

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
