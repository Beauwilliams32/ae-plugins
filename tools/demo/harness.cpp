// ---------------------------------------------------------------------------
// tools/demo/harness.cpp — demonstration renderer for the recovered fleet.
//
// WHAT THIS IS
//   This is NEW code (tools/, not src/) that CALLS the recovered namespaces and
//   writes the results out as images and numbers. It is not a plugin, it is not
//   part of any shipped bundle, and nothing in src/ is modified by it.
//
// WHAT THE IMAGES ARE, AND ARE NOT
//   Every pixel written here comes from executing recovered code on synthetic
//   input composed by this harness. NONE of it is an After Effects render: no AE
//   host has ever loaded a rebuilt bundle (see RECOVERY-STATUS.md), so an AE
//   screenshot is not available and is not faked. Where a panel is assembled
//   with scaffolding this file wrote — a composite, a colour map, a tonemap —
//   the panel is labelled in results.json so the page can say so.
//
//   The honest claim for an output is: "recovered function F, executed on input
//   I, produced this". The page states the function names per panel for exactly
//   that reason.
//
// OUTPUT CONTRACT
//   out/<demo>_<panel>.ppm   binary P6, 8 bit, top-down rows
//   out/results.json         the numbers, provenance and panel lists
//
//   Composing the raw panels into captioned PNGs is a separate, later step and is
//   not part of this harness. test/demo.test.mjs builds and runs it.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "autoflickerfix.h"
#include "beatgrid.h"
#include "beatsnap.h"
#include "chromakey.h"
#include "colorpulse.h"
#include "echo.h"
#include "glitch.h"
#include "lutblend.h"
#include "maskflow.h"
#include "noisefield.h"
#include "pathscribe.h"
#include "shake.h"
#include "spectra.h"
#include "speedramp.h"
#include "typeriser.h"

// ---------------------------------------------------------------------------
// Small JSON writer. No dependency, no escaping surprises: every string that
// reaches it is either a literal from this file or a number formatted here.
// ---------------------------------------------------------------------------
namespace {

std::string gOutDir = "tools/demo/out";

std::string jstr(const std::string& v) {
  std::string s = "\"";
  for (char c : v) {
    if (c == '"' || c == '\\') { s += '\\'; s += c; }
    else if (c == '\n') { s += "\\n"; }
    else s += c;
  }
  s += "\"";
  return s;
}

std::string jnum(double v, int prec = 6) {
  if (std::isnan(v)) return "\"NaN\"";
  if (std::isinf(v)) return v > 0 ? "\"inf\"" : "\"-inf\"";
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.*g", prec, v);
  return buf;
}

std::string jbool(bool b) { return b ? "true" : "false"; }

std::string jarr(const std::vector<double>& v, int prec = 6) {
  std::string s = "[";
  for (size_t i = 0; i < v.size(); ++i) {
    if (i) s += ", ";
    s += jnum(v[i], prec);
  }
  return s + "]";
}

std::string jstrarr(const std::vector<std::string>& v) {
  std::string s = "[";
  for (size_t i = 0; i < v.size(); ++i) {
    if (i) s += ", ";
    s += jstr(v[i]);
  }
  return s + "]";
}

// A JSON object built by chaining ("key", rawJson) pairs.
struct J {
  std::string body;
  bool first = true;
  J& put(const std::string& k, const std::string& raw) {
    if (!first) body += ", ";
    first = false;
    body += jstr(k) + ": " + raw;
    return *this;
  }
  J& str(const std::string& k, const std::string& v) { return put(k, jstr(v)); }
  J& num(const std::string& k, double v, int prec = 6) { return put(k, jnum(v, prec)); }
  J& boolean(const std::string& k, bool v) { return put(k, jbool(v)); }
  J& arr(const std::string& k, const std::vector<double>& v, int prec = 6) {
    return put(k, jarr(v, prec));
  }
  J& strs(const std::string& k, const std::vector<std::string>& v) {
    return put(k, jstrarr(v));
  }
  J& raw(const std::string& k, const J& v) { return put(k, v.dump()); }
  std::string dump() const { return "{" + body + "}"; }
};

// ---------------------------------------------------------------------------
// Shared palette so every panel reads the same way.
//
// These are the Williams Media AE Fleet dark-theme tokens from brand/tokens.css
// (Trident Studio brand system). The hex stays beside each value so a reader, and
// test/brand.test.mjs, can hold this block to the token file. Chart series follow
// the brand order: accent first, accent-2 second, then the shared signals. This is
// display scaffolding only: no recovered function reads any of it.
// ---------------------------------------------------------------------------
constexpr float c8(int v) { return float(v) / 255.0f; }
const float kBg[3] = {c8(0x16), c8(0x15), c8(0x23)};        // --bg        #161523
const float kLine[3] = {c8(0x3B), c8(0x22), c8(0x2C)};      // --line      #3B222C
const float kMuted[3] = {c8(0x9A), c8(0xA7), c8(0xBD)};     // --muted     #9AA7BD
const float kInk[3] = {c8(0xFF), c8(0xFF), c8(0xFF)};       // --text      #FFFFFF
const float kAccent[3] = {c8(0xFF), c8(0x6B), c8(0x5E)};    // --accent    #FF6B5E
const float kAccent2[3] = {c8(0xE0), c8(0x4E), c8(0x8B)};   // --accent-2  #E04E8B
const float kAccentDeep[3] = {c8(0xAD), c8(0x49), c8(0x40)};// --accent-700 #AD4940
const float kAccentPale[3] = {c8(0xFF), c8(0xF3), c8(0xF2)};// --accent-50 #FFF3F2
const float kInfo[3] = {c8(0x3B), c8(0xA8), c8(0xF0)};      // --info      #3BA8F0
const float kSuccess[3] = {c8(0x2F), c8(0xD3), c8(0x9A)};   // --success   #2FD39A

// ---------------------------------------------------------------------------
// Canvas: 8-bit RGB, top-down, written as binary PPM.
// ---------------------------------------------------------------------------
struct Canvas {
  int w = 0, h = 0;
  std::vector<unsigned char> px;

  Canvas() = default;
  Canvas(int width, int height) : w(width), h(height), px(size_t(width) * height * 3, 0) {}

  static unsigned char q(float v) {
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 255;
    return static_cast<unsigned char>(std::lround(v * 255.0f));
  }

  void set(int x, int y, float r, float g, float b) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    const size_t i = (size_t(y) * w + x) * 3;
    px[i] = q(r); px[i + 1] = q(g); px[i + 2] = q(b);
  }

  void setRGB(int x, int y, const float* c) { set(x, y, c[0], c[1], c[2]); }

