// noisefield.cpp — transcribed from the compiled NoiseField bundle.
//
// Three functions and one table. Each carries the instructions that establish it,
// because that is the whole recovery: a claim about the original that cannot be
// traced to an instruction or to bytes in the binary does not belong here.

#include "noisefield.h"

#include <cmath>
#include <cstring>

namespace noisefield {

// __ZN10noisefield6clampfEfff, 5 instrs
//
//   fcmp s0, s2 / fcsel s2, s2, s0, gt   ; s2 = (v > hi) ? hi : v
//   fcmp s0, s1 / fcsel s0, s1, s2, mi   ; s0 = (v < lo) ? lo : s2
float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

// __ZN10noisefield6clampiEiii, 5 instrs
//
//   cmp w0, w2 / csel w8, w0, w2, lt     ; w8 = (v < hi) ? v : hi
//   cmp w0, w1 / csel w0, w1, w8, lt     ; w0 = (v < lo) ? lo : w8
//
// The conditions are inverted relative to clampf's but the behaviour is the same:
// the low bound is applied last, so an inverted range yields `lo`.
int clampi(int value, int lo, int hi) {
  return value < lo ? lo : (value < hi ? value : hi);
}

// __ZN10noisefield15fbmAmplitudeSumEif, 15 instrs
//
//   cmp w0, #0x1 / csinc w8, w0, wzr, gt ; w8 = octaves > 1 ? octaves : 1
//   fmov s2, #1.0 / movi.2d v1, #0       ; amp = 1, sum = 0
//   fadd s1, s2, s1                      ; sum += amp
//   fmul s2, s0, s2                      ; amp *= gain
//   subs w8, w8, #1 / b.ne               ; n times
//   fmov s0, #1e-6                       ; 0x358637bd, exactly 1e-6f
//   fcmp s1, s0 / fmov s0, #1.0 / fcsel s0, s1, s0, gt
//                                        ; (sum > 1e-6) ? sum : 1.0
float fbmAmplitudeSum(int octaves, float gain) {
  const int count = octaves > 1 ? octaves : 1;
  // Declaration order and operand order both come from the instruction stream and
  // both matter to the last bit of the schedule: the shipped code builds 1.0f before
  // 0.0f (`fmov s2, #1.0` then `movi.2d v1, #0`), and accumulates as
  // `fadd s1, s2, s1` — that is `amplitude + sum`, not `sum + amplitude`.
  float amplitude = 1.0f;
  float sum = 0.0f;
  for (int i = 0; i < count; ++i) {
    sum = amplitude + sum;
    amplitude *= gain;
  }
  return sum > 1e-6f ? sum : 1.0f;
}

// __ZN10noisefield12_GLOBAL__N_16kGrad3E, in __TEXT,__const
//
// Read out of the arm64 slice's __const section at 0x3c28 as 36 32-bit words, each
// holding one small integer. The canonical twelve edge gradients of Perlin's improved
// noise, in order, which the self-check asserts entry by entry.
const int kGrad3[12][3] = {
    {1, 1, 0},   {-1, 1, 0},  {1, -1, 0},  {-1, -1, 0},
    {1, 0, 1},   {-1, 0, 1},  {1, 0, -1},  {-1, 0, -1},
    {0, 1, 1},   {0, -1, 1},  {0, 1, -1},  {0, -1, -1},
};

// __ZN10noisefield9PermTable5buildEj, 109 instrs
//
//   adrp/ldr q0..q1 from __const 0x3b20 ... stp q0, q1, [sp] ...   ; 16 x 16 bytes
//       ; the identity permutation 0..255 is COPIED OUT OF __const into a local:
//       ; the constant section literally holds the bytes 00 01 02 ... ff
//   mov w8, #0x79b9 / movk w8, #0x9e37, lsl #16   ; w8 = 0x9e3779b9
//   cmp w1, #0x0 / csel w9, w8, w1, eq            ; seed = arg ? arg : 0x9e3779b9
//   mov w8, #0xff / mov x10, sp                   ; i = 255, tmp = the local copy
// loop:
//   eor w9, w9, w9, lsl #13
//   eor w9, w9, w9, lsr #17
//   eor w9, w9, w9, lsl #5                        ; xorshift32, once per iteration
//   add w11, w8, #0x1 / udiv w12, w9, w11 / msub w11, w12, w11, w9
//                                                 ; j = seed % (i + 1)
//   ldrb w12, [x10, x8] / ldrb w13, [x10, w11, uxtw]
//   strb w13, [x10, x8] / strb w12, [x10, w11, uxtw]
//                                                 ; swap(tmp[i], tmp[j])
//   subs x8, x8, #0x1 / b.hi loop                 ; while (i > 0)
//   ... then 16 stores to [x0, #0x000] and the same 16 again to [x0, #0x100]
//
// Fisher-Yates, descending, seeded by xorshift32. The tail is the duplication: each
// 16-byte chunk of the shuffled copy is loaded once and stored twice, which is
// `p[i] = tmp[i & 255]` over 512 output bytes.
namespace {

// The identity permutation, as a literal table rather than a counting loop.
//
// This is not stylistic: the shipped `build` opens with sixteen 16-byte vector loads
// from `__TEXT,__const` and sixteen stores to the stack — a copy of a CONSTANT ARRAY,
// not a loop that fills 0..255 at runtime. Writing the loop instead compiled to 85
// instructions against the bundle's 109, and the missing 24 were exactly that copy.
const unsigned char kIdentity[256] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
    64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
    80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95,
    96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111,
    112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127,
    128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143,
    144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
    160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175,
    176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,
    192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207,
    208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223,
    224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
    240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255,
};

// One fractal Brownian sample, normalised by its OWN amplitude sum.
//
// This is the block the bundle inlines eight times: four times in `curlField`
// and four times in `fieldAt`'s gradient mode. Each copy is the same shape,
// and the shape is pinned by `fbm3` (which is the same arithmetic, spelled out
// as a separate exported function) and by `fbmAmplitudeSum` (which is the
// amplitude sum, likewise exported).
//
//   count = octaves > 1 ? octaves : 1
//   sum = 0, amplitude = 1, frequency = 1
//   repeat count:
//       sum     = noise3(x*frequency, y*frequency, z*frequency) * amplitude + sum
//       frequency *= freqMult
//       amplitude *= gain
//   den = 0, term = 1
//   repeat count: den += term; term *= gain
//   den = den > 1e-6f ? den : 1.0f
//   return sum / den
//
// The two things that are easy to get wrong and are settled by the listing:
// the sum is accumulated with a FUSED multiply-add (`fmadd s1, s0, s14, s1`),
// and the amplitude sum is divided out PER SAMPLE rather than once per field -
// `curlField` divides each of its four samples by its own denominator before
// taking the differences.
inline float fbmSample(const PermTable& table, float x, float y, float z, const Params& params) {
  const int count = params.octaves > 1 ? params.octaves : 1;

  float sum = 0.0f;
  float amplitude = 1.0f;
  float frequency = 1.0f;
  for (int i = 0; i < count; ++i) {
    sum = std::fma(noise3(table, x * frequency, y * frequency, z * frequency), amplitude, sum);
    frequency *= params.freqMult;
    amplitude *= params.gain;
  }

  float denom = 0.0f;
  float term = 1.0f;
  for (int i = 0; i < count; ++i) {
    denom = denom + term;
    term *= params.gain;
  }
  if (!(denom > 1e-6f)) denom = 1.0f;

  return sum / denom;
}


// __ZN10noisefield12_GLOBAL__N_19curlFieldERKNS_9PermTableEfffRKNS_6ParamsE
// 163 instructions, arm64 0x1d00..0x1f88 in /tmp/ae/noisefield.asm.
//
// The CURL of the field: the 2D PERPENDICULAR derivative of the noise field,
// sampled at four points a fixed epsilon apart. The listing is unambiguous about
// every step of it:
//
//   mov.16b v8, v2 / v9, v1 / v10, v0      ; z, y, x are copied to callee-saved
//                                          ; registers before anything else
//   fmov s0, #0.01f (0x3c23d70a)          ; the epsilon
//   fadd s13, s1, s0                      ; y + eps
//   ldr w8, [x1, #0x10]                   ; Params::octaves
//   ldp s12, s11, [x1, #0x14]             ; Params::freqMult, Params::gain
//   cmp w8, #1 / csinc w20, w8, wzr, gt   ; count = octaves > 1 ? octaves : 1
//
// then FOUR samples, each of which is a fractal Brownian sum INLINED (each one
// calls `noise3` once per octave) followed by its OWN amplitude sum:
//
//   fmov s14, #1.0 / movi.2d v1, #0       ; amplitude = 1, sum = 0
//   fmov s15, #1.0                        ; frequency = 1
//   fmul s0, s10, s15 / s1, s13, s15 / s2, s8, s15   ; (x, y+eps, z) * frequency
//   bl noise3
//   fmadd s1, s0, s14, s1                 ; sum = result * amplitude + sum
//   fmul s15, s12, s15                    ; frequency *= freqMult
//   fmul s14, s11, s14                    ; amplitude *= gain
//   ... then the amplitude sum, rebuilt inline:
//   fmov s0, #1.0 / movi.2d v1, #0
//   fadd s1, s1, s0 / fmul s0, s11, s0    ; den += term; term *= gain
//   fcmp / fcsel with 1e-6f (0x358637bd)  ; den = den > 1e-6f ? den : 1.0f
//
// The four samples are, in order:
//   1. (x,       y + eps, z)      2. (x,       y - eps, z)
//   3. (x + eps, y,       z)      4. (x - eps, y,       z)
// and the epsilon is added to the coordinate BEFORE the frequency multiplies it.
//
// The final step is packed, and it is the reason this function has a name:
//   fdiv.2s  v0, v3, v0          ; {sum1/den1, sum3/den3}
//   fdiv.2s  v1, v2, v1          ; {sum2/den2, sum4/den4}
//   fsub.2s  v0, v0, v1          ; the two differences
//   ldr d1, [x8, #0xc20]         ; __TEXT,__const 0x3c20 = {+0.02f, -0.02f}
//   fdiv.2s  v0, v0, v1          ; divided by that PAIR
//
// so the X component divides by +0.02f and the Y component by -0.02f. That
// asymmetry is the curl: the Y derivative is taken with the opposite sign, which
// is what turns a gradient into a perpendicular one. `fieldAt`'s gradient mode
// does NOT have it - it divides both components by +0.02f (0x3ca3d70a).
//
// The `noinline` is load-bearing: the bundle ships this as a real call
// (`bl ...curlField` at arm64 0xf6c, 0xf98, 0xfb8, 0xfd8 and 0x125c), and a
// curlField that the compiler decides to inline would leave the symbol absent
// from the object file entirely.
__attribute__((noinline))
Vec2 curlField(const PermTable& table, float x, float y, float z, const Params& params) {
  const float eps = 0.01f;

  const float s1 = fbmSample(table, x, y + eps, z, params);
  const float s2 = fbmSample(table, x, y - eps, z, params);
  const float s3 = fbmSample(table, x + eps, y, z, params);
  const float s4 = fbmSample(table, x - eps, y, z, params);

  Vec2 out;
  out.x = (s1 - s2) / 0.02f;
  out.y = (s3 - s4) / -0.02f;
  return out;
}

}  // namespace

