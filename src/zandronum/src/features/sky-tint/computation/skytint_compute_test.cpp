// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/sky-tint/computation/skytint_compute.h"

using namespace zx;

namespace
{

// A sky `width` x `height` whose top half is `top` and bottom half is `bottom`.
std::vector<SkyRgb> TwoTone(int width, int height, SkyRgb top, SkyRgb bottom)
{
	std::vector<SkyRgb> out;
	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
			out.push_back((y < (height / 2)) ? top : bottom);
	}

	return out;
}

} // namespace

// ---------------------------------------------------------------- gamma

TEST(Srgb, RoundTripsEveryByte)
{
	for (int b = 0; b <= 255; ++b)
		EXPECT_EQ(b, SrgbFromLinear(LinearFromSrgb(b))) << "byte " << b;
}

TEST(Srgb, KnowsItsAnchorsAndItsToe)
{
	EXPECT_NEAR(0.0, LinearFromSrgb(0), 1e-9);
	EXPECT_NEAR(1.0, LinearFromSrgb(255), 1e-9);
	// Mid-grey is NOT half the light -- it is about a fifth, which is the whole reason averaging in
	// sRGB is wrong. ((128/255 + 0.055) / 1.055) ^ 2.4.
	EXPECT_NEAR(0.2159, LinearFromSrgb(128), 0.001);
	// Below the knee the transfer is linear, not a power.
	EXPECT_NEAR(10.0 / 255.0 / 12.92, LinearFromSrgb(10), 1e-9);
}

TEST(Srgb, ClampsOutOfRangeInputInsteadOfMisbehaving)
{
	EXPECT_NEAR(0.0, LinearFromSrgb(-5), 1e-9);
	EXPECT_NEAR(1.0, LinearFromSrgb(300), 1e-9);
	EXPECT_EQ(0, SrgbFromLinear(-1.0));
	EXPECT_EQ(255, SrgbFromLinear(2.0));
	EXPECT_EQ(0, SrgbFromLinear(0.0));
}

// [rc4l] The correctness bug this replaces: a mean of sRGB bytes lands darker than the mean of the
// light those bytes encode. Black and white average to light ~127 in bytes but ~188 in linear.
TEST(AverageSky, AveragesTheLightRatherThanItsEncoding)
{
	std::vector<SkyRgb> pixels;
	pixels.push_back(SkyRgb(0, 0, 0));
	pixels.push_back(SkyRgb(255, 255, 255));

	const SkyRgb avg = AverageSky(pixels, 2, SkyAverage::Mean, SkyWeight::Cosine);

	EXPECT_GT(avg.r, 180) << "an sRGB-byte mean would have said about 127";
	EXPECT_LT(avg.r, 195);
	EXPECT_EQ(avg.r, avg.g);
	EXPECT_EQ(avg.g, avg.b);
}

// ---------------------------------------------------------------- row weighting

TEST(RowWeight, HorizonTakesTheLowerHalfOnly)
{
	EXPECT_EQ(0.0, RowWeight(0, 100, SkyWeight::Horizon));
	EXPECT_EQ(0.0, RowWeight(49, 100, SkyWeight::Horizon));
	EXPECT_EQ(1.0, RowWeight(50, 100, SkyWeight::Horizon));
	EXPECT_EQ(1.0, RowWeight(99, 100, SkyWeight::Horizon));
}

TEST(RowWeight, CosineFavoursTheZenithWhichIsTheOppositeEnd)
{
	// The disagreement the header calls out, in one assertion: what lights a floor is not what the
	// player is looking at.
	EXPECT_NEAR(1.0, RowWeight(0, 101, SkyWeight::Cosine), 0.001);		// straight up
	EXPECT_NEAR(0.0, RowWeight(100, 101, SkyWeight::Cosine), 0.001);	// the horizon
	EXPECT_NEAR(0.707, RowWeight(50, 101, SkyWeight::Cosine), 0.01);	// 45 degrees
}

TEST(RowWeight, SurvivesDegenerateGeometry)
{
	EXPECT_EQ(0.0, RowWeight(0, 0, SkyWeight::Cosine));
	EXPECT_EQ(0.0, RowWeight(0, -1, SkyWeight::Horizon));
	// A one-row sky has no gradient to sample; the single row is the zenith by definition.
	EXPECT_NEAR(1.0, RowWeight(0, 1, SkyWeight::Cosine), 0.001);
	// Out-of-range rows clamp instead of reading past the image.
	EXPECT_EQ(RowWeight(0, 10, SkyWeight::Horizon), RowWeight(-5, 10, SkyWeight::Horizon));
	EXPECT_EQ(RowWeight(9, 10, SkyWeight::Horizon), RowWeight(99, 10, SkyWeight::Horizon));
}

