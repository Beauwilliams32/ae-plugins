// Self-check for the recovered glitch:: namespace.
//
// Like the other namespaces: every assertion below is anchored to something the
// disassembly actually establishes, not to what the maths "ought" to do.

#include "glitch.h"

#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

// Whether this build of the self-check exercises the 16-bit instantiation.
//
// Naming `glitch::apply<unsigned short>` ANYWHERE in this translation unit
// would instantiate the template and put the symbol back into the differ's
// comparison — so a commit at which the 16-bit body is still outstanding must
// not name it here either, and this switch is how that is expressed. It is 0
// exactly when the recovered source does not instantiate the 16-bit body.
#ifndef GLITCH_HAVE_16BIT
#define GLITCH_HAVE_16BIT 0
#endif

namespace {

int failures = 0;
int passes = 0;
bool differentialRan = false;

void check(bool ok, const char* why) {
  if (ok) { ++passes; std::printf("  PASS %s\n", why); }
  else    { ++failures; std::printf("  FAIL %s\n", why); }
}

void checkEqI(int got, int want, const char* why) {
  if (got == want) { ++passes; std::printf("  PASS %s\n", why); }
  else { ++failures; std::printf("  FAIL %s  (got %d want %d)\n", why, got, want); }
}

void checkEqU(unsigned got, unsigned want, const char* why) {
  if (got == want) { ++passes; std::printf("  PASS %s\n", why); }
  else { ++failures; std::printf("  FAIL %s  (got %u want %u)\n", why, got, want); }
}

// ---------------------------------------------------------------------------
// A deterministic 8-bit frame and the helpers the apply<T> checks drive.
//
// ONE PIXEL IS FOUR ELEMENTS — A, R, G, B — and View<T>::rowBytes is a stride in
// ELEMENTS of T, so a tightly packed row is width * 4 for BOTH depths. (The 16-bit
// build doubles it to bytes: `lsl x2, x2, #1` at 0x1cec in the 16-bit listing.)
// ---------------------------------------------------------------------------
constexpr int kW = 16;
constexpr int kH = 12;
constexpr long kRowBytes = kW * 4;
constexpr long kFrameBytes = kH * kRowBytes;

// The sentinel the buffers are filled with, so "the output was never written"
// is a thing the test can actually see.
constexpr unsigned char kSentinel = 0xEE;

struct Buf8 {
  std::vector<unsigned char> bytes;
  explicit Buf8(long n) : bytes((size_t)(n < 0 ? 0 : n), kSentinel) {}
  unsigned char* p() { return bytes.data(); }
  const unsigned char* p() const { return bytes.data(); }
};

bool framesEqual(const Buf8& a, const Buf8& b) {
  return a.bytes.size() == b.bytes.size() &&
         std::memcmp(a.p(), b.p(), a.bytes.size()) == 0;
}

bool framesEqual(const Buf8& a, const Buf8& b, long n) {
  return std::memcmp(a.p(), b.p(), (size_t)n) == 0;
}

// True when every byte in [from, to) is still the sentinel.
bool allSentinel(const Buf8& a, long from, long to) {
  for (long i = from; i < to; ++i) {
    if (a.p()[i] != kSentinel) return false;
  }
  return true;
}

bool allSentinel(const Buf8& a) { return allSentinel(a, 0, (long)a.bytes.size()); }

// The alpha ramp is deliberately NOT a multiple of 51, so "alpha was quantised"
// and "alpha was copied" are distinguishable.
void fillFrame(Buf8& b, bool blackWithAlphaRamp) {
  for (int y = 0; y < kH; ++y) {
    for (int x = 0; x < kW; ++x) {
      unsigned char* q = b.p() + (long)y * kRowBytes + (long)x * 4;
      q[0] = (unsigned char)((x * 37 + y * 53) & 0xff);
      q[1] = (unsigned char)(blackWithAlphaRamp ? 0 : ((x * 13 + y * 7) & 0xff));
      q[2] = (unsigned char)(blackWithAlphaRamp ? 0 : ((x * 5 + y * 3) & 0xff));
      q[3] = (unsigned char)(blackWithAlphaRamp ? 0 : ((x * 11 + y * 17) & 0xff));
    }
  }
}

// The render entry point the checks drive.
//
// `apply<T>` takes its output view by REFERENCE — that is part of the recovered
// signature, and the mangled name records it — so the view has to be an lvalue at the
// call. Building both views here keeps every call site a single expression.
void render8(Buf8& in, int inW, int inH, Buf8& out, int outW, int outH,
             const glitch::Params& p, unsigned seed, float maxValue) {
  glitch::View<const unsigned char> vi{in.p(), inW, inH, kRowBytes};
  glitch::View<unsigned char> vo{out.p(), outW, outH, kRowBytes};
  glitch::apply<unsigned char>(vi, vo, p, seed, maxValue);
}

glitch::Params baseParams() {
  glitch::Params p;
  p.mode = 1;
  p.amount = 1.0f;
  p.blockSize = 5;
  p.hashInput = 12345;
  p.rate = 2.0f;
  p.shift = 3.0f;
  p.scanline = 4;
  p.luma = 0;
  p.spread = 1.0f;
  return p;
}

// ---------------------------------------------------------------------------
// apply<T> — the render engine.
//
// These are BEHAVIOURAL assertions, not instruction-level ones: `apply<T>` is
// a behavioural recovery (see the header and RECOVERY-NOTES.md), so the checks
// pin the SEMANTICS rather than the instruction stream.
//
// Every claim below was checked against the SHIPPED bundle BEFORE it was
// checked against this source. The shipped GlitchKit binary was dlopen'd and
// the real mangled symbols were called with the same parameters and the same
// frames; each assertion holds for the shipped code as well. The listing
// address for each behaviour is quoted so the claim can be re-read.
//
// The 8-bit claims are here; the 16-bit twin has its own block.
// ---------------------------------------------------------------------------
void applyChecks8() {
  // --- an empty frame leaves the output completely untouched (0x738..0x744) ---
  //
  // The shipped guard returns early for (width >= 1 && height <= 0) and falls
  // through into the body for a zero width — but every loop in every mode body
  // is empty in that case, so the shipped bundle writes NOTHING for any
  // degenerate shape, in any mode, with any amount and spread.
  {
    Buf8 in(kFrameBytes);
    fillFrame(in, false);
    const glitch::Params p = baseParams();

    Buf8 out(kFrameBytes);
    render8(in, 0, kH, out, kW, kH, p, 1u, 255.0f);
    check(allSentinel(out), "a zero-width input leaves the output untouched");

    Buf8 out2(kFrameBytes);
    render8(in, kW, 0, out2, kW, kH, p, 1u, 255.0f);
    check(allSentinel(out2), "a zero-height input leaves the output untouched");

    Buf8 out3(kFrameBytes);
    render8(in, 0, 0, out3, kW, kH, p, 1u, 255.0f);
    check(allSentinel(out3), "an empty input leaves the output untouched");
  }

  // --- amount: the early copy-and-return (0x764..0x79c) ---
  //
  // The amount is capped at 1, and the LOWER test is made against the
  // ORIGINAL value, so a negative amount stays negative and takes the same
  // early return a zero amount takes.
  {
    Buf8 in(kFrameBytes);
    fillFrame(in, false);

    Buf8 a0(kFrameBytes);
    glitch::Params p0 = baseParams();
    p0.amount = 0.0f;
    render8(in, kW, kH, a0, kW, kH, p0, 7u, 255.0f);
    check(framesEqual(in, a0), "an amount of 0 copies the frame unchanged");

    Buf8 aNeg(kFrameBytes);
    glitch::Params pNeg = baseParams();
    pNeg.amount = -0.5f;
    render8(in, kW, kH, aNeg, kW, kH, pNeg, 7u, 255.0f);
    check(framesEqual(in, aNeg), "a NEGATIVE amount copies the frame unchanged");
    check(framesEqual(a0, aNeg),
          "a negative amount is indistinguishable from an amount of 0");

    Buf8 a1(kFrameBytes);
    Buf8 a2(kFrameBytes);
    glitch::Params p1 = baseParams();
    p1.amount = 1.0f;
    glitch::Params p2 = baseParams();
    p2.amount = 2.0f;
    render8(in, kW, kH, a1, kW, kH, p1, 7u, 255.0f);
    render8(in, kW, kH, a2, kW, kH, p2, 7u, 255.0f);
    check(framesEqual(a1, a2),
          "an amount above 1 is capped, so 2.0 renders exactly as 1.0 does");
  }

  // --- spread: the same early copy-and-return, and the cap at 1 ---
  {
    Buf8 in(kFrameBytes);
    fillFrame(in, false);

    Buf8 s0(kFrameBytes);
    glitch::Params p0 = baseParams();
    p0.spread = 0.0f;
    render8(in, kW, kH, s0, kW, kH, p0, 7u, 255.0f);
    check(framesEqual(in, s0), "a spread of 0 copies the frame unchanged");

    Buf8 sNeg(kFrameBytes);
    glitch::Params pNeg = baseParams();
    pNeg.spread = -0.5f;
    render8(in, kW, kH, sNeg, kW, kH, pNeg, 7u, 255.0f);
    check(framesEqual(in, sNeg), "a NEGATIVE spread copies the frame unchanged");

    Buf8 s1(kFrameBytes);
    Buf8 s2(kFrameBytes);
    glitch::Params p1 = baseParams();
    p1.spread = 1.0f;
    glitch::Params p2 = baseParams();
    p2.spread = 2.0f;
    render8(in, kW, kH, s1, kW, kH, p1, 7u, 255.0f);
    render8(in, kW, kH, s2, kW, kH, p2, 7u, 255.0f);
    check(framesEqual(s1, s2),
          "a spread above 1 is capped, so 2.0 renders exactly as 1.0 does");
  }

  // --- the catch-all mode copies the frame (0x1784) ---
  //
  // The dispatch is ONE-BASED, so mode 0 and mode 6 both fall through to the
  // catch-all, which copies the input and returns.
  {
    Buf8 in(kFrameBytes);
    fillFrame(in, false);
    for (int m = 0; m <= 6; m++) {
      if (m >= 1 && m <= 5) continue;
      Buf8 o(kFrameBytes);
      glitch::Params p = baseParams();
      p.mode = m;
      render8(in, kW, kH, o, kW, kH, p, 7u, 255.0f);
      check(framesEqual(in, o), "an unknown mode falls through to the copy");
    }
  }

  // --- determinism, which is the effect's headline promise ---
  {
    Buf8 in(kFrameBytes);
    fillFrame(in, false);
    Buf8 a(kFrameBytes);
    Buf8 b(kFrameBytes);
    Buf8 c(kFrameBytes);
    const glitch::Params p = baseParams();
    render8(in, kW, kH, a, kW, kH, p, 42u, 255.0f);
    render8(in, kW, kH, b, kW, kH, p, 42u, 255.0f);
    render8(in, kW, kH, c, kW, kH, p, 43u, 255.0f);
    check(framesEqual(a, b), "the same seed renders the same frame");
    check(!framesEqual(a, c), "a different seed renders a different frame");
    check(!framesEqual(in, a), "mode 1 changes the frame it is given");
  }

  // --- mode 2, RGB Channel Shift (0xac8..0xc7c) ---
  //
  // R is taken from the LEFT of the pixel, B from the RIGHT, and A and G come
  // straight from the pixel itself.
  {
    Buf8 in(kFrameBytes);
    fillFrame(in, false);
    Buf8 o(kFrameBytes);
    glitch::Params p = baseParams();
    p.mode = 2;
    render8(in, kW, kH, o, kW, kH, p, 7u, 255.0f);
    bool alphaSame = true;
    bool greenSame = true;
    bool redDiffers = false;
    bool blueDiffers = false;
    for (int y = 0; y < kH; ++y) {
      for (int x = 0; x < kW; ++x) {
        const long i = (long)y * kRowBytes + (long)x * 4;
        if (o.p()[i + 0] != in.p()[i + 0]) alphaSame = false;
        if (o.p()[i + 2] != in.p()[i + 2]) greenSame = false;
        if (o.p()[i + 1] != in.p()[i + 1]) redDiffers = true;
        if (o.p()[i + 3] != in.p()[i + 3]) blueDiffers = true;
      }
    }
    check(alphaSame, "mode 2 passes alpha straight through");
    check(greenSame, "mode 2 passes green straight through");
    check(redDiffers, "mode 2 moves red in from the left");
    check(blueDiffers, "mode 2 moves blue in from the right");
  }

  // --- mode 4, Bad Signal (0xeb8..0x1248) ---
  //
  // The colour channels are quantised to a step of maxValue/51 at an amount of
  // 1 — fma(1, -250, 256) - 1 == 5, and 255 / 5 == 51 — but the ALPHA is
  // never quantised: it is copied from the source.
  {
    Buf8 in(kFrameBytes);
    fillFrame(in, false);
    Buf8 o(kFrameBytes);
    glitch::Params p = baseParams();
    p.mode = 4;
    render8(in, kW, kH, o, kW, kH, p, 7u, 255.0f);

    bool rgbQuantised = true;
    bool differs = false;
    for (int y = 0; y < kH; ++y) {
      for (int x = 0; x < kW; ++x) {
        const long i = (long)y * kRowBytes + (long)x * 4;
        for (int c = 1; c < 4; ++c) {
          if (o.p()[i + c] % 51 != 0) rgbQuantised = false;
          if (o.p()[i + c] != in.p()[i + c]) differs = true;
        }
      }
    }
    check(rgbQuantised, "mode 4 quantises red, green and blue to a multiple of 51");

    // The alpha is copied from a SOURCE pixel, not quantised. Two separate facts
    // establish that: every output alpha is one of the input's own alpha values,
    // and at least one of them is OFF the quantiser's 51-grid — a quantised alpha
    // could not be off the grid, and an invented one could not be in the input's
    // set either.
    bool alphaInInput = true;
    bool alphaOffGrid = false;
    bool inSet[256] = {false};
    for (int y = 0; y < kH; ++y) {
      for (int x = 0; x < kW; ++x) {
        inSet[in.p()[(long)y * kRowBytes + (long)x * 4]] = true;
      }
    }
    for (int y = 0; y < kH; ++y) {
      for (int x = 0; x < kW; ++x) {
        const long i = (long)y * kRowBytes + (long)x * 4;
        if (!inSet[o.p()[i + 0]]) alphaInInput = false;
        if (o.p()[i + 0] % 51 != 0) alphaOffGrid = true;
      }
    }
    check(alphaInInput, "mode 4 copies the alpha from a source pixel");
    check(alphaOffGrid, "mode 4 never quantises the alpha");
    check(differs, "mode 4 changes the frame it is given");
  }

  // --- mode 5, Datamosh Smear (0x888..0xac8) ---
  //
  // The frame is copied first and then smeared, so the alpha is always one of
  // the input's own alpha values — it is copied, never generated.
  {
    Buf8 in(kFrameBytes);
    fillFrame(in, false);
    Buf8 o(kFrameBytes);
    glitch::Params p = baseParams();
    p.mode = 5;
    render8(in, kW, kH, o, kW, kH, p, 7u, 255.0f);
    // The smear copies whole pixels, so the alpha is always one of the input's own
    // alpha values — and at least one of them is off the quantiser's grid, so it was
    // copied rather than generated.
    bool alphaInInput = true;
    bool alphaOffGrid = false;
    bool differs = false;
    bool inSet[256] = {false};
    for (int y = 0; y < kH; ++y) {
      for (int x = 0; x < kW; ++x) {
        inSet[in.p()[(long)y * kRowBytes + (long)x * 4]] = true;
      }
    }
    for (int y = 0; y < kH; ++y) {
      for (int x = 0; x < kW; ++x) {
        const long i = (long)y * kRowBytes + (long)x * 4;
        if (!inSet[o.p()[i + 0]]) alphaInInput = false;
        if (o.p()[i + 0] % 51 != 0) alphaOffGrid = true;
        for (int c = 0; c < 4; ++c) {
          if (o.p()[i + c] != in.p()[i + c]) differs = true;
        }
      }
    }
    check(alphaInInput, "mode 5 copies the alpha rather than generating it");
    check(alphaOffGrid, "mode 5 leaves the alpha off the quantiser's grid");
    check(differs, "mode 5 changes the frame it is given");
  }

  // --- the five modes are five different effects ---
  {
    Buf8 in(kFrameBytes);
    fillFrame(in, false);
    Buf8 o[5] = {Buf8(kFrameBytes), Buf8(kFrameBytes),
                 Buf8(kFrameBytes), Buf8(kFrameBytes),
                 Buf8(kFrameBytes)};
    for (int m = 1; m <= 5; ++m) {
      glitch::Params p = baseParams();
      p.mode = m;
      render8(in, kW, kH, o[m - 1], kW, kH, p, 7u, 255.0f);
    }
    bool distinct = true;
    for (int a = 0; a < 5; ++a) {
      for (int b = a + 1; b < 5; ++b) {
        if (framesEqual(o[a], o[b])) distinct = false;
      }
    }
    check(distinct, "the five modes render five different frames");
  }

  // --- the output view's width and height are NEVER read ---
  //
  // The loops are driven entirely by the input's dimensions, so a caller that
  // hands over a differently sized output view gets the same writes at the
  // input's stride — the extra rows are never touched.
  {
    Buf8 in(kFrameBytes);
    fillFrame(in, false);
    const glitch::Params p = baseParams();

    Buf8 plain(kFrameBytes);
    render8(in, kW, kH, plain, kW, kH, p, 7u, 255.0f);

    Buf8 tall((long)(kH + 5) * kRowBytes);
    render8(in, kW, kH, tall, kW + 7, kH + 5, p, 7u, 255.0f);
    check(framesEqual(plain, tall, kFrameBytes),
          "the output view's width and height are never read");
    check(allSentinel(tall, kFrameBytes, (long)(kH + 5) * kRowBytes),
          "the rows past the input's height are never written");
  }

  // --- the spread blend (0x189c..0x192c) ---
  //
  //     out[i] = llround(fma(in[i], 1 - spread, spread * mode_out[i]))
  //
  // The blend mixes the ORIGINAL input with the mode's own result.
  {
    Buf8 in(kFrameBytes);
    fillFrame(in, false);

    Buf8 full(kFrameBytes);
    Buf8 half(kFrameBytes);
    glitch::Params pf = baseParams();
    pf.mode = 2;
    pf.spread = 1.0f;
    glitch::Params ph = baseParams();
    ph.mode = 2;
    ph.spread = 0.5f;
    render8(in, kW, kH, full, kW, kH, pf, 7u, 255.0f);
    render8(in, kW, kH, half, kW, kH, ph, 7u, 255.0f);

    bool exact = true;
    for (long i = 0; i < kFrameBytes; ++i) {
      const float res = std::fma(static_cast<float>(in.p()[i]), 0.5f,
                                  0.5f * static_cast<float>(full.p()[i]));
      if (static_cast<long>(llroundf(res)) != static_cast<long>(half.p()[i])) {
        exact = false;
      }
    }
    check(exact, "the spread blend is fma(in, 1 - spread, spread * out), rounded");
  }

  // --- the luma flag drives the effect ---
  //
  // With the luma flag set, the displacement is scaled by the luminance of the
  // frame. On a black frame that factor is zero, so nothing moves.
  {
    Buf8 black(kFrameBytes);
    fillFrame(black, true);

    Buf8 o(kFrameBytes);
    glitch::Params p = baseParams();
    p.mode = 1;
    p.luma = 1;
    render8(black, kW, kH, o, kW, kH, p, 7u, 255.0f);
    check(framesEqual(black, o),
          "with the luma flag set, a black frame has zero luminance and does not move");

    bool someSeedMoves = false;
    for (unsigned seed = 1; seed <= 40u; ++seed) {
      Buf8 s(kFrameBytes);
      glitch::Params q = baseParams();
      q.mode = 1;
      q.luma = 0;
      render8(black, kW, kH, s, kW, kH, q, seed, 255.0f);
      if (!framesEqual(black, s)) someSeedMoves = true;
    }
    check(someSeedMoves, "with the luma flag clear, the frame does move");
  }
}

// ---------------------------------------------------------------------------
// The dlopen differential — the shipped bundle against this source.
//
// `apply<T>` is a BEHAVIOURAL RECOVERY. Its instruction stream is a
// re-expression of the shipped one rather than a copy of it, so the question
// is not "does it read like the listing" but "does it produce the same
// pixels". That question is answered here, by MEASUREMENT, not by assertion:
// the shipped GlitchKit bundle is dlopen'd, its own `glitch::apply` is called
// on the same frames with the same parameters, and the two output buffers are
// compared byte for byte.
//
// The comparison covers the WHOLE output buffer, including the padding past
// the last row, so a stride bug cannot hide in the gap between rows.
//
// If the bundle is not installed the differential is SKIPPED, not failed: the
// self-check has to be runnable on a machine with no After Effects installed.
// `differentialRan` records which happened, so the caller can say so.
// ---------------------------------------------------------------------------

const char* kShippedGlitchKit =
    "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media"
    "/GlitchKit.plugin/Contents/MacOS/GlitchKit";

template <typename T>
using ShippedApplyFn = void (*)(const glitch::View<const T>&,
                                glitch::View<T>&,
                                const glitch::Params&, unsigned, float);

struct DiffCounters {
  long vectors = 0;
  long mismatches = 0;
  long bytes = 0;
  long written = 0;  // vectors where the shipped bundle actually wrote something
};

template <typename T>
void compareOne(ShippedApplyFn<T> shipped, const glitch::Params& p, int w, int h,
                long inRB, long outRB, unsigned seed, float maxValue,
                std::mt19937& rng, DiffCounters& c) {
  const size_t rows = (h > 0) ? (size_t)h : 0;
  const size_t inCount = rows * (size_t)(inRB > 0 ? inRB : 0) + 64;
  const size_t outCount = rows * (size_t)(outRB > 0 ? outRB : 0) + 64;

  std::vector<T> in(inCount);
  for (size_t i = 0; i < inCount; ++i) in[i] = static_cast<T>(rng() & 0xFFFFu);

  const T sentinel = sizeof(T) == 1 ? static_cast<T>(0xCDu) : static_cast<T>(0xCDCDu);
  std::vector<T> a(outCount, sentinel);
  std::vector<T> b(outCount, sentinel);

  glitch::View<const T> vi{in.data(), w, h, inRB};
  glitch::View<T> voA{a.data(), w, h, outRB};
  glitch::View<T> voB{b.data(), w, h, outRB};

  shipped(vi, voA, p, seed, maxValue);
  glitch::apply<T>(vi, voB, p, seed, maxValue);

  ++c.vectors;
  c.bytes += static_cast<long>(outCount);

  // The harness has to be able to SEE a write, or "no mismatches" would mean
  // nothing at all. Count the vectors where the shipped bundle left the
  // sentinel behind somewhere.
  for (size_t i = 0; i < outCount; ++i) {
    if (a[i] != sentinel) { ++c.written; break; }
  }

  if (std::memcmp(a.data(), b.data(), outCount * sizeof(T)) != 0) ++c.mismatches;
}

void runDifferential() {
  void* handle = dlopen(kShippedGlitchKit, RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    std::printf("  SKIP the shipped GlitchKit bundle is not installed, so the "
                "dlopen differential did not run\n");
    return;
  }

  auto ship8 = reinterpret_cast<ShippedApplyFn<unsigned char>>(
      dlsym(handle, "_ZN6glitch5applyIhEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEjf"));
#if GLITCH_HAVE_16BIT
  auto ship16 = reinterpret_cast<ShippedApplyFn<unsigned short>>(
      dlsym(handle, "_ZN6glitch5applyItEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEjf"));
  if (!ship8 || !ship16) {
    std::printf("  SKIP the shipped bundle does not export both apply "
                "instantiations, so the dlopen differential did not run\n");
    dlclose(handle);
    return;
  }
#else
  if (!ship8) {
    std::printf("  SKIP the shipped bundle does not export apply<unsigned char>, "
                "so the dlopen differential did not run\n");
    dlclose(handle);
    return;
  }
#endif

  DiffCounters c;
  std::mt19937 rng(0xC0FFEEu);
  const float nan = std::numeric_limits<float>::quiet_NaN();

  // The mode set covers the five real modes, the catch-all (0) and two
  // out-of-range values that must behave like the catch-all.
  const int modes[] = {-2, 0, 1, 2, 3, 4, 5, 6, 8};
  // The amount and spread sets cover the three interesting regions of the
  // clamp: below zero, at zero, inside (0,1), above one, and NaN.
  const float amounts[] = {-1.0f, 0.0f, 0.25f, 0.5f, 1.0f, 2.0f, nan};
  const float spreads[] = {0.0f, 0.5f, 1.0f, 1.5f, nan};
  const int lumas[] = {0, 1};
  // Shapes: a single pixel, a single row, a single column, an odd size, and
  // one where the frame is not a multiple of the block size.
  const int shapes[][2] = {{1, 1}, {1, 17}, {17, 1}, {16, 12}, {33, 7}, {64, 3}};
  const int kShapeCount = (int)(sizeof(shapes) / sizeof(shapes[0]));

  // --- grid 1: the mode x amount x spread x luma cross-product -------------
  int si = 0;
  for (int mode : modes) {
    for (float amount : amounts) {
      for (float spread : spreads) {
        for (int luma : lumas) {
          const int* shape = shapes[si++ % kShapeCount];
          const int w = shape[0];
          const int h = shape[1];
          // Alternate between a tightly packed frame and one with padding at
          // the end of every row, and between an input and output stride that
          // match and ones that do not.
          const bool padded = (si & 1) != 0;
          const long inRB = (long)w * 4 + (padded ? 5 : 0);
          const long outRB = (long)w * 4 + (padded ? 0 : 3);

          glitch::Params p;
          p.mode = mode;
          p.amount = amount;
          p.spread = spread;
          p.blockSize = 2 + (si % 9);
          p.hashInput = 12345 + si;
          p.rate = (si % 5 == 0) ? 0.0f : 0.5f * (float)(si % 7);
          p.shift = (float)(si % 11);
          p.scanline = 1 + (si % 6);
          p.luma = (unsigned char)luma;

          const unsigned seed = (unsigned)(si * 2654435761u);
          compareOne<unsigned char>(ship8, p, w, h, inRB, outRB, seed, 255.0f, rng, c);
#if GLITCH_HAVE_16BIT
          compareOne<unsigned short>(ship16, p, w, h, inRB, outRB, seed, 65535.0f, rng, c);
#endif
        }
      }
    }
  }

  // --- grid 2: the degenerate shapes -------------------------------------
  // A width or a height below one, and an output view SMALLER than the input:
  // the header claims the output's dimensions are never read, and this is
  // where that claim is measured.
  {
    glitch::Params p = baseParams();
    p.amount = 0.75f;
    p.spread = 0.5f;
    for (int mode : modes) {
      p.mode = mode;
      const int dims[][2] = {{0, 5}, {5, 0}, {0, 0}, {1, 1}};
      for (const auto& d : dims) {
        compareOne<unsigned char>(ship8, p, d[0], d[1], 4, 4, 3u, 255.0f, rng, c);
#if GLITCH_HAVE_16BIT
        compareOne<unsigned short>(ship16, p, d[0], d[1], 4, 4, 3u, 65535.0f, rng, c);
#endif
      }
      // An output view that is smaller than the input's frame.
      compareOne<unsigned char>(ship8, p, 16, 12, 64, 64, 5u, 255.0f, rng, c);
#if GLITCH_HAVE_16BIT
      compareOne<unsigned short>(ship16, p, 16, 12, 64, 64, 5u, 65535.0f, rng, c);
#endif
    }
  }

  // --- grid 3: the clamps at their extremes ------------------------------
  // blockSize and scanline are clamped to 2..4096 and 1..4096, and both ends
  // of those ranges are where an off-by-one would show.
  {
    const int sizes[] = {0, 1, 2, 3, 4095, 4096, 4097, 100000};
    glitch::Params p = baseParams();
    p.amount = 1.0f;
    p.spread = 0.75f;
    for (int mode : {1, 3, 5}) {
      p.mode = mode;
      for (int s : sizes) {
        p.blockSize = s;
        p.scanline = s;
        compareOne<unsigned char>(ship8, p, 33, 7, 33 * 4, 33 * 4, 11u, 255.0f, rng, c);
#if GLITCH_HAVE_16BIT
        compareOne<unsigned short>(ship16, p, 33, 7, 33 * 4, 33 * 4, 11u, 65535.0f, rng, c);
#endif
      }
    }
  }

  // --- grid 4: a seed sweep ----------------------------------------------
  // The effect's headline promise is that a seed reproduces a frame, and the
  // per-frame hash is what makes each seed land somewhere different.
  {
    glitch::Params p = baseParams();
    p.amount = 0.9f;
    p.spread = 0.6f;
    for (int mode : {1, 2, 3, 4, 5}) {
      p.mode = mode;
      for (unsigned seed = 0; seed < 64u; ++seed) {
        compareOne<unsigned char>(ship8, p, 24, 9, 24 * 4, 24 * 4, seed, 255.0f, rng, c);
#if GLITCH_HAVE_16BIT
        compareOne<unsigned short>(ship16, p, 24, 9, 24 * 4, 24 * 4, seed, 65535.0f, rng, c);
#endif
      }
    }
  }

  // --- grid 5: a shift sweep, for the RGB channel shift -------------------
  {
    glitch::Params p = baseParams();
    p.mode = 2;
    p.amount = 1.0f;
    p.spread = 0.8f;
    for (float shift : {0.0f, 0.5f, 1.0f, 7.5f, 64.0f, 1000.0f}) {
      p.shift = shift;
      for (int luma : {0, 1}) {
        p.luma = (unsigned char)luma;
        compareOne<unsigned char>(ship8, p, 40, 6, 40 * 4, 40 * 4, 17u, 255.0f, rng, c);
#if GLITCH_HAVE_16BIT
        compareOne<unsigned short>(ship16, p, 40, 6, 40 * 4, 40 * 4, 17u, 65535.0f, rng, c);
#endif
      }
    }
  }

  // --- grid 6: every mode against every shape ----------------------------
  // The cross-product that grid 1 samples rather than enumerates: all six
  // shapes, all nine mode values, a low, a middle and a full amount and
  // spread, both luma settings, and a padded stride on one side.
  {
    glitch::Params p;
    p.hashInput = 777u;
    p.rate = 1.25f;
    p.blockSize = 7;
    p.scanline = 3;
    p.shift = 2.5f;
    const float amounts3[] = {0.25f, 0.75f, 1.0f};
    const float spreads3[] = {0.25f, 0.75f, 1.0f};
    for (const auto& shape : shapes) {
      const int w = shape[0];
      const int h = shape[1];
      const long packedRB = (long)w * 4;
      for (int mode : modes) {
        p.mode = mode;
        for (float amount : amounts3) {
          for (float spread : spreads3) {
            for (int luma : lumas) {
              p.amount = amount;
              p.spread = spread;
              p.luma = (unsigned char)luma;
              compareOne<unsigned char>(ship8, p, w, h, packedRB, packedRB, 9u, 255.0f, rng, c);
#if GLITCH_HAVE_16BIT
              compareOne<unsigned short>(ship16, p, w, h, packedRB, packedRB, 9u, 65535.0f, rng, c);
#endif
              // And once with a padded input stride, so the source stride is
              // not always the tightly packed one.
              compareOne<unsigned char>(ship8, p, w, h, packedRB + 4, packedRB, 9u, 255.0f, rng, c);
#if GLITCH_HAVE_16BIT
              compareOne<unsigned short>(ship16, p, w, h, packedRB + 4, packedRB, 9u, 65535.0f, rng, c);
#endif
            }
          }
        }
      }
    }
  }

  dlclose(handle);
  differentialRan = true;

  std::printf("glitch::apply<T> against the shipped bundle (dlopen differential)\n");
  check(c.mismatches == 0,
        "every output buffer matches the shipped bundle byte for byte");
  std::printf("  vectors=%ld bytes-compared=%ld mismatching-vectors=%ld "
              "vectors-with-a-write=%ld\n",
              c.vectors, c.bytes, c.mismatches, c.written);
  // If the harness could not see a single write it would agree with anything,
  // so the count of vectors the bundle actually wrote to has to be non-zero.
  check(c.written > 0, "the differential really did write to the output buffers");
  check(c.vectors > 1000, "the differential compared a meaningful number of vectors");
}

}  // namespace

