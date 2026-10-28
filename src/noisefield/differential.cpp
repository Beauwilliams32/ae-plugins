// differential.cpp — the recovered noisefield:: code, measured against the SHIPPED bundle.
//
// WHY THIS FILE EXISTS
//
// The differ (`tools/codegen-diff.mjs`) compares INSTRUCTION STREAMS. It can say
// "these two functions differ"; it cannot say "these two functions are the same
// effect". For `noisefield::apply<unsigned char>` and `apply<unsigned short>` the
// recovered code is a BEHAVIOURAL recovery: the semantics were read off the arm64
// listing, but "read off the listing" is a claim, not a measurement.
//
// This harness is the measurement. It dlopen()s the installed NoiseField bundle,
// resolves the SHIPPED functions by their mangled names, calls them at the load
// base's own offsets (verified against the bundle's own symbol table, not assumed),
// and compares the WHOLE destination buffer, byte for byte, against the recovered
// implementation over a large vector grid.
//
// It is deliberately NOT a MAP_JIT copy of the shipped code: the shipped function is
// called in the loader-mapped bundle, at its own address, which is the strong form.
//
// THE ORACLE IS THE BUNDLE. Nothing in this file decides what the right answer is;
// the bundle does.
//
// Usage:
//   clang++ -std=c++17 -O2 -fvisibility=hidden -I src/noisefield \
//       -o /tmp/nfdiff src/noisefield/differential.cpp src/noisefield/noisefield.cpp
//   /tmp/nfdiff [bundle-path]
//
// Exit status: 0 = clean (every comparison bit-identical), 1 = a mismatch was
// found, 2 = the harness could not run (bundle missing, symbol not exported).

#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include "noisefield.h"

using namespace noisefield;

namespace {

// The installed bundle. Read-only: this harness never writes to it.
const char* kDefaultBundle =
    "/Applications/Adobe After Effects 2026/Plug-ins/Williams Media"
    "/NoiseField.plugin/Contents/MacOS/NoiseField";

// The offsets the bundle's own arm64 symbol table gives. These are the values
// `nm -arch arm64 NoiseField` prints, and the harness CHECKS the resolved
// addresses against them, so a bundle that changed under it would fail loudly
// rather than quietly compare the wrong function.
//
//   00000000000006e8 T _ZN10noisefield5applyIhEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEf
//   0000000000001284 T _ZN10noisefield5applyItEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEf
//   0000000000000efc T _ZN10noisefield7fieldAtERKNS_9PermTableEffRKNS_6ParamsE
//   000000000000191c T _ZN10noisefield6noise3ERKNS_9PermTableEfff
//   0000000000001c34 T _ZN10noisefield4fbm3ERKNS_9PermTableEfffiff
//   0000000000000d48 T _ZN10noisefield9PermTable5buildEj
struct Expected {
  const char* mangled;
  uintptr_t offset;
};

const Expected kExpected[] = {
    {"_ZN10noisefield5applyIhEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEf", 0x6e8},
    {"_ZN10noisefield5applyItEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEf", 0x1284},
    {"_ZN10noisefield7fieldAtERKNS_9PermTableEffRKNS_6ParamsE", 0xefc},
    {"_ZN10noisefield6noise3ERKNS_9PermTableEfff", 0x191c},
    {"_ZN10noisefield4fbm3ERKNS_9PermTableEfffiff", 0x1c34},
    {"_ZN10noisefield9PermTable5buildEj", 0xd48},
};

void* g_handle = nullptr;

// The resolved shipped functions.
using ApplyFn = void (*)(const View<const unsigned char>&, View<unsigned char>&, const Params&, float);
using ApplyFn16 = void (*)(const View<const unsigned short>&, View<unsigned short>&, const Params&, float);
using FieldAtFn = Vec2 (*)(const PermTable&, float, float, const Params&);
using Noise3Fn = float (*)(const PermTable&, float, float, float);
using Fbm3Fn = float (*)(const PermTable&, float, float, float, int, float, float);
using BuildFn = void (*)(PermTable*, unsigned int);

ApplyFn gShipU8 = nullptr;
ApplyFn16 gShipU16 = nullptr;
FieldAtFn gShipFieldAt = nullptr;
Noise3Fn gShipNoise3 = nullptr;
Fbm3Fn gShipFbm3 = nullptr;
BuildFn gShipBuild = nullptr;

// Per pixel type counters, so the report can separate the two apply<T>s.
struct Counters {
  long long vectors = 0;
  long long mismatchingVectors = 0;
  long long differingBytes = 0;
  long long differingElements = 0;
};

Counters gU8;
Counters gU16;
long long gScalarMismatches = 0;
bool gFirstMismatchPrinted = false;

void reportFirstMismatch(const char* label, int w, int h, const Params& p, long long bad) {
  if (gFirstMismatchPrinted) return;
  gFirstMismatchPrinted = true;
  std::printf("FIRST_MISMATCH %s w=%d h=%d mode=%d edge=%d disp=%d oct=%d amount=%g scale=%g "
              "freq=%g gain=%g seed=%u evo=%g badElements=%lld\n",
              label, w, h, p.mode, p.edgeMode, p.dispMode, p.octaves, p.amount, p.scale,
              p.freqMult, p.gain, p.seed, p.evolution, bad);
}

// One render vector: build a random source, run the SHIPPED apply and the RECOVERED
// apply on identical inputs and identical destinations, then compare every byte of
// the destination buffers.
template <class T>
void oneVector(void (*shipped)(const View<const T>&, View<T>&, const Params&, float),
               void (*mine)(const View<const T>&, View<T>&, const Params&, float), int w, int h,
               Params p, float time, unsigned long long seed, long long pad) {
  // The stride is in ELEMENTS (see the header), and `pad` adds slack so a wrong
  // stride handling shows up as a byte difference rather than as a crash.
  const long long stride = (long long)w * 4 + pad;
  const size_t count = (size_t)(stride * h) + 8;
  std::vector<T> src(count), a(count), b(count);
  std::mt19937_64 rng(seed);
  for (size_t i = 0; i < count; ++i) src[i] = (T)(rng() & 0xffff);
  std::memcpy(a.data(), src.data(), count * sizeof(T));
  std::memcpy(b.data(), src.data(), count * sizeof(T));

  View<const T> sv{src.data(), w, h, stride};
  View<T> da{a.data(), w, h, stride};
  View<T> db{b.data(), w, h, stride};

  shipped(sv, da, p, time);
  mine(sv, db, p, time);

  Counters& c = (sizeof(T) == 1) ? gU8 : gU16;
  ++c.vectors;

  size_t bad = 0;
  for (size_t i = 0; i < count; ++i)
    if (std::memcmp(&a[i], &b[i], sizeof(T)) != 0) ++bad;
  if (bad) {
    ++c.mismatchingVectors;
    c.differingElements += (long long)bad;
    c.differingBytes += (long long)bad * (long long)sizeof(T);
    reportFirstMismatch(sizeof(T) == 1 ? "apply<unsigned char>" : "apply<unsigned short>", w, h, p,
                        (long long)bad);
  }
}

// The image NAME for a loaded Mach-O, found the way dyld itself indexes them:
// `_dyld_get_image_name` takes an image INDEX, so the base is matched against each
// loaded image's header. `dladdr`'s own `dli_fname` is used as the cross-check.
const char* imageNameForBase(uintptr_t base) {
  for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
    const struct mach_header* header = _dyld_get_image_header(i);
    if (reinterpret_cast<uintptr_t>(header) == base) {
      const char* name = _dyld_get_image_name(i);
      if (name) return name;
    }
  }
  return nullptr;
}