TEST(AverageSky, WeightingChoosesWhichHalfOfATwoToneSkyWins)
{
	// Blue above, orange below: horizon weighting must return orange, cosine must lean blue.
	const std::vector<SkyRgb> sky = TwoTone(4, 100, SkyRgb(40, 80, 255), SkyRgb(255, 120, 40));

	const SkyRgb horizon = AverageSky(sky, 4, SkyAverage::Mean, SkyWeight::Horizon);
	EXPECT_GT(horizon.r, horizon.b) << "the band the player sees is the orange one";

	const SkyRgb cosine = AverageSky(sky, 4, SkyAverage::Mean, SkyWeight::Cosine);
	EXPECT_GT(cosine.b, horizon.b) << "weighting the zenith pulls the answer toward the blue";
}

// ---------------------------------------------------------------- dominant

TEST(AverageSky, DominantPicksTheBiggerRegionInsteadOfSplittingTheDifference)
{
	// Three quarters dark smoke, one quarter bright fire, all in the lower band. A mean lands
	// between the two and matches neither; dominant should name the smoke.
	std::vector<SkyRgb> sky;
	for (int y = 0; y < 4; ++y)
	{
		for (int x = 0; x < 4; ++x)
			sky.push_back(((y >= 2) && (x == 0)) ? SkyRgb(255, 160, 40) : SkyRgb(40, 40, 48));
	}

	const SkyRgb dom = AverageSky(sky, 4, SkyAverage::Dominant, SkyWeight::Horizon);
	EXPECT_LT(dom.r, 100) << "the smoke is the dominant colour";

	const SkyRgb mean = AverageSky(sky, 4, SkyAverage::Mean, SkyWeight::Horizon);
	EXPECT_GT(mean.r, dom.r) << "the mean is dragged toward the fire it does not match";
}

TEST(AverageSky, DominantReturnsAColourThatWasActuallyInTheSky)
{
	std::vector<SkyRgb> sky;
	for (int i = 0; i < 8; ++i)
		sky.push_back(SkyRgb(200, 100, 50));

	// One bucket, one member colour: the answer must be that colour, not the bucket's centre.
	const SkyRgb dom = AverageSky(sky, 2, SkyAverage::Dominant, SkyWeight::Cosine);

	EXPECT_NEAR(200, dom.r, 2);
	EXPECT_NEAR(100, dom.g, 2);
	EXPECT_NEAR(50, dom.b, 2);
}

TEST(AverageSky, NothingToAverageIsWhiteRatherThanBlackOrACrash)
{
	// White is the identity tint, so an unanswerable sky leaves the level alone.
	EXPECT_EQ(SkyRgb(255, 255, 255), AverageSky(std::vector<SkyRgb>(), 4, SkyAverage::Mean, SkyWeight::Horizon));
	EXPECT_EQ(SkyRgb(255, 255, 255), AverageSky(TwoTone(2, 2, SkyRgb(1, 2, 3), SkyRgb(4, 5, 6)), 0, SkyAverage::Mean, SkyWeight::Horizon));

	// Fewer pixels than a row: height computes to zero.
	std::vector<SkyRgb> stub(2, SkyRgb(10, 20, 30));
	EXPECT_EQ(SkyRgb(255, 255, 255), AverageSky(stub, 8, SkyAverage::Mean, SkyWeight::Horizon));
	EXPECT_EQ(SkyRgb(255, 255, 255), AverageSky(stub, 8, SkyAverage::Dominant, SkyWeight::Horizon));
}

TEST(AverageSky, AWeightingThatSelectsNoRowsIsStillWhite)
{
	// A two-row sky under Horizon weighting: row 0 is excluded, so a one-row-high image has
	// nothing left. Both modes must survive the empty selection.
	std::vector<SkyRgb> onerow(4, SkyRgb(90, 90, 90));

	EXPECT_EQ(SkyRgb(255, 255, 255), AverageSky(onerow, 8, SkyAverage::Mean, SkyWeight::Horizon));
	EXPECT_EQ(SkyRgb(255, 255, 255), AverageSky(onerow, 8, SkyAverage::Dominant, SkyWeight::Horizon));
}

// ---------------------------------------------------------------- shaping

TEST(NormaliseBrightness, KeepsHueAndDiscardsBrightness)
{
	const SkyRgb dim = NormaliseBrightness(SkyRgb(100, 50, 25));

	EXPECT_EQ(255, dim.r);
	EXPECT_NEAR(127, dim.g, 2);
	EXPECT_NEAR(63, dim.b, 2);
}

TEST(NormaliseBrightness, ABlackSkyHasNoHueToOffer)
{
	EXPECT_EQ(SkyRgb(255, 255, 255), NormaliseBrightness(SkyRgb(0, 0, 0)));
}