  void fill(float r, float g, float b) {
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) set(x, y, r, g, b);
  }

  void hline(int x0, int x1, int y, float r, float g, float b) {
    if (x1 < x0) std::swap(x0, x1);
    for (int x = x0; x <= x1; ++x) set(x, y, r, g, b);
  }

  void vline(int x, int y0, int y1, float r, float g, float b) {
    if (y1 < y0) std::swap(y0, y1);
    for (int y = y0; y <= y1; ++y) set(x, y, r, g, b);
  }

  void rect(int x0, int y0, int x1, int y1, float r, float g, float b) {
    hline(x0, x1, y0, r, g, b);
    hline(x0, x1, y1, r, g, b);
    vline(x0, y0, y1, r, g, b);
    vline(x1, y0, y1, r, g, b);
  }

  void disc(float cx, float cy, float rad, float r, float g, float b) {
    for (int y = int(cy - rad) - 1; y <= int(cy + rad) + 1; ++y)
      for (int x = int(cx - rad) - 1; x <= int(cx + rad) + 1; ++x) {
        const float dx = float(x) + 0.5f - cx, dy = float(y) + 0.5f - cy;
        if (dx * dx + dy * dy <= rad * rad) set(x, y, r, g, b);
      }
  }

  void line(float ax, float ay, float bx, float by, float r, float g, float b, int thick = 1) {
    const int steps = int(std::max(std::fabs(bx - ax), std::fabs(by - ay))) + 1;
    for (int i = 0; i <= steps; ++i) {
      const float t = float(i) / float(steps);
      const int x = int(std::lround(ax + (bx - ax) * t));
      const int y = int(std::lround(ay + (by - ay) * t));
      for (int oy = 0; oy < thick; ++oy)
        for (int ox = 0; ox < thick; ++ox) set(x + ox, y + oy, r, g, b);
    }
  }

  bool save(const std::string& name) const {
    const std::string path = gOutDir + "/" + name;
    std::ofstream f(path, std::ios::binary);
    if (!f) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return false; }
    f << "P6\n" << w << " " << h << "\n255\n";
    f.write(reinterpret_cast<const char*>(px.data()), std::streamsize(px.size()));
    return bool(f);
  }
};

// ---------------------------------------------------------------------------
// Plot: a framed axes box that maps data coordinates to canvas pixels. Labels
// and axis numbers are drawn by Pillow afterwards; this only lays down geometry
// so the curves are exact pixel plots of the recovered functions.
// ---------------------------------------------------------------------------
struct Plot {
  Canvas& c;
  int x0, y0, x1, y1;          // pixel box (inclusive)
  double xmin, xmax, ymin, ymax;

  Plot(Canvas& canvas, int px0, int py0, int px1, int py1,
       double dxmin, double dxmax, double dymin, double dymax)
      : c(canvas), x0(px0), y0(py0), x1(px1), y1(py1),
        xmin(dxmin), xmax(dxmax), ymin(dymin), ymax(dymax) {}

  int X(double v) const {
    return x0 + int(std::lround((v - xmin) / (xmax - xmin) * double(x1 - x0)));
  }
  int Y(double v) const {
    return y1 - int(std::lround((v - ymin) / (ymax - ymin) * double(y1 - y0)));
  }

  void grid(int nx, int ny) const {
    for (int i = 0; i <= nx; ++i) {
      const double v = xmin + (xmax - xmin) * double(i) / nx;
      c.vline(X(v), y0, y1, kLine[0], kLine[1], kLine[2]);
    }
    for (int i = 0; i <= ny; ++i) {
      const double v = ymin + (ymax - ymin) * double(i) / ny;
      c.hline(x0, x1, Y(v), kLine[0], kLine[1], kLine[2]);
    }
    c.rect(x0, y0, x1, y1, kMuted[0], kMuted[1], kMuted[2]);
  }

  // Both series must be the same length; NaN breaks the polyline so a guard or a
  // hole in the data reads as a break rather than a spurious straight segment.
  void curve(const std::vector<double>& xs, const std::vector<double>& ys,
             float r, float g, float b, int thick = 2) const {
    const size_t n = std::min(xs.size(), ys.size());
    for (size_t i = 1; i < n; ++i) {
      if (std::isnan(xs[i]) || std::isnan(ys[i]) || std::isnan(xs[i - 1]) || std::isnan(ys[i - 1]))
        continue;
      c.line(float(X(xs[i - 1])), float(Y(ys[i - 1])), float(X(xs[i])), float(Y(ys[i])),
             r, g, b, thick);
    }
  }

  void dot(double x, double y, float r, float g, float b, int rad = 3) const {
    c.disc(float(X(x)), float(Y(y)), float(rad), r, g, b);
  }

  void dashedH(double y, float r, float g, float b) const {
    const int py = Y(y);
    for (int x = x0; x <= x1; x += 6)
      for (int k = 0; k < 3; ++k) c.set(x + k, py, r, g, b);
  }
};

// Sequential colour ramp for magnitude panels, built only from palette tokens:
// bg -> accent-2 -> accent -> accent-50, evenly spaced over t in [0, 1].
void brandRamp(float t, float* out) {
  const float* stops[4] = {kBg, kAccent2, kAccent, kAccentPale};
  const float x = std::min(std::max(t, 0.0f), 1.0f) * 3.0f;
  const int i = std::min(int(x), 2);
  const float f = x - float(i);
  for (int k = 0; k < 3; ++k) out[k] = stops[i][k] + (stops[i + 1][k] - stops[i][k]) * f;
}

std::vector<double> linspace(double a, double b, int n) {
  std::vector<double> v(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) v[size_t(i)] = a + (b - a) * double(i) / double(n - 1);
  return v;
}

// ---------------------------------------------------------------------------
// Synthetic inputs. These are the "input I" of the honest claim above: authored
// here, on purpose, so the recovered code has something to act on. They are the
// only part of a demo panel that is not recovered code.
// ---------------------------------------------------------------------------

// A green-screen plate: gradient screen, soft-edged subject, deliberate green
// spill on the subject's rim (the case a key assist exists to fix).
Canvas greenScreenPlate(int w, int h) {
  Canvas c(w, h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const float u = float(x) / float(w - 1), v = float(y) / float(h - 1);
      // Uneven lighting across the screen — the reason a fixed threshold fails.
      const float lit = 0.62f + 0.30f * u + 0.10f * v;
      c.set(x, y, 0.07f * lit, lit, 0.13f * lit);
    }
  }
  const float cx = w * 0.5f, cy = h * 0.52f;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const float dx = (float(x) - cx) / (w * 0.19f);
      const float dy = (float(y) - cy) / (h * 0.34f);
      const float d = std::sqrt(dx * dx + dy * dy);
      if (d > 1.0f) continue;
      const float edge = std::min(1.0f, (1.0f - d) * 6.0f);   // antialiased rim
      const float shade = 0.55f + 0.45f * (1.0f - std::fabs(dx));
      float r = 0.72f * shade, g = 0.50f * shade, b = 0.40f * shade;
      // Spill: the rim picks up the screen's green the way a real shoot does.
      const float spill = 1.0f - edge;
      r *= (1.0f - 0.55f * spill);
      b *= (1.0f - 0.55f * spill);
      g = g * (1.0f - 0.55f * spill) + 0.62f * spill;
      c.set(x, y, r, g, b);
    }
  }
  return c;
}

// A scene for the colour-pulse panel: flat colour regions plus a bright accent,
// which is the input a palette sampler is actually pointed at.
Canvas paletteScene(int w, int h) {
  Canvas c(w, h);
  for (int y = 0; y < h; ++y) {
    const float v = float(y) / float(h - 1);
    for (int x = 0; x < w; ++x) {
      const float u = float(x) / float(w - 1);
      if (v < 0.55f) {                      // sky
        const float t = v / 0.55f;
        c.set(x, y, 0.16f + 0.30f * t, 0.34f + 0.34f * t, 0.62f + 0.24f * t);
      } else if (v < 0.68f) {               // horizon band
        c.set(x, y, 0.86f, 0.62f, 0.36f);
      } else {                              // ground
        const float t = (v - 0.68f) / 0.32f;
        c.set(x, y, 0.20f - 0.05f * t + 0.03f * u, 0.34f - 0.08f * t, 0.20f - 0.05f * t);
      }
    }
  }
  c.disc(w * 0.76f, h * 0.24f, h * 0.11f, 1.0f, 0.94f, 0.72f);          // sun
  c.disc(w * 0.76f, h * 0.24f, h * 0.19f, 0.98f, 0.80f, 0.52f);
  c.disc(w * 0.76f, h * 0.24f, h * 0.11f, 1.0f, 0.95f, 0.78f);
  // A warm foreground subject, off-centre, so regional sampling has something to
  // disagree about.
  for (int y = int(h * 0.60f); y < h; ++y)
    for (int x = int(w * 0.20f); x < int(w * 0.30f); ++x) c.set(x, y, 0.78f, 0.26f, 0.22f);
  for (int y = int(h * 0.44f); y < int(h * 0.60f); ++y)
    for (int x = int(w * 0.215f); x < int(w * 0.285f); ++x) c.set(x, y, 0.88f, 0.66f, 0.52f);
  return c;
}