// Resolve a shipped symbol AND check that it sits at the offset the bundle's own
// symbol table claims, relative to the image base _dyld_get_image_name reports.
void* resolveChecked(const char* mangled, uintptr_t expectedOffset, uintptr_t* baseOut) {
  void* sym = dlsym(g_handle, mangled);
  if (!sym) {
    std::fprintf(stderr, "dlsym failed for %s: %s\n", mangled, dlerror());
    std::exit(2);
  }
  Dl_info info;
  if (dladdr(sym, &info) == 0 || info.dli_fbase == nullptr) {
    std::fprintf(stderr, "dladdr failed for %s\n", mangled);
    std::exit(2);
  }
  const char* image = imageNameForBase((uintptr_t)info.dli_fbase);
  if (!image) {
    std::fprintf(stderr, "_dyld_get_image_name has no image at base 0x%zx for %s\n",
                 (uintptr_t)info.dli_fbase, mangled);
    std::exit(2);
  }
  const uintptr_t base = (uintptr_t)info.dli_fbase;
  const uintptr_t actual = (uintptr_t)sym - base;
  if (actual != expectedOffset) {
    std::fprintf(stderr,
                 "%s resolved at base+0x%zx but the bundle's symbol table says 0x%zx (image %s)\n",
                 mangled, actual, expectedOffset, image);
    std::exit(2);
  }
  if (baseOut) *baseOut = base;
  std::printf("SYMBOL_OK %s base=0x%zx offset=0x%zx image=%s\n", mangled, base, actual, image);
  return sym;
}

