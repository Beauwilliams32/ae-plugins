// glitch.cpp — recovered implementations from the GlitchKit bundle.

#include "glitch.h"

#include <climits>
#include <cmath>
#include <cstdint>

namespace glitch {

// ---------------------------------------------------------------------------
// clampf(float, float, float)
//
// __ZN6glitch6clampfEfff:
//     fcmp  s0, s2 / fcsel s2, s2, s0, gt
//     fcmp  s0, s1 / fcsel s0, s1, s2, mi
//     ret
//
// Byte-for-byte the same as spectra::clampf, lutblend::clampf and
// typeriser::clampf. Four separate namespaces in four separate bundles converged
// on this exact branchless form, which is strong evidence all four transcriptions
// are right — and a test asserts they still agree.
// ---------------------------------------------------------------------------
float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

// ---------------------------------------------------------------------------
// clampi(int, int, int)
//
// __ZN6glitch6clampiEiii:
//     cmp  w0, w2 / csel w8, w0, w2, lt    ; w8 = min(value, hi)
//     cmp  w0, w1 / csel w0, w1, w8, lt    ; w0 = (value < lo) ? lo : w8
//     ret
//
// The integer twin of clampf, with the same shape: the low bound is tested LAST
// so it wins when the range is inverted, exactly as in the float version. Keeping
// that ordering matters — `min(max(v, lo), hi)` would return hi instead of lo for
// an inverted range.
// ---------------------------------------------------------------------------
int clampi(int value, int lo, int hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

// ---------------------------------------------------------------------------
// hashKey(unsigned, unsigned, unsigned)
//
// __ZN6glitch7hashKeyEjjj, 34 instructions, recovered in full.
//
// The shape is two rounds of the same three-step avalanche:
//
//     x = a * 0x85ebca6b;
//     y = a * 0x7af29ac0 + b;      ; madd
//     y += x >> 2;                 ; the shifted add stands in for a multiply
//     y += 0x9e3779b9;
//     x = y ^ x;
//
//     p = x * 0xc2b2ae35;
//     q = x * 0xacab8d40 + c;      ; madd — carries the third input in
//     q += p >> 2;
//     q += 0x27d4eb2f;
//
//     h  = q ^ p;
//     h ^= h >> 16;  h *= 0x7feb352d;
//     h ^= h >> 15;  h *= 0x846ca68b;
//     h ^= h >> 16;
//     return (p != q) ? h : 1;     ; cmp + csinc
//
// The tail is transcribed literally. `csinc w0, w10, wzr, ne` after `cmp w9, w8`
// is a conditional increment: the mixed value normally, but 1 when the two
// intermediate stages are equal. What that guard was for is NOT recoverable from
// the code, so it is reproduced rather than rationalised — an earlier draft
// "simplified" it to `return h;` on the assumption it was a zero-guard, which the
// comparison does not support.
//
// Note the constants are not the usual MurmurHash3 set end to end, and the
// two `madd + (x >> 2)` pairs are how the compiler emitted whatever the original
// multiplier was.
// ---------------------------------------------------------------------------
unsigned int hashKey(unsigned int a, unsigned int b, unsigned int c) {
  unsigned int x = a * 0x85ebca6bu;
  unsigned int y = a * 0x7af29ac0u + b;
  y += x >> 2;
  y += 0x9e3779b9u;
  x = y ^ x;

  const unsigned int p = x * 0xc2b2ae35u;
  unsigned int q = x * 0xacab8d40u + c;
  q += p >> 2;
  q += 0x27d4eb2fu;

  unsigned int h = q ^ p;
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  h ^= h >> 16;

  return (p != q) ? h : 1u;
}

// ---------------------------------------------------------------------------
// patternFrame(unsigned, float)
//
// __ZN6glitch12patternFrameEjf:
//     ucvtf  d1, w0        ; (double)seed, UNSIGNED
//     fcvt   d2, s0        ; (double)time
//     fmul   d1, d1, d2
//     fcvtzu w8, d1        ; truncate toward zero
//     fcmp   s0, #0.0
//     csel   w0, w8, wzr, hi
//     ret
//
// Two details worth keeping:
//   * the multiply is done in DOUBLE, not float, and only the product is
//     truncated. Doing it in float would round before the truncation and pick the
//     wrong frame at some time values.
//   * the guard is `hi`, i.e. strictly greater than zero (or unordered), so
//     time == 0 yields 0 rather than the seed. Negative times also yield 0.
// ---------------------------------------------------------------------------
unsigned int patternFrame(unsigned int seed, float time) {
  const double product = static_cast<double>(seed) * static_cast<double>(time);
  const unsigned int frame = static_cast<unsigned int>(product);
  // Written as a negated `<=` rather than `time > 0`, because that is what the
  // shipped code does: the original emits `csel w0, w8, wzr, hi` and a `time > 0`
  // ternary emits `gt` instead. `hi` is true for greater-than OR UNORDERED, so a
  // NaN time takes the product branch where `gt` would take the zero branch. The
  // two agree in practice only because fcvtzu(NaN) is 0 — matching the form keeps
  // the codegen identical rather than relying on that coincidence.
  if (!(time <= 0.0f)) return frame;
  return 0u;
}

// ---------------------------------------------------------------------------
// The render engine, `apply<T>`.
//
// BEHAVIOURAL RECOVERY — not instruction-identical. The semantics below are
// transcribed from the arm64 listing of the shipped GlitchKit bundle:
//
//   __ZN6glitch5applyIhEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEjf
//       0x738 .. 0x194c   (1157 instructions)
//   __ZN6glitch5applyItEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEjf
//       0x1a04 .. 0x2d34   (1228 instructions)
//
// Every fact claimed here is read from those listings; src/glitch/RECOVERY-NOTES.md
// gives the address (and the numbered-listing line) each one was read from.
//
// The SHAPE, in the shipped order:
//
//   * an EMPTY frame leaves the output completely untouched
//     (0x738..0x744, `ldp w15,w16,[x0,#8] / cmp w15,#1 / ccmp w16,#1,#8,ge / b.lt`).
//   * amount and spread are clamped at the TOP by two fcsel pairs (0x764..0x790), and
//     if EITHER of them is <= 0 (or unordered, i.e. NaN) the frame is copied and the
//     function returns WITHOUT the spread blend (0x794 / 0x79c -> 0x9a0).
//   * a top hash is mixed: hashKey(hashInput, patternFrame(seed, rate), mode)
//     (0x7f4..0x864).
//   * one of five mode bodies runs, selected by a ONE-BASED switch (0x868..0x884).
//   * the spread blend mixes the mode's result with the original input
//     (0x189c..0x192c), and is skipped when spread >= 1.
//
// The float arithmetic is reproduced instruction for instruction in *meaning*:
// every shipped `fmadd` is an explicit std::fma, every shipped separate
// multiply/add pair is a separate multiply and add, `fcvtas` is llroundf,
// `frinta` is std::round and `fcvtzs` is a truncating cast. Contractual
// fusion is switched off below so the compiler cannot silently turn a shipped
// multiply-and-add into an fma with different rounding.
// ---------------------------------------------------------------------------

#pragma clang fp contract(off)

namespace {

// --- the shared PRNG -------------------------------------------------------
//
//     x ^= x << 13;
//     x ^= x >> 17;
//     x ^= x << 5;
//
// (0xb40..0xb48 in mode 2, and the same three instructions everywhere else.)
inline unsigned int xs32(unsigned int x) {
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return x;
}

// The unit random number: the top 24 bits of the state, as a float in [0, 1).
//     lsr  w9, w9, #8
//     ucvtf s4, w9, #0x18
inline float unitFloat(unsigned int state) {
  return static_cast<float>(state >> 8) * 0x1p-24f;
}

// --- the shipped's float-to-integer conversions ------------------------------
//
// `fcvtas` — round to the NEAREST integer, ties AWAY from zero, into a signed
// 64-bit integer. A NaN converts to 0; an out-of-range value saturates.
//
// `llroundf` rounds the same way, but leaves the NaN and the overflow case
// UNSPECIFIED. The shipped code's behaviour in those two cases is a measured
// fact rather than an accident of the C library, so the conversion is written
// out here instead of delegated. On this platform `llroundf(NaN)` happens to
// answer 0 and `llroundf(inf)` happens to answer LLONG_MAX, which is what
// `fcvtas` does — but "happens to" is not a specification.
inline long long fcvtasToI64(float v) {
  if (std::isnan(v)) return 0;
  // The widening to double is exact for a float, so rounding the double rounds
  // the original value's halfway cases the same way.
  const double r = std::round(static_cast<double>(v));
  if (r >= 9223372036854775808.0) return LLONG_MAX;   // 2^63, the first
  if (r <= -9223372036854775808.0) return LLONG_MIN;  // unrepresentable double
  return static_cast<long long>(r);
}

// `fcvtzs` — truncate toward zero into a signed 32-bit integer, saturating,
// with a NaN converting to 0.
//
// `static_cast<int>` of an out-of-range or NaN float is UNDEFINED BEHAVIOUR,
// and the shipped code is defined in both cases, so the conversion is spelled
// out.
inline int fcvtzsToI32(float v) {
  if (std::isnan(v)) return 0;
  const double t = std::trunc(static_cast<double>(v));
  if (t >= 2147483647.0) return INT32_MAX;
  if (t <= -2147483648.0) return INT32_MIN;
  return static_cast<int>(t);
}

// The shipped code takes the LOW 32 BITS of the `fcvtas` result for the
// displacement it then clamps (`sxtw` on the way in, a 32-bit `add` on the way
// out). This reproduces that, including the wrap.
//
// The word is kept UNSIGNED, because that is what the listing manipulates:
// `add w, w, w` and `sub w, w, w` are 32-bit operations on the wrapped
// word, and the comparisons that follow them are SIGNED 32-bit
// comparisons. Reinterpreting the word as a signed integer too early
// changes the answer whenever the displacement is large enough for the
// top bit to be set -- which mode 2 can reach, because `shift` is a
// user-supplied float with no upper bound of its own.
inline uint32_t fcvtasLow32(float v) {
  const long long wide = fcvtasToI64(v);
  return static_cast<uint32_t>(static_cast<unsigned long long>(wide) &
                               0xffffffffull);
}

// The signed reading of a 32-bit word. `sxtw` is the instruction that does
// this in the listing; writing it out avoids the implementation-defined
// unsigned-to-signed conversion for the values above INT32_MAX.
inline int32_t asSigned32(uint32_t v) {
  return (v <= 0x7fffffffu)
             ? static_cast<int32_t>(v)
             : static_cast<int32_t>(static_cast<int64_t>(v) - 0x100000000LL);
}

// `frinta` — round to the nearest integer, TIES AWAY FROM ZERO.
//
// The tie rule is worth stating because the four ARM rounding instructions are
// easy to confuse: `frintn` is ties to EVEN, `frintp` is ties toward +infinity,
// `frintm` is ties toward -infinity, `frintz` is toward zero, and `frinta` is
// ties AWAY FROM ZERO. `std::round` is the C spelling of `frinta`;
// `std::nearbyint` is `frintn`, and using it here disagrees with the shipped
// bundle on 13084 of 710104 differential vectors.
//
// `fcvtas` is the integer-destination form of the same rule, which is why
// `fcvtasToI64` above rounds with `std::round` too.
inline float frintaToFloat(float v) {
  return std::round(v);
}

// A hash of zero is replaced by a fixed seed before it enters the PRNG
// (0xb38..0xb3c: `cmp w9,#0 / csel w9, w12, w9, eq`).
const unsigned int kSeedFallback = 0x9e3779b9u;

// --- luminance --------------------------------------------------------------
//
//     luma = B * 0.114 + (R * 0.299 + G * 0.587)
//     factor = luma / maxValue
//
// The two `fmadd`s are nested exactly as the listing nests them
// (0x1404..0x1428): G is scaled first, R is folded in next, and B is folded in
// LAST, so the rounding is the shipped rounding and not an equivalent-looking
// reassociation.
const float kLumaR = 0.299f;
const float kLumaG = 0.587f;
const float kLumaB = 0.114f;

template <typename T>
inline float lumaFactor(const T* p, float maxValue) {
  const float g = static_cast<float>(p[2]) * kLumaG;
  const float rg = std::fma(static_cast<float>(p[1]), kLumaR, g);
  const float l = std::fma(static_cast<float>(p[3]), kLumaB, rg);
  return l / maxValue;
}

// --- the amount / spread clamp ----------------------------------------------
//
//     fcsel s2, s3, s1, gt     ; 1.0 or the value
//     fcsel s2, s1, s2, mi     ; the value or the first result
//
// The SECOND test is against the ORIGINAL value, not against the first
// result, so a negative value wins over the upper clamp, and a NaN passes
// straight through (0x764..0x790).
inline float clampAmount(float v) {
  const float hi = (v > 1.0f) ? 1.0f : v;
  return (v < 0.0f) ? v : hi;
}

// --- frame copy ------------------------------------------------------------
//
// width * 4 ELEMENTS per row, source stride in.rowBytes, destination stride
// out.rowBytes (0x888..0xab8, and the same shape at 0x9a0 and 0x1784).
// rowBytes counts ELEMENTS of T, not bytes: the 16-bit build multiplies it
// by two when it forms a byte address.
template <typename T>
void copyFrame(const View<const T>& in, View<T>& out, int width, int height) {
  const long count = static_cast<long>(width) * 4;
  for (int row = 0; row < height; ++row) {
    const T* s = in.data + static_cast<long>(row) * in.rowBytes;
    T* d = out.data + static_cast<long>(row) * out.rowBytes;
    for (long i = 0; i < count; ++i) d[i] = s[i];
  }
}

// --- the spread blend ------------------------------------------------------
//
//     res = fma((float)in[i], 1 - spread, spread * (float)out[i])
//     out[i] = (T)llround(res)
//
// (0x18fc..0x1920.) The blend reads the ORIGINAL input and the mode's output.
template <typename T>
void blendFrames(const View<const T>& in, View<T>& out, int width, int height,
                 float spread) {
  const float inv = 1.0f - spread;
  // The caller has already rejected an empty frame, so the width is >= 1 and
  // the count is at least 4. The shipped blend loop runs zero times for a
  // zero-width row rather than clamping the count up to one, which is why
  // there is no `count < 1` guard here: the empty frame never reaches this.
  const long count = static_cast<long>(width) * 4;
  for (int row = 0; row < height; ++row) {
    const T* s = in.data + static_cast<long>(row) * in.rowBytes;
    T* d = out.data + static_cast<long>(row) * out.rowBytes;
    for (long i = 0; i < count; ++i) {
      const float res = std::fma(static_cast<float>(s[i]), inv,
                                  spread * static_cast<float>(d[i]));
      // The shipped code converts with `fcvtas` and stores the low byte (or
      // halfword) of the 64-bit result, with no clamp of its own.
      d[i] = static_cast<T>(fcvtasToI64(res));
    }
  }
}

// --- mode 1: Block Displace (0x1248..0x156c) ------------------------------
//
// The frame is tiled into blockSize squares. Each block picks two
// displacements from its own hash and copies its region from the source,
// displaced. The SOURCE is displaced, not the destination.
template <typename T>
void blockDisplace(const View<const T>& in, View<T>& out, const Params& p,
                   unsigned int top, float amountC, float maxValue) {
  const int width = in.width;
  const int height = in.height;

  // block = clamp(blockSize, 2, 4096) (0x1248..0x1260)
  int block = p.blockSize > 2 ? p.blockSize : 2;
  block = block < 4096 ? block : 4096;

  const int numBlockRows = (height + block - 1) / block;
  const int numBlockCols = (width + block - 1) / block;
  if (numBlockRows < 1) return;   // 0x1274: dead in practice, but shipped

  // maxMag = (amountC * block) * 2, a plain multiply then a doubling
  // (0x12b8..0x12c0) — not an fma.
  const float maxMag = (amountC * static_cast<float>(block)) * 2.0f;

  for (int blockRow = 0; blockRow < numBlockRows; ++blockRow) {
    const int y0 = blockRow * block;
    const int y1 = (height < y0 + block) ? height : y0 + block;
    for (int blockCol = 0; blockCol < numBlockCols; ++blockCol) {
      const int x0 = blockCol * block;
      const int x1 = (width < x0 + block) ? width : x0 + block;

      // The block's hash: the top hash, mixed with the block's position and
      // the mode's salt (0x1304..0x1380).
      const unsigned int h =
          hashKey(top, static_cast<unsigned int>(blockRow) * 65537u +
                           static_cast<unsigned int>(blockCol),
                  0x101u);
      unsigned int state = h ? h : kSeedFallback;

      // The luminance sample is taken at the block's centre (0x13ac..0x1428).
      float lf = 1.0f;
      if (p.luma & 1) {
        const int cx = (x0 + x1) / 2;
        const int cy = (y0 + y1) / 2;
        const int sx = (cx < width - 1) ? cx : width - 1;
        const int sy = (cy < height - 1) ? cy : height - 1;
        lf = lumaFactor(in.data + static_cast<long>(sy) * in.rowBytes +
                            static_cast<long>(sx) * 4,
                        maxValue);
      }

      // One draw decides whether the block moves at all; two more produce the
      // two displacements (0x142c..0x1494).
      uint32_t disp = 0;
      uint32_t disp2 = 0;
      state = xs32(state);
      const float r1 = unitFloat(state);
      if (r1 < amountC * lf) {
        state = xs32(state);
        disp = fcvtasLow32((maxMag * std::fma(unitFloat(state), 2.0f, -1.0f)) * lf);
        state = xs32(state);
        disp2 = fcvtasLow32(((maxMag * std::fma(unitFloat(state), 2.0f, -1.0f)) *
                             0.5f) *
                            lf);
      }

      for (int row = y0; row < y1; ++row) {
        // The shipped clamp is a 32-bit ADD (`add w3, w28, w30` at 0x14dc),
        // so the sum wraps in 32 bits and the SIGNED comparison that follows
        // sees the wrapped value. The displacement is bounded by 2 * amount *
        // block, which is at most 8192, so the wrap is unreachable in
        // practice; it is reproduced anyway rather than assumed away.
        int sr = asSigned32(disp2 + static_cast<uint32_t>(row));
        if (sr > height - 1) sr = height - 1;
        if (sr < 0) sr = 0;
        const T* srow = in.data + static_cast<long>(sr) * in.rowBytes;
        T* drow = out.data + static_cast<long>(row) * out.rowBytes;
        for (int col = x0; col < x1; ++col) {
          // 0x1504: `add w9, w26, w10` -- 32-bit, and the comparisons at
          // 0x1508..0x1514 are signed 32-bit ones.
          int sc = asSigned32(disp + static_cast<uint32_t>(col));
          if (sc > width - 1) sc = width - 1;
          if (sc < 0) sc = 0;
          const T* s = srow + sc * 4;
          T* d = drow + static_cast<long>(col) * 4;
          d[0] = s[0];
          d[1] = s[1];
          d[2] = s[2];
          d[3] = s[3];
        }
      }
    }
  }
}

// --- mode 2: RGB Channel Shift (0xac8..0xc7c) -----------------------------
//
// R is taken from the LEFT of the pixel and B from the RIGHT; A and G are
// taken from the pixel itself. There is no prior copy: every pixel is written.
template <typename T>
void channelShift(const View<const T>& in, View<T>& out, const Params& p,
                  unsigned int top, float amountC, float maxValue) {
  const int width = in.width;
  const int height = in.height;

  const unsigned int h = hashKey(top, 0u, 0x202u);
  unsigned int state = h ? h : kSeedFallback;
  state = xs32(state);
  const float jitter = std::fma(unitFloat(state), 0.25f, 0.75f);
  // shiftMag = (shift * amountC) * jitter, two separate multiplies (0xb60).
  const float shiftMag = (p.shift * amountC) * jitter;

  for (int row = 0; row < height; ++row) {
    const T* srow = in.data + static_cast<long>(row) * in.rowBytes;
    T* drow = out.data + static_cast<long>(row) * out.rowBytes;
    for (int col = 0; col < width; ++col) {
      float mag = shiftMag;
      if (p.luma & 1) {
        mag = shiftMag * lumaFactor(srow + static_cast<long>(col) * 4, maxValue);
      }
      // 0xbd8: `fcvtas x14, s6` -- the displacement as a 64-bit integer, of
      // which only the LOW 32 BITS are used from here on.
      //
      // 0xbe0: `sub w16, w9, x14` -- a 32-BIT subtraction, so it WRAPS.
      // 0xbec: `add w14, w14, x9` -- a 32-BIT addition, so it WRAPS.
      //
      // The comparisons that follow are signed 32-bit ones, so a wrapped
      // negative index is clamped to zero rather than used. `shift` is a
      // user-supplied float with no upper bound, so this is reachable: a
      // displacement whose low 32 bits have the top bit set lands on column
      // zero, not on the last column.
      const uint32_t disp = fcvtasLow32(mag);
      // The shipped code takes `min(x - shift, width - 1)` and then replaces a
      // NEGATIVE index with 0, so the order is min-then-zero.
      int left = asSigned32(static_cast<uint32_t>(col) - disp);
      if (left > width - 1) left = width - 1;
      if (left < 0) left = 0;
      int right = asSigned32(disp + static_cast<uint32_t>(col));
      if (right > width - 1) right = width - 1;
      if (right < 0) right = 0;

      const T* s = srow + static_cast<long>(col) * 4;
      T* d = drow + static_cast<long>(col) * 4;
      d[0] = s[0];                                  // A, straight through
      d[1] = srow[left * 4 + 1];                    // R, from the left
      d[2] = s[2];                                  // G, straight through
      d[3] = srow[right * 4 + 3];                   // B, from the right
    }
  }
}

// --- mode 3: Scanline Jitter (0xc7c..0xeb8) -------------------------------
//
// The frame is cut into `scanline`-tall bands. Each band picks one sideways
// displacement and slides its rows; the source row is the row itself.
template <typename T>
void scanlineJitter(const View<const T>& in, View<T>& out, const Params& p,
                    unsigned int top, float amountC, float maxValue) {
  const int width = in.width;
  const int height = in.height;

  const int band = (p.scanline > 1) ? p.scanline : 1;
  const int clampedBand = band < 4096 ? band : 4096;
  const int bandHalf = clampedBand >> 1;
  // amp = (amountC * width) * 0.25 (0xc98..0xcb0)
  const float amp = (amountC * static_cast<float>(width)) * 0.25f;

  for (int row = 0; row < height; row += clampedBand) {
    const unsigned int h =
        hashKey(top, static_cast<unsigned int>(row / clampedBand), 0x303u);
    unsigned int state = h ? h : kSeedFallback;

    // The luminance sample is taken half a band below the band's first row,
    // in the middle column (0xd6c..0xdbc).
    float lf = 1.0f;
    if (p.luma & 1) {
      int lr = bandHalf + row;
      if (lr > height - 1) lr = height - 1;
      const int lc = width / 2;
      lf = lumaFactor(in.data + static_cast<long>(lr) * in.rowBytes +
                          static_cast<long>(lc) * 4,
                      maxValue);
    }

    // One draw decides whether the band moves; a second produces the shift.
    uint32_t disp = 0;
    state = xs32(state);
    const float r1 = unitFloat(state);
    if (r1 < amountC * lf) {
      state = xs32(state);
      disp = fcvtasLow32((amp * std::fma(unitFloat(state), 2.0f, -1.0f)) * lf);
    }

    const int rowEnd = (height < row + clampedBand) ? height : row + clampedBand;
    for (int r = row; r < rowEnd; ++r) {
      const T* srow = in.data + static_cast<long>(r) * in.rowBytes;
      T* drow = out.data + static_cast<long>(r) * out.rowBytes;
      for (int col = 0; col < width; ++col) {
        // 0xe00..0xe7c: `fcvtas x9, s17` then `sxtw x19, w9`, so the
        // displacement enters as the SIGN-EXTENDED low 32 bits. The sum is
        // then formed in 64 bits (`add x10, x19, x16`), the upper bound is
        // tested against the sum's LOW 32 bits, and the zero test is made
        // against the full 64-bit sum.
        const int64_t sum = static_cast<int64_t>(asSigned32(disp)) + col;
        int sc = asSigned32(static_cast<uint32_t>(sum));
        if (sc > width - 1) sc = width - 1;
        if (sum < 0) sc = 0;
        const T* s = srow + static_cast<long>(sc) * 4;
        T* d = drow + static_cast<long>(col) * 4;
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        d[3] = s[3];
      }
    }
  }
}

// --- mode 4: Bad Signal (0xeb8..0x1248) -----------------------------------
//
// A rolling band of displaced pixels, quantised to a coarse step. The alpha
// channel is NOT quantised.
template <typename T>
void badSignal(const View<const T>& in, View<T>& out, const Params& p,
               unsigned int top, unsigned int frame, float amountC,
               float maxValue) {
  const int width = in.width;
  const int height = in.height;

  // bandHalf = fmaxnm(((amountC * 0.2 + 0.05) * height) * 0.5, 1)
  // (0xebc..0xeec)
  const float bandHalf =
      std::fmax((std::fma(amountC, 0.2f, 0.05f) * static_cast<float>(height)) *
                    0.5f,
                1.0f);
  // magScale = (amountC * width) * 0.12 (0xef0..0xf04)
  const float magScale = (amountC * static_cast<float>(width)) * 0.12f;
  // step = fmaxnm(fma(amountC, -250, 256) - 1, 1); quant = maxValue / step
  // (0xf08..0xf28)
  float step = std::fma(amountC, -250.0f, 256.0f) - 1.0f;
  step = std::fmax(step, 1.0f);
  const float quant = maxValue / step;

  // The frame's own hash and amplitude (0xf2c..0xfe4).
  const unsigned int fh = hashKey(top, 0u, 0x404u);
  unsigned int fstate = fh ? fh : kSeedFallback;
  fstate = xs32(fstate);
  const float frameAmp = std::fma(unitFloat(fstate), 0.4f, 0.6f);

  // The band rolls around the frame: centre = (frame * 7) % height (0xf30..0xfb8).
  const unsigned int centre =
      static_cast<unsigned int>((static_cast<unsigned long long>(frame) * 7ull) %
                                 static_cast<unsigned long long>(height));

  for (int row = 0; row < height; ++row) {
    // d = |row - centre|, folded into [0, height/2] (0x1028..0x1038).
    int d = row - static_cast<int>(centre);
    if (d < 0) d = -d;
    const int folded = height - d;
    if (folded < d) d = folded;

    // band = 1 - d / bandHalf, clamped to [0, 1] (0x103c..0x1054).
    float band = 1.0f - static_cast<float>(d) / bandHalf;
    const float capped = (band > 1.0f) ? 1.0f : band;
    band = (band < 0.0f) ? 0.0f : capped;

    float amplitude = frameAmp * band;
    if (p.luma & 1) {
      const int lc = width / 2;
      amplitude = amplitude *
                  lumaFactor(in.data + static_cast<long>(row) * in.rowBytes +
                                 static_cast<long>(lc) * 4,
                             maxValue);
    }

    // The row's own hash (0x1058..0x10b4).
    const unsigned int h =
        hashKey(top, static_cast<unsigned int>(row) ^ 0xb16b00b5u, 0x404u);
    unsigned int state = h ? h : kSeedFallback;
    state = xs32(state);
    const float disp =
        (magScale * std::fma(unitFloat(state), 2.0f, -1.0f)) * amplitude;
    // 0x1150: `fcvtas x19, s23` then `sxtw x20, w19` at 0x1158.
    const uint32_t dcol = fcvtasLow32(disp);

    const T* srow = in.data + static_cast<long>(row) * in.rowBytes;
    T* drow = out.data + static_cast<long>(row) * out.rowBytes;
    for (int col = 0; col < width; ++col) {
      // 0x118c: `add x9, x20, x6` -- a 64-bit sum of the sign-extended
      // displacement and the column; 0x1194: `add w11, w19, x6` -- the same
      // sum but 32-bit, and THAT is the one the upper bound is tested
      // against. The zero test at 0x11a8 is made against the 64-bit sum.
      const int64_t sum = static_cast<int64_t>(asSigned32(dcol)) + col;
      int sc = asSigned32(dcol + static_cast<uint32_t>(col));
      if (sc > width - 1) sc = width - 1;
      if (sum < 0) sc = 0;
      const T* s = srow + static_cast<long>(sc) * 4;
      T* d = drow + static_cast<long>(col) * 4;
      d[0] = s[0];                                   // A, never quantised
      if (amountC > 0.001f) {
        // v = trunc(clamp(round(val / quant) * quant, 0, maxValue))
        // (0x11c8..0x11e8 and its two siblings.)
        for (int c = 1; c < 4; ++c) {
          float q = static_cast<float>(s[c]) / quant;
          q = frintaToFloat(q);
          q = q * quant;
          const float hi = (q > maxValue) ? maxValue : q;
          q = (q < 0.0f) ? 0.0f : hi;
          // The shipped code truncates with `fcvtzs` — toward zero, saturating
          // — and stores the low halfword of the 32-bit result.
          d[c] = static_cast<T>(static_cast<unsigned>(
              static_cast<unsigned long long>(fcvtzsToI32(q)) & 0xffffull));
        }
      } else {
        d[1] = s[1];
        d[2] = s[2];
        d[3] = s[3];
      }
    }
  }
}

// --- mode 5: Datamosh Smear (0x156c..0x1784) ------------------------------
//
// The frame is copied first (0x888..0xab8), then rows are smeared rightward:
// one pixel is stretched over a run of columns.
template <typename T>
void datamoshSmear(const View<const T>& in, View<T>& out, const Params& p,
                   unsigned int top, float amountC, float maxValue) {
  const int width = in.width;
  const int height = in.height;

  // maxMag = (amountC * width) * 0.5 (0x1578..0x1594)
  const float maxMag = (amountC * static_cast<float>(width)) * 0.5f;
  const int smearLen = fcvtzsToI32(amountC * 5.0f);
  const unsigned int divisor = static_cast<unsigned int>(smearLen) + 1u;

  for (int row = 0; row < height; ++row) {
    const unsigned int h = hashKey(top, static_cast<unsigned int>(row), 0x505u);
    unsigned int state = h ? h : kSeedFallback;

    // One draw decides whether the row is smeared at all (0x1644..0x165c).
    state = xs32(state);
    const float r1 = unitFloat(state);
    if (r1 >= amountC) continue;

    // A second draw gives the smear's length in pixels (0x1660..0x1674).
    state = xs32(state);
    // The shipped code divides with `udiv`, which answers 0 for a divisor of 0
    // rather than trapping. The divisor is `trunc(amount * 5) + 1`, and the
    // amount is strictly positive by the time a mode body runs, so the divisor
    // is at least 1 and the case is unreachable — but it is answered the
    // shipped way rather than left to the C++ rules for division by zero.
    const unsigned int rem = divisor ? (state % divisor) : 0u;
    if (rem & 0x80000000u) continue;   // 0x1674: dead, rem < divisor <= 6

    // rem + 1 iterations, each with two more draws (0x1680..0x1710).
    for (unsigned int i = 0; i <= rem; ++i) {
      state = xs32(state);
      const unsigned int colOff = state % static_cast<unsigned int>(width);

      float lf = 1.0f;
      if (p.luma & 1) {
        lf = lumaFactor(in.data + static_cast<long>(row) * in.rowBytes +
                            static_cast<long>(colOff) * 4,
                        maxValue);
      }

      state = xs32(state);
      const float scaled = maxMag * unitFloat(state);
      const int disp = fcvtzsToI32(scaled * lf);

      // 0x1718: `add w9, w20, w9` -- a 32-bit sum of the column offset and
      // the smear length; 0x171c..0x1720 clamp it against width - 1 with a
      // signed compare, and 0x1724 drops the smear when the offset is past
      // the end.
      int end = asSigned32(static_cast<uint32_t>(colOff) +
                           static_cast<uint32_t>(disp));
      if (end > width - 1) end = width - 1;
      if (static_cast<int>(colOff) > end) continue;

      const T* s = in.data + static_cast<long>(row) * in.rowBytes +
                   static_cast<long>(colOff) * 4;
      T* drow = out.data + static_cast<long>(row) * out.rowBytes;
      for (int col = static_cast<int>(colOff); col <= end; ++col) {
        T* d = drow + static_cast<long>(col) * 4;
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        d[3] = s[3];
      }
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// apply<T> — the dispatch.
// ---------------------------------------------------------------------------
template <typename T>
void apply(const View<const T>& in, View<T>& out, const Params& params,
           unsigned seed, float maxValue) {
  const int width = in.width;
  const int height = in.height;

  // 0x738..0x744: an empty frame is left completely alone.
  //
  // The shipped guard is `ldp w15,w16,[x0,#8] / cmp w15,#1 / ccmp w16,#1,#8,ge
  // / b.lt 0x1948`, and `0x1948` is the plain `ret` of the epilogue, reached
  // BEFORE `sub sp, sp, #0x70` — so the early return leaves the stack untouched.
  //
  // MEASURED, not assumed: that exact instruction sequence was assembled and run
  // on this machine. The `ccmp` only compares the height when the width is >= 1,
  // and the `b.lt` takes the return when the height is <= 0. When the width is
  // < 1 the `ccmp` does NOT compare at all: it installs the immediate #8, i.e.
  // NZCV = 0b1000, so N is set and V is clear, and `b.lt` (N != V) is taken.
  // So BOTH instantiations return early for `width < 1 || height < 1` — the
  // 8-bit one at 0x738..0x744 and the 16-bit one at 0x1a24..0x1a30, whose
  // `b.ge 0x1a54` has the same effect with the opposite branch sense.
  if (width < 1 || height < 1) return;

  const float amountC = clampAmount(params.amount);
  const float spreadC = clampAmount(params.spread);

  // 0x794 / 0x79c: no amount or no spread means a plain copy, with no blend.
  //
  // The shipped test is `fcmp sX, #0.0 / b.ls`, and it was MEASURED, not
  // assumed: running that instruction sequence on the machine takes the branch
  // exactly when the value is <= 0, and does NOT take it for an unordered
  // (NaN) comparison. So a NaN amount does NOT take the copy-and-return path;
  // it runs the mode body with a NaN amount. A negated `!(x > 0)` would
  // wrongly capture the NaN, which is why this is written as `<=`.
  if (amountC <= 0.0f) {
    copyFrame(in, out, width, height);
    return;
  }
  if (spreadC <= 0.0f) {
    copyFrame(in, out, width, height);
    return;
  }

  // 0x7f4..0x864: the frame's pattern index and the top hash.
  const unsigned int frame = patternFrame(seed, params.rate);
  const unsigned int top =
      hashKey(static_cast<unsigned int>(params.hashInput), frame,
              static_cast<unsigned int>(params.mode));

  // 0x868..0x884: the one-based dispatch.
  switch (params.mode) {
    case 1:
      blockDisplace(in, out, params, top, amountC, maxValue);
      break;
    case 2:
      channelShift(in, out, params, top, amountC, maxValue);
      break;
    case 3:
      scanlineJitter(in, out, params, top, amountC, maxValue);
      break;
    case 4:
      badSignal(in, out, params, top, frame, amountC, maxValue);
      break;
    case 5:
      copyFrame(in, out, width, height);
      datamoshSmear(in, out, params, top, amountC, maxValue);
      break;
    default:
      copyFrame(in, out, width, height);
      break;
  }

  // 0x189c..0x192c: the spread blend, skipped when spread >= 1.
  if (spreadC < 1.0f) blendFrames(in, out, width, height, spreadC);
}

#pragma clang fp contract(on)

// The differ discovers the bundle's symbols by NAME, so the template has to be
// instantiated explicitly — a definition in this file with no instantiation
// would leave `glitch::apply<unsigned char>` absent.
template void apply<unsigned char>(const View<const unsigned char>&,
                                   View<unsigned char>&, const Params&,
                                   unsigned, float);
// The 16-bit twin — `__ZN6glitch5applyItEEvRKNS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEjf`
// at 0x1a04..0x2d34, 1228 instructions in the bundle, listed in
// /tmp/ae/glitch_us.txt — is the SAME source at a different channel width: the
// only difference is that View<T>::rowBytes counts ELEMENTS of T and the
// element type is a wider unsigned. It is instantiated for the same reason as
// the 8-bit one: the differ discovers the bundle's symbols by NAME, so a
// template with no explicit instantiation is ABSENT from the comparison.
template void apply<unsigned short>(const View<const unsigned short>&,
                                    View<unsigned short>&, const Params&,
                                    unsigned, float);

}  // namespace glitch