// A colour chart for the LUT panel: a hue sweep row plus a neutral ramp.
Canvas colourChart(int w, int h) {
  Canvas c(w, h);
  const int rows = 4;
  for (int y = 0; y < h; ++y) {
    const int row = y * rows / h;
    const float v = float(row) / float(rows - 1);
    for (int x = 0; x < w; ++x) {
      const float u = float(x) / float(w - 1);
      float r, g, b;
      if (u < 0.34f) {                      // neutral ramp
        r = g = b = u / 0.34f * 0.9f + 0.02f;
      } else if (u < 0.37f) {               // divider
        r = g = b = 0.06f;
      } else if (u < 0.70f) {               // hue sweep at this row's brightness
        const float hue = (u - 0.37f) / 0.33f * 6.0f;
        const float s = 1.0f, val = 0.35f + 0.65f * v;
        const int i = int(hue) % 6;
        const float f = hue - std::floor(hue);
        const float p = val * (1 - s), q = val * (1 - s * f), t = val * (1 - s * (1 - f));
        switch (i) {
          case 0: r = val; g = t; b = p; break;
          case 1: r = q; g = val; b = p; break;
          case 2: r = p; g = val; b = t; break;
          case 3: r = p; g = q; b = val; break;
          case 4: r = t; g = p; b = val; break;
          default: r = val; g = p; b = q; break;
        }
      } else {                              // saturated patches
        const float k = (u - 0.70f) / 0.30f;
        const int i = int(k * 5.0f) % 5;
        const float patch[5][3] = {{0.85f, 0.14f, 0.12f}, {0.95f, 0.72f, 0.10f},
                                   {0.16f, 0.68f, 0.32f}, {0.14f, 0.42f, 0.86f},
                                   {0.72f, 0.24f, 0.78f}};
        r = patch[i][0] * (0.5f + 0.5f * v);
        g = patch[i][1] * (0.5f + 0.5f * v);
        b = patch[i][2] * (0.5f + 0.5f * v);
      }
      c.set(x, y, r, g, b);
    }
  }
  return c;
}

// A source image for the echo panel: premultiplied RGBA floats, which is what
// echo::ImageF holds. A ring plus a wedge reads clearly when it is rotated and
// scaled 9 times.
echo::ImageF echoSource(int w, int h) {
  echo::ImageF img;
  img.width = w;
  img.height = h;
  img.pixels.assign(size_t(w) * h * 4, 0.0f);
  const float cx = w * 0.5f, cy = h * 0.5f;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const float dx = float(x) + 0.5f - cx, dy = float(y) + 0.5f - cy;
      const float d = std::sqrt(dx * dx + dy * dy);
      const float ang = std::atan2(dy, dx);
      float a = 0.0f, r = 0.0f, g = 0.0f, b = 0.0f;
      const float ring = std::fabs(d - h * 0.30f);
      if (ring < h * 0.035f) {                       // ring
        a = 1.0f - ring / (h * 0.035f);
        r = 0.32f; g = 0.82f; b = 0.98f;
      }
      if (d < h * 0.30f && std::fabs(ang) < 0.45f) { // wedge
        a = 1.0f;
        r = 0.99f; g = 0.72f; b = 0.26f;
      }
      if (d < h * 0.06f) {                           // core
        a = 1.0f;
        r = 0.96f; g = 0.34f; b = 0.58f;
      }
      if (a > 0.0f) {
        float* p = &img.pixels[(size_t(y) * w + x) * 4];
        p[0] = r * a; p[1] = g * a; p[2] = b * a; p[3] = a;   // premultiplied
      }
    }
  }
  return img;
}

// A synthetic audio signal for the spectra panel: three steady tones plus a
// rising sweep, at the sample rate the config declares.
std::vector<float> testSignal(int sampleRate, double seconds) {
  const int n = int(sampleRate * seconds);
  std::vector<float> s(size_t(n), 0.0f);
  for (int i = 0; i < n; ++i) {
    const double t = double(i) / sampleRate;
    double v = 0.38 * std::sin(2.0 * M_PI * 220.0 * t);       // steady low
    v += 0.30 * std::sin(2.0 * M_PI * 660.0 * t);             // steady mid
    v += 0.22 * std::sin(2.0 * M_PI * 1760.0 * t);            // steady high
    const double sweep = 300.0 + 5200.0 * (t / seconds);      // chirp
    v += 0.30 * std::sin(2.0 * M_PI * sweep * t);
    // A gap in the middle so a level meter has something to fall to.
    if (t > 0.42 && t < 0.52) v *= 0.05;
    s[size_t(i)] = float(v);
  }
  return s;
}

}  // namespace

// ---------------------------------------------------------------------------
// Demo 1 — NoiseField (noisefield::)
// ---------------------------------------------------------------------------
static J demoNoiseField() {
  const int N = 320;
  noisefield::PermTable a, b;
  a.build(1337u);
  b.build(7u);

  Canvas p1(N, N), p2(N, N), p3(N, N);
  const float freq = 5.0f;
  for (int y = 0; y < N; ++y) {
    for (int x = 0; x < N; ++x) {
      const float fx = float(x) / N * freq, fy = float(y) / N * freq;
      const float n = noisefield::noise3(a, fx, fy, 0.37f);
      const float g1 = 0.5f + 0.5f * n;
      p1.set(x, y, g1, g1, g1);
      const float f3 = noisefield::fbm3(a, fx, fy, 0.37f, 3, 0.5f, 2.0f);
      const float g2 = 0.5f + 0.5f * f3;
      p2.set(x, y, g2, g2, g2);
      const float f6 = noisefield::fbm3(b, fx, fy, 0.37f, 6, 0.5f, 2.0f);
      const float g3 = 0.5f + 0.5f * f6;
      p3.set(x, y, g3, g3, g3);
    }
  }
  p1.save("noisefield_perlin.ppm");
  p2.save("noisefield_fbm3oct.ppm");
  p3.save("noisefield_fbm6oct_seed7.ppm");

  // The value distribution is the number worth printing: an fBm whose gain is
  // mis-set is flat or clipped, and that shows up here and not in the eye.
  std::vector<float> vals;
  vals.reserve(size_t(N) * N);
  for (int y = 0; y < N; ++y)
    for (int x = 0; x < N; ++x)
      vals.push_back(noisefield::fbm3(a, float(x) / N * freq, float(y) / N * freq, 0.37f,
                                      6, 0.5f, 2.0f));
  float lo = vals[0], hi = vals[0];
  double sum = 0.0;
  for (float v : vals) { lo = std::min(lo, v); hi = std::max(hi, v); sum += v; }

  J j;
  j.str("plugin", "NoiseField").str("namespace", "noisefield::")
   .str("input", "No analytic input: the field is a function of coordinates, sampled on a "
                 "320x320 grid at 5 cycles across the panel, z fixed at 0.37.")
   .strs("panels", {"noisefield_perlin.ppm", "noisefield_fbm3oct.ppm",
                    "noisefield_fbm6oct_seed7.ppm"})
   .strs("panelLabels", {"noise3 (single octave)", "fbm3 - 3 octaves",
                         "fbm3 - 6 octaves, seed 7"})
   .strs("recovered", {"PermTable::build", "noise3", "fbm3", "fbmAmplitudeSum"})
   .num("seedA", 1337).num("seedB", 7)
   .num("fbmMin", lo).num("fbmMax", hi).num("fbmMean", sum / double(vals.size()))
   .num("amplitudeSum3", noisefield::fbmAmplitudeSum(3, 0.5f))
   .num("amplitudeSum6", noisefield::fbmAmplitudeSum(6, 0.5f))
   .str("note", "Both seeds run the same Fisher-Yates shuffle recovered from the bundle; "
                "the difference between the two fBm panels is the permutation, not the "
                "octave count, which is why the third panel names its seed.");
  return j;
}

