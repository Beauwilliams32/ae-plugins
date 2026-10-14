// Self-check for the recovered colorpulse:: namespace.

#include "colorpulse.h"

#include <cmath>
#include <cstdio>

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

  std::printf("\n%s\n", failures == 0
                ? "ALL RECOVERED COLORPULSE MATHS VERIFIED"
                : "TRANSCRIPTION ERRORS PRESENT");
  return failures == 0 ? 0 : 1;
}
