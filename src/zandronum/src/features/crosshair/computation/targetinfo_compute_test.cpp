// [MobiusU9B] Tests for the target-info computations, covering every branch of targetinfo_compute.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "features/crosshair/computation/targetinfo_compute.h"

#include <gtest/gtest.h>

#include <limits>
#include <vector>

using zx::crosshair::ComputeActorNameAllowed;
using zx::crosshair::ComputeActorNameText;
using zx::crosshair::ComputeActorScanPassesThrough;
using zx::crosshair::ComputeCrosshairClampInt;
using zx::crosshair::ComputeCrosshairInfoAnchor;
using zx::crosshair::ComputeCrosshairInfoCanvas;
using zx::crosshair::ComputeCrosshairInfoLayout;
using zx::crosshair::ComputeCrosshairTextScale;
using zx::crosshair::ComputeIdentifyFadeAlpha;
using zx::crosshair::ComputeIdentifyHealthKnown;
using zx::crosshair::ComputeLineCount;
using zx::crosshair::CrosshairRect;
using zx::crosshair::InfoCanvas;
using zx::crosshair::InfoElement;
using zx::crosshair::InfoPos;
using zx::crosshair::kActorNameClass;
using zx::crosshair::kActorNameOff;
using zx::crosshair::kActorNameTag;

namespace
{
	const double kNaN = std::numeric_limits<double>::quiet_NaN();

	// [MobiusU9B] The stock hold and fade of the bottom-of-screen message at 35 tics a second.
	const int kHoldTics = 70;
	const int kFadeTics = 12;

	InfoElement Element(bool visible, int lines, int offsetX = 0, int offsetY = 0)
	{
		return InfoElement{visible, lines, offsetX, offsetY};
	}
}

TEST(ComputeCrosshairClampInt, KeepsValuesInRange)
{
	EXPECT_EQ(0, ComputeCrosshairClampInt(0, -200, 200));
	EXPECT_EQ(-200, ComputeCrosshairClampInt(-200, -200, 200));
	EXPECT_EQ(200, ComputeCrosshairClampInt(200, -200, 200));
}

TEST(ComputeCrosshairClampInt, ClampsToTheEnds)
{
	EXPECT_EQ(-200, ComputeCrosshairClampInt(-201, -200, 200));
	EXPECT_EQ(2, ComputeCrosshairClampInt(9, 0, 2));
}

TEST(ComputeCrosshairTextScale, IsTheScreenOverTheHudHeight)
{
	EXPECT_DOUBLE_EQ(2.25, ComputeCrosshairTextScale(1080, 480));
	EXPECT_DOUBLE_EQ(1.0, ComputeCrosshairTextScale(1080, 1080));
	EXPECT_DOUBLE_EQ(0.5, ComputeCrosshairTextScale(480, 960));
}

TEST(ComputeCrosshairTextScale, FallsBackToOneForANonsenseSize)
{
	EXPECT_DOUBLE_EQ(1.0, ComputeCrosshairTextScale(1080, 0));
	EXPECT_DOUBLE_EQ(1.0, ComputeCrosshairTextScale(1080, -5));
	EXPECT_DOUBLE_EQ(1.0, ComputeCrosshairTextScale(0, 480));
}

TEST(ComputeCrosshairInfoCanvas, ShrinksTheScreenByTheScale)
{
	const InfoCanvas canvas = ComputeCrosshairInfoCanvas(1920, 1080, 2.25);
	EXPECT_EQ(853, canvas.width);
	EXPECT_EQ(480, canvas.height);
}

TEST(ComputeCrosshairInfoCanvas, UsesTheScreenForABadScale)
{
	const InfoCanvas zero = ComputeCrosshairInfoCanvas(1920, 1080, 0.0);
	EXPECT_EQ(1920, zero.width);
	EXPECT_EQ(1080, zero.height);
	const InfoCanvas nan = ComputeCrosshairInfoCanvas(1920, 1080, kNaN);
	EXPECT_EQ(1920, nan.width);
	EXPECT_EQ(1080, nan.height);
}

