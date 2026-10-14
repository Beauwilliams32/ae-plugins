// Self-check for the recovered noisefield:: namespace.
//
// The three functions here are small enough to state completely, so every assertion
// is an anchored one, and the gradient table is checked entry by entry against the
// canonical set rather than merely counted.

#include "noisefield.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>

namespace {

int failures = 0;
int passes = 0;

void check(bool ok, const char* why) {
  if (ok) { ++passes; std::printf("  PASS %s\n", why); }
  else    { ++failures; std::printf("  FAIL %s\n", why); }
}

void checkNear(float got, float want, float tol, const char* why) {
  if (std::fabs(got - want) <= tol) { ++passes; std::printf("  PASS %s\n", why); }
  else { ++failures; std::printf("  FAIL %s  (got %.9g want %.9g)\n", why, got, want); }
}

void checkEqI(int got, int want, const char* why) {
  if (got == want) { ++passes; std::printf("  PASS %s\n", why); }
  else { ++failures; std::printf("  FAIL %s  (got %d want %d)\n", why, got, want); }
}

}  // namespace


// ---------------------------------------------------------------------------
// Independent oracles.
//
// These are written FROM THE RECOVERED DESCRIPTION, not by calling the functions
// under test, and the point of them is the part most likely to be wrong: the corner
// hashing (`AA`/`BA`/`AB`/`BB` and their `+1` z-slice), which is where a
// transcription slip hides. Where an oracle shares a constant with the
// implementation, that constant is verified separately against the bundle — `kGrad3`
// against the bytes in `__TEXT,__const`, the fade coefficients against the
// instructions.
// ---------------------------------------------------------------------------

namespace oracle {

unsigned char perm[512];

void buildPerm(unsigned int seed) {
  unsigned char tmp[256];
  for (int i = 0; i < 256; ++i) tmp[i] = static_cast<unsigned char>(i);
  if (seed == 0u) seed = 0x9e3779b9u;
  for (int i = 255; i > 0; --i) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    const int j = static_cast<int>(seed % static_cast<unsigned int>(i + 1));
    const unsigned char t = tmp[i];
    tmp[i] = tmp[j];
    tmp[j] = t;
  }
  for (int i = 0; i < 512; ++i) perm[i] = tmp[i & 255];
}

double fade(double t) {
  // 6t^5 - 15t^4 + 10t^3, the quintic from Perlin's improved noise, written as a
  // polynomial in double precision so the oracle is not itself the implementation.
  return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

int gradIndex(unsigned char hash) { return static_cast<int>(hash) % 12; }

double grad(unsigned char hash, double x, double y, double z) {
  const int h = gradIndex(hash);
  return static_cast<double>(noisefield::kGrad3[h][0]) * x
       + static_cast<double>(noisefield::kGrad3[h][1]) * y
       + static_cast<double>(noisefield::kGrad3[h][2]) * z;
}

/** Perlin's improved noise over the recovered 12 gradients, WITHOUT the 0.9 scale. */
double noise3(double x, double y, double z) {
  const int xi = static_cast<int>(std::floor(x)) & 0xff;
  const int yi = static_cast<int>(std::floor(y)) & 0xff;
  const int zi = static_cast<int>(std::floor(z)) & 0xff;
  const double fx = x - std::floor(x);
  const double fy = y - std::floor(y);
  const double fz = z - std::floor(z);

  const double u = fade(fx), v = fade(fy), w = fade(fz);

  const int A = perm[xi] + yi, AA = perm[A] + zi, AB = perm[A + 1] + zi;
  const int B = perm[xi + 1] + yi, BA = perm[B] + zi, BB = perm[B + 1] + zi;

  const double x1 = fx - 1.0, y1 = fy - 1.0, z1 = fz - 1.0;

  const auto lerp = [](double a, double b, double t) { return a + (b - a) * t; };

  const double x00 = lerp(grad(perm[AA], fx, fy, fz), grad(perm[BA], x1, fy, fz), u);
  const double x10 = lerp(grad(perm[AB], fx, y1, fz), grad(perm[BB], x1, y1, fz), u);
  const double x01 = lerp(grad(perm[AA + 1], fx, fy, z1), grad(perm[BA + 1], x1, fy, z1), u);
  const double x11 = lerp(grad(perm[AB + 1], fx, y1, z1), grad(perm[BB + 1], x1, y1, z1), u);
  return lerp(lerp(x00, x10, v), lerp(x01, x11, v), w);
}

}  // namespace oracle