// __ZN10noisefield7fieldAtERKNS_9PermTableEffRKNS_6ParamsE
// 226 instructions, arm64 0x0efc..0x1280 in /tmp/ae/noisefield.asm.
//
// The field sampler. The PROLOGUE fixes the coordinate convention and the
// MODE DISPATCH is the whole shape of the function:
//
//   ldr s2, [x1, #0x4]      ; Params::scale
//   mov w8, #0x126f / movk w8, #0x3a83, lsl #16
//   fmov s3, w8             ; 0.3a83126f = 0.001f
//   fmaxnm s2, s2, s3       ; scale = fmax(scale, 0.001f)
//   fdiv s9, s0, s2         ; px = x / scale
//   fdiv s14, s1, s2        ; py = y / scale
//   ldr s13, [x1, #0xc]     ; Params::evolution -> z
//   ldr w8, [x1]            ; Params::mode
//   cmp w8, #0x3 / b.eq    ; the GRADIENT mode is tested FIRST
//   cmp w8, #0x2 / b.ne    ; ... and the ADVECT mode second
//
// MODE 3 -- the gradient. Four fractal-Brownian samples around the point,
// each with its OWN amplitude sum, and the two differences divided by 0.02f:
//
//   a = fbm(px + 0.01f, py, z)     ; s8 = px + 0.01f, built at 0x1000
//   b = fbm(px - 0.01f, py, z)     ; [sp,#0x18], built at 0x1088
//   c = fbm(px, py + 0.01f, z)     ; [sp,#0x14], built at 0x1110
//   d = fbm(px, py - 0.01f, z)     ; [sp,#0x14], rebuilt at 0x1188
//   return { (a - b) / 0.02f, (c - d) / 0.02f }
//
// The divisor is `0x3ca3d70a` = 0.02f for BOTH components. That is the
// difference from `curlField`, which divides its Y component by -0.02f
// (`__TEXT,__const` 0x3c20 = {+0.02f, -0.02f}) because a curl is a
// perpendicular derivative.
//
// MODE 2 -- the advection. FOUR steps along the curl field, UNROLLED:
//
//   ax = px; ay = py
//   repeat 4 times:  c = curlField(table, ax, ay, z, params)
//                    ax = c.x * 0.35f + ax
//                    ay = c.y * 0.35f + ay
//   return { ax - px, ay - py }
//
// The four `bl ...curlField` calls are at 0xf6c, 0xf98, 0xfb8 and 0xfd8, the
// step is `0x3eb33333` = 0.35f, and the accumulation is `fmadd s11, s0, s8, s9`
// -- a FUSED multiply-add, not a separate multiply and add.
//
// MODES 0 AND 1 -- the plain curl, a single tail call to `curlField`.
Vec2 fieldAt(const PermTable& table, float x, float y, const Params& params) {
  const float scale = std::fmax(params.scale, 0.001f);
  const float px = x / scale;
  const float py = y / scale;
  const float z = params.evolution;

  if (params.mode == 3) {
    const float eps = 0.01f;
    const float a = fbmSample(table, px + eps, py, z, params);
    const float b = fbmSample(table, px - eps, py, z, params);
    const float c = fbmSample(table, px, py + eps, z, params);
    const float d = fbmSample(table, px, py - eps, z, params);

    Vec2 out;
    out.x = (a - b) / 0.02f;
    out.y = (c - d) / 0.02f;
    return out;
  }

  if (params.mode == 2) {
    float ax = px;
    float ay = py;
    for (int i = 0; i < 4; ++i) {
      const Vec2 c = curlField(table, ax, ay, z, params);
      ax = std::fma(c.x, 0.35f, ax);
      ay = std::fma(c.y, 0.35f, ay);
    }
    Vec2 out;
    out.x = ax - px;
    out.y = ay - py;
    return out;
  }

  return curlField(table, px, py, z, params);
}