// ---------------------------------------------------------------------------
// Demo 2 — ChromaKeyAssist (chromakey::)
// ---------------------------------------------------------------------------
static J demoChromaKey() {
  const int W = 480, H = 270;
  Canvas plate = greenScreenPlate(W, H);

  const chromakey::RGB keyRgb{0.14f, 0.72f, 0.16f};
  const chromakey::YCbCr key = chromakey::rgbToYCbCr(keyRgb);

  const float sens = 40.0f;
  const float thr = chromakey::sensitivityToThreshold(sens);
  const float thrHard = chromakey::sensitivityToThreshold(12.0f);

  std::vector<float> dist(size_t(W) * H, 0.0f);
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      const size_t i = (size_t(y) * W + x) * 3;
      chromakey::RGB px{float(plate.px[i]) / 255.0f, float(plate.px[i + 1]) / 255.0f,
                        float(plate.px[i + 2]) / 255.0f};
      dist[size_t(y) * W + x] = chromakey::chromaDistance(chromakey::rgbToYCbCr(px), key);
    }
  }
  // Auto-level: the 97th percentile of the distance field is where the screen is,
  // so a threshold at 60% of it keys most of the screen without eating the subject.
  const float p97 = chromakey::percentile(dist.data(), dist.size(), 97.0f);
  const float thrAuto = p97 * 0.60f;

  Canvas matteFixed(W, H), matteAuto(W, H), comp(W, H);
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      const float d = dist[size_t(y) * W + x];
      const float mF = d >= thr ? 1.0f : 0.0f;
      const float s = std::min(1.0f, std::max(0.0f, (d - thrAuto * 0.75f) / (thrAuto * 0.35f)));
      const float mA = s * s * (3.0f - 2.0f * s);   // soft edge, this harness's choice
      matteFixed.set(x, y, 0.55f * mF + 0.05f, 0.72f * mF + 0.06f, 0.95f * mF + 0.08f);
      matteAuto.set(x, y, 0.55f * mA + 0.05f, 0.72f * mA + 0.06f, 0.95f * mA + 0.08f);

      const size_t i = (size_t(y) * W + x) * 3;
      const float fr = float(plate.px[i]) / 255.0f;
      const float fg = float(plate.px[i + 1]) / 255.0f;
      const float fb = float(plate.px[i + 2]) / 255.0f;
      const float u = float(x) / float(W - 1), v = float(y) / float(H - 1);
      const float br = 0.92f - 0.62f * v, bg = 0.55f - 0.30f * v, bb = 0.30f + 0.55f * u;
      comp.set(x, y, fr * mA + br * (1.0f - mA), fg * mA + bg * (1.0f - mA),
               fb * mA + bb * (1.0f - mA));
    }
  }
  plate.save("chromakey_input.ppm");
  matteFixed.save("chromakey_matte_fixed.ppm");
  matteAuto.save("chromakey_matte_auto.ppm");
  comp.save("chromakey_composite.ppm");

  // Edge energy on the matte, through the recovered gradientMagnitude, is what
  // tells a keyer whether the edge is soft enough to hold up.
  double edgeSum = 0.0;
  float edgeMax = 0.0f;
  std::vector<float> matteField(size_t(W) * H, 0.0f);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const float d = dist[size_t(y) * W + x];
      const float s = std::min(1.0f, std::max(0.0f, (d - thrAuto * 0.75f) / (thrAuto * 0.35f)));
      matteField[size_t(y) * W + x] = s * s * (3.0f - 2.0f * s);
    }
  for (int y = 1; y < H - 1; ++y)
    for (int x = 1; x < W - 1; ++x) {
      const float g = chromakey::gradientMagnitude(matteField.data(), W, H, long(W), x, y);
      edgeSum += g;
      edgeMax = std::max(edgeMax, g);
    }

  J j;
  j.str("plugin", "ChromaKeyAssist").str("namespace", "chromakey::")
   .str("input", "A 480x270 synthetic green-screen plate: unevenly lit screen, soft-edged "
                 "subject, and deliberate green spill on the subject rim.")
   .strs("panels", {"chromakey_input.ppm", "chromakey_matte_fixed.ppm",
                    "chromakey_matte_auto.ppm", "chromakey_composite.ppm"})
   .strs("panelLabels", {"synthetic plate", "chromaDistance >= threshold (sensitivity 40)",
                         "auto-levelled matte (97th percentile)", "matte applied over a backdrop"})
   .strs("recovered", {"rgbToYCbCr", "chromaDistance", "sensitivityToThreshold", "percentile",
                       "gradientMagnitude"})
   .num("keyR", keyRgb.r).num("keyG", keyRgb.g).num("keyB", keyRgb.b)
   .num("keyY", key.y).num("keyCb", key.cb).num("keyCr", key.cr)
   .num("sensitivity", sens).num("threshold", thr)
   .num("hardThreshold", thrHard)
   .num("percentile97", p97).num("autoThreshold", thrAuto)
   .num("distanceMin", *std::min_element(dist.begin(), dist.end()))
   .num("distanceMax", *std::max_element(dist.begin(), dist.end()))
   .num("edgeMean", edgeSum / double((H - 2) * (W - 2))).num("edgeMax", edgeMax)
   .str("note", "The plate, the 97th-percentile auto-level and the composite are this "
                "harness's scaffolding. The distance field, the threshold mapping, the "
                "percentile and the edge magnitude are the recovered functions.");
  return j;
}

