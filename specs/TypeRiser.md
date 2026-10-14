# TypeRiser

Recovered by static analysis of the compiled bundle only. The original C++ source was destroyed on 2026-10-13 and had no git remote, so **everything below was reconstructed from the binary**, not copied from source.

## Effect identity (from the PiPL resource)

| Field | Value |
|---|---|
| Effect name | `TypeRiser` |
| Menu name in After Effects | `WM_TypeRiser` |
| Category | `Williams Media` |
| PiPL kind | `eFKT` |
| Effect version | `0x00080001` |
| SDK version | `0x000d001d` — AE SDK 25.6 class (0x000d001d) |
| Entry points | `EffectMain` (Intel) / `EffectMain` (ARM64) |
| Globals | `0x02000000` / `0x08000000` |

## Binary provenance

| Field | Value |
|---|---|
| Bundle identifier | `com.williamsmedia.aftereffects.TypeRiser` |
| Bundle version | `1.0.0` |
| Minimum macOS | `11.0` |
| Architectures | x86_64, arm64 |
| Binary size | 259,536 bytes |
| Symbols recovered | 77 |
| Stripped | no — full symbol table present |
| MD5 | `ddb76d4a2287ded772df235c70f5ff58` |
| SHA-256 | `098086d92e9cde097b05eb10c0d1df1e55a0ca6248ca30d81f61c878f5a0e767` |

## Original source files (named in the binary)

These filenames survive in the symbol table and reveal how the effect was split up:

- `TypeRiser_Srt.cpp`

## Recovered architecture

**Plugin-facing functions** — these take Adobe SDK types, so they are the effect's own API surface (parameter handling, rendering, UI hooks):

```cpp
TR_BuildPopupMenu()
TR_CaptionsFolderPath()
TR_CuesForPopupValue(int)
TR_ScanSrtFolder()
TR_SrtFileList()
```

**Domain code** — namespaced logic, i.e. the actual signal/image processing: names and signatures are exact, because the binaries were built without stripping.

```cpp
typeriser::Cue* std::__1::vector<typeriser::Cue, std::__1::allocator<typeriser::Cue>>::__emplace_back_slow_path<typeriser::Cue const&>(typeriser::Cue const&)
typeriser::clampf(float, float, float)
typeriser::cueIndexAt(std::__1::vector<typeriser::Cue, std::__1::allocator<typeriser::Cue>> const&, long long)
typeriser::ease(typeriser::Easing, float)
typeriser::karaokeWordAt(long long, long long, long long, int)
typeriser::parseSrt(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>> const&)
typeriser::parseTimestamp(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>> const&)
typeriser::parseTimingLine(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>> const&, long long*, long long*)
typeriser::splitWords(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>> const&)
typeriser::trim(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>> const&)
typeriser::wordProgress(long long, long long, int, float, float)
typeriser::wordStyle(typeriser::Anim, typeriser::Easing, long long, long long, long long, int, int, float, float, float)
typeriser::wrapWords(std::__1::vector<float, std::__1::allocator<float>> const&, float, float)
```

**Other named symbols:**