// [rc4l] The problem this answers: a DIM green sky and a blazing one normalise to the same vivid
// hue, so a dark sky can read as a filter over the whole map. Turning Max colour down would tame it
// but is global, costing the tint on every map where it was already fine. This is per-sky.
TEST(SkyLuminance, ReadsADarkSkyAsDarkAndABrightOneAsBright)
{
	EXPECT_NEAR(0.0, SkyLuminance(SkyRgb(0, 0, 0)), 0.001);
	EXPECT_NEAR(1.0, SkyLuminance(SkyRgb(255, 255, 255)), 0.001);

	const double dim = SkyLuminance(SkyRgb(20, 60, 20));
	const double bright = SkyLuminance(SkyRgb(120, 255, 120));
	EXPECT_LT(dim, bright);
	EXPECT_LT(dim, 0.1) << "a dark green sky must not read as bright";
}

TEST(SkyLuminance, WeighsGreenHeaviestAsAnEyeDoes)
{
	// Which matters here precisely because the skies that cause trouble are the green ones.
	EXPECT_GT(SkyLuminance(SkyRgb(0, 200, 0)), SkyLuminance(SkyRgb(200, 0, 0)));
	EXPECT_GT(SkyLuminance(SkyRgb(0, 200, 0)), SkyLuminance(SkyRgb(0, 0, 200)));
}

TEST(StrengthForSky, IgnoresTheSkyAtZeroAndObeysItAtFull)
{
	const double dark = SkyLuminance(SkyRgb(20, 60, 20));

	EXPECT_EQ(40, StrengthForSky(40, dark, 0)) << "hue only: the sky's brightness is discarded";
	EXPECT_LT(StrengthForSky(40, dark, 100), 8) << "a dark sky should barely tint at all";
	EXPECT_EQ(40, StrengthForSky(40, 1.0, 100)) << "a white sky tints at full strength";
}

TEST(StrengthForSky, IsADialRatherThanASwitch)
{
	const double mid = 0.5;
	const int none = StrengthForSky(40, mid, 0);
	const int half = StrengthForSky(40, mid, 50);
	const int full = StrengthForSky(40, mid, 100);

	EXPECT_GT(none, half);
	EXPECT_GT(half, full) << "a map with a mid-bright sky should land in between";
}

TEST(StrengthForSky, ClampsNonsenseInsteadOfTrusting)
{
	EXPECT_EQ(40, StrengthForSky(40, 5.0, 100)) << "luminance above 1 is still just full";
	EXPECT_EQ(0, StrengthForSky(40, -1.0, 100));
	EXPECT_EQ(40, StrengthForSky(40, 1.0, 500));
}

TEST(SaturationPct, MeasuresTheDistanceFromGrey)
{
	EXPECT_EQ(0, SaturationPct(SkyRgb(200, 200, 200)));
	EXPECT_EQ(100, SaturationPct(SkyRgb(255, 0, 0)));
	EXPECT_EQ(50, SaturationPct(SkyRgb(200, 100, 100)));
	EXPECT_EQ(0, SaturationPct(SkyRgb(0, 0, 0)));
}

TEST(ClampSaturation, LeavesATameColourAloneAndPullsAViolentOne)
{
	const SkyRgb tame = SkyRgb(255, 230, 210);
	EXPECT_EQ(tame, ClampSaturation(tame, 60));

	// Pure red at a 40% ceiling: still red, much closer to grey, and no darker than it was.
	const SkyRgb tamed = ClampSaturation(SkyRgb(255, 0, 0), 40);
	EXPECT_LE(SaturationPct(tamed), 41);
	EXPECT_GT(tamed.g, 100);
	EXPECT_EQ(255, tamed.r) << "clamping saturation must not cost brightness";
}

TEST(ClampSaturation, ZeroIsGreyAndOutOfRangeInputIsClamped)
{
	const SkyRgb grey = ClampSaturation(SkyRgb(255, 0, 0), 0);
	EXPECT_EQ(0, SaturationPct(grey));

	EXPECT_EQ(SkyRgb(255, 0, 0), ClampSaturation(SkyRgb(255, 0, 0), 200));
	EXPECT_EQ(0, SaturationPct(ClampSaturation(SkyRgb(255, 0, 0), -10)));
	// A black colour has no saturation to clamp, and must not divide by its own zero.
	EXPECT_EQ(SkyRgb(0, 0, 0), ClampSaturation(SkyRgb(0, 0, 0), 50));
}

TEST(BlendFromWhite, FadesTheEffectOutWithoutASpecialCase)
{
	const SkyRgb tint(255, 128, 0);

	EXPECT_EQ(SkyRgb(255, 255, 255), BlendFromWhite(tint, 0));
	EXPECT_EQ(tint, BlendFromWhite(tint, 100));

	const SkyRgb half = BlendFromWhite(tint, 50);
	EXPECT_EQ(255, half.r);
	EXPECT_NEAR(191, half.g, 1);
	EXPECT_NEAR(127, half.b, 1);
}