void PermTable::build(unsigned int seed) {
  unsigned char tmp[256];
  for (int i = 0; i < 256; ++i) tmp[i] = kIdentity[i];

  if (seed == 0u) seed = 0x9e3779b9u;

  for (int i = 255; i > 0; --i) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    const int j = static_cast<int>(seed % static_cast<unsigned int>(i + 1));
    const unsigned char swap = tmp[i];
    tmp[i] = tmp[j];
    tmp[j] = swap;
  }

  for (int i = 0; i < 512; ++i) p[i] = tmp[i & 255];
}

// __ZN10noisefield6noise3ERKNS_9PermTableEfff, 183 instrs
//
//   frintm s3, s0 / fcvtms w8, s0 / fsub s3, s0, s3
//       ; xi = (int)floor(x)   and   fx = x - floor(x)
//   and w9, w9, #0xff ...         ; yi, zi masked to a byte; x is masked by the
//                                 ; `uxtb` of the addressing mode instead
//   fmul s0, s3, s3 / fmul s0, s3, s0
//   fmov s2, #-15.0 / fmov s5, #6.0 / fmadd s1, s3, s5, s2
//   fmov s7, #10.0 / fmadd s1, s3, s1, s7 / fmul s0, s0, s1
//       ; u = fx^3 * (fx * (fx * 6 - 15) + 10)   -- the quintic fade, for x, y and z
//   add x8, x0, w8, uxtb / ldrb w11, [x8]        ; p[xi]
//   add x9, x0, x9                               ; &p[yi]
//   add x11, x9, x11 / ldrb w12, [x11]           ; p[A]    (A = yi + p[xi])
//   ldrb w13, [x11, #0x1]                        ; p[A + 1]
//   ldrb w8, [x8, #0x1] / add x8, x9, x8         ; p[xi + 1] -> &p[B]
//   ldrb w16, [x8] / ldrb w14, [x8, #0x1]        ; p[B], p[B + 1]
//   add x15, x0, x10                             ; &p[zi]
//   add x11, x15, x12 / ldrb w10, [x11]          ; p[AA]   (AA = zi + p[A])
//   add x12, x15, x16 / ldrb w16, [x12]          ; p[BA]
//   add x13, x15, x13 / ldrb w16, [x13]          ; p[AB]
//   add x14, x15, x14 / ldrb w15, [x14]          ; p[BB]
//   ldrb w11, [x11, #0x1] ... x12, x13, x14 + 1  ; the same four with zi INCREMENTED,
//                                                ; which the table's second copy makes
//                                                ; an in-range read
//
//   For each of those eight hashes:
//     mul w8, h, #0xab / lsr w12, w8, #11 / msub h, w12, #12, h
//       ; h % 12, compiled to (h * 171) >> 11 -- exact for a byte
//     and x12, h, #0xff / umaddl x12, w12, #12, &kGrad3
//       ; the gradient entry: 12 BYTES apart, and #12 is also the entry size
//     ldp s5, s7, [x12] / scvtf s5 / scvtf s7 / ldr s7, [x12, #0x8] / scvtf s7
//       ; three SIGNED INTEGERS converted to float -- which is how kGrad3's storage
//       ; as int[12][3] was confirmed independently of reading its bytes
//     fmul g1, fy / fmadd g0, fx / fmadd g2, fz
//       ; g0*fx + g1*fy + g2*fz, accumulated with the fy term FIRST
//
//   Then the trilinear combination, and finally:
//     mov w8, #0x6666 / movk w8, #0x3f66 / fmul s0, s0, s1     ; result * 0.9f
float noise3(const PermTable& table, float x, float y, float z) {
  const float fx0 = std::floor(x);
  const float fy0 = std::floor(y);
  const float fz0 = std::floor(z);
  const int xi = static_cast<int>(fx0) & 0xff;
  const int yi = static_cast<int>(fy0) & 0xff;
  const int zi = static_cast<int>(fz0) & 0xff;
  const float fx = x - fx0;
  const float fy = y - fy0;
  const float fz = z - fz0;

  const float u = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
  const float v = fy * fy * fy * (fy * (fy * 6.0f - 15.0f) + 10.0f);
  const float w = fz * fz * fz * (fz * (fz * 6.0f - 15.0f) + 10.0f);

  const unsigned char* p = table.p;
  const int A = p[xi] + yi;
  const int AA = p[A] + zi;
  const int AB = p[A + 1] + zi;
  const int B = p[xi + 1] + yi;
  const int BA = p[B] + zi;
  const int BB = p[B + 1] + zi;

  // One corner: the hash picks a gradient, and the gradient is dotted with the
  // offset to that corner. The hash is a byte, so `% 12` is exact.
  const auto grad = [](unsigned char hash, float dx, float dy, float dz) {
    const int h = hash % 12;
    return static_cast<float>(kGrad3[h][0]) * dx
         + static_cast<float>(kGrad3[h][1]) * dy
         + static_cast<float>(kGrad3[h][2]) * dz;
  };

  const float x1 = fx - 1.0f;
  const float y1 = fy - 1.0f;
  const float z1 = fz - 1.0f;

  const float v000 = grad(p[AA], fx, fy, fz);
  const float v100 = grad(p[BA], x1, fy, fz);
  const float v010 = grad(p[AB], fx, y1, fz);
  const float v110 = grad(p[BB], x1, y1, fz);
  const float v001 = grad(p[AA + 1], fx, fy, z1);
  const float v101 = grad(p[BA + 1], x1, fy, z1);
  const float v011 = grad(p[AB + 1], fx, y1, z1);
  const float v111 = grad(p[BB + 1], x1, y1, z1);

  const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
  const float x00 = lerp(v000, v100, u);
  const float x10 = lerp(v010, v110, u);
  const float x01 = lerp(v001, v101, u);
  const float x11 = lerp(v011, v111, u);
  const float y0 = lerp(x00, x10, v);
  const float y1v = lerp(x01, x11, v);
  return lerp(y0, y1v, w) * 0.9f;
}

