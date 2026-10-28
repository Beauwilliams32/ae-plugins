// Self-check for the recovered shake:: namespace.
//
// The point of most of these is the EVEN case: `shake::median` averages the two middle
// values where `beatsnap::medianOfWindow` returns the upper one. Two functions in one fleet
// with the same name for the same job and opposite answers on an even count is exactly the
// kind of thing a rewrite gets wrong quietly.

#include "shake.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {

int failures = 0;
int passes = 0;

void check(bool ok, const char* why) {
  if (ok) { ++passes; std::printf("  PASS %s\n", why); }
  else    { ++failures; std::printf("  FAIL %s\n", why); }
}

void checkNear(double got, double want, double tol, const char* why) {
  if (std::fabs(got - want) <= tol) { ++passes; std::printf("  PASS %s\n", why); }
  else { ++failures; std::printf("  FAIL %s  (got %.9g want %.9g)\n", why, got, want); }
}

}  // namespace

int main() {
  std::printf("shake::clampf\n");
  {
    check(shake::clampf(5.0f, 0.0f, 10.0f) == 5.0f, "in range passes through");
    check(shake::clampf(-1.0f, 0.0f, 10.0f) == 0.0f, "below lo clamps to lo");
    check(shake::clampf(11.0f, 0.0f, 10.0f) == 10.0f, "above hi clamps to hi");
    check(shake::clampf(5.0f, 10.0f, 0.0f) == 10.0f,
          "an INVERTED range yields lo, because lo is tested last");
  }

  std::printf("shake::median\n");
  {
    check(shake::median({}) == 0.0f, "an empty vector is 0, not a crash");

    // Odd counts take the middle outright.
    checkNear(shake::median({5.0f}), 5.0, 0.0, "one value is itself");
    checkNear(shake::median({3.0f, 1.0f, 2.0f}), 2.0, 0.0, "three unsorted values give 2");
    checkNear(shake::median({9.0f, 1.0f, 5.0f, 3.0f, 7.0f}), 5.0, 0.0, "five give the middle of five");

    // EVEN counts AVERAGE the two middles. This is the check that separates this function
    // from `beatsnap::medianOfWindow`, which would answer 3 here.
    checkNear(shake::median({1.0f, 2.0f, 3.0f, 4.0f}), 2.5, 0.0,
              "four values AVERAGE the middle pair: 2.5, not 3");
    checkNear(shake::median({10.0f, 0.0f}), 5.0, 0.0, "two values average to their midpoint");
    checkNear(shake::median({4.0f, 1.0f, 3.0f, 2.0f}), 2.5, 0.0, "and the input need not be sorted");

    // It reorders its OWN copy, so the caller's vector is untouched — the parameter is by
    // value. A by-reference signature here would silently sort the caller's data.
    std::vector<float> mine{9.0f, 1.0f, 5.0f, 3.0f};
    const float got = shake::median(mine);
    checkNear(got, 4.0, 0.0, "an even count of four averages 3 and 5");
    check(mine[0] == 9.0f && mine[1] == 1.0f && mine[2] == 5.0f && mine[3] == 3.0f,
          "the caller's vector is NOT reordered (the parameter is by value)");

    // Duplicates and negatives are ordinary values.
    checkNear(shake::median({2.0f, 2.0f, 2.0f}), 2.0, 0.0, "all-equal values give that value");
    checkNear(shake::median({-5.0f, -1.0f, -3.0f}), -3.0, 0.0, "negative values sort normally");
    checkNear(shake::median({-2.0f, 2.0f}), 0.0, 0.0, "an even pair straddling zero averages to 0");

    // A LARGE EVEN COUNT IS THE TRUE MEDIAN. This was asserted the other way round until
    // 2026-10-28: the shipped code's second `__nth_element` call was read as covering
    // `[begin + mid - 1, begin + mid)` — ONE ELEMENT, a no-op — so that `values[mid - 1]`
    // was read straight out of the partition, which libc++ leaves in an unspecified order.
    //
    // The listing says otherwise: the call's arguments are
    // (begin, begin + mid - 1, begin + mid), so its RANGE is the lower partition
    // `[begin, begin + mid)` and its nth is that partition's LAST element. It selects the
    // largest of the `mid` smallest — the true lower middle — and leaves it at `mid - 1`.
    //
    // That was measured, not assumed: the shipped bundle's exported `shake::median` was
    // called on 8260 vectors (every length from 0 to 80 with heavy duplicates, 3000 large
    // random ones, and sorted and reversed inputs) and on 8000 more in the differential
    // run, and it ALWAYS returned the exact median. The first transcription's version
    // disagrees with the true median on 10530 of 20000 random even-count vectors.
    std::vector<float> big;
    for (int i = 0; i < 1000; ++i) big.push_back(static_cast<float>((i * 7919) % 1000));
    const float bigMedian = shake::median(big);
    // A permutation of 0..999 has 1000 elements, so the two middle order statistics are
    // 499 and 500 and the even-count median is their average, 499.5.
    checkNear(bigMedian, 499.5, 0.0, "a large even count IS the true median (499.5 here)");
    checkNear(shake::median(std::vector<float>{499.0f, 500.0f}), 499.5, 0.0,
              "and the pair that produces it is the middle pair");

    // ORDER INDEPENDENCE. The median of a multiset must not depend on the order of its
    // inputs, and the shipped bundle's does not — it was measured on 8260 vectors and
    // returned the exact median every time. The FIRST transcription did depend on the
    // order: it disagreed with the true median on 10530 of 20000 random even-count
    // vectors. This is the assertion that pins the correction down, because it is the
    // exact property the first transcription got wrong.
    {
      std::vector<float> shuffled = big;
      std::mt19937 shuffle_rng(20261028);
      std::shuffle(shuffled.begin(), shuffled.end(), shuffle_rng);
      checkNear(shake::median(shuffled), 499.5, 0.0,
                "the median does not depend on the order of the inputs");
      std::reverse(shuffled.begin(), shuffled.end());
      checkNear(shake::median(shuffled), 499.5, 0.0,
                "nor on it being reversed");
    }
  }

  // --- the filter chain (recovered 2026-10-27): movingAverage -> highPass -> extractJitter.
  std::printf("shake::movingAverage\n");
  {
    const std::vector<float> flat(8, 0.25f);
    const std::vector<float> average = shake::movingAverage(flat, 5);
    check(average.size() == flat.size(), "the output is the same length as the input, including at the ends");
    bool allQuarter = true;
    for (float v : average) if (std::fabs(v - 0.25f) > 1e-6f) allQuarter = false;
    check(allQuarter, "a constant series average to that constant everywhere (the divisor is the real count)");

    const std::vector<float> copy = shake::movingAverage(flat, 1);
    check(copy == flat, "a window of 1 is a copy (half = 0)");
    check(shake::movingAverage({}, 5).empty(), "an empty series gives an empty answer");

    // A step: the sample either side of the edge blends, and the far samples do not.
    std::vector<float> step(9, 0.0f);
    for (size_t i = 5; i < step.size(); ++i) step[i] = 1.0f;
    const std::vector<float> smoothed = shake::movingAverage(step, 3);
    checkNear(smoothed[0], 0.0f, 1e-6f, "away from the step the average is untouched");
    checkNear(smoothed[8], 1.0f, 1e-6f, "and so is the far side");
    check(smoothed[4] > 0.0f && smoothed[4] < 1.0f, "at the step it blends");

    // The sum is accumulated in DOUBLE: 2^24 + 1 is not representable in float, so a float
    // accumulator loses the ones and a double one does not.
    std::vector<float> wide{16777216.0f, 1.0f, 1.0f, 1.0f};
    const float mean = shake::movingAverage(wide, 4)[1];
    // 16777216 + 3 in a float accumulator rounds each 1.0 away and lands on 4194304.0 exactly;
    // the double sum is 16777219 / 4 = 4194304.75, whose nearest float is 4194305.
    checkNear(mean, static_cast<float>((16777216.0 + 3.0) / 4.0), 1.0f,
              "the window sum is accumulated in double (a float accumulator would land on 4194304)");
    check(mean != 16777216.0f / 4.0f, "and that is demonstrably not what a float accumulator gives");
  }

  std::printf("shake::highPass\n");
  {
    const std::vector<float> flat(6, 0.75f);
    bool allZero = true;
    for (float v : shake::highPass(flat, 3)) if (std::fabs(v) > 1e-6f) allZero = false;
    check(allZero, "a constant series has no residual");
    check(shake::highPass({}, 3).empty(), "an empty series gives an empty residual");

    std::vector<float> ramp(10);
    for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = static_cast<float>(i);
    const std::vector<float> residual = shake::highPass(ramp, 5);
    check(residual.size() == ramp.size(), "the residual covers every sample");
    // A straight ramp is reproduced exactly by a centred average, so its residual is ~0 in the
    // interior and non-zero only where the window is clipped at the ends.
    check(std::fabs(residual[5]) < 1e-6f, "the interior of a ramp has no residual");
    check(std::fabs(residual[0]) > 0.0f, "the clipped end does");
  }

  std::printf("shake::extractJitter\n");
  {
    shake::ShakeProfile profile;
    profile.horizontal = {0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f};
    profile.vertical = {1.0f, 2.0f, 3.0f, 4.0f};
    const shake::ShakeProfile jitter = shake::extractJitter(profile, 3);
    check(jitter.horizontal.size() == profile.horizontal.size(), "the horizontal track keeps its length");
    check(jitter.vertical.size() == profile.vertical.size(), "and so does the vertical one");
    check(jitter.horizontal == shake::highPass(profile.horizontal, 3), "horizontal is the high-passed track");
    check(jitter.vertical == shake::highPass(profile.vertical, 3), "vertical likewise, in that order");
    checkNear(jitter.horizontal[3], 10.0f - shake::movingAverage(profile.horizontal, 3)[3], 1e-6f,
              "and both are the residual of their own series");
    check(sizeof(shake::ShakeProfile) == 48, "the struct is two vectors, 24 bytes apart");
  }

  // --- accumulate / normalizeUnitAmplitude (recovered 2026-10-27).
  std::printf("shake::accumulate\n");
  {
    const std::vector<shake::Vec2> path{{1.0f, 0.0f}, {0.0f, 2.0f}, {-1.0f, -1.0f}};
    const shake::ShakeProfile track = shake::accumulate(path);
    check(track.horizontal.size() == path.size() + 1, "one more sample than the input (the origin)");
    checkNear(track.horizontal[0], 0.0f, 0.0f, "out[0] is the origin, not the first delta");
    checkNear(track.horizontal[1], 1.0f, 0.0f, "then each delta accumulates");
    checkNear(track.vertical[1], 0.0f, 0.0f, "the other channel accumulates independently");
    checkNear(track.horizontal[3], 0.0f, 0.0f, "and a positive delta can bring the path back");
    checkNear(track.vertical[3], 1.0f, 0.0f, "on both channels");
    const shake::ShakeProfile empty = shake::accumulate({});
    check(empty.horizontal.size() == 1 && empty.vertical.size() == 1, "an empty path is just the origin");
    checkNear(empty.horizontal[0], 0.0f, 0.0f, "at zero");
  }

  std::printf("shake::normalizeUnitAmplitude\n");
  {
    shake::ShakeProfile profile;
    profile.horizontal = {0.5f, -2.0f, 1.0f};
    profile.vertical = {0.25f, 0.0f};
    shake::normalizeUnitAmplitude(profile);
    checkNear(profile.horizontal[1], -1.0f, 1e-6f, "the largest absolute sample across BOTH channels becomes -1");
    checkNear(profile.horizontal[0], 0.25f, 1e-6f, "and everything else scales with it");
    checkNear(profile.vertical[0], 0.125f, 1e-6f, "including the other channel");

    shake::ShakeProfile still;                       // all zeros: the guard must not divide
    still.horizontal = {0.0f, 0.0f};
    still.vertical = {0.0f, 0.0f};
    shake::normalizeUnitAmplitude(still);
    bool finiteAndZero = true;
    for (float v : still.horizontal) if (!(v == 0.0f)) finiteAndZero = false;
    for (float v : still.vertical) if (!(v == 0.0f)) finiteAndZero = false;
    check(finiteAndZero, "an all-zero profile is left alone rather than filled with NaN");
  }

  std::printf("shake::sampleLooped\n");
  {
    const std::vector<float> ramp{0.0f, 10.0f, 20.0f, 30.0f};
    checkNear(shake::sampleLooped({}, 1.5, shake::LoopMode::Clamp), 0.0f, 0.0f, "an empty series samples as 0");
    checkNear(shake::sampleLooped({5.0f}, 0.0, shake::LoopMode::Clamp), 5.0f, 0.0f, "a one-sample series at t=0 is that sample");
    checkNear(shake::sampleLooped({5.0f}, 3.0, shake::LoopMode::Clamp), 0.0f, 0.0f,
              "and answers 0 in Clamp mode otherwise (a shipped quirk, kept)");
    checkNear(shake::sampleLooped({5.0f}, 3.0, shake::LoopMode::Loop), 5.0f, 0.0f, "but the sample in Loop mode");

    checkNear(shake::sampleLooped(ramp, 1.5, shake::LoopMode::Clamp), 15.0f, 1e-5f, "Clamp interpolates between neighbours");
    checkNear(shake::sampleLooped(ramp, -2.0, shake::LoopMode::Clamp), 0.0f, 0.0f, "Clamp below the range is 0, not the first sample");
    // Clamp does NOT hold the last sample outside the range: the shipped code sends BOTH
    // out-of-range tests (t < 0 and t > last) to the SAME zero return, and only the
    // endpoints themselves give the end samples (listing 0x229c-0x22bc). This was
    // measured against the shipped bundle, which answers 0.0f at t = 3.001 and t = 99.
    checkNear(shake::sampleLooped(ramp, 3.0, shake::LoopMode::Clamp), 30.0f, 1e-5f, "Clamp gives the last sample AT the exact end");
    checkNear(shake::sampleLooped(ramp, 3.001, shake::LoopMode::Clamp), 0.0f, 1e-5f, "but just past the end it is 0, not the last sample");
    checkNear(shake::sampleLooped(ramp, 99.0, shake::LoopMode::Clamp), 0.0f, 1e-5f, "and well past the end it is 0 as well");

    checkNear(shake::sampleLooped(ramp, 4.5, shake::LoopMode::Loop), 5.0f, 1e-5f, "Loop wraps past the end (4.5 -> 0.5)");
    checkNear(shake::sampleLooped(ramp, -1.5, shake::LoopMode::Loop), 25.0f, 1e-5f, "and below zero (-1.5 -> 2.5)");
    checkNear(shake::sampleLooped(ramp, 2.5, shake::LoopMode::PingPong), 25.0f, 1e-5f, "PingPong mirrors the second half");
    checkNear(shake::sampleLooped(ramp, 4.5, shake::LoopMode::PingPong), 15.0f, 1e-5f, "and continues mirrored");
    checkNear(shake::sampleLooped(ramp, 1.0, shake::LoopMode::None), 0.0f, 0.0f, "None never reads the series");
    checkNear(shake::sampleLooped(ramp, 1.0, static_cast<shake::LoopMode>(99)), 0.0f, 0.0f,
              "and an out-of-enum mode falls through to the same place");
  }

  std::printf("shake::resampleProfile\n");
  {
    const std::vector<float> ramp{0.0f, 10.0f, 20.0f, 30.0f};
    const std::vector<float> even = shake::resampleProfile(ramp, 1.0, 4, shake::LoopMode::Clamp);
    check(even == ramp, "a step of 1 over the whole series reproduces it");
    const std::vector<float> half = shake::resampleProfile(ramp, 0.5, 3, shake::LoopMode::Clamp);
    checkNear(half[1], 5.0f, 1e-5f, "a step of 0.5 samples between the stored ones");
    check(shake::resampleProfile(ramp, 1.0, 0, shake::LoopMode::Clamp).empty(), "a count below 1 gives nothing");
    const std::vector<float> zeroStep = shake::resampleProfile(ramp, 0.0, 3, shake::LoopMode::Clamp);
    check(zeroStep.size() == 3 && zeroStep[0] == 0.0f && zeroStep[2] == 0.0f,
          "a non-positive step returns the zero-filled array without sampling");
    check(shake::resampleProfile({}, 1.0, 3, shake::LoopMode::Clamp).size() == 3, "an empty source still sizes the output");
  }

  // --- the tracker (recovered 2026-10-28): trackTranslation, 960 instructions.
  //
  // Every expectation below was MEASURED against the shipped ShakeCapture bundle, whose
  // exported `shake::trackTranslation` was called on the same inputs through dlopen. The
  // numbers are not guesses from the listing: the whole-pixel shifts come out exactly, the
  // half-pixel shift comes out at 0.501372933 in BOTH implementations, and the tie case
  // comes out at -radius in BOTH.
  std::printf("shake::trackTranslation\n");
  {
    const int W = 64;
    const int H = 64;
    const int BLK = 16;
    const int RAD = 4;
    const size_t pixels = static_cast<size_t>(W) * static_cast<size_t>(H);

    // A deterministic frame with texture in both directions, so that no block of it is
    // flat and every block survives the shipped variance test.
    std::vector<float> frame(pixels);
    for (int y = 0; y < H; ++y) {
      for (int x = 0; x < W; ++x) {
        frame[static_cast<size_t>(y) * W + x] =
            0.5f + 0.4f * std::sin(x * 0.37f) * std::cos(y * 0.23f)
            + 0.05f * static_cast<float>((x * 7 + y * 13) % 11);
      }
    }
    std::vector<float> other(pixels, 0.0f);

    const auto zero = [](const shake::Vec2& v) { return v.x == 0.0f && v.y == 0.0f; };

    // The FIVE guards. Each of them answers {0, 0} without ever looking at a pixel.
    check(zero(shake::trackTranslation(nullptr, other.data(), W, H, BLK, RAD)),
          "a null first frame gives no translation (listing 0x830)");
    check(zero(shake::trackTranslation(frame.data(), nullptr, W, H, BLK, RAD)),
          "a null second frame gives no translation (listing 0x834)");
    check(zero(shake::trackTranslation(frame.data(), other.data(), W, H, 3, RAD)),
          "a block smaller than 4 gives no translation (listing 0x838-0x83c)");
    check(zero(shake::trackTranslation(frame.data(), other.data(), W, H, BLK, 0)),
          "a radius below 1 gives no translation (listing 0x840-0x848)");
    check(zero(shake::trackTranslation(frame.data(), other.data(), 20, H, BLK, RAD)),
          "a frame narrower than block + 2*radius gives no translation (listing 0x850-0x85c)");
    check(zero(shake::trackTranslation(frame.data(), other.data(), W, 20, BLK, RAD)),
          "and so does a frame shorter than block + 2*radius (listing 0x860-0x864)");

    // The TEXTURE TEST: a block whose variance is below 1e-6 carries no position, so the
    // shipped code skips it and the block contributes nothing to the medians. Every block
    // of a constant frame is skipped, so there is nothing left to take a median of.
    std::vector<float> flat(pixels, 0.25f);
    check(zero(shake::trackTranslation(flat.data(), frame.data(), W, H, BLK, RAD)),
          "a FLAT template is skipped by the variance test, so there is no translation (listing 0x9a8-0xa10)");

    // Identical frames: the best match is the offset (0, 0) at a cost of exactly zero,
    // and the parabola refinement is skipped because the best cost is not above 1e-12.
    check(zero(shake::trackTranslation(frame.data(), frame.data(), W, H, BLK, RAD)),
          "identical frames have no translation");

    // A whole-pixel shift: the picture in `b` is the picture in `a` moved sideways.
    const auto shiftedBy = [&](int pixelsRight) {
      std::vector<float> out(pixels, 0.0f);
      for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
          const int source = x - pixelsRight;
          if (source >= 0 && source < W) {
            out[static_cast<size_t>(y) * W + x] = frame[static_cast<size_t>(y) * W + source];
          }
        }
      }
      return out;
    };

    const shake::Vec2 left = shake::trackTranslation(frame.data(), shiftedBy(-3).data(), W, H, BLK, RAD);
    checkNear(left.x, -3.0, 0.0, "a shift of 3 pixels to the left is found EXACTLY (listing 0xa24-0xa90)");
    checkNear(left.y, 0.0, 0.0, "and the other axis does not move");
    const shake::Vec2 right = shake::trackTranslation(frame.data(), shiftedBy(2).data(), W, H, BLK, RAD);
    checkNear(right.x, 2.0, 0.0, "a shift of 2 pixels to the right is found EXACTLY");
    checkNear(right.y, 0.0, 0.0, "and again the other axis does not move");

    // The search reaches exactly `radius` pixels and no further. At the limit the
    // sub-pixel refinement does NOT run - it needs -radius < dx < radius - so the answer
    // is the whole number, not a fraction of one.
    const shake::Vec2 atLimit = shake::trackTranslation(frame.data(), shiftedBy(RAD).data(), W, H, BLK, RAD);
    checkNear(atLimit.x, static_cast<double>(RAD), 0.0,
              "a shift of exactly the radius is found, and is NOT refined (listing 0xc08-0xc18)");
    const shake::Vec2 pastLimit = shake::trackTranslation(frame.data(), shiftedBy(RAD + 2).data(), W, H, BLK, RAD);
    checkNear(pastLimit.x, static_cast<double>(RAD), 0.0,
              "a shift beyond the radius saturates AT the radius, because the search cannot reach further");

    // A HALF-PIXEL shift: the picture is resampled, so the answer is fractional. The
    // shipped parabola through the three neighbouring costs puts it at 0.501372933 here,
    // and this build puts it at the same number.
    std::vector<float> half(pixels, 0.0f);
    for (int y = 0; y < H; ++y) {
      for (int x = 0; x < W; ++x) {
        const double position = static_cast<double>(x) - 0.5;
        const int leftIndex = static_cast<int>(std::floor(position));
        const double weight = position - static_cast<double>(leftIndex);
        const float a = (leftIndex >= 0 && leftIndex < W)
                            ? frame[static_cast<size_t>(y) * W + leftIndex] : 0.0f;
        const float b = (leftIndex + 1 >= 0 && leftIndex + 1 < W)
                            ? frame[static_cast<size_t>(y) * W + leftIndex + 1] : 0.0f;
        half[static_cast<size_t>(y) * W + x] = static_cast<float>(a + (b - a) * weight);
      }
    }
    const shake::Vec2 sub = shake::trackTranslation(frame.data(), half.data(), W, H, BLK, RAD);
    checkNear(sub.x, 0.5, 0.01,
              "a half-pixel shift is recovered to a tenth of a pixel by the parabola (listing 0xf64-0xfa8)");

    // A TIE keeps the EARLIER candidate. The search walks dx from -radius upwards and only
    // replaces the best on a STRICTLY smaller cost, so an alternating-column frame - where
    // every even offset matches equally well - reports the FIRST offset that was tried.
    std::vector<float> stripes(pixels, 0.0f);
    for (int y = 0; y < H; ++y) {
      for (int x = 0; x < W; ++x) {
        stripes[static_cast<size_t>(y) * W + x] = (x & 1) ? 1.0f : 0.0f;
      }
    }
    const shake::Vec2 tie = shake::trackTranslation(stripes.data(), stripes.data(), W, H, BLK, RAD);
    checkNear(tie.x, -static_cast<double>(RAD), 0.0,
              "a tie keeps the FIRST candidate tried, so the answer is -radius (listing 0xa24-0xa90)");
    checkNear(tie.y, -static_cast<double>(RAD), 0.0, "on both axes");
  }

  if (failures == 0) std::printf("ALL RECOVERED SHAKE MATHS VERIFIED (%d checks)\n", passes);
  else std::printf("%d FAILURES out of %d checks\n", failures, passes + failures);
  return failures == 0 ? 0 : 1;
}