TEST(BlendFromWhite, ClampsAPercentageOutOfRange)
{
	EXPECT_EQ(SkyRgb(255, 255, 255), BlendFromWhite(SkyRgb(0, 0, 0), -20));
	EXPECT_EQ(SkyRgb(0, 0, 0), BlendFromWhite(SkyRgb(0, 0, 0), 150));
}

// ---------------------------------------------------------------- the bleed

TEST(StrengthAtDistance, IsFullAtTheSkyAndNothingBeyondTheReach)
{
	EXPECT_EQ(40, StrengthAtDistance(40, 0.0, 512.0));
	EXPECT_EQ(0, StrengthAtDistance(40, 512.0, 512.0));
	EXPECT_EQ(0, StrengthAtDistance(40, 900.0, 512.0));
}

TEST(StrengthAtDistance, FallsOffFastAtFirstAndThenTrails)
{
	// Squared curve: half way out keeps a quarter, not a half.
	EXPECT_EQ(10, StrengthAtDistance(40, 256.0, 512.0));
	EXPECT_GT(StrengthAtDistance(40, 128.0, 512.0), 20);
	EXPECT_LT(StrengthAtDistance(40, 384.0, 512.0), 5);
}

// [rc4l] The reason this replaced a per-hop halving: the answer must not depend on how finely the
// mapper cut their sectors. Same room, same distance, whether it is one sector or forty.
TEST(StrengthAtDistance, DoesNotCareHowManySectorsTheDistanceWasSplitInto)
{
	const int oneBigStep = StrengthAtDistance(60, 300.0, 800.0);

	double walked = 0.0;
	for (int i = 0; i < 40; ++i)
		walked += 300.0 / 40.0;		// the same 300 units, chopped into detail sectors

	EXPECT_EQ(oneBigStep, StrengthAtDistance(60, walked, 800.0));
}

TEST(StrengthAtDistance, RefusesNonsenseInsteadOfInventingLight)
{
	EXPECT_EQ(0, StrengthAtDistance(0, 10.0, 512.0));
	EXPECT_EQ(0, StrengthAtDistance(40, 10.0, 0.0));
	EXPECT_EQ(0, StrengthAtDistance(40, 10.0, -5.0));
	EXPECT_EQ(40, StrengthAtDistance(40, -1.0, 512.0)) << "behind the origin is still the origin";

	// [rc4l] Reach governs how far light travels INDOORS. Zero must mean "outdoors only", not
	// "feature off" -- the open sky is lit by definition and has no distance to travel.
	EXPECT_EQ(40, StrengthAtDistance(40, 0.0, 0.0));
}

TEST(OpeningFactor, PassesEverythingThroughAFullGapAndNothingThroughAClosedDoor)
{
	EXPECT_DOUBLE_EQ(1.0, OpeningFactor(128.0, 128.0));
	EXPECT_DOUBLE_EQ(1.0, OpeningFactor(200.0, 128.0)) << "wider than the wall is still just open";
	EXPECT_DOUBLE_EQ(0.5, OpeningFactor(64.0, 128.0));
	EXPECT_DOUBLE_EQ(0.0, OpeningFactor(0.0, 128.0));
	EXPECT_DOUBLE_EQ(0.0, OpeningFactor(-5.0, 128.0));
	EXPECT_DOUBLE_EQ(0.0, OpeningFactor(64.0, 0.0));
}

TEST(StepCost, MakesANarrowOpeningCostMoreDistanceRatherThanBlockingOutright)
{
	EXPECT_DOUBLE_EQ(100.0, StepCost(100.0, 1.0));
	EXPECT_DOUBLE_EQ(200.0, StepCost(100.0, 0.5));

	// Floored at a tenth, so a hairline gap is expensive but finite -- an unbounded multiplier would
	// hand the result to floating-point noise in the opening height.
	EXPECT_DOUBLE_EQ(1000.0, StepCost(100.0, 0.01));
	EXPECT_DOUBLE_EQ(1000.0, StepCost(100.0, 0.1));
}

TEST(StepCost, SaysImpassableRatherThanReturningAHugeNumber)
{
	EXPECT_LT(StepCost(100.0, 0.0), 0.0);
	EXPECT_DOUBLE_EQ(0.0, StepCost(-50.0, 1.0)) << "a negative distance is no distance";
}

TEST(SkyRgb, DefaultsToBlackAndClampsWhatItIsGiven)
{
	const SkyRgb zero;
	EXPECT_EQ(0, zero.r);
	EXPECT_EQ(0, zero.g);
	EXPECT_EQ(0, zero.b);

	EXPECT_EQ(SkyRgb(255, 0, 255), SkyRgb(999, -20, 255));
	EXPECT_FALSE(SkyRgb(1, 2, 3) == SkyRgb(1, 2, 4));
}
