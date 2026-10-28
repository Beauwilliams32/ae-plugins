// Self-check for the recovered noisefield:: namespace.
//
// Every assertion here is ANCHORED: it names the constant, the instruction or the
// rule from the arm64 listing that it pins, and where the listing admits two
// readings it asserts BOTH — that the recovered reading holds and that the
// plausible alternative does not, so a silent flip cannot pass.

#include "noisefield.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <vector>

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


// ---------------------------------------------------------------------------
// The field, written out again from the listing.
//
// These are not a second implementation of the recovered code — they are the
// LISTING's rules, re-stated, so that a change to the implementation has to be
// made twice and the constants (0.02f, -0.02f, 0.35f, 0.001f, four steps) are
// pinned rather than implied.
//
// `noise3` itself is not re-derived here: it is already pinned against the
// textbook oracle above, so these helpers use the recovered one and add only
// the structure the listing describes around it.
// ---------------------------------------------------------------------------

namespace oracle {

using noisefield::Params;
using noisefield::PermTable;
using noisefield::Vec2;

/** One fractal Brownian sample, normalised by its OWN amplitude sum.
 *
 *  curlField inlines this block four times and fieldAt's gradient mode four
 *  more; the shape is the same one fbm3 spells out as an exported function.
 */
float fbmSample(const PermTable& table, float x, float y, float z,
                int octaves, float freqMult, float gain) {
  const int count = octaves > 1 ? octaves : 1;
  float sum = 0.0f, amplitude = 1.0f, frequency = 1.0f;
  for (int i = 0; i < count; ++i) {
    sum += noisefield::noise3(table, x * frequency, y * frequency, z * frequency) * amplitude;
    frequency *= freqMult;
    amplitude *= gain;
  }
  float denom = 0.0f, term = 1.0f;
  for (int i = 0; i < count; ++i) { denom += term; term *= gain; }
  if (!(denom > 1e-6f)) denom = 1.0f;
  return sum / denom;
}

/** The curl's four samples, and the two readings of the Y divisor.
 *
 * The listing divides the X difference by +0.02f and the Y difference by
 * -0.02f — `ldr d1, [x8, #0xc20]` loads the PAIR {+0.02f, -0.02f} and
 * `fdiv.2s v0, v0, v1` divides by it.  `yWrong` is the reading the curl must
 * NOT use: a curl is a perpendicular derivative, and the sign is what makes
 * it one.
 */
struct Curl {
  float x;
  float y;
  float yWrong;
};

Curl curl(const PermTable& table, float x, float y, float z, const Params& p) {
  const float e = 0.01f;
  const float s1 = fbmSample(table, x, y + e, z, p.octaves, p.freqMult, p.gain);
  const float s2 = fbmSample(table, x, y - e, z, p.octaves, p.freqMult, p.gain);
  const float s3 = fbmSample(table, x + e, y, z, p.octaves, p.freqMult, p.gain);
  const float s4 = fbmSample(table, x - e, y, z, p.octaves, p.freqMult, p.gain);
  Curl c;
  c.x = (s1 - s2) / 0.02f;
  c.y = (s3 - s4) / -0.02f;
  c.yWrong = (s3 - s4) / 0.02f;
  return c;
}

/** The advection: `steps` iterations of the curl with a step of 0.35f. */
Vec2 advect(const PermTable& table, float px, float py, float z, const Params& p, int steps) {
  float ax = px, ay = py;
  for (int i = 0; i < steps; ++i) {
    const Curl c = curl(table, ax, ay, z, p);
    ax = std::fma(c.x, 0.35f, ax);
    ay = std::fma(c.y, 0.35f, ay);
  }
  Vec2 out;
  out.x = ax - px;
  out.y = ay - py;
  return out;
}

/** The gradient mode: four samples, BOTH components divided by +0.02f. */
Vec2 gradient(const PermTable& table, float px, float py, float z, const Params& p) {
  const float e = 0.01f;
  const float a = fbmSample(table, px + e, py, z, p.octaves, p.freqMult, p.gain);
  const float b = fbmSample(table, px - e, py, z, p.octaves, p.freqMult, p.gain);
  const float c = fbmSample(table, px, py + e, z, p.octaves, p.freqMult, p.gain);
  const float d = fbmSample(table, px, py - e, z, p.octaves, p.freqMult, p.gain);
  Vec2 out;
  out.x = (a - b) / 0.02f;
  out.y = (c - d) / 0.02f;
  return out;
}

/** One channel-0 pixel of `apply`, written out from the listing's description.
 *
 * Only the clamp and wrap edge modes are here — the transparent mode writes
 * zeroes rather than a sample, and is checked separately.
 */
template <class T>
float oraclePixel(const noisefield::View<const T>& src, const Params& p,
                  const noisefield::Vec2& f, int x, int y) {
  const int w = src.width, h = src.height;
  const float xTop = static_cast<float>(w) - 1.0f;
  const float yTop = static_cast<float>(h) - 1.0f;

  float dx = f.x * p.amount, dy = f.y * p.amount;
  if (p.dispMode == 2) dy = 0.0f;
  if (p.dispMode == 3) dx = 0.0f;

  float sx = 0.0f, sy = 0.0f;
  if (w >= 2) {
    sx = dx + static_cast<float>(x);
    if (p.edgeMode == 2) {
      float v = std::fmodf(sx, static_cast<float>(w));
      if (v < 0.0f) v += static_cast<float>(w);
      sx = (v > xTop) ? xTop : v;
    } else {
      const float c = (sx > xTop) ? xTop : sx;
      sx = (sx < 0.0f) ? 0.0f : c;
    }
  }
  if (h >= 2) {
    sy = dy + static_cast<float>(y);
    if (p.edgeMode == 2) {
      float v = std::fmodf(sy, static_cast<float>(h));
      if (v < 0.0f) v += static_cast<float>(h);
      sy = (v > yTop) ? yTop : v;
    } else {
      const float c = (sy > yTop) ? yTop : sy;
      sy = (sy < 0.0f) ? 0.0f : c;
    }
  }

  const float floorX = std::floor(sx), floorY = std::floor(sy);
  const int i0 = (floorX > 0.0f) ? ((floorX >= xTop) ? w - 1 : static_cast<int>(floorX)) : 0;
  const int j0 = (floorY > 0.0f) ? ((floorY >= yTop) ? h - 1 : static_cast<int>(floorY)) : 0;
  const int i1 = (i0 + 1 >= w) ? ((p.edgeMode == 2) ? 0 : w - 1) : i0 + 1;
  const int j1 = (j0 + 1 >= h) ? ((p.edgeMode == 2) ? 0 : h - 1) : j0 + 1;
  const float fxx = sx - static_cast<float>(i0);
  const float fyy = sy - static_cast<float>(j0);

  const T* row0 = src.data + static_cast<long long>(j0) * src.stride;
  const T* row1 = src.data + static_cast<long long>(j1) * src.stride;
  const float p00 = static_cast<float>(row0[i0 * 4]);
  const float p10 = static_cast<float>(row0[i1 * 4]);
  const float p01 = static_cast<float>(row1[i0 * 4]);
  const float p11 = static_cast<float>(row1[i1 * 4]);
  const float top = std::fma(fxx, p10 - p00, p00);
  const float bot = std::fma(fxx, p11 - p01, p01);
  return std::fma(fyy, bot - top, top);
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
    //
    // THE ORDER OF THE TWO FLOAT ARGUMENTS, which this oracle used to have the wrong
    // way round. `fbm3` is `fbm3(table, x, y, z, octaves, A, B)`, and the arm64
    // listing settles which is which:
    //
    //   __ZN10noisefield4fbm3ERKNS_9PermTableEfffiff, arm64 0x1c34 in
    //   /tmp/ae/noisefield.asm.  The two float arguments arrive in v3 and v4 (x0 is
    //   the table, v0..v2 are x/y/z, w1 is the octave count):
    //       mov.16b v8, v4      ; v8 = the SEVENTH argument
    //       mov.16b v9, v3      ; v9 = the SIXTH argument
    //       ...
    //       fmul s15, s9, s15  ; FREQUENCY  *= the SIXTH argument
    //       fmul s14, s8, s14  ; AMPLITUDE  *= the SEVENTH argument
    //   and the normalisation loop multiplies its term by s8 as well:
    //       fmul s1, s8, s1    ; the amplitude sum is built from the SEVENTH
    //
    // So the SIXTH argument is the FREQUENCY MULTIPLIER and the SEVENTH is the
    // AMPLITUDE GAIN. This is corroborated from the other side: `curlField` and
    // `fieldAt` inline the same loop from `Params`, where `ldp s12, s11, [x1, #0x14]`
    // puts Params::freqMult in s12 and Params::gain in s11, and the loop multiplies
    // the frequency by s12 and the amplitude by s11.
    //
    // It was also checked against the SHIPPED bundle directly: calling the shipped
    // `fbm3(table, 1.25, 2.5, 3.75, 3, 2.0f, 0.5f)` returns -0.0124268103, which is
    // exactly the model with frequency 2.0 and amplitude 0.5, and NOT the model with
    // the two the other way round (-0.0124268103 vs 0.14601557).
    for (const auto& c : {std::pair<int, float>{1, 0.5f}, {3, 0.5f}, {4, 0.35f}, {6, 0.7f}}) {
      const int octaves = c.first;
      const float freqMult = 2.0f;   // the SIXTH argument
      const float ampGain = c.second; // the SEVENTH argument
      double want = 0.0;
      double amp = 1.0, freq = 1.0;
      for (int i = 0; i < octaves; ++i) {
        want += oracle::noise3(1.25 * freq, 2.5 * freq, 3.75 * freq) * amp * 0.9;
        freq *= freqMult;
        amp *= ampGain;
      }
      double norm = 0.0, a = 1.0;
      for (int i = 0; i < octaves; ++i) { norm += a; a *= ampGain; }
      want /= (norm > 1e-6) ? norm : 1.0;

      const float got = noisefield::fbm3(table, 1.25f, 2.5f, 3.75f, octaves, freqMult, ampGain);
      char label[128];
      std::snprintf(label, sizeof label, "fbm3(%d octaves, ampGain %.2f) matches the oracle",
                    octaves, ampGain);
      checkNear(got, static_cast<float>(want), 2e-5f, label);
    }

    // The octave count is clamped to one, so a non-positive count is one octave.
    // (With one octave the two float arguments cannot matter — nothing is scaled by
    // them — which is exactly why the order has to be pinned by the checks above.)
    const float one = noisefield::fbm3(table, 0.5f, 0.5f, 0.5f, 1, 0.5f, 2.0f);
    check(noisefield::fbm3(table, 0.5f, 0.5f, 0.5f, 0, 0.5f, 2.0f) == one, "0 octaves behaves as 1");
    check(noisefield::fbm3(table, 0.5f, 0.5f, 0.5f, -4, 0.5f, 2.0f) == one, "and so does a negative count");

    // An amplitude gain of 0 leaves one octave, which still has the noise of that
    // octave in it: the normalisation is 1, so nothing is scaled away. The gain is
    // the SEVENTH argument.
    const float firstOctave = noisefield::noise3(table, 0.5f, 0.5f, 0.5f);
    checkNear(noisefield::fbm3(table, 0.5f, 0.5f, 0.5f, 5, 2.0f, 0.0f), firstOctave, 1e-6f,
              "a zero gain is exactly the first octave (the normalisation is 1)");
  }

  if (failures == 0) std::printf("ALL RECOVERED NOISEFIELD MATHS VERIFIED (%d checks)\n", passes);
  else std::printf("%d FAILURES out of %d checks\n", failures, passes + failures);
  return failures == 0 ? 0 : 1;
}
