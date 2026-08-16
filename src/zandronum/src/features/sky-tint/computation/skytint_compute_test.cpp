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

	const SkyRgb avg = AverageSky(pixels, 2);

	EXPECT_GT(avg.r, 180) << "an sRGB-byte mean would have said about 127";
	EXPECT_LT(avg.r, 195);
	EXPECT_EQ(avg.r, avg.g);
	EXPECT_EQ(avg.g, avg.b);
}

// ---------------------------------------------------------------- row weighting

TEST(RowWeight, HorizonTakesTheLowerHalfOnly)
{
	EXPECT_EQ(0.0, RowWeight(0, 100));
	EXPECT_EQ(0.0, RowWeight(49, 100));
	EXPECT_EQ(1.0, RowWeight(50, 100));
	EXPECT_EQ(1.0, RowWeight(99, 100));
}

TEST(RowWeight, SurvivesDegenerateGeometry)
{
	EXPECT_EQ(0.0, RowWeight(0, 0));
	EXPECT_EQ(0.0, RowWeight(0, -1));
	// A one-row sky: row 0 is both halves, so it counts.
	EXPECT_EQ(1.0, RowWeight(0, 1));
	// Out-of-range rows clamp instead of reading past the image.
	EXPECT_EQ(RowWeight(0, 10), RowWeight(-5, 10));
	EXPECT_EQ(RowWeight(9, 10), RowWeight(99, 10));
}

// ---------------------------------------------------------------- layered skies

TEST(CompositeOver, TheEndsAreTheLayersThemselves)
{
	const SkyRgb front(255, 0, 0), back(0, 0, 255);
	EXPECT_TRUE(back == CompositeOver(front, 0, back));		// fully transparent front
	EXPECT_TRUE(front == CompositeOver(front, 255, back));	// fully opaque front
	EXPECT_TRUE(back == CompositeOver(front, -3, back));		// nonsense alpha clamps
	EXPECT_TRUE(front == CompositeOver(front, 999, back));
}

TEST(CompositeOver, HalfAlphaLandsHalfwayInLIGHTNotInTheEncoding)
{
	// The whole reason this is not a byte lerp. Half of 255 and half of 0 is 188 in sRGB, because
	// half the LIGHT is a much brighter byte than half the number. A naive (255+0)/2 gives 128, which
	// is visibly too dark and is the same mistake linear averaging exists to avoid.
	const SkyRgb mid = CompositeOver(SkyRgb(255, 255, 255), 128, SkyRgb(0, 0, 0));
	EXPECT_NEAR(188, mid.r, 2);
	EXPECT_NEAR(188, mid.g, 2);
	EXPECT_NEAR(188, mid.b, 2);
}

TEST(CompositeSkyLayers, MissingBackLayerLeavesTheFrontAlone)
{
	const std::vector<SkyRgb> front(4, SkyRgb(10, 20, 30));
	const std::vector<int> alpha(4, 128);
	const std::vector<SkyRgb> none;

	const std::vector<SkyRgb> out = CompositeSkyLayers(front, alpha, 2, none, 0);
	ASSERT_EQ(front.size(), out.size());
	EXPECT_TRUE(front[0] == out[0]);
}

TEST(CompositeSkyLayers, LayersOfDifferentSizesLineUpProportionally)
{
	// A 2x2 front over a 1x1 back. Every front pixel must see the single back pixel rather than
	// reading off the end, which is what indexing by raw offset would do.
	std::vector<SkyRgb> front(4, SkyRgb(255, 255, 255));
	const std::vector<int> alpha(4, 0);				// fully transparent: result must be all back
	const std::vector<SkyRgb> back(1, SkyRgb(7, 8, 9));

	const std::vector<SkyRgb> out = CompositeSkyLayers(front, alpha, 2, back, 1);
	ASSERT_EQ(size_t(4), out.size());
	for (size_t i = 0; i < out.size(); ++i)
		EXPECT_TRUE(SkyRgb(7, 8, 9) == out[i]);
}

TEST(CompositeSkyLayers, AnOpaqueFrontHidesTheBackCompletely)
{
	const std::vector<SkyRgb> front(4, SkyRgb(1, 2, 3));
	const std::vector<int> alpha(4, 255);
	const std::vector<SkyRgb> back(4, SkyRgb(200, 200, 200));

	const std::vector<SkyRgb> out = CompositeSkyLayers(front, alpha, 2, back, 2);
	ASSERT_EQ(size_t(4), out.size());
	for (size_t i = 0; i < out.size(); ++i)
		EXPECT_TRUE(SkyRgb(1, 2, 3) == out[i]);
}

