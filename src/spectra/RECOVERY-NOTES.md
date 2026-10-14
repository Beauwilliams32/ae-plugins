# Recovery notes — SpectraKeys DSP

Working notes for the disassembly recovery. `FEATURE-MAP.md` tracks state; this
file records the evidence behind each claim so the next session does not have to
re-derive it.

Source bundle: `SpectraKeys.plugin` (universal, x86_64 + arm64).
Binary MD5 `79960266bd76621d60213efd98b4ca60`, SHA-256 and size in `specs/SpectraKeys.json`.

## Recovered and verified

Implemented in `spectra.cpp`, exercised by `selfcheck.cpp` (30 checks via
`npm test`). Each has the arm64 instructions cited inline in the source.

| Function | Recovered result |
|---|---|
| `clampf` | `value < lo ? lo : (value > hi ? hi : value)` |
| `isPowerOfTwo` | `n < 1 ? false : (n & (n-1)) == 0` — **1 counts as a power of two** |
| `linearToDb` | `20 * log10(max(x, 1e-9f))` — amplitude, factor 20, with a log(0) floor |
| `dbToLinear` | `10^(db/20)` — tail-calls `exp10f` |
| `hannWindow` | `0.5 * (1 - cos(2*pi*i/n))` — **periodic** (divides by `n`, not `n-1`); `n == 1` special-cased to `1.0f` |
| `hannCoherentGain` | constant `0.5f` |

The `hannCoherentGain` / `hannWindow` pair is the load-bearing check: recovered
from two unrelated code paths, and the mean of the recovered window equals the
recovered constant exactly at every size tested. That agreement is what justifies
trusting the rest of the transcription.

### Immediates worth recording

Decoding the raw bit patterns is what turns apparently-arbitrary constants into
meaning. Verified in `test/spectra.test.mjs`:

| Immediate | Decodes to | Meaning |
|---|---|---|
| `0x3089705f` | `9.9999997e-10` | `1e-9f` — the log(0) guard in `linearToDb` |
| `0x40c90fdb` | `6.2831855` | `float(2*pi)` in `hannWindow` |
| `0x3f000000` | `0.5` | the Hann 0.5 multiplier (emitted as `movi.4s #0x3f, lsl #24`) |
| `0x3f800000` | `1.0` | the `n == 1` fill |

## Recovered from reading, not yet implemented

### `downmixToMono(const float*, size_t frames, int channels)`

`__ZN7spectra13downmixToMonoEPKfmi`, recovered shape:

```
cbz  x0, <ret>          ; nullptr -> return empty
cmp  w2, #0x1 / b.lt    ; channels < 1 -> return empty
... resize(frames) ...
cbz  x20, <ret>         ; frames == 0 -> return empty
loop: sum the `channels` interleaved samples, then
      fdiv s1, s1, s0   ; divide the running sum by (float)channels
      str  s1, [x10, x8, lsl #2]
```

**It averages the channels, it does not sum them.** Worth stating because a
downmix that sums is the more common implementation, and the difference is a
straight level change of `channels`.

### `fftRadix2(std::vector<float>& re, std::vector<float>& im)`

`__ZN7spectra9fftRadix2E...`. Only the guard clause is recovered so far, and it
is unusually explicit — the function returns without touching anything unless
all four conditions hold:

```
ldp  x19, x8, [x0]      ; re.data, re.end
asr  x20, x8, #2        ; n = re.size()
cmp  x20, #0x2 / b.lo   ; 1. n >= 2
ldp  x21, x9, [x1]
cmp  x9, x8 / b.ne      ; 2. im.size() == re.size()   (byte-compare)
subs w9, w8, #1
and  w8, w8, w9         ; n & (n-1)
ccmp / b.eq             ; 3. isPowerOfTwo(n), via the helper above
```

Condition 2 compares *byte lengths*, so it is a size equality check, not a
capacity check.

The body is 101 instructions, and its **structure** is now recovered even though
the exact indexing is not yet transcribed instruction-by-instruction. Evidence:

- **Exactly one external call: `___sincosf_stret`.** The twiddles are computed by
  a single combined sin+cos, not separate `sinf`/`cosf` calls. There is no other
  call in the function.
- **The angle immediate is `0xc0c90fdb`, which is `-2*pi`** — the negative twin of
  the `0x40c90fdb` (`+2*pi`) found in `hannWindow`. A forward FFT needs
  `exp(-2*pi*i*k/N)`, so the sign is the forward convention and no conjugation is
  required afterwards.