TEST(ComputeCrosshairInfoAnchor, SitsRightOfTheCrosshairCentredOnIt)
{
	const CrosshairRect rect{960, 540, 16, 16};
	const InfoPos pos = ComputeCrosshairInfoAnchor(rect, 1.0, 8, 4);
	EXPECT_EQ(960 + 8 + 4, pos.x);
	EXPECT_EQ(540 - 4, pos.y);
}

TEST(ComputeCrosshairInfoAnchor, ConvertsToTheTextSpace)
{
	// [MobiusU9B] At scale 2 the crosshair's right edge, 1000 real pixels in, is 500 text pixels in.
	const CrosshairRect rect{980, 600, 40, 40};
	const InfoPos pos = ComputeCrosshairInfoAnchor(rect, 2.0, 9, 3);
	EXPECT_EQ(500 + 3, pos.x);
	EXPECT_EQ(300 - 4, pos.y);
}

TEST(ComputeCrosshairInfoAnchor, StillWorksWithoutACrosshairImage)
{
	const CrosshairRect rect{960, 540, 0, 0};
	EXPECT_EQ(964, ComputeCrosshairInfoAnchor(rect, 1.0, 8, 4).x);
}

TEST(ComputeCrosshairInfoAnchor, TreatsABadScaleAsOne)
{
	const CrosshairRect rect{960, 540, 16, 16};
	EXPECT_EQ(972, ComputeCrosshairInfoAnchor(rect, -1.0, 8, 4).x);
}

TEST(ComputeCrosshairInfoLayout, StacksVisibleElementsInOrder)
{
	const std::vector<InfoElement> elements = { Element(true, 1), Element(true, 3), Element(true, 1) };
	const std::vector<InfoPos> pos = ComputeCrosshairInfoLayout(elements, InfoPos{100, 50}, 10);
	ASSERT_EQ(3U, pos.size());
	EXPECT_EQ(100, pos[0].x);
	EXPECT_EQ(50, pos[0].y);
	EXPECT_EQ(60, pos[1].y);
	EXPECT_EQ(90, pos[2].y);
}

TEST(ComputeCrosshairInfoLayout, HiddenElementsTakeNoRows)
{
	const std::vector<InfoElement> elements = { Element(false, 1), Element(true, 2), Element(false, 4), Element(true, 1) };
	const std::vector<InfoPos> pos = ComputeCrosshairInfoLayout(elements, InfoPos{100, 50}, 10);
	ASSERT_EQ(4U, pos.size());
	EXPECT_EQ(50, pos[1].y);
	EXPECT_EQ(70, pos[3].y);
}

TEST(ComputeCrosshairInfoLayout, AddsEachElementsOwnOffsets)
{
	const std::vector<InfoElement> elements = { Element(true, 1, -30, 5), Element(true, 1, 12, -40) };
	const std::vector<InfoPos> pos = ComputeCrosshairInfoLayout(elements, InfoPos{100, 50}, 10);
	EXPECT_EQ(70, pos[0].x);
	EXPECT_EQ(55, pos[0].y);
	EXPECT_EQ(112, pos[1].x);
	EXPECT_EQ(20, pos[1].y);
}

TEST(ComputeCrosshairInfoLayout, HandlesNoElements)
{
	EXPECT_TRUE(ComputeCrosshairInfoLayout(std::vector<InfoElement>(), InfoPos{0, 0}, 10).empty());
}

TEST(ComputeLineCount, CountsLines)
{
	EXPECT_EQ(1, ComputeLineCount("Player"));
	EXPECT_EQ(3, ComputeLineCount("100 / 50\nShotgun 8\nAlly"));
	EXPECT_EQ(2, ComputeLineCount("trailing\n"));
}

TEST(ComputeLineCount, IsZeroForNoText)
{
	EXPECT_EQ(0, ComputeLineCount(""));
	EXPECT_EQ(0, ComputeLineCount(nullptr));
}