// ---------------------------------------------------------------------------
// Demo 3 — LUTBlend (lutblend::)
// ---------------------------------------------------------------------------
static J demoLutBlend() {
  const int W = 480, H = 270;
  Canvas chart = colourChart(W, H);

  const int size = 17;
  lutblend::CubeLUT identity, warm, cool;
  lutblend::makeIdentity(&identity, size, true);
  lutblend::makeIdentity(&warm, size, true);
  lutblend::makeIdentity(&cool, size, true);

  // The two grade LUTs are authored here, not recovered: they exist so the
  // recovered sampler and blender have something to mix. Each entry is RGB.
  for (int bi = 0; bi < size; ++bi) {
    for (int gi = 0; gi < size; ++gi) {
      for (int ri = 0; ri < size; ++ri) {
        const size_t i = (size_t(bi) * size * size + size_t(gi) * size + size_t(ri)) * 3;
        const float r = float(ri) / (size - 1), g = float(gi) / (size - 1),
                    b = float(bi) / (size - 1);
        // warm: lift reds, pull blues in the shadows, slight filmic toe
        warm.data[i] = std::pow(std::min(1.0f, r * 1.14f + 0.02f), 0.94f);
        warm.data[i + 1] = std::pow(g, 0.98f) * 1.02f;
        warm.data[i + 2] = std::pow(std::min(1.0f, b * 0.86f), 1.06f);
        // cool: teal shadows, blue highlights
        cool.data[i] = std::pow(r, 1.08f) * 0.92f;
        cool.data[i + 1] = std::pow(std::min(1.0f, g * 1.04f), 0.98f);
        cool.data[i + 2] = std::pow(std::min(1.0f, b * 1.18f + 0.02f), 0.92f);
      }
    }
  }

  const float blends[3] = {0.0f, 0.5f, 1.0f};
  std::vector<std::string> panels, labels;
  for (int k = 0; k < 3; ++k) {
    Canvas out(W, H);
    for (int y = 0; y < H; ++y) {
      for (int x = 0; x < W; ++x) {
        const size_t i = (size_t(y) * W + x) * 3;
        const float rgb[3] = {float(chart.px[i]) / 255.0f, float(chart.px[i + 1]) / 255.0f,
                              float(chart.px[i + 2]) / 255.0f};
        float o[3] = {0.0f, 0.0f, 0.0f};
        lutblend::applyLUTBlend(warm, cool, blends[k], 1.0f, true, rgb, o);
        out.set(x, y, o[0], o[1], o[2]);
      }
    }
    char name[64];
    std::snprintf(name, sizeof name, "lutblend_blend%02d.ppm", int(blends[k] * 100));
    out.save(name);
    panels.push_back(name);
    char lab[64];
    std::snprintf(lab, sizeof lab, "blend = %.2f (warm -> cool, preserveLuma)", blends[k]);
    labels.push_back(lab);
  }

  // One probe through sampleLUT, so the page can show the interpolation itself.
  float probe[3] = {0.0f, 0.0f, 0.0f};
  lutblend::sampleLUT(warm, 0.42f, 0.66f, 0.18f, probe);
  float probeCool[3] = {0.0f, 0.0f, 0.0f};
  lutblend::sampleLUT(cool, 0.42f, 0.66f, 0.18f, probeCool);
  bool identityOk = true;
  {
    float o[3];
    lutblend::applyLUTBlend(identity, identity, 0.0f, 1.0f, false, probe, o);
    for (int i = 0; i < 3; ++i) identityOk = identityOk && std::fabs(o[i] - probe[i]) < 1e-5f;
  }

  J j;
  j.str("plugin", "LUTBlend").str("namespace", "lutblend::")
   .str("input", "A 480x270 colour chart: neutral ramp, hue sweep at four brightness levels, "
                 "and saturated patches. Two 17^3 grade LUTs are authored here; the sampling, "
                 "the blend and the luma preservation are recovered code.")
   .strs("panels", panels).strs("panelLabels", labels)
   .strs("recovered", {"makeIdentity", "sampleLUT", "applyLUTBlend", "lerpf", "luma709",
                       "CubeLUT::valid"})
   .num("lutSize", size)
   .num("probeR", 0.42).num("probeG", 0.66).num("probeB", 0.18)
   .num("probeWarmR", probe[0]).num("probeWarmG", probe[1]).num("probeWarmB", probe[2])
   .num("probeCoolR", probeCool[0]).num("probeCoolG", probeCool[1]).num("probeCoolB", probeCool[2])
   .boolean("identityRoundTripExact", identityOk)
   .boolean("identityValid", identity.valid())
   .boolean("warmValid", warm.valid())
   .str("note", "preserveLuma is on for all three panels: the blend is rescaled so Rec.709 "
                "luma matches the input, which is why the mid-grey row stays put as the "
                "grade swings warm to cool.");
  return j;
}

// ---------------------------------------------------------------------------
// Demo 4 — SpectraKeys (spectra::)
// ---------------------------------------------------------------------------
static J demoSpectra() {
  const int rate = 44100;
  const double seconds = 1.0;
  std::vector<float> mono = testSignal(rate, seconds);

  spectra::AnalysisConfig cfg;
  cfg.sampleRate = double(rate);
  cfg.fftSize = 512;
  cfg.hopRateHz = 400.0;

  spectra::Spectrogram sg = spectra::analyse(mono.data(), mono.size(), cfg);

  Canvas spec(sg.frameCount > 0 ? sg.frameCount : 1, sg.binCount > 0 ? sg.binCount : 1);
  for (int f = 0; f < sg.frameCount; ++f) {
    for (int b = 0; b < sg.binCount; ++b) {
      const float mag = sg.frames[size_t(f)][size_t(b)];
      const float db = spectra::linearToDb(mag);
      // -90..0 dB mapped through the recovered clamp, then the brand ramp:
      // bg -> accent-2 -> accent -> accent-50.
      const float t = spectra::clampf((db + 90.0f) / 90.0f, 0.0f, 1.0f);
      float rgb[3];
      brandRamp(std::pow(t, 1.4f), rgb);
      spec.set(f, sg.binCount - 1 - b, rgb[0], rgb[1], rgb[2]);
    }
  }
  spec.save("spectra_spectrogram.ppm");

  // A band meter driven by bandValues: the level of a band over time, which is
  // the thing the plugin actually publishes as keyframe-able values.
  spectra::BandSpec low{};
  low.lowHz = 150.0f; low.highHz = 320.0f; low.mode = 3;      // RMS
  low.thresholdDb = -60.0f; low.floorDb = -60.0f; low.ceilingDb = 0.0f; low.gainDb = 0.0f;
  low.outputMin = 0.0f; low.outputMax = 100.0f; low.falloffMode = 3; low.falloffTime = 0.12f;
  low.binarise = false;
  spectra::BandSpec mid = low;
  mid.lowHz = 600.0f; mid.highHz = 720.0f;
  spectra::BandSpec high = low;
  high.lowHz = 1600.0f; high.highHz = 1900.0f;

  const int steps = 200;
  std::vector<double> ts = linspace(0.0, seconds, steps);
  std::vector<double> lowV, midV, highV;
  // bandValues(spectrogram, band, hopRateHz) returns ONE value per frame: the third
  // argument is the analysis hop RATE (it sets the falloff step), not a time. So it
  // is called once per band with cfg.hopRateHz, and the curve reads frame
  // round(t * hopRate). An empty vector is a legitimate answer (an out of range
  // band, or an empty spectrogram); indexing it blindly is a segfault, so empty
  // becomes a hole in the curve.
  const std::vector<float> lowAll = spectra::bandValues(sg, low, cfg.hopRateHz);
  const std::vector<float> midAll = spectra::bandValues(sg, mid, cfg.hopRateHz);
  const std::vector<float> highAll = spectra::bandValues(sg, high, cfg.hopRateHz);
  auto levelAt = [&](const std::vector<float>& v, double t) {
    if (v.empty()) return std::nan("");
    const size_t i = std::min(v.size() - 1, size_t(std::max(0.0, std::round(t * cfg.hopRateHz))));
    return double(v[i]);
  };
  for (double t : ts) {
    lowV.push_back(levelAt(lowAll, t));
    midV.push_back(levelAt(midAll, t));
    highV.push_back(levelAt(highAll, t));
  }
  // Peak of a curve, ignoring holes; NaN only when the whole curve is a hole.
  auto peak = [](const std::vector<double>& v) {
    double m = std::nan("");
    for (double x : v) if (!std::isnan(x) && (std::isnan(m) || x > m)) m = x;
    return m;
  };

  Canvas meter(640, 260);
  meter.fill(kBg[0], kBg[1], kBg[2]);
  Plot pl(meter, 40, 20, 620, 220, 0.0, seconds, 0.0, 100.0);
  pl.grid(10, 5);
  pl.curve(ts, lowV, kAccent[0], kAccent[1], kAccent[2]);
  pl.curve(ts, midV, kAccent2[0], kAccent2[1], kAccent2[2]);
  pl.curve(ts, highV, kInfo[0], kInfo[1], kInfo[2]);
  meter.save("spectra_bands.ppm");

  J j;
  j.str("plugin", "SpectraKeys").str("namespace", "spectra::")
   .str("input", "A 1.0 s synthetic mix at 44.1 kHz: 220 Hz, 660 Hz and 1760 Hz tones plus a "
                 "300 -> 5500 Hz sweep, with a 0.1 s dropout at 0.42 s.")
   .strs("panels", {"spectra_spectrogram.ppm", "spectra_bands.ppm"})
   .strs("panelLabels", {"spectrogram, fftSize 512, 400 hops/s (log magnitude, -90..0 dB)",
                         "bandValues: 150-320 Hz, 600-720 Hz, 1600-1900 Hz (RMS)"})
   .strs("recovered", {"analyse", "hannWindow", "fftRadix2", "downmixToMono", "bandValues",
                       "linearToDb", "Spectrogram::hzToBin"})
   .num("sampleRate", rate).num("seconds", seconds)
   .num("fftSize", cfg.fftSize).num("hopRateHz", cfg.hopRateHz)
   .num("binCount", sg.binCount).num("frameCount", sg.frameCount).num("hzPerBin", sg.hzPerBin)
   .num("hannCoherentGain", spectra::hannCoherentGain())
   .num("lowBandPeak", peak(lowV))
   .num("midBandPeak", peak(midV))
   .num("highBandPeak", peak(highV))
   .str("note", "The 220 Hz tone lands in bin 2 at 86.1 Hz per bin, which is what "
                "Spectrogram::hzToBin exists to compute. Colour ramp and dB window are this "
                "harness's display choice; the magnitudes are recovered output.");
  return j;
}

