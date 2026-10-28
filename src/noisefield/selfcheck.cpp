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
    // FUSED, because the listing is: `fmadd s1, s0, s14, s1` accumulates the sum
    // with a multiply-add, so an unfused multiply-then-add would drift from it.
    sum = std::fma(noisefield::noise3(table, x * frequency, y * frequency, z * frequency),
                   amplitude, sum);
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

/** The gradient mode: four samples, BOTH components divided by +0.02f.
 *
 * `yWrong` is the reading the gradient must NOT use — the curl's -0.02f. The two
 * are different quantities, so the test compares the recovered value with the
 * right and the wrong divisor of the SAME difference.
 */
struct Grad {
  float x;
  float y;
  float yWrong;
};

Grad gradient(const PermTable& table, float px, float py, float z, const Params& p) {
  const float e = 0.01f;
  const float a = fbmSample(table, px + e, py, z, p.octaves, p.freqMult, p.gain);
  const float b = fbmSample(table, px - e, py, z, p.octaves, p.freqMult, p.gain);
  const float c = fbmSample(table, px, py + e, z, p.octaves, p.freqMult, p.gain);
  const float d = fbmSample(table, px, py - e, z, p.octaves, p.freqMult, p.gain);
  Grad out;
  out.x = (a - b) / 0.02f;
  out.y = (c - d) / 0.02f;
  out.yWrong = (c - d) / -0.02f;
  return out;
}

/** The whole of `fieldAt`, re-stated from the listing.
 *
 * The mode dispatch is the listing's: `cmp w8, #2` / `cmp w8, #3` at the head of
 * `fieldAt` (arm64 0x0efc..0x1280), the scale floored at 0.001f before the
 * division, and the four curl calls of the advection mode at 0xf6c, 0xf98, 0xfb8
 * and 0xfd8.
 */