```cpp
(anonymous namespace)::CreateWordLine(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>> const&, __CTFont const*, CGColor*, CGColor*, float)
(anonymous namespace)::LaidOutWord& std::__1::vector<(anonymous namespace)::LaidOutWord, std::__1::allocator<(anonymous namespace)::LaidOutWord>>::emplace_back<(anonymous namespace)::LaidOutWord>((anonymous namespace)::LaidOutWord&&)::'lambda0'()::operator()() const
(anonymous namespace)::MeasureWordWidth(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>> const&, __CTFont const*)
EffectMain
VTT for std::__1::basic_ifstream<char, std::__1::char_traits<char>>
VTT for std::__1::basic_ostringstream<char, std::__1::char_traits<char>, std::__1::allocator<char>>
__dyld_private
bool std::__1::__insertion_sort_incomplete[abi:nqe220106]<std::__1::_ClassicAlgPolicy, std::__1::__less<void, void>&, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*>(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::__less<void, void>&)
bool std::__1::__sort3[abi:nqe220106]<std::__1::_ClassicAlgPolicy, std::__1::__less<void, void>&, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, 0>(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::__less<void, void>&)
construction vtable for std::__1::basic_istream<char, std::__1::char_traits<char>>-in-std::__1::basic_ifstream<char, std::__1::char_traits<char>>
construction vtable for std::__1::basic_ostream<char, std::__1::char_traits<char>>-in-std::__1::basic_ostringstream<char, std::__1::char_traits<char>, std::__1::allocator<char>>
float& std::__1::vector<float, std::__1::allocator<float>>::emplace_back<float>(float&&)::'lambda0'()::operator()() const
g_cueCache
g_emptyCues
g_popupMenu
g_srtFiles
g_srtMutex
typeinfo for std::__1::__shared_ptr_emplace<std::__1::vector<typeriser::Cue, std::__1::allocator<typeriser::Cue>> const, std::__1::allocator<std::__1::vector<typeriser::Cue, std::__1::allocator<typeriser::Cue>>>>
typeinfo for std::__1::basic_filebuf<char, std::__1::char_traits<char>>
typeinfo for std::__1::basic_ifstream<char, std::__1::char_traits<char>>
typeinfo for std::__1::basic_ostringstream<char, std::__1::char_traits<char>, std::__1::allocator<char>>
typeinfo for std::__1::basic_stringbuf<char, std::__1::char_traits<char>, std::__1::allocator<char>>
typeinfo name for std::__1::__shared_ptr_emplace<std::__1::vector<typeriser::Cue, std::__1::allocator<typeriser::Cue>> const, std::__1::allocator<std::__1::vector<typeriser::Cue, std::__1::allocator<typeriser::Cue>>>>
typeinfo name for std::__1::basic_filebuf<char, std::__1::char_traits<char>>
typeinfo name for std::__1::basic_ifstream<char, std::__1::char_traits<char>>
typeinfo name for std::__1::basic_ostringstream<char, std::__1::char_traits<char>, std::__1::allocator<char>>
typeinfo name for std::__1::basic_stringbuf<char, std::__1::char_traits<char>, std::__1::allocator<char>>
virtual thunk to std::__1::basic_ifstream<char, std::__1::char_traits<char>>::~basic_ifstream()
virtual thunk to std::__1::basic_ostringstream<char, std::__1::char_traits<char>, std::__1::allocator<char>>::~basic_ostringstream()
void std::__1::__inplace_merge<std::__1::_ClassicAlgPolicy, typeriser::parseSrt(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>> const&)::$_0&, std::__1::__wrap_iter<typeriser::Cue*>>(std::__1::__wrap_iter<typeriser::Cue*>, std::__1::__wrap_iter<typeriser::Cue*>, std::__1::__wrap_iter<typeriser::Cue*>, typeriser::parseSrt(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>> const&)::$_0&, std::__1::iterator_traits<std::__1::__wrap_iter<typeriser::Cue*>>::difference_type, std::__1::iterator_traits<std::__1::__wrap_iter<typeriser::Cue*>>::difference_type, std::__1::iterator_traits<std::__1::__wrap_iter<typeriser::Cue*>>::value_type*, long)
void std::__1::__insertion_sort[abi:nqe220106]<std::__1::_ClassicAlgPolicy, std::__1::__less<void, void>&, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*>(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::__less<void, void>&)
void std::__1::__insertion_sort_unguarded[abi:nqe220106]<std::__1::_ClassicAlgPolicy, std::__1::__less<void, void>&, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*>(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::__less<void, void>&)
void std::__1::__introsort<std::__1::_ClassicAlgPolicy, std::__1::__less<void, void>&, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, false>(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::__less<void, void>&, std::__1::iterator_traits<std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*>::difference_type, bool)
void std::__1::__sift_down[abi:nqe220106]<std::__1::_ClassicAlgPolicy, false, std::__1::__less<void, void>&, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*>(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::__less<void, void>&, std::__1::iterator_traits<std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*>::difference_type, std::__1::iterator_traits<std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*>::difference_type)
void std::__1::__sift_up[abi:nqe220106]<std::__1::_ClassicAlgPolicy, std::__1::__less<void, void>&, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*>(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::__less<void, void>&, std::__1::iterator_traits<std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*>::difference_type)
void std::__1::__sort4[abi:nqe220106]<std::__1::_ClassicAlgPolicy, std::__1::__less<void, void>&, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, 0>(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::__less<void, void>&)
void std::__1::__sort5[abi:nqe220106]<std::__1::_ClassicAlgPolicy, std::__1::__less<void, void>&, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, 0>(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::__less<void, void>&)
void std::__1::__sort_heap[abi:nqe220106]<std::__1::_ClassicAlgPolicy, std::__1::__less<void, void>&, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*>(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>*, std::__1::__less<void, void>&)
void std::__1::__stable_sort<std::__1::_ClassicAlgPolicy, typeriser::parseSrt(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>> const&)::$_0&, std::__1::__wrap_iter<typeriser::Cue*>>(std::__1::__wrap_iter<typeriser::Cue*>, std::__1::__wrap_iter<typeriser::Cue*>, typeriser::parseSrt(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>> const&)::$_0&, std::__1::iterator_traits<std::__1::__wrap_iter<typeriser::Cue*>>::difference_type, std::__1::iterator_traits<std::__1::__wrap_iter<typeriser::Cue*>>::value_type*, long)
void std::__1::__stable_sort_move<std::__1::_ClassicAlgPolicy, typeriser::parseSrt(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>> const&)::$_0&, std::__1::__wrap_iter<typeriser::Cue*>>(std::__1::__wrap_iter<typeriser::Cue*>, std::__1::__wrap_iter<typeriser::Cue*>, typeriser::parseSrt(std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>> const&)::$_0&, std::__1::iterator_traits<std::__1::__wrap_iter<typeriser::Cue*>>::difference_type, std::__1::iterator_traits<std::__1::__wrap_iter<typeriser::Cue*>>::value_type*)
void std::__1::__tree_balance_after_insert[abi:nqe220106]<std::__1::__tree_node_base<void*>*>(std::__1::__tree_node_base<void*>*, std::__1::__tree_node_base<void*>*)
vtable for std::__1::__shared_ptr_emplace<std::__1::vector<typeriser::Cue, std::__1::allocator<typeriser::Cue>> const, std::__1::allocator<std::__1::vector<typeriser::Cue, std::__1::allocator<typeriser::Cue>>>>
vtable for std::__1::basic_filebuf<char, std::__1::char_traits<char>>
vtable for std::__1::basic_ifstream<char, std::__1::char_traits<char>>
vtable for std::__1::basic_ostringstream<char, std::__1::char_traits<char>, std::__1::allocator<char>>
vtable for std::__1::basic_stringbuf<char, std::__1::char_traits<char>, std::__1::allocator<char>>
```

## Recovered parameters and UI labels

Parameter labels are registered at runtime rather than stored in the PiPL, so these were recovered from the binary's string table. Treat them as a **high-confidence draft of the UI**, to be confirmed against the effect when it runs:

- `Fill ColI`
- `Fill Color`
- `Font SizI`
- `Font Size (px)`
- `Position Y (%)`
- `Safe WidI`
- `Safe Width (%)`
- `Stroke CI`
- `Stroke Color`
- `Stroke Width (px)`
- `Word Stagger (ms)`

**Likely menu / mode values:**

- `Animation`
- `Easing`
- `Font`

## Behavioural messages

User-visible strings document what the effect actually does, including its guard conditions. These are the closest thing to recovered requirements:

- Drop .srt files into ~%s and they appear in the SRT File menu. Each word pops, fades, rises, or karaoke-highlights in sync with the cue timing.
- Kinetic per-word captions from an .srt subtitle file.

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