// ---------------------------------------------------------------------------
// The scalar helpers: the renderer leans on all of them, so each is compared
// directly against the shipped one as well.
// ---------------------------------------------------------------------------
void compareScalars() {
  PermTable mine, theirs;
  for (unsigned s : {0u, 1u, 2u, 0x9e3779b9u, 12345u}) {
    mine.build(s);
    gShipBuild(&theirs, s);
    if (std::memcmp(&mine, &theirs, sizeof(PermTable)) != 0) {
      std::printf("SCALAR_MISMATCH PermTable::build seed=%u\n", s);
      ++gScalarMismatches;
    }
  }

  std::mt19937_64 rng(99);
  for (int i = 0; i < 20000; ++i) {
    const float x = (float)(rng() % 20000) / 100.0f - 100.0f;
    const float y = (float)(rng() % 20000) / 100.0f - 100.0f;
    const float z = (float)(rng() % 20000) / 100.0f - 100.0f;
    const float a = noise3(mine, x, y, z);
    const float b = gShipNoise3(theirs, x, y, z);
    if (std::memcmp(&a, &b, 4) != 0) {
      if (!gScalarMismatches) std::printf("SCALAR_MISMATCH noise3 at %g %g %g: %g vs %g\n", x, y, z, a, b);
      ++gScalarMismatches;
    }
    for (int o : {1, 2, 3, 5}) {
      const float c = fbm3(mine, x, y, z, o, 2.0f, 0.5f);
      const float d = gShipFbm3(theirs, x, y, z, o, 2.0f, 0.5f);
      if (std::memcmp(&c, &d, 4) != 0) {
        if (!gScalarMismatches) std::printf("SCALAR_MISMATCH fbm3 oct=%d: %g vs %g\n", o, c, d);
        ++gScalarMismatches;
      }
    }
  }

  for (int mode = 0; mode < 4; ++mode) {
    Params p{};
    p.mode = mode;
    p.scale = 3.0f;
    p.amount = 2.0f;
    p.evolution = 0.5f;
    p.octaves = 3;
    p.freqMult = 2.0f;
    p.gain = 0.5f;
    p.seed = 7;
    p.dispMode = 0;
    p.edgeMode = 0;
    for (int i = 0; i < 5000; ++i) {
      const float x = (float)(rng() % 5000) / 50.0f - 50.0f;
      const float y = (float)(rng() % 5000) / 50.0f - 50.0f;
      const Vec2 m = fieldAt(mine, x, y, p);
      const Vec2 s = gShipFieldAt(theirs, x, y, p);
      if (std::memcmp(&m, &s, 8) != 0) {
        if (!gScalarMismatches)
          std::printf("SCALAR_MISMATCH fieldAt mode=%d at %g,%g: (%g,%g) vs (%g,%g)\n", mode, x, y,
                      m.x, m.y, s.x, s.y);
        ++gScalarMismatches;
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  const char* bundlePath = (argc > 1) ? argv[1] : kDefaultBundle;
  g_handle = dlopen(bundlePath, RTLD_NOW | RTLD_LOCAL);
  if (!g_handle) {
    std::fprintf(stderr, "dlopen failed for %s: %s\n", bundlePath, dlerror());
    return 2;
  }

  uintptr_t base = 0;
  gShipU8 = (ApplyFn)resolveChecked(kExpected[0].mangled, kExpected[0].offset, &base);
  gShipU16 = (ApplyFn16)resolveChecked(kExpected[1].mangled, kExpected[1].offset, nullptr);
  gShipFieldAt = (FieldAtFn)resolveChecked(kExpected[2].mangled, kExpected[2].offset, nullptr);
  gShipNoise3 = (Noise3Fn)resolveChecked(kExpected[3].mangled, kExpected[3].offset, nullptr);
  gShipFbm3 = (Fbm3Fn)resolveChecked(kExpected[4].mangled, kExpected[4].offset, nullptr);
  gShipBuild = (BuildFn)resolveChecked(kExpected[5].mangled, kExpected[5].offset, nullptr);

  // The shipped symbols must come from the image _dyld_get_image_name reports, and
  // that image must be the bundle we were asked to open.
  {
    Dl_info info;
    if (dladdr((void*)gShipU8, &info) == 0 || info.dli_fbase == nullptr) return 2;
    const char* image = imageNameForBase((uintptr_t)info.dli_fbase);
    if (!image || std::strcmp(image, bundlePath) != 0) {
      std::fprintf(stderr, "the shipped symbols do not come from %s (got %s)\n", bundlePath,
                   image ? image : "(null)");
      return 2;
    }
    std::printf("IMAGE %s base=0x%zx\n", image, (uintptr_t)info.dli_fbase);
  }

  compareScalars();

  // The structured grid: every mode, edge mode and displacement mode, octave counts
  // 0..5, zero / negative / huge amounts, zero / negative / tiny / huge scales,
  // several seeds, and padded strides. `time` is varied even though the shipped code
  // ignores it, so that "time is dead" is a measured claim rather than an assumption.
  const int widths[] = {1, 2, 3, 5, 7};
  const int heights[] = {1, 2, 3, 5};
  const int modes[] = {0, 1, 2, 3};
  const int edges[] = {0, 1, 2, 3};
  const int disps[] = {0, 1, 2, 3};
  const int octs[] = {0, 1, 2, 5};
  const float amounts[] = {0.0f, 0.5f, -0.5f, 3.0f, 25.0f};
  const float scales[] = {1.0f, 0.0f, -1.0f, 0.0005f, 1e6f};
  const float times[] = {0.0f, 1.5f, -7.25f};
  const unsigned seeds[] = {0u, 12345u};
  const float evolutions[] = {0.0f, 0.37f};

  unsigned long long v = 0;
  for (int w : widths)
    for (int h : heights)
      for (int mode : modes)
        for (int edge : edges)
          for (int disp : disps)
            for (int oct : octs)
              for (float amt : amounts)
                for (float sc : scales)
                  for (float tm : times)
                    for (unsigned sd : seeds)
                      for (float evo : evolutions) {
                        Params p{};
                        p.mode = mode;
                        p.scale = sc;
                        p.amount = amt;
                        p.evolution = evo;
                        p.octaves = oct;
                        p.freqMult = 2.0f;
                        p.gain = 0.5f;
                        p.seed = sd;
                        p.dispMode = disp;
                        p.edgeMode = edge;
                        ++v;
                        oneVector<unsigned char>(gShipU8, &apply<unsigned char>, w, h, p, tm, v, 0);
                        oneVector<unsigned short>(gShipU16, &apply<unsigned short>, w, h, p, tm, v,
                                                 0);
                      }

  // The randomised sweep over wider parameter ranges.
  std::mt19937_64 rng(4242);
  for (int i = 0; i < 12000; ++i) {
    Params p{};
    p.mode = (int)(rng() % 4);
    p.edgeMode = (int)(rng() % 4);
    p.dispMode = (int)(rng() % 4);
    p.octaves = (int)(rng() % 6) - 1;
    p.seed = (unsigned)rng();
    p.scale = ((float)(rng() % 1000) / 100.0f) - 2.0f;
    p.amount = ((float)(rng() % 5000) / 100.0f) - 20.0f;
    p.evolution = ((float)(rng() % 1000) / 10.0f) - 20.0f;
    p.freqMult = 1.0f + (float)(rng() % 300) / 100.0f;
    p.gain = (float)(rng() % 100) / 100.0f;
    int w = 1 + (int)(rng() % 34);
    int h = 1 + (int)(rng() % 34);
    if (i % 7 == 0) w = 1 + (int)(rng() % 3);
    if (i % 11 == 0) h = 1 + (int)(rng() % 3);
    if (i % 13 == 0) p.amount = 0.0f;
    if (i % 17 == 0) p.amount = (float)(rng() % 200000) / 100.0f;
    if (i % 19 == 0) p.scale = 1e-6f;
    if (i % 23 == 0) p.evolution = 1e6f;
    if (i % 29 == 0) p.gain = 0.0f;
    if (i % 31 == 0) p.freqMult = 0.0f;
    if (i % 37 == 0) p.octaves = 0;
    const long long pad = (i % 5 == 0) ? (long long)(rng() % 8) * 4 : 0;
    const float tm = (float)(rng() % 1000) / 100.0f - 5.0f;
    oneVector<unsigned char>(gShipU8, &apply<unsigned char>, w, h, p, tm, 1000000 + i, pad);
    oneVector<unsigned short>(gShipU16, &apply<unsigned short>, w, h, p, tm, 2000000 + i, pad);
  }

  const long long totalVectors = gU8.vectors + gU16.vectors;
  const long long totalMismatchVectors = gU8.mismatchingVectors + gU16.mismatchingVectors;
  const long long totalBytes = gU8.differingBytes + gU16.differingBytes;

  std::printf("SCALAR_MISMATCHES %lld\n", gScalarMismatches);
  std::printf("APPLY_U8_VECTORS %lld\n", gU8.vectors);
  std::printf("APPLY_U8_MISMATCH_VECTORS %lld\n", gU8.mismatchingVectors);
  std::printf("APPLY_U8_DIFFERING_BYTES %lld\n", gU8.differingBytes);
  std::printf("APPLY_U16_VECTORS %lld\n", gU16.vectors);
  std::printf("APPLY_U16_MISMATCH_VECTORS %lld\n", gU16.mismatchingVectors);
  std::printf("APPLY_U16_DIFFERING_BYTES %lld\n", gU16.differingBytes);
  std::printf("VECTORS %lld\n", totalVectors);
  std::printf("MISMATCH_VECTORS %lld\n", totalMismatchVectors);
  std::printf("DIFFERING_BYTES %lld\n", totalBytes);
  const bool clean = (gScalarMismatches == 0 && totalMismatchVectors == 0);
  std::printf("RESULT %s\n", clean ? "clean" : "dirty");

  dlclose(g_handle);
  return clean ? 0 : 1;
}