int main() {
  std::printf("glitch::clampf / clampi\n");
  {
    check(glitch::clampf(5.0f, 0.0f, 10.0f) == 5.0f, "in range passes through");
    check(glitch::clampf(-1.0f, 0.0f, 10.0f) == 0.0f, "below lo clamps to lo");
    check(glitch::clampf(11.0f, 0.0f, 10.0f) == 10.0f, "above hi clamps to hi");
    // The low bound is tested LAST, so it wins on an inverted range. This is the
    // detail shared with the other three clampf copies in the fleet.
    check(glitch::clampf(5.0f, 10.0f, 0.0f) == 10.0f,
          "an INVERTED range yields lo, because lo is tested last");

    checkEqI(glitch::clampi(5, 0, 10), 5, "clampi in range");
    checkEqI(glitch::clampi(-1, 0, 10), 0, "clampi below lo");
    checkEqI(glitch::clampi(11, 0, 10), 10, "clampi above hi");
    checkEqI(glitch::clampi(5, 10, 0), 10, "clampi also yields lo on an inverted range");
    // Integer extremes must not overflow or wrap.
    checkEqI(glitch::clampi(2147483647, 0, 100), 100, "clampi handles INT_MAX");
    checkEqI(glitch::clampi(-2147483647 - 1, 0, 100), 0, "clampi handles INT_MIN");
  }

  std::printf("glitch::patternFrame\n");
  {
    // seed * time, truncated. The multiply is in double, so a large seed with a
    // fractional time does not round the way a float multiply would.
    checkEqU(glitch::patternFrame(1000u, 0.5f), 500u, "seed * time, truncated");
    checkEqU(glitch::patternFrame(1000u, 2.999f), 2999u, "truncation, not rounding");
    checkEqU(glitch::patternFrame(7u, 1.0f), 7u, "one frame in");

    // The guard is a STRICT > 0.
    checkEqU(glitch::patternFrame(1000u, 0.0f), 0u, "time 0 yields 0, not the seed");
    checkEqU(glitch::patternFrame(1000u, -1.0f), 0u, "negative time yields 0");
    checkEqU(glitch::patternFrame(0u, 5.0f), 0u, "seed 0 yields 0 at any time");

    // Double, not float: 16777217 cannot be represented in float, so a float
    // multiply would round it and pick a different frame.
    const unsigned big = glitch::patternFrame(16777217u, 1.0f);
    checkEqU(big, 16777217u, "the multiply really is in double (16777217 survives)");

    // Monotone in time for a fixed seed.
    bool mono = true;
    unsigned prev = 0;
    for (int i = 0; i <= 40; ++i) {
      const unsigned f = glitch::patternFrame(500u, static_cast<float>(i) * 0.7f);
      if (f < prev) mono = false;
      prev = f;
    }
    check(mono, "patternFrame is non-decreasing in time");
  }

  std::printf("glitch::hashKey\n");
  {
    // Determinism is the effect's headline promise.
    checkEqU(glitch::hashKey(1u, 2u, 3u), glitch::hashKey(1u, 2u, 3u),
             "same inputs give the same hash");
    check(glitch::hashKey(1u, 2u, 3u) != glitch::hashKey(3u, 2u, 1u),
          "argument order matters");

    // Avalanche: a one-bit change in any input should change roughly half the
    // output bits, and must not leave the output unchanged.
    auto popcount = [](unsigned v) { int n = 0; while (v) { n += v & 1u; v >>= 1; } return n; };
    int minFlips = 32, totalFlips = 0, cases = 0;
    for (unsigned a = 0; a < 64; ++a) {
      for (unsigned b = 0; b < 4; ++b) {
        const unsigned base = glitch::hashKey(a, b, 0u);
        for (int bit = 0; bit < 32; bit += 7) {
          const unsigned alt = glitch::hashKey(a ^ (1u << bit), b, 0u);
          const int flips = popcount(base ^ alt);
          if (flips < minFlips) minFlips = flips;
          totalFlips += flips;
          ++cases;
        }
      }
    }
    check(cases > 0, "the avalanche sweep ran");
    check(minFlips > 0, "every single-bit input change changes the output");
    const double avgBits = static_cast<double>(totalFlips) / static_cast<double>(cases);
    check(avgBits > 8.0 && avgBits < 24.0,
          "single-bit changes flip a plausible number of output bits");

    // The `csinc ... ne` tail: the result is never left at 0 by the guard's
    // "equal" branch. Scanning a spread of inputs, no output is 0 unless the
    // mixed value genuinely was.
    bool anyZero = false;
    for (unsigned a = 0; a < 2000; ++a) {
      if (glitch::hashKey(a, a * 7u, a * 13u) == 0u) anyZero = true;
    }
    check(!anyZero, "the guard keeps the result away from a run of zeros");

    // Determinism across a wider sweep.
    bool stable = true;
    for (unsigned a = 0; a < 500; ++a) {
      if (glitch::hashKey(a, 1u, 2u) != glitch::hashKey(a, 1u, 2u)) stable = false;
    }
    check(stable, "hashKey is stable across 500 inputs");
  }

  std::printf("glitch::apply<unsigned char>\n");
  applyChecks8();

  // The oracle. `apply<T>` is a behavioural recovery, so the shipped bundle is
  // the authority: this calls the bundle's own apply on the same frames and
  // compares the output buffers byte for byte.
  runDifferential();
  if (!differentialRan) {
    std::printf("  NOTE the dlopen differential did not run — the shipped "
                "GlitchKit bundle is not installed, so the apply<T> recovery "
                "is NOT output-verified on this machine\n");
  }

  std::printf("\n%s\n", failures == 0
                ? "ALL RECOVERED GLITCH MATHS VERIFIED"
                : "TRANSCRIPTION ERRORS PRESENT");
  return failures == 0 ? 0 : 1;
}