- **`fneg s2, s0` immediately after the call** negates the sine result, which is
  consistent with using `-sin` from the twiddle pair rather than post-negating.
- **The stride arithmetic is `lsr x9, x22, #1` (= N/2, the butterfly's partner
  offset) and `lsl x10, x22, #2` (= N*4 bytes, the per-stage stride).** The loads
  then read `[x13]` and `[x13, x9, lsl #2]` for the real part and the same pair
  for the imaginary part — i.e. `a[k]` and `a[k + N/2]`, the decimation-in-time
  pairing.
- **The opcode mix is the butterfly and nothing else:** 4 `fmadd`, 3 `fmul`,
  2 `fadd`, 2 `fsub`, 1 `fnmul`, 1 `fneg`. That is one complex
  multiply-accumulate (2 `fmadd` + 2 `fmul`) feeding a sum/difference pair
  (2 `fadd` + 2 `fsub`), plus the `-sin` handling.

So: an **iterative radix-2 decimation-in-time Cooley-Tukey FFT**, forward
convention, twiddles from a single `sincosf`.

**What is NOT established:** the exact twiddle table indexing per stage (whether
it steps by `N/2^stage` or recomputes), whether bit-reversal is performed inside
this function or by the caller, and how many stages the loop nest actually runs.

That distinction matters for the code: **no implementation of `fftRadix2` is
provided.** Implementing a textbook radix-2 FFT and labelling it "recovered" would
be a claim this evidence does not support, and an unverified FFT would be worse
than an honest gap. The remaining work is instruction-level transcription of the
loop nest, then property tests against a naive DFT.

### `SK_DrawSpectrumIntoFrame` — the render pipeline

`__Z24SK_DrawSpectrumIntoFrameP9PF_InDataP10PF_OutDataPP11PF_ParamDefP11PF_LayerDef`
is the clearest read on how the effect actually works. Recovered outline:

1. Constructs an `AEGP_SuiteHandler` from `in_data->pica_basicP`.
2. `SK_FetchLayerAudio(in_data, param, 30.0, &mono, &sampleRate)` — the third
   argument is a **double 30.0**, i.e. it analyses a fixed 30 seconds of layer
   audio, not the whole layer.
3. Falls back through `max(<fetched>, 0.0)` and a large constant before
   configuring analysis.
4. `SK_FftSizeFromParam(param)` derives the FFT size from a parameter.
5. Divides by the sample rate, guarding a zero rate to `1.0`, then **floors the
   window duration at 30.0** (`fcsel d0, d1, d0, ls`).
6. Calls `spectra::analyse(mono, frames, config)`.
7. For each band, builds a normalised value: `exp10(...)` → index into the
   spectrogram → `linearToDb` → `(db + 100.0) / 100.0` → `clampf(_, 0.0, 1.0)`.
   So the 0..1 mapping is **dB in [-100, 0]**, which ties directly to the
   recovered `linearToDb` and `clampf`.
8. `SK_PublishOverlaySpectrum(spectrum, seconds)` then bakes into the output
   frame through separate 8-bit and 16-bit paths (`strb` vs `strh`), with the
   channel count read from `PF_LayerDef` and each written sample clamped.

Constants seen in this function include `100.0` (`0x4059000000000000`), `30.0`,
`3.0`, `2.0` and `1.0`. The `-100..0 dB` normalisation is the notable one.

This is enough to reconstruct the effect's *shape* with confidence. The band
loop's colour maths is not yet recovered.

## Not recovered

- `spectra::analyse`, `analyseBands`, `rawBandLevel`, `bandValues` bodies
- `Spectrogram::hzToBin` body (present at `0x0b48`, not yet transcribed)
- the `fftRadix2` butterfly
- `SK_*` overlay drawing, `SK_BakeKeyframes`, `SK_FetchLayerAudio` bodies
- the other 15 effects' bodies

## Method that worked

`otool -tV -arch arm64 <binary>` plus `nm -C`. Two cautions learned the hard way:

- **Do not pipe a wide `sed -n '/sym:/,/^$/p'` range into a dump.** otool's blank
  lines fall between functions unpredictably, so a range meant for one function
  can swallow a dozen. Read one function at a time, or grep for its own address
  bounds.
- Decode float immediates rather than eyeballing them. `0x3089705f` reads like an
  arbitrary epsilon and is exactly `1e-9f`; `0x40c90fdb` reads like noise and is
  exactly `2*pi`.