// ---------------------------------------------------------------------------
// Demo 5 — EchoTrails (echo::)
// ---------------------------------------------------------------------------
static J demoEchoTrails() {
  const int N = 300;
  echo::ImageF src = echoSource(N, N);
  echo::ImageF flat;
  flat.width = N;
  flat.height = N;
  flat.pixels.assign(size_t(N) * N * 4, 0.0f);
  for (size_t i = 0; i < size_t(N) * N; ++i) {
    const float a = src.pixels[i * 4 + 3];
    if (a > 0.0f) {
      flat.pixels[i * 4 + 0] = src.pixels[i * 4 + 0] / a;
      flat.pixels[i * 4 + 1] = src.pixels[i * 4 + 1] / a;
      flat.pixels[i * 4 + 2] = src.pixels[i * 4 + 2] / a;
    }
    flat.pixels[i * 4 + 3] = a;
  }

  const int n = 9;
  std::vector<echo::TrailStep> ladder = echo::buildLadder(n, 0.93f, 6.0f, 0.13f);
  std::vector<double> scales, angles, amounts;
  for (const echo::TrailStep& s : ladder) {
    scales.push_back(double(s.scale));
    angles.push_back(double(s.angleDeg));
    amounts.push_back(double(s.amount));
  }

  // Two ladders, same recovered code, different blend mode.
  auto renderLadder = [&](echo::BlendMode mode, Canvas& out) {
    echo::ImageF accum;
    accum.width = N;
    accum.height = N;
    accum.pixels.assign(size_t(N) * N * 4, 0.0f);
    echo::ImageF scratch;
    for (const echo::TrailStep& step : ladder) {
      echo::transformBilinear(src, scratch, step.scale, step.angleDeg);
      echo::compositeOver(accum, scratch, mode, step.amount);
    }
    for (int y = 0; y < N; ++y) {
      for (int x = 0; x < N; ++x) {
        const size_t i = (size_t(y) * N + x) * 4;
        const float a = std::max(accum.pixels[i + 3], 1e-6f);
        // Unpremultiply for display, then a soft highlight rolloff: nine Add
        // composites legitimately exceed 1.0, and clipping them flat would hide
        // exactly the accumulation this panel is meant to show.
        float r = accum.pixels[i + 0] / a, g = accum.pixels[i + 1] / a,
              b = accum.pixels[i + 2] / a;
        r = r / (1.0f + r * 0.45f);
        g = g / (1.0f + g * 0.45f);
        b = b / (1.0f + b * 0.45f);
        const float bg = 0.045f;
        out.set(x, y, r * a + bg * (1 - a), g * a + bg * (1 - a), b * a + bg * (1 - a));
      }
    }
  };

  Canvas srcPanel(N, N), screenPanel(N, N), addPanel(N, N);
  for (int y = 0; y < N; ++y)
    for (int x = 0; x < N; ++x) {
      const size_t i = (size_t(y) * N + x) * 4;
      const float a = flat.pixels[i + 3];
      srcPanel.set(x, y, flat.pixels[i] * a + 0.045f * (1 - a),
                   flat.pixels[i + 1] * a + 0.045f * (1 - a),
                   flat.pixels[i + 2] * a + 0.045f * (1 - a));
    }
  renderLadder(echo::BlendMode::Screen, screenPanel);
  renderLadder(echo::BlendMode::Add, addPanel);
  srcPanel.save("echo_source.ppm");
  screenPanel.save("echo_screen.ppm");
  addPanel.save("echo_add.ppm");

  // One bilinear sample, to show the documented half-brightness edge behaviour.
  float edge[4] = {0, 0, 0, 0};
  echo::sampleBilinear(src, -0.5f, float(N) * 0.5f, edge);

  J j;
  j.str("plugin", "EchoTrails").str("namespace", "echo::")
   .str("input", "A 300x300 generated plate (ring, wedge, core) as premultiplied RGBA floats, "
                 "run through a 9-rung ladder of 0.93 scale and 6 degrees per rung.")
   .strs("panels", {"echo_source.ppm", "echo_screen.ppm", "echo_add.ppm"})
   .strs("panelLabels", {"source plate", "ladder composited with Screen", "ladder composited with Add"})
   .strs("recovered", {"buildLadder", "transformBilinear", "sampleBilinear", "compositeOver",
                       "blendPixel"})
   .arr("ladderScale", scales).arr("ladderAngleDeg", angles).arr("ladderAmount", amounts)
   .num("rungs", n)
   .num("offEdgeSampleR", edge[0]).num("offEdgeSampleG", edge[1]).num("offEdgeSampleB", edge[2])
   .num("offEdgeSampleA", edge[3])
   .str("note", "The off-edge sample is the documented behaviour, not a bug: a tap that falls "
                "outside the image contributes zero and the surviving weights are NOT "
                "renormalised, so half a pixel off the left border returns half brightness. "
                "The highlight rolloff above is this harness's display transform.");
  return j;
}