// __ZN10noisefield4fbm3ERKNS_9PermTableEfffiff, 51 instrs
//
//   cmp w1, #0x1 / csinc w20, w1, wzr, gt   ; count = max(octaves, 1)
//   fmov s14, #1.0 / movi.2d v13, #0       ; amplitude = 1, sum = 0
//   fmov s15, #1.0                          ; frequency = 1
// loop:
//   fmul s0, x, s15 / fmul s1, y, s15 / fmul s2, z, s15   ; scale by the frequency
//   bl noise3
//   fmadd s13, s0, s14, s13                 ; sum += result * amplitude
//   fmul s15, s9, s15                       ; frequency *= the SIXTH parameter
//   fmul s14, s8, s14                       ; amplitude *= the SEVENTH parameter
//   subs / b.ne
//   ... then a second loop rebuilding `1 + gain + gain^2 + ...` inline, the same
//   1e-6f guard, and `fdiv s0, s13, s0` at the end
float fbm3(const PermTable& table, float x, float y, float z, int octaves, float freqMult,
           float ampGain) {
  const int count = octaves > 1 ? octaves : 1;
  float sum = 0.0f;
  float amplitude = 1.0f;
  float frequency = 1.0f;
  for (int i = 0; i < count; ++i) {
    sum += noise3(table, x * frequency, y * frequency, z * frequency) * amplitude;
    frequency *= freqMult;
    amplitude *= ampGain;
  }
  return sum / fbmAmplitudeSum(octaves, ampGain);
}