// ---------------------------------------------------------------- dominant

TEST(AverageSky, NothingToAverageIsWhiteRatherThanBlackOrACrash)
{
	// White is the identity tint, so an unanswerable sky leaves the level alone.
	EXPECT_EQ(SkyRgb(255, 255, 255), AverageSky(std::vector<SkyRgb>(), 4));
	EXPECT_EQ(SkyRgb(255, 255, 255), AverageSky(TwoTone(2, 2, SkyRgb(1, 2, 3), SkyRgb(4, 5, 6)), 0));

	// Fewer pixels than a row: height computes to zero.
	std::vector<SkyRgb> stub(2, SkyRgb(10, 20, 30));
	EXPECT_EQ(SkyRgb(255, 255, 255), AverageSky(stub, 8));
}

// [rc4l] The failure that removed the dominant-colour mode, kept as a regression guard. A sky that
// is mostly near-black used to return near-black, which NormaliseBrightness turns into pure white --
// the no-tint value -- so the feature switched itself off and no slider could bring it back. A mean
// cannot do that: a dark region drags it darker, it cannot capture it.
TEST(AverageSky, AMostlyBlackSkyStillReportsTheColourInIt)
{
	std::vector<SkyRgb> sky;
	for (int y = 0; y < 10; ++y)
	{
		for (int x = 0; x < 4; ++x)
			sky.push_back((y == 8) ? SkyRgb(255, 120, 0) : SkyRgb(2, 2, 2));
	}

	const SkyRgb avg = AverageSky(sky, 4);

	EXPECT_FALSE(avg == SkyRgb(255, 255, 255)) << "white here would mean the tint silently vanished";
	EXPECT_GT(avg.r, avg.b) << "the one band with a colour still tilts the answer warm";
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

// ---------------------------------------------------------------- luminance preservation

TEST(PreserveLuminance, WhiteIsAlreadyNeutralAndIsLeftAlone)
{
	const SkyRgb white = PreserveLuminance(SkyRgb(255, 255, 255));

	EXPECT_NEAR(255, white.r, 1);
	EXPECT_NEAR(255, white.g, 1);
	EXPECT_NEAR(255, white.b, 1);
}

TEST(PreserveLuminance, AnOrangeTintStopsEatingMostOfTheLight)
{
	// The piss-filter case, and the honest limit of fixing it with a multiply.
	//
	// (255,120,0) passes about 0.55 of the light, so every surface it touches comes out nearly half
	// as dim as well as oranger. Correction cannot reach 1.0 here: red is already at 255 and cannot
	// rise to pay for the missing green and blue, so the clamp caps recovery at about 0.83. That is
	// still half the loss recovered, and a fully saturated tint is the worst case by construction.
	// Going further would mean desaturating the tint, which is what the Max colour slider is for.
	const SkyRgb before(255, 120, 0);
	const SkyRgb after = PreserveLuminance(before);

	const double passBefore = (0.2126 * before.r + 0.7152 * before.g + 0.0722 * before.b) / 255.0;
	const double passAfter = (0.2126 * after.r + 0.7152 * after.g + 0.0722 * after.b) / 255.0;

	EXPECT_NEAR(0.55, passBefore, 0.03) << "the uncorrected tint swallows nearly half the light";
	EXPECT_GT(passAfter, passBefore + 0.2) << "correction recovers most of what the clamp allows";
	EXPECT_NEAR(0.83, passAfter, 0.03);
}

// [rc4l] A COLOURED tint can never be fully corrected by scaling, and it is worth writing down why
// rather than discovering it again.
//
// Reaching luminance 1.0 would put the largest channel at max * 255/pass. `pass` is a weighted
// average of the three channels, so it is always at or below the largest one, which means that
// product is always at or above 255 and the brightest channel always clamps. Only an exactly neutral
// colour escapes. Full preservation would need the tint desaturated toward white first -- trading
// colour for light -- and that is what the Max colour slider already does, deliberately and visibly.
//
// So the promise here is "recovers most of the loss", not "loses nothing".
TEST(PreserveLuminance, AlwaysImprovesAColouredTintAndNeverMakesItWorse)
{
	const SkyRgb tints[] = { SkyRgb(255, 120, 0), SkyRgb(200, 170, 150), SkyRgb(80, 200, 90),
		SkyRgb(30, 30, 200), SkyRgb(255, 255, 200) };

	for (int i = 0; i < 5; ++i)
	{
		const SkyRgb after = PreserveLuminance(tints[i]);
		const double before = (0.2126 * tints[i].r + 0.7152 * tints[i].g + 0.0722 * tints[i].b) / 255.0;
		const double now = (0.2126 * after.r + 0.7152 * after.g + 0.0722 * after.b) / 255.0;

		EXPECT_GE(now, before - 0.001) << "correction must never pass LESS light, tint " << i;
		EXPECT_LE(now, 1.001) << "and must never invent light, tint " << i;
	}
}

TEST(PreserveLuminance, KeepsTheHueItWasGiven)
{
	// Correcting brightness must not turn one colour into another: the channel ORDER has to survive,
	// otherwise the sky's colour is not what lands on the walls.
	const SkyRgb after = PreserveLuminance(SkyRgb(200, 100, 50));

	EXPECT_GT(after.r, after.g);
	EXPECT_GT(after.g, after.b);
}

TEST(PreserveLuminance, ATintThatPassesNoLightLeavesTheLevelAlone)
{
	// Black would multiply every surface to black. White is the identity, which is the safe answer.
	EXPECT_EQ(SkyRgb(255, 255, 255), PreserveLuminance(SkyRgb(0, 0, 0)));
}

// [rc4l] The dial the sky-side ones could not be. Two maps under equally dark skies were reduced
// alike by Follow sky brightness, because the sky is the same for a dark room and a bright yard.
// The sector's own light level is what tells them apart.
TEST(StrengthForSectorLight, TellsADarkRoomFromABrightYard)
{
	const int dark = StrengthForSectorLight(40, 60, 100);
	const int bright = StrengthForSectorLight(40, 220, 100);

	EXPECT_LT(dark, bright);
	EXPECT_LT(dark, 12) << "a dim room should barely take the tint";
	EXPECT_GT(bright, 30) << "a bright yard should keep almost all of it";
}

TEST(StrengthForSectorLight, IgnoresTheRoomAtZeroAndIsADialInBetween)
{
	EXPECT_EQ(40, StrengthForSectorLight(40, 0, 0)) << "off means every sector tinted alike";

	const int none = StrengthForSectorLight(40, 64, 0);
	const int half = StrengthForSectorLight(40, 64, 50);
	const int full = StrengthForSectorLight(40, 64, 100);
	EXPECT_GT(none, half);
	EXPECT_GT(half, full);
}

TEST(StrengthForSectorLight, ClampsALightLevelOutsideDoomsRange)
{
	EXPECT_EQ(StrengthForSectorLight(40, 255, 100), StrengthForSectorLight(40, 999, 100));
	EXPECT_EQ(StrengthForSectorLight(40, 0, 100), StrengthForSectorLight(40, -50, 100));
	EXPECT_EQ(StrengthForSectorLight(40, 255, 100), StrengthForSectorLight(40, 255, 400));
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

namespace
{
	// Channel spread, which is what "how much colour is being pushed" means for a multiplier.
	int SpreadOf(SkyRgb c)
	{
		int mx = c.r; if (c.g > mx) mx = c.g; if (c.b > mx) mx = c.b;
		int mn = c.r; if (c.g < mn) mn = c.g; if (c.b < mn) mn = c.b;
		return mx - mn;
	}
}

// [rc4l] The dial is a fraction of what THIS sky has, so it pushes in proportion to how coloured the
// sky is. An earlier version aimed at a fixed absolute spread; that drove a washed-out sky to its own
// maximum as hard as a vivid one, and pinned the dial (Eon Collection aeon11, sky at 25% saturation,
// identical output from strength 25 upward).
TEST(EqualiseHuePush, PushesInProportionToHowColouredTheSkyIs)
{
	const int want = 40;
	const SkyRgb pale(190, 238, 255);		// Eon aeon11, 25% saturated
	const SkyRgb vivid(255, 1, 1);			// SoD MAP29, near-pure red

	EXPECT_LT(SpreadOf(EqualiseHuePush(pale, want)), SpreadOf(EqualiseHuePush(vivid, want)))
		<< "a pale sky must tint more gently than a saturated one at the same setting";

	// Each delivers pct% of its OWN range, which is what makes the dial mean one thing everywhere.
	EXPECT_NEAR(SpreadOf(EqualiseHuePush(pale, want)), (65 * want) / 100, 2);
	EXPECT_NEAR(SpreadOf(EqualiseHuePush(vivid, want)), (254 * want) / 100, 2);
}

// The regression aeon11 exposed: the dial has to keep moving across its whole travel, on every sky.
TEST(EqualiseHuePush, StaysLinearAcrossTheWholeDialEvenForAPaleSky)
{
	const SkyRgb pale(190, 238, 255);
	int last = -1;
	for (int pct = 10; pct <= 100; pct += 10)
	{
		const int s = SpreadOf(EqualiseHuePush(pale, pct));
		EXPECT_GT(s, last) << "strength " << pct << " gave the same push as the step below it";
		last = s;
	}
}

// [rc4l] The rule this replaces was yes/no, and the cliff was the bug: Eon Collection aeon13 lays one
// faint [254,194,194] wash over the whole level, which disqualified 158 of 180 sky-seeing spots.
TEST(SkyShareForSectorColour, LetsAFaintWashKeepMostOfItsSkyLight)
{
	// aeon13's actual sector colour: 24% saturated, so it keeps about three quarters.
	EXPECT_NEAR(76, SkyShareForSectorColour(SkyRgb(254, 194, 194)), 2);
}

TEST(SkyShareForSectorColour, StandsAsideForASectorTheMapperReallyColoured)
{
	// A hard red room is a deliberate statement and keeps almost none of the sky.
	EXPECT_LE(SkyShareForSectorColour(SkyRgb(255, 20, 20)), 10);
	EXPECT_EQ(0, SkyShareForSectorColour(SkyRgb(255, 0, 0)));
}

TEST(SkyShareForSectorColour, AnUncolouredSectorTakesTheLot)
{
	EXPECT_EQ(100, SkyShareForSectorColour(SkyRgb(255, 255, 255)));
	EXPECT_EQ(100, SkyShareForSectorColour(SkyRgb(128, 128, 128))) << "grey is a level, not a hue";
	EXPECT_EQ(100, SkyShareForSectorColour(SkyRgb(0, 0, 0))) << "black has no hue and no divisor";
}

// No boundary to argue about: the share has to fall off smoothly as the sector gets more coloured,
// which is the whole reason for preferring this over a saturation threshold.
TEST(SkyShareForSectorColour, FallsOffSmoothlyWithNoCliff)
{
	int last = 101;
	for (int drop = 0; drop <= 255; drop += 15)
	{
		const int share = SkyShareForSectorColour(SkyRgb(255, 255 - drop, 255 - drop));
		EXPECT_LE(share, last);
		EXPECT_LE(last - share, 12) << "a jump this big at drop " << drop << " is a cliff";
		last = share;
	}
}

TEST(EqualiseHuePush, ScalesWithTheRequestAndZeroIsOff)
{
	const SkyRgb dir(255, 1, 1);
	EXPECT_EQ(SkyRgb(255, 255, 255), EqualiseHuePush(dir, 0)) << "zero is off, not a faint tint";
	EXPECT_LT(SpreadOf(EqualiseHuePush(dir, 20)), SpreadOf(EqualiseHuePush(dir, 60)));
}

TEST(EqualiseHuePush, LeavesANeutralSkyAlone)
{
	// A grey sky has no hue to push at any strength, so it must not invent one.
	EXPECT_EQ(SkyRgb(255, 255, 255), EqualiseHuePush(SkyRgb(200, 200, 200), 100));
	EXPECT_EQ(SkyRgb(255, 255, 255), EqualiseHuePush(SkyRgb(255, 255, 255), 50));
}

TEST(EqualiseHuePush, WillNotExtrapolatePastTheSkysOwnColour)
{
	// A barely-tinted sky tops out at its real colour rather than being pushed into a hue it never
	// had: asking for more than it has must saturate at the sky itself, not overshoot.
	const SkyRgb faint(255, 240, 240);
	EXPECT_EQ(faint, EqualiseHuePush(faint, 100));
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