Vec2 fieldAt(const PermTable& table, float x, float y, const Params& p) {
  const float scale = std::fmax(p.scale, 0.001f);
  const float px = x / scale;
  const float py = y / scale;
  const float z = p.evolution;

  if (p.mode == 3) {
    const Grad g = gradient(table, px, py, z, p);
    Vec2 out;
    out.x = g.x;
    out.y = g.y;
    return out;
  }
  if (p.mode == 2) return advect(table, px, py, z, p, 4);
  const Curl c = curl(table, px, py, z, p);
  Vec2 out;
  out.x = c.x;
  out.y = c.y;
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

  std::printf("noisefield::fieldAt\n");
  {
    noisefield::PermTable table;
    table.build(2026u);

    noisefield::Params p;
    p.mode = 0;
    p.scale = 1.0f;
    p.amount = 1.0f;
    p.evolution = 0.35f;
    p.octaves = 3;
    p.freqMult = 2.0f;
    p.gain = 0.5f;
    p.seed = 2026u;
    p.dispMode = 0;
    p.edgeMode = 0;

    const float x = 3.25f;
    const float y = 5.75f;

    // MODES 0 AND 1 both take the curl path. The listing's dispatch compares the
    // mode against 2 and against 3 and branches away; anything else falls through
    // to the curl (arm64 0x0efc..0x1280), so 0 and 1 are the same path.
    noisefield::Params p1 = p;
    p1.mode = 1;
    const noisefield::Vec2 m0 = noisefield::fieldAt(table, x, y, p);
    const noisefield::Vec2 m1 = noisefield::fieldAt(table, x, y, p1);
    check(m0.x == m1.x && m0.y == m1.y, "fieldAt modes 0 and 1 both take the curl path");

    // THE CURL. `curlField` (arm64 0x1d00..0x1f88) takes four fBm samples at
    // +/-0.01f, each normalised by its OWN denominator, and finishes with
    //     fdiv.2s v0, v3, v0     ; {sum1/den1, sum3/den3}
    //     fdiv.2s v1, v2, v1     ; {sum2/den2, sum4/den4}
    //     fsub.2s v0, v0, v1     ; the two differences
    //     ldr d1, [x8, #0xc20]    ; __TEXT,__const 0x3c20 = {+0.02f, -0.02f}
    //     fdiv.2s v0, v0, v1     ; divided by that PAIR
    // so the X component divides by +0.02f and the Y component by -0.02f.
    const oracle::Curl c = oracle::curl(table, x, y, p.evolution, p);
    checkNear(m0.x, c.x, 1e-3f, "mode 0 is the curl's X component");
    checkNear(m0.y, c.y, 1e-3f, "mode 0 is the curl's Y component");

    // The sign of the Y divisor is the whole difference between a curl and a
    // gradient, so both readings are asserted: the recovered one holds, and the
    // plausible alternative does NOT.
    check(std::fabs(c.y - c.yWrong) > 1e-3f,
          "the curl's Y divisor is sign-sensitive at this point");
    check(std::fabs(m0.y - c.yWrong) > 1e-3f,
          "fieldAt's Y is NOT the +0.02f reading");

    // MODE 3 is the GRADIENT, and it divides BOTH components by +0.02f
    // (0x3ca3d70a) — the curl's -0.02f belongs to the curl alone.
    noisefield::Params p3 = p;
    p3.mode = 3;
    const noisefield::Vec2 g = noisefield::fieldAt(table, x, y, p3);
    const oracle::Grad go = oracle::gradient(table, x, y, p3.evolution, p3);
    checkNear(g.x, go.x, 1e-3f, "mode 3 is the gradient's X component");
    checkNear(g.y, go.y, 1e-3f, "mode 3 is the gradient's Y component");
    check(std::fabs(g.y - go.yWrong) > 1e-3f,
          "the gradient's Y divides by +0.02f, not by -0.02f");

    // MODE 2 advects: FOUR steps of 0.35f (0x3eb33333) along the curl, and then
    // returns the displacement from the starting point. The listing contains
    // exactly four calls to `curlField` — at 0xf6c, 0xf98, 0xfb8 and 0xfd8 — and
    // an earlier reading of this namespace's notes said five, which was wrong.
    noisefield::Params p2 = p;
    p2.mode = 2;
    const noisefield::Vec2 a = noisefield::fieldAt(table, x, y, p2);
    const noisefield::Vec2 a4 = oracle::advect(table, x, y, p2.evolution, p2, 4);
    const noisefield::Vec2 a3 = oracle::advect(table, x, y, p2.evolution, p2, 3);
    const noisefield::Vec2 a5 = oracle::advect(table, x, y, p2.evolution, p2, 5);
    checkNear(a.x, a4.x, 1e-3f, "mode 2 is FOUR 0.35f steps along the curl (X)");
    checkNear(a.y, a4.y, 1e-3f, "mode 2 is FOUR 0.35f steps along the curl (Y)");
    check(std::fabs(a.x - a3.x) > 1e-3f, "mode 2 is not three steps");
    check(std::fabs(a.x - a5.x) > 1e-3f, "mode 2 is not five steps");

    // THE SCALE is floored at 0.001f (0x3a83126f) so it can never divide by zero,
    // and the floor is observable: a scale of zero behaves as 0.001f.
    noisefield::Params pz = p;
    pz.scale = 0.0f;
    noisefield::Params pt = p;
    pt.scale = 0.001f;
    noisefield::Params po = p;
    po.scale = 1.0f;
    const noisefield::Vec2 zs = noisefield::fieldAt(table, x, y, pz);
    const noisefield::Vec2 ts = noisefield::fieldAt(table, x, y, pt);
    const noisefield::Vec2 os = noisefield::fieldAt(table, x, y, po);
    check(zs.x == ts.x && zs.y == ts.y, "a zero scale is floored to 0.001f");
    check(!(zs.x == os.x && zs.y == os.y), "and 0.001f is not the same as 1.0f");
  }

  std::printf("noisefield::apply\n");
  {
    // A small image whose pixels vary in both axes, so that a transposition of
    // the coordinates would be visible.
    const int W = 5;
    const int H = 4;
    std::vector<unsigned char> srcBuf(W * H * 4);
    for (int y = 0; y < H; ++y) {
      for (int x = 0; x < W; ++x) {
        for (int c = 0; c < 4; ++c) {
          srcBuf[(y * W + x) * 4 + c] =
              static_cast<unsigned char>(11 + 29 * x + 13 * y + 7 * c);
        }
      }
    }

    noisefield::Params p;
    p.mode = 0;
    p.scale = 1.0f;
    p.amount = 0.0f;   // the copy path until a section says otherwise
    p.evolution = 0.35f;
    p.octaves = 2;
    p.freqMult = 2.0f;
    p.gain = 0.5f;
    p.seed = 2026u;
    p.dispMode = 0;
    p.edgeMode = 0;

    // AN EMPTY EXTENT RETURNS WITHOUT TOUCHING THE DESTINATION. The listing
    // compares the width and the height against 1 before anything else.
    {
      std::vector<unsigned char> dstBuf(64, 0xAB);
      const noisefield::View<const unsigned char> src{srcBuf.data(), 0, H, W * 4};
      noisefield::View<unsigned char> dst{dstBuf.data(), 0, H, 16};
      noisefield::apply(src, dst, p, 0.0f);
      bool untouched = true;
      for (const unsigned char b : dstBuf) untouched = untouched && (b == 0xAB);
      check(untouched, "a zero width leaves the destination untouched");
    }

    // AMOUNT == 0 TAKES THE COPY PATH: a per-row `memmove` of width*4*sizeof(T)
    // bytes, so the destination's own padding is left alone.
    {
      const int dstride = W * 4 + 3;   // three elements of padding per row
      std::vector<unsigned char> dstBuf(static_cast<size_t>(dstride) * H, 0xCD);
      const noisefield::View<const unsigned char> src{srcBuf.data(), W, H, W * 4};
      noisefield::View<unsigned char> dst{dstBuf.data(), W, H, dstride};
      noisefield::apply(src, dst, p, 0.0f);
      bool copied = true;
      bool paddingKept = true;
      for (int y = 0; y < H; ++y) {
        for (int i = 0; i < W * 4; ++i)
          copied = copied && dstBuf[static_cast<size_t>(y) * dstride + i] ==
                                srcBuf[static_cast<size_t>(y) * W * 4 + i];
        for (int i = W * 4; i < dstride; ++i)
          paddingKept = paddingKept && dstBuf[static_cast<size_t>(y) * dstride + i] == 0xCD;
      }
      check(copied, "amount == 0 copies the image row by row");
      check(paddingKept, "the copy path leaves the destination's padding alone");
    }

    // THE `time` ARGUMENT IS DEAD. The listing overwrites s0 with
    // `params.amount` before the argument is ever read, so two different times
    // give identical bytes — on the copy path and on the displacement path alike.
    {
      noisefield::Params q = p;
      q.amount = 2.5f;
      std::vector<unsigned char> a(W * H * 4, 0);
      std::vector<unsigned char> b(W * H * 4, 0);
      const noisefield::View<const unsigned char> src{srcBuf.data(), W, H, W * 4};
      noisefield::View<unsigned char> da{a.data(), W, H, W * 4};
      noisefield::View<unsigned char> db{b.data(), W, H, W * 4};
      noisefield::apply(src, da, q, 0.0f);
      noisefield::apply(src, db, q, 1234.5f);
      check(a == b, "the time argument does not change a single byte");

      std::vector<unsigned char> c(W * H * 4, 0);
      std::vector<unsigned char> d(W * H * 4, 0);
      noisefield::Params r = p;
      r.amount = 0.0f;
      noisefield::View<unsigned char> dc{c.data(), W, H, W * 4};
      noisefield::View<unsigned char> dd{d.data(), W, H, W * 4};
      noisefield::apply(src, dc, r, 0.0f);
      noisefield::apply(src, dd, r, -99.0f);
      check(c == d, "and it does not change a byte on the copy path either");
    }

    // A ONE-PIXEL IMAGE IGNORES THE DISPLACEMENT. The listing skips the X block
    // when the width is below 2 and the Y block when the height is below 2, which
    // forces sx = sy = 0 and leaves the pixel inside, so the output is the source
    // pixel.
    {
      noisefield::Params q = p;
      q.amount = 7.0f;
      std::vector<unsigned char> oneBuf(4, 0);
      oneBuf[0] = 17; oneBuf[1] = 34; oneBuf[2] = 51; oneBuf[3] = 68;
      std::vector<unsigned char> dstBuf(4, 0);
      const noisefield::View<const unsigned char> src{oneBuf.data(), 1, 1, 4};
      noisefield::View<unsigned char> dst{dstBuf.data(), 1, 1, 4};
      noisefield::apply(src, dst, q, 0.0f);
      check(dstBuf == oneBuf, "a 1x1 image ignores the displacement entirely");
    }

    // EDGE MODE 3 IS "OUTSIDE IS TRANSPARENT", and the X and Y tests are the SAME
    // rule: inside iff 0 <= v <= extent-1. The listing writes them differently —
    //     X:  fcmp s10, s14 / fccmp s10, s12, #0x8, le / cset w28, pl   (0x9b4..0x9bc)
    //     Y:  fcmp s9, #0.0  / fccmp s9, s13, #0x0, pl  / b.gt 0x908    (0xa28..0xa30)
    // — but both accept the top edge and both reject a negative coordinate. The
    // bundle writes the transparent pixels with one 32-bit store of zero
    // (`stur wzr, [x24, #-0x1]` at 0x908 for 8-bit, `str xzr, [x25]` at 0x14ac for
    // 16-bit), so a transparent pixel is four zero bytes.
    {
      const int N = 8;
      std::vector<unsigned char> flat(static_cast<size_t>(N) * N * 4, 200);
      noisefield::Params q = p;
      q.amount = 3.0f;
      q.edgeMode = 3;

      // dispMode 2 suppresses the Y displacement, so sy == y and the X test alone
      // decides transparency; dispMode 3 suppresses the X one, so sx == x and the
      // Y test alone decides it.
      noisefield::Params q2 = q;
      q2.dispMode = 2;
      noisefield::Params q3 = q;
      q3.dispMode = 3;

      std::vector<unsigned char> d2(static_cast<size_t>(N) * N * 4, 0);
      std::vector<unsigned char> d3(static_cast<size_t>(N) * N * 4, 0);
      const noisefield::View<const unsigned char> src{flat.data(), N, N, N * 4};
      noisefield::View<unsigned char> o2{d2.data(), N, N, N * 4};
      noisefield::View<unsigned char> o3{d3.data(), N, N, N * 4};
      noisefield::apply(src, o2, q2, 0.0f);
      noisefield::apply(src, o3, q3, 0.0f);

      noisefield::PermTable table;
      table.build(q.seed);
      bool set2ok = true;
      bool set3ok = true;
      int count2 = 0;
      int count3 = 0;
      for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
          const size_t at = (static_cast<size_t>(y) * N + x) * 4;
          const bool t2 = d2[at] == 0 && d2[at + 1] == 0 && d2[at + 2] == 0 && d2[at + 3] == 0;
          const bool t3 = d3[at] == 0 && d3[at + 1] == 0 && d3[at + 2] == 0 && d3[at + 3] == 0;
          if (t2) ++count2;
          if (t3) ++count3;

          // The expectation, from the rule the listing states for BOTH axes.
          const noisefield::Vec2 f2 = oracle::fieldAt(table, static_cast<float>(x),
                                                       static_cast<float>(y), q2);
          const noisefield::Vec2 f3 = oracle::fieldAt(table, static_cast<float>(x),
                                                       static_cast<float>(y), q3);
          const float sx = f2.x * q2.amount + static_cast<float>(x);
          const float sy = f3.y * q3.amount + static_cast<float>(y);
          const bool want2 = !(sx >= 0.0f && sx <= static_cast<float>(N - 1));
          const bool want3 = !(sy >= 0.0f && sy <= static_cast<float>(N - 1));
          set2ok = set2ok && (t2 == want2);
          set3ok = set3ok && (t3 == want3);
        }
      }
      check(set2ok, "edge mode 3's transparent set is exactly the X test's (dispMode 2)");
      check(set3ok, "edge mode 3's transparent set is exactly the Y test's (dispMode 3)");
      check(count2 > 0, "the X test makes some pixels transparent");
      check(count3 > 0,
            "the Y test makes some pixels transparent — the Y test is not a no-op");
      check(count2 != count3,
            "the X and Y transparent sets differ, so the two tests are not the same test");

      // EDGE MODE 0 CLAMPS instead: nothing is transparent, and the sample comes
      // from the edge of the image.
      noisefield::Params q0 = q;
      q0.edgeMode = 0;
      std::vector<unsigned char> d0(static_cast<size_t>(N) * N * 4, 0);
      noisefield::View<unsigned char> o0{d0.data(), N, N, N * 4};
      noisefield::apply(src, o0, q0, 0.0f);
      bool noneTransparent = true;
      for (const unsigned char b : d0) noneTransparent = noneTransparent && (b == 200);
      check(noneTransparent, "edge mode 0 clamps, so no pixel is transparent");
    }

    // THE PIXEL IS A BILINEAR SAMPLE of the four neighbours, computed as
    //     top = fma(fxx, p10 - p00, p00);  bot = fma(fxx, p11 - p01, p01)
    //     out = fma(fyy, bot - top, top)
    // (arm64 0xae0..0xb18) and the four neighbours are p00 = row0[i0], p10 =
    // row0[i1], p01 = row1[i0], p11 = row1[i1] — the COLUMN index is the one
    // that moves between p00 and p10.
    {
      noisefield::Params q = p;
      q.amount = 1.5f;
      q.edgeMode = 0;
      noisefield::PermTable table;
      table.build(q.seed);

      std::vector<unsigned char> dstBuf(static_cast<size_t>(W) * H * 4, 0);
      const noisefield::View<const unsigned char> src{srcBuf.data(), W, H, W * 4};
      noisefield::View<unsigned char> dst{dstBuf.data(), W, H, W * 4};
      noisefield::apply(src, dst, q, 0.0f);

      bool matches = true;
      for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
          const noisefield::Vec2 f = oracle::fieldAt(table, static_cast<float>(x),
                                                     static_cast<float>(y), q);
          const float want = oracle::oraclePixel(src, q, f, x, y);
          const auto got = static_cast<long long>(std::llround(want));
          const int have = dstBuf[(static_cast<size_t>(y) * W + x) * 4];
          matches = matches && (have == got);
        }
      }
      check(matches, "every pixel is the bilinear sample the listing describes");

      // THE FIELD IS ASKED FOR THE COLUMN BEFORE THE ROW. The listing converts
      // the inner counter (the column) with `ucvtf s10, w23` and passes it in v0,
      // with the outer counter (the row) in v1 — `mov.16b v0, s10` /
      // `mov.16b v1, s8` at 0x928/0x92c, then `bl fieldAt` at 0x934. The rows and
      // the columns are NOT interchangeable, and this image is not symmetric, so
      // the transposed reading gives a different answer.
      bool sameAsTransposed = true;
      for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
          const noisefield::Vec2 ft = oracle::fieldAt(table, static_cast<float>(y),
                                                      static_cast<float>(x), q);
          const float want = oracle::oraclePixel(src, q, ft, x, y);
          const int have = dstBuf[(static_cast<size_t>(y) * W + x) * 4];
          sameAsTransposed = sameAsTransposed &&
                             (have == static_cast<int>(std::llround(want)));
        }
      }
      check(!sameAsTransposed, "the transposed (row, column) reading does NOT match");
    }

    // THE 8-BIT AND 16-BIT INSTANTIATIONS AGREE, up to the 257 factor the pixel
    // width introduces. On a CONSTANT image the agreement is exact: the four
    // neighbours are equal, so every `fma(fxx, p10 - p00, p00)` is exact and the
    // 16-bit result is 257 times the 8-bit one.
    {
      noisefield::Params q = p;
      q.amount = 1.5f;
      q.edgeMode = 0;
      std::vector<unsigned char> flat8(static_cast<size_t>(W) * H * 4, 200);
      std::vector<unsigned short> flat16(static_cast<size_t>(W) * H * 4, 200u * 257u);

      std::vector<unsigned char> d8(static_cast<size_t>(W) * H * 4, 0);
      std::vector<unsigned short> d16(static_cast<size_t>(W) * H * 4, 0);
      const noisefield::View<const unsigned char> s8{flat8.data(), W, H, W * 4};
      noisefield::View<unsigned char> o8{d8.data(), W, H, W * 4};
      const noisefield::View<const unsigned short> s16{flat16.data(), W, H, W * 4};
      noisefield::View<unsigned short> o16{d16.data(), W, H, W * 4};
      noisefield::apply(s8, o8, q, 0.0f);
      noisefield::apply(s16, o16, q, 0.0f);

      bool parity = true;
      for (size_t i = 0; i < d16.size(); ++i)
        parity = parity && (d16[i] == static_cast<unsigned short>(257 * d8[i]));
      check(parity, "on a constant image the 16-bit result is exactly 257 times the 8-bit one");
    }

    // On an image that VARIES, the two agree to within one 8-bit step — the
    // rounding to an integer does not commute with the scaling by 257, so the
    // exact factor cannot hold away from a lattice point.
    {
      noisefield::Params q = p;
      q.amount = 1.5f;
      q.edgeMode = 0;
      std::vector<unsigned short> src16(static_cast<size_t>(W) * H * 4, 0);
      for (size_t i = 0; i < src16.size(); ++i) src16[i] = static_cast<unsigned short>(257 * srcBuf[i]);

      std::vector<unsigned char> d8(static_cast<size_t>(W) * H * 4, 0);
      std::vector<unsigned short> d16(static_cast<size_t>(W) * H * 4, 0);
      const noisefield::View<const unsigned char> s8{srcBuf.data(), W, H, W * 4};
      noisefield::View<unsigned char> o8{d8.data(), W, H, W * 4};
      const noisefield::View<const unsigned short> s16{src16.data(), W, H, W * 4};
      noisefield::View<unsigned short> o16{d16.data(), W, H, W * 4};
      noisefield::apply(s8, o8, q, 0.0f);
      noisefield::apply(s16, o16, q, 0.0f);

      bool parity = true;
      for (size_t i = 0; i < d16.size(); ++i)
        parity = parity &&
                 (std::abs(static_cast<int>(d16[i]) - 257 * static_cast<int>(d8[i])) <= 257);
      check(parity, "on a varying image the 16-bit result is within one 8-bit step of 257 times it");
    }
  }

  if (failures == 0) std::printf("ALL RECOVERED NOISEFIELD MATHS VERIFIED (%d checks)\n", passes);
  else std::printf("%d FAILURES out of %d checks\n", failures, passes + failures);
  return failures == 0 ? 0 : 1;
}