// ---------------------------------------------------------------------------
// Demo 6 — ColorPulse (colorpulse::)
// ---------------------------------------------------------------------------
static J demoColorPulse() {
  const int W = 480, H = 270;
  Canvas scene = paletteScene(W, H);

  std::vector<colorpulse::RGB> px(size_t(W) * H);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const size_t i = (size_t(y) * W + x) * 3;
      px[size_t(y) * W + x] = colorpulse::RGB{float(scene.px[i]) / 255.0f,
                                              float(scene.px[i + 1]) / 255.0f,
                                              float(scene.px[i + 2]) / 255.0f};
    }

  const colorpulse::RGB avg = colorpulse::averageColor(px.data(), px.size());
  const colorpulse::RGB dom = colorpulse::dominantColor(px.data(), px.size(), 6);
  const colorpulse::RGB bright = colorpulse::brightestRegionColor(px.data(), W, H, 4);
  const colorpulse::RGB byModeAvg = colorpulse::sampleColor(px.data(), W, H, colorpulse::Mode::Average);
  const colorpulse::RGB byModeDom = colorpulse::sampleColor(px.data(), W, H, colorpulse::Mode::Dominant);
  const colorpulse::RGB byModeBright =
      colorpulse::sampleColor(px.data(), W, H, colorpulse::Mode::BrightestRegion);
  const colorpulse::RGB boosted = colorpulse::saturationBoost(dom, 0.35f);

  // The region sampler, over each recovered region rectangle.
  const colorpulse::Region regions[4] = {
      colorpulse::Region::TopLeftThreeQuarters, colorpulse::Region::BottomRightThreeQuarters,
      colorpulse::Region::TopThird, colorpulse::Region::BottomThird};
  const char* regionNames[4] = {"TopLeftThreeQuarters", "BottomRightThreeQuarters", "TopThird",
                                "BottomThird"};
  std::vector<colorpulse::RGB> regionColors;
  std::vector<double> regionX0, regionY0, regionW, regionH;
  Canvas regionsPanel = scene;
  const float regCols[4][3] = {{kInk[0], kInk[1], kInk[2]}, {kAccent[0], kAccent[1], kAccent[2]},
                               {kAccent2[0], kAccent2[1], kAccent2[2]},
                               {kSuccess[0], kSuccess[1], kSuccess[2]}};
  for (int r = 0; r < 4; ++r) {
    const colorpulse::Rect b = colorpulse::regionBounds(regions[r], W, H);
    const int bw = b.maxX - b.minX, bh = b.maxY - b.minY;
    regionX0.push_back(b.minX);
    regionY0.push_back(b.minY);
    regionW.push_back(bw);
    regionH.push_back(bh);
    std::vector<colorpulse::RGB> crop;
    for (int y = b.minY; y < b.minY + bh; ++y)
      for (int x = b.minX; x < b.minX + bw; ++x)
        if (x >= 0 && y >= 0 && x < W && y < H) crop.push_back(px[size_t(y) * W + x]);
    regionColors.push_back(colorpulse::averageColor(crop.data(), crop.size()));
    // Outline the rectangle and tick its top-left corner so the panel shows the
    // geometry the recovered bounds function actually selected.
    for (int x = b.minX; x < b.minX + bw; x += 3) {
      regionsPanel.set(x, b.minY, regCols[r][0], regCols[r][1], regCols[r][2]);
      regionsPanel.set(x, b.minY + bh - 1, regCols[r][0], regCols[r][1], regCols[r][2]);
    }
    for (int y = b.minY; y < b.minY + bh; y += 3) {
      regionsPanel.set(b.minX, y, regCols[r][0], regCols[r][1], regCols[r][2]);
      regionsPanel.set(b.minX + bw - 1, y, regCols[r][0], regCols[r][1], regCols[r][2]);
    }
    for (int dy = 0; dy < 12; ++dy)
      for (int dx = 0; dx < 12; ++dx) regionsPanel.set(b.minX + dx, b.minY + dy, regCols[r][0],
                                                       regCols[r][1], regCols[r][2]);
  }
  scene.save("colorpulse_scene.ppm");
  regionsPanel.save("colorpulse_regions.ppm");

  auto hex = [](const colorpulse::RGB& c) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", int(std::lround(c.r * 255.0f)),
                  int(std::lround(c.g * 255.0f)), int(std::lround(c.b * 255.0f)));
    return std::string(buf);
  };

  std::vector<std::string> swatchNames, swatchHex;
  std::vector<double> swatchR, swatchG, swatchB;
  auto addSwatch = [&](const char* name, const colorpulse::RGB& c) {
    swatchNames.push_back(name);
    swatchHex.push_back(hex(c));
    swatchR.push_back(c.r);
    swatchG.push_back(c.g);
    swatchB.push_back(c.b);
  };
  addSwatch("averageColor(all)", avg);
  addSwatch("dominantColor(6 levels)", dom);
  addSwatch("brightestRegionColor(step 4)", bright);
  addSwatch("sampleColor(Mode::Average)", byModeAvg);
  addSwatch("sampleColor(Mode::Dominant)", byModeDom);
  addSwatch("sampleColor(Mode::BrightestRegion)", byModeBright);
  addSwatch("saturationBoost(dominant, 0.35)", boosted);
  for (int r = 0; r < 4; ++r) addSwatch(regionNames[r], regionColors[size_t(r)]);

  J j;
  j.str("plugin", "ColorPulse").str("namespace", "colorpulse::")
   .str("input", "A 480x270 generated scene: graded sky, sun disc with halo, horizon band, "
                 "ground, and an off-centre warm subject.")
   .strs("panels", {"colorpulse_scene.ppm", "colorpulse_regions.ppm"})
   .strs("panelLabels", {"scene", "sampling regions returned by regionBounds()"})
   .strs("recovered", {"averageColor", "dominantColor", "brightestRegionColor", "sampleColor",
                       "regionBounds", "saturationBoost", "smoothColor", "luma709"})
   .strs("swatchNames", swatchNames).strs("swatchHex", swatchHex)
   .arr("swatchR", swatchR).arr("swatchG", swatchG).arr("swatchB", swatchB)
   .arr("regionX", regionX0).arr("regionY", regionY0).arr("regionW", regionW).arr("regionH", regionH)
   .num("averageLuma", colorpulse::luma709(avg))
   .num("dominantLuma", colorpulse::luma709(dom))
   .num("dominantSaturationBoostLuma", colorpulse::luma709(boosted))
   .str("note", "Region outlines are drawn by this harness on top of the scene; the four "
                "rectangles are the recovered regionBounds() output, printed alongside.");
  return j;
}