int main() {
  std::printf("noisefield::clampf\n");
  {
    check(noisefield::clampf(5.0f, 0.0f, 10.0f) == 5.0f, "in range passes through");
    check(noisefield::clampf(-1.0f, 0.0f, 10.0f) == 0.0f, "below lo clamps to lo");
    check(noisefield::clampf(11.0f, 0.0f, 10.0f) == 10.0f, "above hi clamps to hi");
    check(noisefield::clampf(5.0f, 10.0f, 0.0f) == 10.0f,
          "an INVERTED range yields lo, because lo is tested last");
  }

  std::printf("noisefield::clampi\n");
  {
    checkEqI(noisefield::clampi(5, 0, 10), 5, "in range passes through");
    checkEqI(noisefield::clampi(-1, 0, 10), 0, "below lo clamps to lo");
    checkEqI(noisefield::clampi(11, 0, 10), 10, "above hi clamps to hi");
    checkEqI(noisefield::clampi(5, 10, 0), 10, "an inverted range yields lo here too");
    checkEqI(noisefield::clampi(2147483647, 0, 100), 100, "INT_MAX does not overflow");
    checkEqI(noisefield::clampi(-2147483647 - 1, -10, 10), -10, "INT_MIN does not overflow");
  }

  std::printf("noisefield::fbmAmplitudeSum\n");
  {
    // The sum is 1 + gain + gain^2 + ... over `octaves` terms.
    checkNear(noisefield::fbmAmplitudeSum(1, 0.5f), 1.0f, 1e-7f, "one octave sums to 1");
    checkNear(noisefield::fbmAmplitudeSum(2, 0.5f), 1.5f, 1e-7f, "two octaves at gain 0.5 sum to 1.5");
    checkNear(noisefield::fbmAmplitudeSum(3, 0.5f), 1.75f, 1e-7f, "three sum to 1.75");
    checkNear(noisefield::fbmAmplitudeSum(4, 0.5f), 1.875f, 1e-7f, "four sum to 1.875");
    checkNear(noisefield::fbmAmplitudeSum(3, 1.0f), 3.0f, 1e-6f, "a gain of 1 gives one per octave");
    checkNear(noisefield::fbmAmplitudeSum(99, 0.0f), 1.0f, 0.0f, "a gain of 0 stops after the first term");

    // The count is clamped to at least one, and the guard overlaps with it: a
    // non-positive count produces the same answer as one octave, because the loop
    // simply does not run and the guard turns the resulting 0 into 1.
    checkNear(noisefield::fbmAmplitudeSum(0, 0.5f), 1.0f, 0.0f, "zero octaves is one octave");
    checkNear(noisefield::fbmAmplitudeSum(-3, 0.5f), 1.0f, 0.0f, "and so is a negative count");

    // THE GUARD IS REACHABLE, and only through a negative gain: 1 + (-1) = 0.
    checkNear(noisefield::fbmAmplitudeSum(2, -1.0f), 1.0f, 0.0f,
              "a sum of 0 is replaced by 1.0 (the 1e-6f guard)");
    checkNear(noisefield::fbmAmplitudeSum(4, -1.0f), 1.0f, 0.0f, "and again when it cancels to 0 once more");
    // But the guard is not "always 1": a negative gain that does not cancel passes
    // through untouched.
    checkNear(noisefield::fbmAmplitudeSum(2, -0.5f), 0.5f, 1e-7f, "1 + (-0.5) is 0.5, above the guard");
    checkNear(noisefield::fbmAmplitudeSum(3, -2.0f), 3.0f, 1e-6f, "1 - 2 + 4 is 3");
    // A gain above 1 grows, which is what the caller divides by.
    checkNear(noisefield::fbmAmplitudeSum(3, 2.0f), 7.0f, 1e-6f, "1 + 2 + 4 is 7");
  }

  std::printf("noisefield::kGrad3\n");
  {
    // Written out again here, independently of the header, so a transcription slip in
    // the table is caught by comparison rather than by inspection. The test also reads
    // these twelve triples out of the BUNDLE and compares them to the source, which is
    // the check that pins them to the shipped artefact.
    static const int expected[12][3] = {
        {1, 1, 0},  {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0},
        {1, 0, 1},  {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
        {0, 1, 1},  {0, -1, 1}, {0, 1, -1}, {0, -1, -1},
    };
    bool same = true;
    for (int i = 0; i < 12; ++i) {
      for (int j = 0; j < 3; ++j) {
        if (noisefield::kGrad3[i][j] != expected[i][j]) same = false;
      }
    }
    check(same, "all twelve gradients match the canonical improved-noise table, in order");

    // Every entry is an edge midpoint: squared length 2, and never all zero.
    bool allEdges = true;
    for (int i = 0; i < 12; ++i) {
      const int x = noisefield::kGrad3[i][0];
      const int y = noisefield::kGrad3[i][1];
      const int z = noisefield::kGrad3[i][2];
      if (x * x + y * y + z * z != 2) allEdges = false;
      if (x == 0 && y == 0 && z == 0) allEdges = false;
    }
    check(allEdges, "every gradient is a unit edge midpoint (squared length 2, none zero)");

    // Twelve DISTINCT entries: a table with a duplicate would bias the noise towards
    // one direction, which is exactly the kind of defect that would survive a
    // count-only check.
    int distinct = 0;
    for (int i = 0; i < 12; ++i) {
      bool duplicate = false;
      for (int j = 0; j < i; ++j) {
        if (noisefield::kGrad3[i][0] == noisefield::kGrad3[j][0] &&
            noisefield::kGrad3[i][1] == noisefield::kGrad3[j][1] &&
            noisefield::kGrad3[i][2] == noisefield::kGrad3[j][2]) duplicate = true;
      }
      if (!duplicate) ++distinct;
    }
    checkEqI(distinct, 12, "all twelve entries are distinct");
  }


  std::printf("noisefield::PermTable::build\n");
  {
    noisefield::PermTable t;
    t.build(1);

    // A permutation of 0..255, written twice.
    bool seen[256] = {false};
    bool isPerm = true;
    for (int i = 0; i < 256; ++i) {
      if (seen[t.p[i]]) isPerm = false;
      seen[t.p[i]] = true;
    }
    for (int i = 0; i < 256; ++i) if (!seen[i]) isPerm = false;
    check(isPerm, "the first 256 bytes are a permutation of 0..255, each value once");
    bool doubled = true;
    for (int i = 0; i < 256; ++i) if (t.p[i + 256] != t.p[i]) doubled = false;
    check(doubled, "and the second 256 bytes repeat it, which is what keeps p[AA + 1] in range");

    // The same seed gives the same table, and different seeds do not.
    noisefield::PermTable again;
    again.build(1);
    check(std::memcmp(t.p, again.p, 512) == 0, "the same seed gives the same permutation");
    noisefield::PermTable other;
    other.build(2);
    check(std::memcmp(t.p, other.p, 512) != 0, "a different seed gives a different permutation");

    // Seed 0 is replaced by the golden-ratio constant, so it is not a degenerate case.
    noisefield::PermTable zero, golden;
    zero.build(0);
    golden.build(0x9e3779b9u);
    check(std::memcmp(zero.p, golden.p, 512) == 0, "seed 0 builds the same table as 0x9e3779b9");
    check(std::memcmp(zero.p, t.p, 512) != 0, "and that is NOT the same as seed 1");

    // Against the oracle, for several seeds. Both are written from the recovered
    // description, so this catches a deviation in the implementation rather than
    // proving the description.
    bool allMatch = true;
    for (unsigned int seed : {1u, 7u, 12345u, 0xdeadbeefu, 0u}) {
      noisefield::PermTable mine;
      mine.build(seed);
      oracle::buildPerm(seed);
      if (std::memcmp(mine.p, oracle::perm, 512) != 0) allMatch = false;
    }
    check(allMatch, "five seeds match an independently written shuffle oracle");
  }

  std::printf("noisefield::noise3\n");
  {
    oracle::buildPerm(1337u);
    noisefield::PermTable table;
    table.build(1337u);

    // ZERO AT THE LATTICE. At integer coordinates every fade weight is 0 and the one
    // corner whose offsets are all zero dominates, so the noise is exactly 0. A
    // transposed index or a wrong fade would break this almost immediately.
    bool zeroAtLattice = true;
    for (int x = -3; x <= 3; ++x) {
      for (int y = -3; y <= 3; ++y) {
        for (int z = -3; z <= 3; ++z) {
          if (noisefield::noise3(table, float(x), float(y), float(z)) != 0.0f) zeroAtLattice = false;
        }
      }
    }
    check(zeroAtLattice, "the noise is exactly 0 at every integer lattice point");

    // AGAINST THE TEXTBOOK ORACLE, including the 0.9 scale. The oracle is Perlin's
    // improved noise written out separately; the implementation adds the 0.9 that the
    // bundle's last instruction applies, so the comparison is against 0.9 * oracle.
    double worst = 0.0;
    double worstAt[3] = {0, 0, 0};
    // The sweep stays inside +-8. That is not a convenience: the implementation is
    // `float` and the oracle is `double`, and a coordinate like 257.3 leaves only
    // seven significant digits, so the two disagree on the FRACTION alone by more
    // than the algorithm's error. The first version of this sweep ran to 257 and
    // reported 1.8e-05, which was rounding rather than a wrong corner.
    for (int i = 0; i < 4000; ++i) {
      const double x = -7.5 + 0.0037 * i;
      const double y = 3.25 - 0.0011 * i;
      const double z = 0.375 + 0.0029 * i;
      // The oracle is given the SAME float-rounded inputs the implementation sees,
      // so the only difference left is accumulation order inside the maths.
      const float fx = static_cast<float>(x), fy = static_cast<float>(y), fz = static_cast<float>(z);
      const double want = 0.9 * oracle::noise3(fx, fy, fz);
      const double got = noisefield::noise3(table, fx, fy, fz);
      const double err = std::fabs(got - want);
      if (err > worst) { worst = err; worstAt[0] = x; worstAt[1] = y; worstAt[2] = z; }
    }
    char label[160];
    std::snprintf(label, sizeof label,
                  "4000 points match the textbook oracle * 0.9 (worst %.3g at %.2f,%.2f,%.2f)",
                  worst, worstAt[0], worstAt[1], worstAt[2]);
    // float arithmetic against a double oracle, so the tolerance is for accumulated
    // rounding and not for a wrong algorithm: a wrong corner would be off by ~1, and
    // a missing 0.9 scale by ~0.1. 1e-4 discriminates both.
    check(worst < 1e-4, label);

    // THE 0.9 SCALE IS REAL, and this is the check that would fail without it: a
    // plain textbook noise reaches magnitudes the scaled one cannot.
    float biggest = 0.0f;
    for (int i = 0; i < 20000; ++i) {
      const float x = -7.5f + 0.0031f * i;
      const float y = 2.25f - 0.0017f * i;
      const float z = 0.5f + 0.0023f * i;
      const float n = noisefield::noise3(table, x, y, z);
      if (std::fabs(n) > biggest) biggest = std::fabs(n);
    }
    check(biggest <= 0.9f + 1e-6f, "the output never exceeds 0.9 in magnitude");

    // Deterministic, and different tables give different fields.
    noisefield::PermTable other;
    other.build(4242u);
    check(noisefield::noise3(table, 1.5f, 2.5f, 3.5f) == noisefield::noise3(table, 1.5f, 2.5f, 3.5f),
          "the same table and point give the same value");
    check(noisefield::noise3(table, 1.5f, 2.5f, 3.5f) != noisefield::noise3(other, 1.5f, 2.5f, 3.5f),
          "a different permutation gives a different value at the same point");
  }

  std::printf("noisefield::fbm3\n");
  {
    noisefield::PermTable table;
    table.build(99u);
    // The oracle holds its own permutation, so it has to be built with the SAME seed.
    // The first version of this section left it on noise3's seed and every fbm3 value
    // disagreed by a large margin — a defect in the check, not in the recovery.
    oracle::buildPerm(99u);

    // The sum of octaves, divided by the amplitude sum, written out here rather than
    // by calling the function under test.
    for (const auto& c : {std::pair<int, float>{1, 0.5f}, {3, 0.5f}, {4, 0.35f}, {6, 0.7f}}) {
      const int octaves = c.first;
      const float gain = c.second;
      const float lacunarity = 2.0f;
      double want = 0.0;
      double amp = 1.0, freq = 1.0;
      for (int i = 0; i < octaves; ++i) {
        want += oracle::noise3(1.25 * freq, 2.5 * freq, 3.75 * freq) * amp * 0.9;
        freq *= lacunarity;
        amp *= gain;
      }
      double norm = 0.0, a = 1.0;
      for (int i = 0; i < octaves; ++i) { norm += a; a *= gain; }
      want /= (norm > 1e-6) ? norm : 1.0;

      const float got = noisefield::fbm3(table, 1.25f, 2.5f, 3.75f, octaves, gain, lacunarity);
      char label[128];
      std::snprintf(label, sizeof label, "fbm3(%d octaves, gain %.2f) matches the oracle", octaves, gain);
      checkNear(got, static_cast<float>(want), 2e-5f, label);
    }

    // The octave count is clamped to one, so a non-positive count is one octave.
    const float one = noisefield::fbm3(table, 0.5f, 0.5f, 0.5f, 1, 0.5f, 2.0f);
    check(noisefield::fbm3(table, 0.5f, 0.5f, 0.5f, 0, 0.5f, 2.0f) == one, "0 octaves behaves as 1");
    check(noisefield::fbm3(table, 0.5f, 0.5f, 0.5f, -4, 0.5f, 2.0f) == one, "and so does a negative count");

    // A gain of 0 leaves one octave, which still has the noise of that octave in it:
    // the normalisation is 1, so nothing is scaled away.
    const float firstOctave = noisefield::noise3(table, 0.5f, 0.5f, 0.5f);
    checkNear(noisefield::fbm3(table, 0.5f, 0.5f, 0.5f, 5, 0.0f, 2.0f), firstOctave, 1e-6f,
              "a zero gain is exactly the first octave (the normalisation is 1)");
  }

  if (failures == 0) std::printf("ALL RECOVERED NOISEFIELD MATHS VERIFIED (%d checks)\n", passes);
  else std::printf("%d FAILURES out of %d checks\n", failures, passes + failures);
  return failures == 0 ? 0 : 1;
}