TEST(ComputeIdentifyFadeAlpha, HoldsThenFades)
{
	EXPECT_DOUBLE_EQ(1.0, ComputeIdentifyFadeAlpha(0, kHoldTics, kFadeTics));
	EXPECT_DOUBLE_EQ(1.0, ComputeIdentifyFadeAlpha(kHoldTics, kHoldTics, kFadeTics));
	EXPECT_DOUBLE_EQ(0.5, ComputeIdentifyFadeAlpha(kHoldTics + 6, kHoldTics, kFadeTics));
	EXPECT_DOUBLE_EQ(0.0, ComputeIdentifyFadeAlpha(kHoldTics + kFadeTics, kHoldTics, kFadeTics));
	EXPECT_DOUBLE_EQ(0.0, ComputeIdentifyFadeAlpha(100000, kHoldTics, kFadeTics));
}

TEST(ComputeIdentifyFadeAlpha, HidesTextFromTheFuture)
{
	// [MobiusU9B] A seen tic ahead of now only happens after the tic counter restarts, so treat it as stale.
	EXPECT_DOUBLE_EQ(0.0, ComputeIdentifyFadeAlpha(-1, kHoldTics, kFadeTics));
}

TEST(ComputeIdentifyFadeAlpha, CutsOffWithoutAFade)
{
	EXPECT_DOUBLE_EQ(0.0, ComputeIdentifyFadeAlpha(kHoldTics + 1, kHoldTics, 0));
}

TEST(ComputeIdentifyHealthKnown, TrustsEverythingOffline)
{
	EXPECT_TRUE(ComputeIdentifyHealthKnown(false, false));
	EXPECT_TRUE(ComputeIdentifyHealthKnown(false, true));
}

TEST(ComputeIdentifyHealthKnown, NeedsTheServerOnline)
{
	EXPECT_TRUE(ComputeIdentifyHealthKnown(true, true));
	EXPECT_FALSE(ComputeIdentifyHealthKnown(true, false));
}

TEST(ComputeActorNameAllowed, NeedsTheModeOn)
{
	EXPECT_FALSE(ComputeActorNameAllowed(kActorNameOff, false, false));
	EXPECT_TRUE(ComputeActorNameAllowed(kActorNameClass, false, false));
	EXPECT_TRUE(ComputeActorNameAllowed(kActorNameTag, false, false));
}

TEST(ComputeActorNameAllowed, EitherServerSwitchForbidsIt)
{
	EXPECT_FALSE(ComputeActorNameAllowed(kActorNameClass, true, false));
	EXPECT_FALSE(ComputeActorNameAllowed(kActorNameClass, false, true));
	EXPECT_FALSE(ComputeActorNameAllowed(kActorNameTag, true, true));
}

TEST(ComputeActorNameText, PicksTheRequestedName)
{
	EXPECT_EQ("ZombieMan", ComputeActorNameText(kActorNameClass, "Former Human", "ZombieMan"));
	EXPECT_EQ("Former Human", ComputeActorNameText(kActorNameTag, "Former Human", "ZombieMan"));
	EXPECT_EQ("", ComputeActorNameText(kActorNameOff, "Former Human", "ZombieMan"));
}

TEST(ComputeActorNameText, IsEmptyForAMissingName)
{
	EXPECT_EQ("", ComputeActorNameText(kActorNameTag, nullptr, "ZombieMan"));
	EXPECT_EQ("", ComputeActorNameText(kActorNameClass, "Former Human", nullptr));
	EXPECT_EQ("", ComputeActorNameText(99, "Former Human", "ZombieMan"));
}

TEST(ComputeActorScanPassesThrough, OnlyLooksPastInvisibleActors)
{
	EXPECT_TRUE(ComputeActorScanPassesThrough(true, true));
	EXPECT_FALSE(ComputeActorScanPassesThrough(true, false));
	EXPECT_FALSE(ComputeActorScanPassesThrough(false, true));
	EXPECT_FALSE(ComputeActorScanPassesThrough(false, false));
}