// ---------------------------------------------------------------------------
// Demo 7 — SpeedRampCurve (speedramp::)
// ---------------------------------------------------------------------------
static J demoSpeedRamp() {
  speedramp::RampParams r;
  r.start = 0.35;
  r.end = 0.80;
  r.before = 50.0;   // 50% speed before the ramp
  r.after = 200.0;   // 200% after
  r.timeOffset = 0.0;

  const std::vector<double> ts = linspace(0.0, 1.2, 481);
  const speedramp::Easing kinds[5] = {
      speedramp::Easing::LinearA, speedramp::Easing::Smoothstep, speedramp::Easing::Smootherstep,
      speedramp::Easing::Step, speedramp::Easing::LinearB};
  const char* names[5] = {"LinearA", "Smoothstep", "Smootherstep", "Step", "LinearB"};

  Canvas c1(680, 300), c2(680, 300);
  c1.fill(kBg[0], kBg[1], kBg[2]);
  c2.fill(kBg[0], kBg[1], kBg[2]);
  Plot p1(c1, 46, 24, 656, 250, 0.0, 1.2, 0.0, 1.2);
  Plot p2(c2, 46, 24, 656, 250, 0.0, 1.2, 0.0, 220.0);
  p1.grid(6, 6);
  p2.grid(6, 4);

  const float cols[5][3] = {{kInk[0], kInk[1], kInk[2]}, {kAccent[0], kAccent[1], kAccent[2]},
                            {kAccent2[0], kAccent2[1], kAccent2[2]}, {kInfo[0], kInfo[1], kInfo[2]},
                            {kSuccess[0], kSuccess[1], kSuccess[2]}};
  std::vector<double> mapEnds;
  for (int k = 0; k < 5; ++k) {
    speedramp::RampParams rk = r;
    rk.easing = int(kinds[k]);
    std::vector<double> mapped, speed;
    for (double t : ts) {
      mapped.push_back(speedramp::mapTime(rk, t));
      speed.push_back(speedramp::speedAt(rk, t));
    }
    p1.curve(ts, mapped, cols[k][0], cols[k][1], cols[k][2]);
    p2.curve(ts, speed, cols[k][0], cols[k][1], cols[k][2]);
    mapEnds.push_back(mapped.back());
  }
  // Region markers: the ramp window and the two constant-speed plateaus.
  p2.dashedH(50.0, kMuted[0], kMuted[1], kMuted[2]);
  p2.dashedH(200.0, kMuted[0], kMuted[1], kMuted[2]);
  c1.save("speedramp_maptime.ppm");
  c2.save("speedramp_speed.ppm");

  // The integral of speed over the window is the duration the ramp consumes,
  // which is the number an editor actually cares about.
  std::vector<double> integrals;
  for (int k = 0; k < 5; ++k) {
    speedramp::RampParams rk = r;
    rk.easing = int(kinds[k]);
    integrals.push_back(speedramp::easeIntegral(kinds[k], 1.0));
  }

  J j;
  j.str("plugin", "SpeedRampCurve").str("namespace", "speedramp::")
   .str("input", "A ramp from 0.35 s to 0.80 s, 50% speed before and 200% after, evaluated at "
                 "481 time samples across 0 to 1.2 s for each of the five recovered easings.")
   .strs("panels", {"speedramp_maptime.ppm", "speedramp_speed.ppm"})
   .strs("panelLabels", {"mapTime: source time -> output time",
                         "speedAt: output time -> speed percent"})
   .strs("recovered", {"easeValue", "easeIntegral", "speedAt", "mapTime"})
   .num("start", r.start).num("end", r.end).num("before", r.before).num("after", r.after)
   .strs("easingNames", std::vector<std::string>(names, names + 5))
   .arr("mapTimeAtEnd", mapEnds)
   .arr("easeIntegralAt1", integrals)
   .str("note", "LinearA and LinearB are pixel-identical in the shipped bundle: the binary "
                "has no way to tell them apart, so both are named for what they do. Step is "
                "the discontinuity visible in the Step curve.");
  return j;
}

// ---------------------------------------------------------------------------
// Demo 8 — GlitchKit (glitch::)
// ---------------------------------------------------------------------------
static J demoGlitch() {
  // The recovered render path is not in the bundle: only the PRNG and the
  // frame-selection survive as code, so this panel plots exactly those two and
  // claims nothing about the glitch image itself.
  const int fps = 24;
  const double seconds = 3.0;
  const int frames = int(fps * seconds);

  Canvas c(680, 300);
  c.fill(kBg[0], kBg[1], kBg[2]);
  Plot pl(c, 46, 24, 656, 250, 0.0, seconds, 0.0, 1.0);
  pl.grid(6, 5);

  std::vector<double> ts, gate, patternA, patternB;
  std::vector<double> perFrameGate;
  for (int f = 0; f < frames; ++f) {
    const double t = double(f) / fps;
    ts.push_back(t);
    const unsigned int h = glitch::hashKey(unsigned(f), 0x9e3779b9u, 1337u);
    const double g = double(h) / 4294967295.0;
    gate.push_back(g);
    perFrameGate.push_back(g);
    const unsigned int pf = glitch::patternFrame(24u, float(t));
    patternA.push_back(double(pf % 8u) / 7.0);
    patternB.push_back(double(glitch::patternFrame(4u, float(t)) % 8u) / 7.0);
  }
  pl.curve(ts, gate, kAccent[0], kAccent[1], kAccent[2]);
  for (size_t i = 0; i < ts.size(); ++i) {
    // Stem plot: the frame's gate value is a discrete decision, not a signal.
    c.vline(pl.X(ts[i]), pl.Y(0.0), pl.Y(gate[i]), kAccentDeep[0], kAccentDeep[1], kAccentDeep[2]);
    pl.dot(ts[i], gate[i], kAccent[0], kAccent[1], kAccent[2], 2);
  }
  pl.curve(ts, patternA, kAccent2[0], kAccent2[1], kAccent2[2], 2);
  pl.curve(ts, patternB, kInfo[0], kInfo[1], kInfo[2], 2);
  c.save("glitch_gate.ppm");

  double mean = 0.0;
  for (double g : perFrameGate) mean += g;
  mean /= double(perFrameGate.size());
  double var = 0.0;
  for (double g : perFrameGate) var += (g - mean) * (g - mean);
  var /= double(perFrameGate.size());

  // Truncation boundary: the frames where patternFrame's double multiply steps.
  std::vector<double> stepTimes;
  for (int f = 0; f < frames; ++f) {
    const double t = double(f) / fps;
    if (glitch::patternFrame(24u, float(t)) != glitch::patternFrame(24u, float(t - 1.0f / fps)) &&
        f > 0)
      stepTimes.push_back(t);
  }

  J j;
  j.str("plugin", "GlitchKit").str("namespace", "glitch::")
   .str("input", "Frame indices at 24 fps over 3 s. The recovered code here is the PRNG and "
                 "the pattern-frame selector; the glitch render path itself is not in the "
                 "bundle, so nothing on this panel is claimed to be the glitch image.")
   .strs("panels", {"glitch_gate.ppm"})
   .strs("panelLabels", {"hashKey gate per frame, with patternFrame staircases (seed 24 and seed 4)"})
   .strs("recovered", {"hashKey", "patternFrame"})
   .num("fps", fps).num("frames", frames)
   .num("gateMean", mean).num("gateVariance", var)
   .num("gateMin", *std::min_element(perFrameGate.begin(), perFrameGate.end()))
   .num("gateMax", *std::max_element(perFrameGate.begin(), perFrameGate.end()))
   .num("patternSteps", double(stepTimes.size()))
   .arr("patternStepTimes", stepTimes)
   .str("note", "patternFrame multiplies in DOUBLE and truncates only the product, and its guard "
                "is `> 0` so t = 0 yields frame 0. Both are visible in the staircase.");
  return j;
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
  if (argc > 1) gOutDir = argv[1];

  std::string mk = "mkdir -p '" + gOutDir + "'";
  if (std::system(mk.c_str()) != 0) std::fprintf(stderr, "warning: mkdir failed\n");

  J demos;
  demos.raw("noisefield", demoNoiseField());
  demos.raw("chromakey", demoChromaKey());
  demos.raw("lutblend", demoLutBlend());
  demos.raw("spectra", demoSpectra());
  demos.raw("echo", demoEchoTrails());
  demos.raw("colorpulse", demoColorPulse());
  demos.raw("speedramp", demoSpeedRamp());
  demos.raw("glitch", demoGlitch());

  J root;
  root.str("harness", "tools/demo/harness.cpp")
      .str("kind", "computed-from-recovered-source")
      .str("disclaimer",
           "Every output below was produced by executing the recovered source in src/ on "
           "synthetic input authored by the harness. No After Effects host has loaded a "
           "rebuilt bundle, so none of this is an AE render.")
      .raw("demos", demos);

  const std::string path = gOutDir + "/results.json";
  std::ofstream f(path);
  if (!f) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return 1; }
  f << root.dump() << "\n";
  std::printf("wrote %s\n", path.c_str());
  return 0;
}