// __ZN10noisefield5applyIhEENS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEf, 408 instrs
// __ZN10noisefield5applyItEENS_4ViewIKT_EERNS1_IS2_EERKNS_6ParamsEf, 412 instrs
//
// Read off arm64 0x06e8..0x0d48 (8-bit) and 0x1284..0x18f4 (16-bit) in
// /tmp/ae/noisefield.asm; the two differ only in the width of the pixel.
//
//   * `src.width < 1 || src.height < 1` returns without touching `dst`.
//
//   * `params.amount == 0.0f` takes the COPY path: per row, a `memmove` of
//     `width * 4 * sizeof(T)` bytes.  The fourth argument (`time`) is DEAD: the
//     stream overwrites `s0` with `params.amount` before it is ever read
//     (`ldr s0, [x2, #8]`).
//
//   * Otherwise the permutation is built from `params.seed` (seed 0 becomes
//     0x9e3779b9, inside `PermTable::build`) and every pixel is displaced:
//         (fx, fy) = fieldAt(table, (float)x, (float)y, params)
//         dx = fx * amount;  dy = fy * amount
//         if (dispMode == 2) dy = 0;      // the Y displacement is suppressed
//         if (dispMode == 3) dx = 0;      // the X displacement is suppressed
//     `sx = x + dx` and `sy = y + dy`, but the X block is SKIPPED ENTIRELY when
//     `width < 2` (in which case `sx` stays 0), and likewise the Y block when
//     `height < 2`.
//
//   * The edge behaviour, selected by `edgeMode`:
//         0, 1  clamp to [0, extent-1]
//         2     wrap:  f = fmodf(v, extent); if (f < 0) f += extent;
//               then min(f, extent-1)
//         3     "outside is transparent": the pixel is written as four zero
//               bytes (8 for 16-bit) instead of a sample
//     In edge mode 3 the SHIPPED Y test is a no-op: `fcmp s9, #0` /
//     `fccmp s9, s13, #0x0, pl` / `b.gt` can never reject, because nzcv = 0
//     clears the flags when sy >= 0, and when sy < 0 the second compare (sy
//     against extent-1) cannot be "greater".  Transparency is therefore decided
//     by the X TEST ALONE.  This is transcribed as observed, not as intended.
//
//   * The sample is bilinear between four neighbours:
//         i0 = clamp(floor(sx), 0, width-1);   i1 = (i0+1 >= width)
//                                               ? (edgeMode == 2 ? 0 : width-1)
//                                               : i0+1
//         j0 = clamp(floor(sy), 0, height-1); j1 likewise
//         top = fma(fxx, p10 - p00, p00)
//         bot = fma(fxx, p11 - p01, p01)
//         out = fma(fyy, bot - top, top)
//     with p00 = src[j0][i0], p10 = src[j0][i1], p01 = src[j1][i0],
//     p11 = src[j1][i1], all read UNSIGNED.
//
//   * The result is converted with `fcvtas` -- round to nearest, ties away from
//     zero, saturating to a 64-bit integer -- and the LOW byte (or halfword) is
//     stored.  The conversion therefore WRAPS rather than clamps: 300 becomes
//     44 in 8-bit, 70000 becomes 4464 in 16-bit.
//
//   * `stride` is counted in ELEMENTS of T, not bytes: the row pointer is
//     `data + stride * row`, and the element offset is added separately.
static inline long long cvtAsSat(float value) {
  if (std::isnan(value)) return 0;
  if (value >= 9223372036854775808.0f) return 0x7fffffffffffffffLL;
  if (value <= -9223372036854775808.0f) return -0x7fffffffffffffffLL - 1;
  return static_cast<long long>(std::llround(value));
}

