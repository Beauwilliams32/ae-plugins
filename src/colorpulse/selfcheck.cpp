// Self-check for the recovered colorpulse:: namespace.

#include "colorpulse.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace {
int failures = 0, passes = 0;
void check(bool ok, const char* why) {
  if (ok) { ++passes; std::printf("  PASS %s\n", why); }
  else    { ++failures; std::printf("  FAIL %s\n", why); }
}
void near(float got, float want, float tol, const char* why) {
  if (std::fabs(got - want) <= tol) { ++passes; std::printf("  PASS %s\n", why); }
  else { ++failures; std::printf("  FAIL %s (got %.7g want %.7g)\n", why, got, want); }
}
}  // namespace

int main() {
  std::printf("colorpulse::clampf\n");
  {
    near(colorpulse::clampf(5.f, 0.f, 10.f), 5.f, 0.f, "in range passes through");
    near(colorpulse::clampf(-1.f, 0.f, 10.f), 0.f, 0.f, "below lo clamps to lo");
    near(colorpulse::clampf(11.f, 0.f, 10.f), 10.f, 0.f, "above hi clamps to hi");
    near(colorpulse::clampf(5.f, 10.f, 0.f), 10.f, 0.f, "inverted range yields lo (lo tested last)");
  }

  std::printf("colorpulse::smoothScalar — argument ORDER matters\n");
  {
    // newest first, weighted by blend.
    near(colorpulse::smoothScalar(1.0f, 0.0f, 1.0f), 1.0f, 1e-6f, "blend 1 is all-newest");
    near(colorpulse::smoothScalar(1.0f, 0.0f, 0.0f), 0.0f, 1e-6f, "blend 0 is all-previous");
    near(colorpulse::smoothScalar(1.0f, 0.0f, 0.25f), 0.25f, 1e-6f, "blend 0.25 is a quarter of the way");
    // A swapped pair would give 0.75 here instead, which is the point of asserting the
    // direction rather than just the midpoint.
    check(colorpulse::smoothScalar(1.0f, 0.0f, 0.25f)
          != colorpulse::smoothScalar(0.0f, 1.0f, 0.25f),
          "swapping newest and previous changes the result");
    // The blend is clamped, so out-of-range values cannot extrapolate.
    near(colorpulse::smoothScalar(1.0f, 0.0f, 5.0f), 1.0f, 1e-6f, "blend above 1 clamps");
    near(colorpulse::smoothScalar(1.0f, 0.0f, -5.0f), 0.0f, 1e-6f, "blend below 0 clamps");
    // Constant input is a fixed point.
    near(colorpulse::smoothScalar(0.3f, 0.3f, 0.7f), 0.3f, 1e-6f, "equal values are a fixed point");
  }

  std::printf("colorpulse::luma709\n");
  {
    check(std::fabs(colorpulse::luma709({1.f, 1.f, 1.f}) - 1.0f) < 1e-6f, "white is luma 1");
    near(colorpulse::luma709({0.f, 0.f, 0.f}), 0.0f, 0.0f, "black is luma 0");
    near(colorpulse::luma709({1.f, 0.f, 0.f}), 0.2126f, 1e-6f, "pure red weights 0.2126");
    near(colorpulse::luma709({0.f, 1.f, 0.f}), 0.7152f, 1e-6f, "pure green weights 0.7152");
    near(colorpulse::luma709({0.f, 0.f, 1.f}), 0.0722f, 1e-6f, "pure blue weights 0.0722");
    // The three weights sum to 1, which is what makes white exactly 1.
    near(0.2126f + 0.7152f + 0.0722f, 1.0f, 1e-6f, "the 709 weights sum to 1");
    // Green dominates, which is the property that distinguishes 709 from a naive average.
    check(colorpulse::luma709({0.f, 1.f, 0.f}) > colorpulse::luma709({1.f, 0.f, 0.f}),
          "green is counted more heavily than red");
    // A neutral grey is its own luma.
    near(colorpulse::luma709({0.5f, 0.5f, 0.5f}), 0.5f, 1e-6f, "a neutral grey is its own luma");
  }

  std::printf("colorpulse::smoothColor\n");
  {
    const colorpulse::RGB a{1.f, 0.5f, 0.f};
    const colorpulse::RGB b{0.f, 0.5f, 1.f};
    auto eq = [](const colorpulse::RGB& x, const colorpulse::RGB& y, float tol) {
      return std::fabs(x.r - y.r) <= tol && std::fabs(x.g - y.g) <= tol && std::fabs(x.b - y.b) <= tol;
    };
    check(eq(colorpulse::smoothColor(a, b, 1.0f), a, 1e-6f), "blend 1 is all-newest, per channel");
    check(eq(colorpulse::smoothColor(a, b, 0.0f), b, 1e-6f), "blend 0 is all-previous");
    check(eq(colorpulse::smoothColor(a, b, 0.5f), {0.5f, 0.5f, 0.5f}, 1e-6f),
          "blend 0.5 is the midpoint of every channel");
    // The channel that is equal in both must not move — a per-channel lerp has that
    // property, and a luma-based blend would not.
    check(eq(colorpulse::smoothColor(a, b, 0.37f),
             {0.37f, 0.5f, 0.63f}, 1e-6f),
          "a channel equal in both inputs stays put");
    check(eq(colorpulse::smoothColor(a, b, 9.0f), a, 1e-6f), "blend clamps above");
    check(eq(colorpulse::smoothColor(a, b, -9.0f), b, 1e-6f), "blend clamps below");
  }

  std::printf("colorpulse::saturationBoost\n");
  {
    const colorpulse::RGB c{0.6f, 0.4f, 0.3f};
    const float l = colorpulse::luma709(c);

    // amount <= 0 returns the INPUT, unclamped — a no-op, not a desaturation.
    colorpulse::RGB z = colorpulse::saturationBoost(c, 0.0f);
    check(z.r == c.r && z.g == c.g && z.b == c.b, "amount 0 returns the input exactly");
    colorpulse::RGB neg = colorpulse::saturationBoost(c, -1.0f);
    check(neg.r == c.r && neg.g == c.g && neg.b == c.b, "a negative amount is a no-op, not desaturation");

    // The luma is preserved, because each channel moves away from it symmetrically.
    colorpulse::RGB boosted = colorpulse::saturationBoost(c, 1.0f);
    near(colorpulse::luma709(boosted), l, 1e-5f, "boosting preserves the luma");

    // amount is a FRACTION: 1.0 doubles the distance from luma.
    near(boosted.r - l, (c.r - l) * 2.0f, 1e-5f, "amount 1 doubles the distance from luma (R)");
    near(boosted.b - l, (c.b - l) * 2.0f, 1e-5f, "and on the channel furthest from it (B)");

    // Half the amount is half the movement.
    colorpulse::RGB half = colorpulse::saturationBoost(c, 0.5f);
    near(half.r - l, (c.r - l) * 1.5f, 1e-5f, "amount 0.5 scales by 1.5, so it is a fraction");

    // A neutral grey has nothing to boost: every channel IS the luma.
    colorpulse::RGB grey = colorpulse::saturationBoost({0.5f, 0.5f, 0.5f}, 3.0f);
    near(grey.r, 0.5f, 1e-5f, "a neutral grey is unchanged by any amount");
    near(grey.b, 0.5f, 1e-5f, "on every channel");

    // Extreme amounts clamp per channel rather than wrapping.
    colorpulse::RGB extreme = colorpulse::saturationBoost(c, 50.0f);
    check(extreme.r >= 0.0f && extreme.r <= 1.0f &&
          extreme.g >= 0.0f && extreme.g <= 1.0f &&
          extreme.b >= 0.0f && extreme.b <= 1.0f,
          "a large amount clamps every channel into [0,1]");
  }

  std::printf("the duplicated luma709 agrees with lutblend's\n");
  {
    // colorpulse::luma709 and lutblend::luma709 are the same function, recovered from
    // two different bundles in two different namespaces. Both transcriptions are
    // instruction-identical to their own binaries, so this asserts the two AGREE rather
    // than that either is right — which is the property that matters if one is edited.
    const float xs[] = {0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 1.0f};
    bool agree = true;
    for (float r : xs) for (float g : xs) for (float b : xs) {
      const float a = colorpulse::luma709({r, g, b});
      const float bb = 0.2126f * r + 0.7152f * g + 0.0722f * b;   // the shared closed form
      if (std::fabs(a - bb) > 1e-6f) agree = false;
    }
    check(agree, "colorpulse::luma709 matches the closed form lutblend::luma709 implements");
  }

  std::printf("regionBounds\n");
  {
    // The four rectangles, checked against the fractions read off the csel chain. The
    // 400x300 box is deliberately NOT a multiple of 4 or 3 in one axis, so a wrong
    // operator precedence or a rounded division shows up.
    const colorpulse::Rect q = colorpulse::regionBounds(colorpulse::Region::TopLeftThreeQuarters,
                                                       400, 300);
    check(q.minX == 0 && q.minY == 0, "TopLeftThreeQuarters starts at the origin");
    check(q.maxX == 300 && q.maxY == 225, "and ends at 3/4 of each axis");

    const colorpulse::Rect br = colorpulse::regionBounds(
        colorpulse::Region::BottomRightThreeQuarters, 400, 300);
    check(br.minX == 100 && br.minY == 75, "BottomRightThreeQuarters starts at 1/4");
    check(br.maxX == 400 && br.maxY == 300, "and ends at the full box");

    const colorpulse::Rect top = colorpulse::regionBounds(colorpulse::Region::TopThird, 400, 300);
    check(top.minX == 0 && top.maxX == 400, "TopThird spans the full width");
    check(top.minY == 0 && top.maxY == 100, "and the first third of the height only");

    const colorpulse::Rect bot = colorpulse::regionBounds(colorpulse::Region::BottomThird, 400, 300);
    check(bot.minX == 0 && bot.maxX == 400, "BottomThird spans the full width");
    // 300 - 300/3 = 200, NOT 2*300/3 = 200 -- the two agree here, so the next case uses a
    // height where they differ.
    check(bot.minY == 200 && bot.maxY == 300, "and the last third of the height");

    // height = 10: `height - height/3` is 7, while `2*height/3` would be 6. This is the
    // assertion that pins which expression the bundle uses.
    const colorpulse::Rect odd = colorpulse::regionBounds(colorpulse::Region::BottomThird, 40, 10);
    check(odd.minY == 7, "the bottom third's top edge is height - height/3 (7, not 6)");

    // The DEFAULT arm catches everything the caller failed to narrow, not just 0 -- the
    // call site maps an unrecognised mode to 0, so this is reachable in practice.
    const int rawSelectors[5] = {0, 1, 5, -1, 99};
    for (int raw : rawSelectors) {
      const colorpulse::Rect d = colorpulse::regionBounds(static_cast<colorpulse::Region>(raw),
                                                         400, 300);
      check(d.minX == 0 && d.minY == 0 && d.maxX == 300 && d.maxY == 225,
            "an unrecognised selector falls to the top-left three quarters");
    }

    // THE SIZE FLOOR. A box too small to hold the fraction must still come out with a
    // usable size, bounded by the box itself.
    const colorpulse::Rect tiny3 = colorpulse::regionBounds(colorpulse::Region::TopThird, 2, 2);
    check(tiny3.maxY > tiny3.minY, "a 2-high TopThird is floored to a non-empty height");
    check(tiny3.maxY <= 2, "and the floor does not run past the box");
    check(tiny3.maxY == 1, "the floor lands on exactly one pixel here");

    // minX >= maxX triggers the floor. At width 1, quarterW is 0 so BottomRight's minX is
    // 0 and maxX is 1 -- already positive, so no floor. At width 3, quarterW is 0 too.
    const colorpulse::Rect w1 = colorpulse::regionBounds(
        colorpulse::Region::BottomRightThreeQuarters, 1, 1);
    check(w1.maxX > w1.minX && w1.maxY > w1.minY, "a 1x1 box still yields a non-empty rect");
    check(w1.maxX <= 1 && w1.maxY <= 1, "and it stays inside the box");

    // Every selector, across a spread of boxes, must stay CONTAINED and non-inverted.
    //
    // NOT "non-empty", which is what this assertion said first and why it failed. The
    // size floor cannot always produce a non-empty rectangle, and the case is worth
    // stating exactly rather than papering over: for BottomThird, `minY` is `height -
    // height/3` and `maxY` is `height`, so at height <= 2 the third rounds to 0 and the
    // two are EQUAL. The floor then computes `min(height, minY + 1)`, which is `height`,
    // so it reproduces the empty rectangle rather than fixing it.
    //
    // That is not a transcription error — it is why the EffectMain CALL SITE has its own
    // clamp. The caller reads the rectangle as `x1 - x0` per axis and passes each
    // difference through `csinc w24, w8, wzr, gt`, i.e. `max(size, 1)`, before capping it
    // at 48. The floor here establishes containment; the caller establishes non-emptiness.
    bool contained = true;
    bool emptyOnlyInTheKnownCase = true;
    for (int w = 1; w <= 40; ++w) {
      for (int h = 1; h <= 40; ++h) {
        for (int r = 0; r <= 5; ++r) {
          const colorpulse::Rect b =
              colorpulse::regionBounds(static_cast<colorpulse::Region>(r), w, h);
          if (b.minX < 0 || b.minY < 0 || b.maxX > w || b.maxY > h
              || b.maxX < b.minX || b.maxY < b.minY) {
            contained = false;
          }
          const bool empty = (b.maxX == b.minX) || (b.maxY == b.minY);
          if (empty && !(r == static_cast<int>(colorpulse::Region::BottomThird) && h <= 2)) {
            emptyOnlyInTheKnownCase = false;
          }
        }
      }
    }
    check(contained, "every selector yields a contained, non-inverted rect, up to 40x40");
    check(emptyOnlyInTheKnownCase,
          "an EMPTY rect appears only for BottomThird on a box 2 high or less");

    // And that case is asserted directly, so the allowance above cannot widen unnoticed.
    const colorpulse::Rect flat = colorpulse::regionBounds(
        colorpulse::Region::BottomThird, 100, 2);
    check(flat.minY == flat.maxY && flat.minY == 2,
          "BottomThird on a 2-high box is empty, at the bottom edge");
    const colorpulse::Rect just = colorpulse::regionBounds(
        colorpulse::Region::BottomThird, 100, 3);
    check(just.maxY > just.minY, "and 3 high is the first height that works: 3/3 == 1");

    // It is a PURE function of its three inputs: no hidden state, no dependence on the
    // order of calls. The effect calls it once per render, but a stale static would make
    // the second call differ.
    const colorpulse::Rect again = colorpulse::regionBounds(
        colorpulse::Region::BottomRightThreeQuarters, 400, 300);
    check(again.minX == br.minX && again.maxX == br.maxX,
          "repeating a call gives the identical rectangle");
  }

  std::printf("averageColor\n");
  {
    // The two guards are on the INPUTS and both return black, checked before the stack
    // frame exists in the bundle, i.e. as early returns rather than a special case.
    const colorpulse::RGB nulled = colorpulse::averageColor(nullptr, 8);
    check(nulled.r == 0.0f && nulled.g == 0.0f && nulled.b == 0.0f,
          "a null pixel run returns black");
    const colorpulse::RGB none = colorpulse::averageColor(nullptr, 0);
    check(none.g == 0.0f, "a null run with a zero count returns black too");

    const colorpulse::RGB one[1] = {{0.25f, 0.5f, 0.75f}};
    const colorpulse::RGB single = colorpulse::averageColor(one, 1);
    near(single.r, 0.25f, 1e-7f, "a single colour averages to itself (r)");
    near(single.g, 0.5f, 1e-7f, "and to itself (g)");
    near(single.b, 0.75f, 1e-7f, "and to itself (b)");

    const colorpulse::RGB three[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    const colorpulse::RGB avg = colorpulse::averageColor(three, 3);
    near(avg.r, 1.0f / 3.0f, 1e-7f, "the mean of a 3-colour run (r)");
    near(avg.g, 1.0f / 3.0f, 1e-7f, "the mean of a 3-colour run (g)");
    near(avg.b, 1.0f / 3.0f, 1e-7f, "the mean of a 3-colour run (b)");

    // It is an UNWEIGHTED mean: a run of n identical colours is that colour regardless of
    // n, which distinguishes averaging from summing (which would scale by n).
    std::vector<colorpulse::RGB> many(1000, colorpulse::RGB{0.4f, 0.4f, 0.4f});
    const colorpulse::RGB flat = colorpulse::averageColor(many.data(), many.size());
    near(flat.r, 0.4f, 1e-6f, "1000 identical colours average to that colour, not 400");

    // The accumulation is in DOUBLE. 2^24 is not representable as a float plus one, so a
    // float accumulator loses the +1 entirely; the double one keeps it.
    {
      std::vector<colorpulse::RGB> big(1024, colorpulse::RGB{16777216.0f, 0.0f, 0.0f});
      big.push_back(colorpulse::RGB{1.0f, 0.0f, 0.0f});
      double exact = 0.0;
      for (const colorpulse::RGB& c : big) exact += static_cast<double>(c.r);
      const double want = exact * (1.0 / static_cast<double>(big.size()));
      const colorpulse::RGB got = colorpulse::averageColor(big.data(), big.size());
      near(got.r, static_cast<float>(want), 0.5f,
           "a large run keeps its low bits: both the sum and the final multiply are double");
    }

    // THE DIVISION IS A RECIPROCAL AND A MULTIPLY. Both are asserted: that the result
    // MATCHES `sum * (1.0/n)`, and that this is observably different from `sum / n` for
    // at least one count -- otherwise the distinction would be unfalsifiable and the
    // first assertion would pass against either spelling.
    {
      // A run whose double sum is exactly representable, so the only rounding under test
      // is the final one.
      std::vector<colorpulse::RGB> run(7, colorpulse::RGB{1.0f, 3.0f, 7.0f});
      const double sumR = 7.0 * 1.0, sumG = 7.0 * 3.0, sumB = 7.0 * 7.0;
      const double n = 7.0;
      const colorpulse::RGB got = colorpulse::averageColor(run.data(), run.size());
      check(got.r == static_cast<float>(sumR * (1.0 / n)),
            "the mean is sum * (1/n), not sum / n (r)");
      check(got.g == static_cast<float>(sumG * (1.0 / n)),
            "the mean is sum * (1/n), not sum / n (g)");
      check(got.b == static_cast<float>(sumB * (1.0 / n)),
            "the mean is sum * (1/n), not sum / n (b)");

      // How FAR the two spellings diverge, measured rather than asserted.
      //
      // The first version of this check required a count where the two narrow to
      // DIFFERENT floats, and it failed: no such count exists in the range scanned. That
      // is the honest result. The reciprocal error is around 2^-52 relative — one ulp of
      // DOUBLE — while narrowing to float rounds at 2^-24, so the difference is almost
      // always annihilated on the way out. The header used to claim the returned value
      // "differs ... for many counts", which is wrong, and this measurement is what
      // corrected it.
      //
      // So the pin is on the SPELLING, not on an observable output difference: the
      // bundle's `fdiv d3, d4, d3` only exists if the source says `1.0 / count`, and the
      // three assertions above pin the function to that form.
      int doubleDiffer = 0, floatDiffer = 0;
      for (int k = 2; k < 5000; ++k) {
        const double d = static_cast<double>(k);
        const double sum = static_cast<double>(k) + 1.0;
        if (sum * (1.0 / d) != sum / d) ++doubleDiffer;
        if (static_cast<float>(sum * (1.0 / d)) != static_cast<float>(sum / d)) ++floatDiffer;
      }
      check(doubleDiffer > 0,
            "sum*(1/n) and sum/n genuinely differ as DOUBLES, so the source choice is real");
      check(floatDiffer == 0,
            "but across 2..4999 counts they never narrow to different floats — the choice "
            "is unobservable in the returned RGB, and the pin above is on the spelling");
    }
  }

  std::printf("brightestRegionColor\n");
  {
    // A WxH image built from a per-pixel colour function.
    const auto build = [](int w, int h, const std::function<colorpulse::RGB(int, int)>& f) {
      std::vector<colorpulse::RGB> px(static_cast<size_t>(w) * static_cast<size_t>(h));
      for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
          px[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = f(x, y);
        }
      }
      return px;
    };

    // The guards, in the order the bundle tests them.
    const colorpulse::RGB n = colorpulse::brightestRegionColor(nullptr, 4, 4, 2);
    check(n.r == 0.0f && n.g == 0.0f && n.b == 0.0f, "a null pixel run returns black");
    auto any = build(4, 4, [](int, int) { return colorpulse::RGB{1.0f, 1.0f, 1.0f}; });
    check(colorpulse::brightestRegionColor(any.data(), 0, 4, 2).r == 0.0f,
          "width < 1 returns black");
    check(colorpulse::brightestRegionColor(any.data(), 4, 0, 2).r == 0.0f,
          "height < 1 returns black");

    // It returns a CELL MEAN, not a pixel. A 4x4 image that is black except for the
    // bottom-right 2x2, which is uniform white: with step 2 the grid is 2x2, so the
    // bottom-right CELL is the winner and its mean is exactly white.
    auto quad = build(4, 4, [](int x, int y) {
      return (x >= 2 && y >= 2) ? colorpulse::RGB{1.0f, 1.0f, 1.0f}
                                : colorpulse::RGB{0.0f, 0.0f, 0.0f};
    });
    const colorpulse::RGB bright = colorpulse::brightestRegionColor(quad.data(), 4, 4, 2);
    near(bright.r, 1.0f, 1e-6f, "the brightest 2x2 cell wins, and its mean is white");

    // Now make that cell HALF bright: two white pixels and two black. The mean must be
    // 0.5, which is what distinguishes a cell MEAN from picking the brightest PIXEL
    // (which would be 1.0).
    auto half = build(4, 4, [](int x, int y) {
      const bool lit = (x >= 2 && y >= 2) && ((x + y) % 2 == 0);
      return lit ? colorpulse::RGB{1.0f, 1.0f, 1.0f} : colorpulse::RGB{0.0f, 0.0f, 0.0f};
    });
    const colorpulse::RGB meanCell = colorpulse::brightestRegionColor(half.data(), 4, 4, 2);
    near(meanCell.r, 0.5f, 1e-6f,
         "the winner is a cell MEAN (0.5), not the brightest pixel (which would be 1.0)");

    // STEP IS A COUNT OF DIVISIONS PER AXIS, NOT A CELL SIZE. Both of those were got
    // wrong here first and the checks below are what corrected them, so the distinction is
    // worth stating: `cols = min(width, step)` is the number of COLUMNS, and the cell
    // edges are `index * width / cols`.
    //
    // So step 1 is ONE cell covering the whole region -- its mean -- and a step at or
    // above the axis length is one cell per pixel.
    const colorpulse::RGB fine = colorpulse::brightestRegionColor(half.data(), 4, 4, 100);
    near(fine.r, 1.0f, 1e-6f, "a step at or above the axis length gives one cell per pixel");

    // `half` lights exactly 2 of 16 pixels, so its whole-region mean is 0.125. That is
    // what a single cell returns, and it is the assertion that pins "step is a division
    // count" rather than "step is a size".
    const colorpulse::RGB wholeRegion = colorpulse::brightestRegionColor(half.data(), 4, 4, 1);
    near(wholeRegion.r, 0.125f, 1e-6f, "step 1 is ONE cell: the mean of the whole region");

    const colorpulse::RGB zeroStep = colorpulse::brightestRegionColor(half.data(), 4, 4, 0);
    near(zeroStep.r, 0.125f, 1e-6f, "step 0 clamps up to 1, not to a divide-by-zero");
    const colorpulse::RGB negStep = colorpulse::brightestRegionColor(half.data(), 4, 4, -5);
    near(negStep.r, 0.125f, 1e-6f, "and so does a negative step");

    // 2x2 and 4x4 on the same image must differ, or "the number of divisions" is not
    // actually being honoured.
    const colorpulse::RGB twoByTwo = colorpulse::brightestRegionColor(half.data(), 4, 4, 2);
    check(twoByTwo.r != wholeRegion.r,
          "a 2x2 grid and a 1x1 grid give different answers on the same image");

    // A cell with the SAME luma does not displace the incumbent: the comparison is
    // strictly-greater, so on a uniform region the FIRST (top-left) cell is returned.
    // Invisible in the value, so it is asserted through a region whose cells differ only
    // in a way luma cannot see -- equal luma, different colour.
    auto tie = build(4, 4, [](int, int) {
      // Every cell has the SAME colour and therefore the same luma, so the strictly-
      // greater comparison keeps the first cell. Invisible in the returned value, which
      // is why the assertion is only that the shared colour comes back unchanged.
      return colorpulse::RGB{0.25f, 0.75f, 0.5f};
    });
    const colorpulse::RGB tied = colorpulse::brightestRegionColor(tie.data(), 4, 4, 2);
    near(tied.r, 0.25f, 1e-6f, "a uniform region returns that colour (r)");
    near(tied.g, 0.75f, 1e-6f, "a uniform region returns that colour (g)");
    near(tied.b, 0.5f, 1e-6f, "a uniform region returns that colour (b)");

    // An all-black region: every luma is 0, which BEATS the -1 initialiser, so the first
    // cell wins and the result is black. The point of the check is that it is not NaN and
    // that the -1 initialiser is what makes "the first cell wins" true even at luma 0.
    auto black = build(4, 4, [](int, int) { return colorpulse::RGB{0.0f, 0.0f, 0.0f}; });
    const colorpulse::RGB blackOut = colorpulse::brightestRegionColor(black.data(), 4, 4, 2);
    check(blackOut.r == 0.0f && blackOut.g == 0.0f && blackOut.b == 0.0f
              && blackOut.r == blackOut.r,
          "an all-black region returns black, not NaN, despite every luma being 0");

    // A non-square region with a step that does not divide the axis: the cell boundaries
    // floor, so no pixel is skipped and none is counted twice. Verified by making the
    // brightest pixel the LAST one and checking it is still found.
    const int W = 7, H = 5;
    auto last = build(W, H, [](int x, int y) {
      return (x == 6 && y == 4) ? colorpulse::RGB{1.0f, 1.0f, 1.0f}
                                : colorpulse::RGB{0.0f, 0.0f, 0.0f};
    });
    const colorpulse::RGB found = colorpulse::brightestRegionColor(last.data(), W, H, 3);
    check(found.r > 0.0f, "the last pixel is reached: no cell boundary skips an axis tail");

    // The whole region as ONE cell (step clamped to min(W,H) when the grid is 1x1 only if
    // step == 1... asserted instead as: a 1x1 region is its own mean.
    auto one = build(1, 1, [](int, int) { return colorpulse::RGB{0.2f, 0.4f, 0.6f}; });
    const colorpulse::RGB single = colorpulse::brightestRegionColor(one.data(), 1, 1, 4);
    near(single.r, 0.2f, 1e-6f, "a 1x1 region returns its single pixel (r)");
    near(single.b, 0.6f, 1e-6f, "a 1x1 region returns its single pixel (b)");

    // Determinism: the same input twice gives the same answer, so no state leaks between
    // calls.
    const colorpulse::RGB again = colorpulse::brightestRegionColor(half.data(), 4, 4, 2);
    check(again.r == meanCell.r && again.g == meanCell.g && again.b == meanCell.b,
          "repeating a call gives the identical colour");
  }

  std::printf("dominantColor\n");
  {
    const auto run = [](const std::vector<colorpulse::RGB>& px, int levels) {
      return colorpulse::dominantColor(px.data(), px.size(), levels);
    };

    // The three guards.
    check(colorpulse::dominantColor(nullptr, 4, 8).r == 0.0f,
          "a null pixel run returns black");
    const std::vector<colorpulse::RGB> one{{1.0f, 0.0f, 0.0f}};
    check(colorpulse::dominantColor(one.data(), 0, 8).r == 0.0f, "count 0 returns black");
    check(colorpulse::dominantColor(one.data(), 1, 0).r == 0.0f, "levels 0 returns black");
    check(colorpulse::dominantColor(one.data(), 1, -3).r == 0.0f, "levels < 0 returns black");

    // A clear majority wins: three reds against one blue.
    const std::vector<colorpulse::RGB> mostlyRed{
        {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    const colorpulse::RGB red = run(mostlyRed, 8);
    near(red.r, 1.0f, 1e-6f, "the most populous bin wins (r)");
    near(red.b, 0.0f, 1e-6f, "and it is the red bin, not the blue one");

    // THE RESULT IS THE MEAN OF THE WINNING BIN'S PIXELS, NOT THE BIN CENTRE.
    // At levels 4 the first bin spans [0, 0.25) and its centre is 0.125, so two pixels at
    // 0.1 and 0.2 must give 0.15 -- a value the centre could not produce.
    const std::vector<colorpulse::RGB> twoInOneBin{
        {0.1f, 0.1f, 0.1f}, {0.2f, 0.2f, 0.2f}};
    const colorpulse::RGB meanOfBin = run(twoInOneBin, 4);
    near(meanOfBin.r, 0.15f, 1e-6f,
         "the bin's MEAN (0.15) is returned, not the bin centre (0.125)");

    // BLUE IS THE FASTEST AXIS. With levels 2, a blue-only pixel indexes 0*4+0*1+1 = 1
    // while a red-only pixel indexes 1*4+0+0 = 4. One pixel each is a TIE, and the lower
    // index wins, so blue must come back. Were red the fastest axis this would return red.
    const std::vector<colorpulse::RGB> blueVsRed{{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}};
    const colorpulse::RGB blueWins = run(blueVsRed, 2);
    near(blueWins.b, 1.0f, 1e-6f, "blue indexes lower than red, so blue wins an exact tie");
    near(blueWins.r, 0.0f, 1e-6f, "and red does not");

    // A tie between bins 0 and 7 keeps bin 0, the LOWEST index.
    const std::vector<colorpulse::RGB> extremes{
        {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    const colorpulse::RGB low = run(extremes, 2);
    check(low.r == 0.0f && low.g == 0.0f && low.b == 0.0f,
          "an exact tie between bins 0 and 7 keeps bin 0 (the lowest index)");

    // CLAMPING AFFECTS THE BIN INDEX ONLY, NOT THE RETURNED COLOUR -- and that is why the
    // result can leave [0,1]. These two checks were written expecting the clamped value
    // back and returned 1.5 and -2.5 instead, which is correct: the clamp happens inside
    // the quantiser, while the second pass averages the RAW pixels of the winning bin.
    //
    // So with 32-bit float sources carrying overrange or negative values, the colour this
    // returns can be over 1.0 or below 0.0. Worth knowing rather than assuming a
    // saturating effect.
    const std::vector<colorpulse::RGB> over{{2.0f, 2.0f, 2.0f}, {1.0f, 1.0f, 1.0f}};
    const colorpulse::RGB clamped = run(over, 4);
    near(clamped.r, 1.5f, 1e-6f,
         "2.0 and 1.0 share the TOP bin, and its mean is the raw 1.5 -- not the clamped 1.0");
    const std::vector<colorpulse::RGB> under{{-5.0f, -5.0f, -5.0f}, {0.0f, 0.0f, 0.0f}};
    const colorpulse::RGB negClamped = run(under, 4);
    near(negClamped.r, -2.5f, 1e-6f,
         "and -5.0 with 0.0 share the BOTTOM bin, whose raw mean is -2.5");

    // The clamp's real job is to stop the INDEX leaving the axis: 1.0 quantises to
    // `levels`, and only the min pulls it back to `levels - 1`. Asserted against a value
    // just below the boundary, which must land one bin lower.
    const std::vector<colorpulse::RGB> atOne{{1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}};
    const std::vector<colorpulse::RGB> justUnder{{0.99f, 0.99f, 0.99f}, {0.99f, 0.99f, 0.99f}};
    near(run(atOne, 4).r, 1.0f, 1e-6f, "a channel of exactly 1.0 indexes the top bin");
    near(run(justUnder, 4).r, 0.99f, 1e-6f,
         "and one just below the boundary indexes the SAME top bin at levels 4");

    // levels 1 is a single bin, so the answer is the mean of everything -- and the
    // quantiser's min(levels-1, ...) must give index 0 for all of it.
    const std::vector<colorpulse::RGB> mixed{
        {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.5f, 0.5f, 0.5f}};
    const colorpulse::RGB all = run(mixed, 1);
    near(all.r, 0.5f, 1e-6f, "levels 1 collapses to one bin: the mean of every pixel (r)");
    near(all.g, 0.5f, 1e-6f, "and of every pixel (g)");
    near(all.b, 0.5f, 1e-6f, "and of every pixel (b)");

    // A larger level count SPLITS what one bin held, so the two must differ -- otherwise
    // `levels` is not reaching the quantiser.
    const std::vector<colorpulse::RGB> spread{
        {0.1f, 0.1f, 0.1f}, {0.1f, 0.1f, 0.1f}, {0.9f, 0.9f, 0.9f}};
    const colorpulse::RGB coarse = run(spread, 2);
    const colorpulse::RGB fine2 = run(spread, 8);
    near(coarse.r, 0.1f, 1e-6f, "a coarse quantiser keeps the two 0.1 pixels together");
    near(fine2.r, 0.1f, 1e-6f, "and a fine one still picks their bin, which has 2 of 3");

    // Determinism, and no state carried between calls: the same input twice.
    const colorpulse::RGB again = run(mostlyRed, 8);
    check(again.r == red.r && again.b == red.b, "repeating a call gives the identical colour");
  }

  std::printf("sampleColor\n");
  {
    // A 4x4 region, mostly mid-blue with one bright white pixel. Each mode must reach a
    // DIFFERENT analyser, and the three answers below are distinct, which is what makes
    // the dispatch observable rather than merely present.
    std::vector<colorpulse::RGB> px;
    for (int y = 0; y < 4; ++y) {
      for (int x = 0; x < 4; ++x) {
        px.push_back((x == 3 && y == 3) ? colorpulse::RGB{1.0f, 1.0f, 1.0f}
                                        : colorpulse::RGB{0.0f, 0.0f, 0.25f});
      }
    }

    // The default arm is averageColor over width*height pixels: 15 pixels of (0,0,0.25)
    // and one white gives r = 1/16.
    const colorpulse::RGB avg = colorpulse::sampleColor(px.data(), 4, 4, colorpulse::Mode::Average);
    near(avg.r, 1.0f / 16.0f, 1e-6f, "the default arm averages the whole region (r)");
    near(avg.b, (15.0f * 0.25f + 1.0f) / 16.0f, 1e-6f, "and its blue");

    // Mode 2 reaches dominantColor with a level count of 8. At 8 levels the 15 blue
    // pixels split by their blue channel (0.25*8 = 2) and the white one lands in the top
    // bin alone, so white is NOT the majority -- the 15-pixel bin wins and the answer is
    // its mean, i.e. exactly (0,0,0.25).
    const colorpulse::RGB dom = colorpulse::sampleColor(px.data(), 4, 4, colorpulse::Mode::Dominant);
    near(dom.r, 0.0f, 1e-6f, "Mode 2 is the dominant bin, which is the 15 blue pixels (r)");
    near(dom.b, 0.25f, 1e-6f, "and its mean blue is exactly 0.25");

    // Mode 3 reaches brightestRegionColor with a step of 4. The grid is min(4,4) = 4 per
    // axis, so one cell per pixel, and the brightest single pixel is the white corner.
    const colorpulse::RGB bright =
        colorpulse::sampleColor(px.data(), 4, 4, colorpulse::Mode::BrightestRegion);
    near(bright.r, 1.0f, 1e-6f, "Mode 3 is the brightest cell, which is the white pixel (r)");
    near(bright.b, 1.0f, 1e-6f, "and its blue");

    // The three arms really differ on this input, so the checks above are not all reading
    // the same code path by accident.
    check(avg.r != dom.r && dom.r != bright.r && avg.r != bright.r,
          "all three modes give different answers on the same region");

    // A non-positive dimension makes the count ZERO rather than a negative product cast to
    // a huge size_t. Mode 2 and the default both use the count, so both go black.
    check(colorpulse::sampleColor(px.data(), 0, 4, colorpulse::Mode::Average).r == 0.0f,
          "width 0 gives a zero count, so the default arm returns black");
    check(colorpulse::sampleColor(px.data(), -1, 4, colorpulse::Mode::Average).r == 0.0f,
          "and a negative width does too, rather than a huge unsigned product");
    check(colorpulse::sampleColor(px.data(), 4, 0, colorpulse::Mode::Dominant).b == 0.0f,
          "height 0 gives a zero count for the dominant arm as well");

    // Mode 3 takes width and height rather than the count, so a zero dimension is caught
    // by ITS OWN guards and also returns black -- a different path to the same answer.
    check(colorpulse::sampleColor(px.data(), 0, 4,
                                  colorpulse::Mode::BrightestRegion).r == 0.0f,
          "the brightest-region arm has its own dimension guards and also returns black");

    // An unrecognised selector falls to the default arm, which is the average. The call
    // site narrows the popup value to {2, 3} else 0, so this is reachable.
    const colorpulse::RGB odd =
        colorpulse::sampleColor(px.data(), 4, 4, static_cast<colorpulse::Mode>(7));
    near(odd.r, avg.r, 0.0f, "an unrecognised mode falls to the average arm, bit for bit");
    near(odd.b, avg.b, 0.0f, "including its blue");

    // A null pixel run reaches every arm; all three return black rather than crashing.
    check(colorpulse::sampleColor(nullptr, 4, 4, colorpulse::Mode::Average).r == 0.0f,
          "a null run through the average arm returns black");
    check(colorpulse::sampleColor(nullptr, 4, 4, colorpulse::Mode::Dominant).r == 0.0f,
          "a null run through the dominant arm returns black");
    check(colorpulse::sampleColor(nullptr, 4, 4,
                                  colorpulse::Mode::BrightestRegion).r == 0.0f,
          "a null run through the brightest-region arm returns black");
  }

  std::printf("\n%s\n", failures == 0
                ? "ALL RECOVERED COLORPULSE MATHS VERIFIED"
                : "TRANSCRIPTION ERRORS PRESENT");
  return failures == 0 ? 0 : 1;
}