template <class T>
void apply(const View<const T>& src, View<T>& dst, const Params& params, float /*time*/) {
  const int width = src.width;
  const int height = src.height;
  if (width < 1 || height < 1) return;

  if (params.amount == 0.0f) {
    const size_t bytes = static_cast<size_t>(width) * 4 * sizeof(T);
    for (int y = 0; y < height; ++y)
      std::memmove(dst.data + static_cast<long long>(y) * dst.stride,
                   src.data + static_cast<long long>(y) * src.stride, bytes);
    return;
  }

  PermTable table;
  table.build(params.seed);

  const int edgeMode = params.edgeMode;
  const int dispMode = params.dispMode;
  const float amount = params.amount;
  const float xExtent = static_cast<float>(width);
  const float yExtent = static_cast<float>(height);
  const float xTop = xExtent - 1.0f;
  const float yTop = yExtent - 1.0f;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const Vec2 f = fieldAt(table, static_cast<float>(x), static_cast<float>(y), params);
      float dx = f.x * amount;
      float dy = f.y * amount;
      if (dispMode == 2) dy = 0.0f;
      if (dispMode == 3) dx = 0.0f;

      bool inside = true;
      float sx = 0.0f;
      float sy = 0.0f;

      if (width >= 2) {
        sx = dx + static_cast<float>(x);
        if (edgeMode == 2) {
          float w = std::fmodf(sx, xExtent);
          if (w < 0.0f) w += xExtent;
          sx = (w > xTop) ? xTop : w;
        } else if (edgeMode == 3) {
          inside = (sx <= xTop) && (sx >= 0.0f);
        } else {
          const float c = (sx > xTop) ? xTop : sx;
          sx = (sx < 0.0f) ? 0.0f : c;
        }
      }

      if (height >= 2) {
        sy = dy + static_cast<float>(y);
        if (edgeMode == 2) {
          float w = std::fmodf(sy, yExtent);
          if (w < 0.0f) w += yExtent;
          sy = (w > yTop) ? yTop : w;
        } else if (edgeMode == 3) {
          // The shipped Y test is a no-op; only X decides transparency.
        } else {
          const float c = (sy > yTop) ? yTop : sy;
          sy = (sy < 0.0f) ? 0.0f : c;
        }
      }

      T* out = dst.data + static_cast<long long>(y) * dst.stride
             + static_cast<long long>(x) * 4;
      if (!inside) {
        out[0] = out[1] = out[2] = out[3] = 0;
        continue;
      }

      const float floorX = std::floor(sx);
      const float floorY = std::floor(sy);
      const int i0 = (floorX > 0.0f) ? ((floorX >= xTop) ? width - 1
                                                    : static_cast<int>(floorX))
                                    : 0;
      const int j0 = (floorY > 0.0f) ? ((floorY >= yTop) ? height - 1
                                                    : static_cast<int>(floorY))
                                    : 0;
      const int i1 = (i0 + 1 >= width) ? ((edgeMode == 2) ? 0 : width - 1) : i0 + 1;
      const int j1 = (j0 + 1 >= height) ? ((edgeMode == 2) ? 0 : height - 1) : j0 + 1;
      const float fxx = sx - static_cast<float>(i0);
      const float fyy = sy - static_cast<float>(j0);

      const T* row0 = src.data + static_cast<long long>(j0) * src.stride;
      const T* row1 = src.data + static_cast<long long>(j1) * src.stride;

      for (int c = 0; c < 4; ++c) {
        const float p00 = static_cast<float>(row0[i0 * 4 + c]);
        const float p10 = static_cast<float>(row0[i1 * 4 + c]);
        const float p01 = static_cast<float>(row1[i0 * 4 + c]);
        const float p11 = static_cast<float>(row1[i1 * 4 + c]);
        const float top = std::fma(fxx, p10 - p00, p00);
        const float bot = std::fma(fxx, p11 - p01, p01);
        const float value = std::fma(fyy, bot - top, top);
        out[c] = static_cast<T>(static_cast<unsigned long long>(cvtAsSat(value)));
      }
    }
  }
}

template void apply<unsigned char>(const View<const unsigned char>&, View<unsigned char>&,
                                  const Params&, float);
template void apply<unsigned short>(const View<const unsigned short>&, View<unsigned short>&,
                                   const Params&, float);

}  // namespace noisefield
