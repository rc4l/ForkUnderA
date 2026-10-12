// [MobiusU9B] Tests for the crosshair computations, covering every branch of crosshair_compute.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "features/crosshair/computation/crosshair_compute.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>

using zx::crosshair::ComputeCrosshairClampOffset;
using zx::crosshair::ComputeCrosshairClampScale;
using zx::crosshair::ComputeCrosshairRect;
using zx::crosshair::CrosshairRect;
using zx::crosshair::CrosshairRectInput;

namespace
{
	const double kNaN = std::numeric_limits<double>::quiet_NaN();
	const double kInf = std::numeric_limits<double>::infinity();
	const int64_t kFracUnit = int64_t(1) << 16;

	// [MobiusU9B] FlashCrosshair's starting size, XHAIRPICKUPSIZE in shared_sbar.cpp.
	const int64_t kPickupSize = (kFracUnit * 2) + (kFracUnit / 18);

	// [MobiusU9B] A 1920x1080 screen with the full-screen HUD, so the view is the whole screen.
	CrosshairRectInput FullView(int imageWidth, int imageHeight)
	{
		CrosshairRectInput in{};
		in.imageWidth = imageWidth;
		in.imageHeight = imageHeight;
		in.scaleWithScreen = false;
		in.screenHeight = 1080;
		in.grow = false;
		in.growSize = kFracUnit;
		in.userScale = 1.0;
		in.viewX = 0;
		in.viewY = 0;
		in.viewWidth = 1920;
		in.viewHeight = 1080;
		in.offsetXPercent = 0.0;
		in.offsetYPercent = 0.0;
		return in;
	}

	// [MobiusU9B] DBaseStatusBar::DrawCrosshair's size and position maths as it stood before this feature, on fixed_t's raw 64-bit value.
	CrosshairRect LegacyRect(const CrosshairRectInput &in)
	{
		int64_t size = in.scaleWithScreen ? in.screenHeight * kFracUnit / 200 : kFracUnit;
		if (in.grow) {
			size = (size * in.growSize) >> 16;
		}
		CrosshairRect rect{};
		rect.x = in.viewWidth / 2 + in.viewX;
		rect.y = in.viewHeight / 2 + in.viewY;
		rect.width = static_cast<int>((in.imageWidth * size) >> 16);
		rect.height = static_cast<int>((in.imageHeight * size) >> 16);
		return rect;
	}

	void ExpectSameRect(const CrosshairRect &expected, const CrosshairRect &actual)
	{
		EXPECT_EQ(expected.x, actual.x);
		EXPECT_EQ(expected.y, actual.y);
		EXPECT_EQ(expected.width, actual.width);
		EXPECT_EQ(expected.height, actual.height);
	}
}

TEST(ComputeCrosshairClampScale, KeepsValuesInRange)
{
	EXPECT_DOUBLE_EQ(1.0, ComputeCrosshairClampScale(1.0));
	EXPECT_DOUBLE_EQ(0.25, ComputeCrosshairClampScale(0.25));
	EXPECT_DOUBLE_EQ(2.35, ComputeCrosshairClampScale(2.35));
	EXPECT_DOUBLE_EQ(4.0, ComputeCrosshairClampScale(4.0));
}

TEST(ComputeCrosshairClampScale, ClampsToTheSliderEnds)
{
	EXPECT_DOUBLE_EQ(0.25, ComputeCrosshairClampScale(0.1));
	EXPECT_DOUBLE_EQ(0.25, ComputeCrosshairClampScale(0.0));
	EXPECT_DOUBLE_EQ(0.25, ComputeCrosshairClampScale(-3.0));
	EXPECT_DOUBLE_EQ(0.25, ComputeCrosshairClampScale(-kInf));
	EXPECT_DOUBLE_EQ(4.0, ComputeCrosshairClampScale(4.01));
	EXPECT_DOUBLE_EQ(4.0, ComputeCrosshairClampScale(kInf));
}

TEST(ComputeCrosshairClampScale, TreatsNaNAsTheStockSize)
{
	EXPECT_DOUBLE_EQ(1.0, ComputeCrosshairClampScale(kNaN));
}

TEST(ComputeCrosshairClampOffset, KeepsValuesInRange)
{
	EXPECT_DOUBLE_EQ(0.0, ComputeCrosshairClampOffset(0.0));
	EXPECT_DOUBLE_EQ(-50.0, ComputeCrosshairClampOffset(-50.0));
	EXPECT_DOUBLE_EQ(12.5, ComputeCrosshairClampOffset(12.5));
	EXPECT_DOUBLE_EQ(50.0, ComputeCrosshairClampOffset(50.0));
}

TEST(ComputeCrosshairClampOffset, ClampsToTheSliderEnds)
{
	EXPECT_DOUBLE_EQ(-50.0, ComputeCrosshairClampOffset(-50.5));
	EXPECT_DOUBLE_EQ(-50.0, ComputeCrosshairClampOffset(-kInf));
	EXPECT_DOUBLE_EQ(50.0, ComputeCrosshairClampOffset(75.0));
	EXPECT_DOUBLE_EQ(50.0, ComputeCrosshairClampOffset(kInf));
}

TEST(ComputeCrosshairClampOffset, TreatsNaNAsCentred)
{
	EXPECT_DOUBLE_EQ(0.0, ComputeCrosshairClampOffset(kNaN));
}

TEST(ComputeCrosshairRect, MatchesTheStockMathsAtDefaults)
{
	// [MobiusU9B] Both stock toggles over two images and four views: full screen, odd half-width, stopped at the status bar, and a smaller window.
	const std::array<std::array<int, 2>, 2> imageSizes = {{ { 16, 16 }, { 33, 17 } }};
	const std::array<std::array<int, 5>, 4> views = {{
		{ 1920, 1080, 0, 0, 1080 },
		{ 1366, 768, 0, 0, 768 },
		{ 640, 368, 0, 0, 400 },
		{ 1152, 504, 64, 47, 600 },
	}};
	for (const auto &image : imageSizes) {
		for (const auto &view : views) {
			for (int toggles = 0; toggles < 4; ++toggles) {
				CrosshairRectInput in = FullView(image[0], image[1]);
				in.viewWidth = view[0];
				in.viewHeight = view[1];
				in.viewX = view[2];
				in.viewY = view[3];
				in.screenHeight = view[4];
				in.scaleWithScreen = (toggles & 1) != 0;
				in.grow = (toggles & 2) != 0;
				in.growSize = kPickupSize;
				SCOPED_TRACE(testing::Message() << image[0] << "x" << image[1] << " view " << view[0] << "x" << view[1] << " toggles " << toggles);
				ExpectSameRect(LegacyRect(in), ComputeCrosshairRect(in));
			}
		}
	}
}

TEST(ComputeCrosshairRect, CentresOnTheView)
{
	const CrosshairRect rect = ComputeCrosshairRect(FullView(16, 16));
	EXPECT_EQ(960, rect.x);
	EXPECT_EQ(540, rect.y);
	EXPECT_EQ(16, rect.width);
	EXPECT_EQ(16, rect.height);
}

TEST(ComputeCrosshairRect, ScalesWithTheScreenWhenAsked)
{
	CrosshairRectInput in = FullView(16, 16);
	in.scaleWithScreen = true;
	const CrosshairRect rect = ComputeCrosshairRect(in);
	// [MobiusU9B] 1080 lines is 5.4 times the 200-line base, and 16 * 5.4 truncates to 86.
	EXPECT_EQ(86, rect.width);
	EXPECT_EQ(86, rect.height);
}

TEST(ComputeCrosshairRect, MultipliesByTheSizeSlider)
{
	CrosshairRectInput in = FullView(16, 10);
	in.userScale = 2.5;
	const CrosshairRect rect = ComputeCrosshairRect(in);
	EXPECT_EQ(40, rect.width);
	EXPECT_EQ(25, rect.height);
}

TEST(ComputeCrosshairRect, ShrinksWithTheSizeSlider)
{
	CrosshairRectInput in = FullView(16, 16);
	in.userScale = 0.25;
	EXPECT_EQ(4, ComputeCrosshairRect(in).width);
}

TEST(ComputeCrosshairRect, ClampsTheSizeSlider)
{
	CrosshairRectInput in = FullView(16, 16);
	in.userScale = 100.0;
	EXPECT_EQ(64, ComputeCrosshairRect(in).width);
	in.userScale = kNaN;
	EXPECT_EQ(16, ComputeCrosshairRect(in).width);
}

TEST(ComputeCrosshairRect, StacksTheSliderOnScreenScalingAndGrowth)
{
	CrosshairRectInput in = FullView(16, 16);
	in.scaleWithScreen = true;
	in.screenHeight = 400;
	in.userScale = 1.5;
	in.grow = true;
	in.growSize = 2 * kFracUnit;
	// [MobiusU9B] 16 pixels, doubled for a 400-line screen, times 1.5, then doubled again by the pickup growth.
	EXPECT_EQ(96, ComputeCrosshairRect(in).width);
}

TEST(ComputeCrosshairRect, IgnoresGrowthWhenItIsOff)
{
	CrosshairRectInput in = FullView(16, 16);
	in.growSize = kPickupSize;
	EXPECT_EQ(16, ComputeCrosshairRect(in).width);
}

TEST(ComputeCrosshairRect, MovesByAPercentageOfTheView)
{
	CrosshairRectInput in = FullView(16, 16);
	in.offsetXPercent = 10.0;
	in.offsetYPercent = -25.0;
	const CrosshairRect rect = ComputeCrosshairRect(in);
	EXPECT_EQ(960 + 192, rect.x);
	EXPECT_EQ(540 - 270, rect.y);
}

TEST(ComputeCrosshairRect, ReachesTheViewEdgesAtTheSliderEnds)
{
	CrosshairRectInput in = FullView(16, 16);
	in.offsetXPercent = -50.0;
	in.offsetYPercent = 50.0;
	CrosshairRect rect = ComputeCrosshairRect(in);
	EXPECT_EQ(0, rect.x);
	EXPECT_EQ(1080, rect.y);

	in.offsetXPercent = 999.0;
	in.offsetYPercent = kNaN;
	rect = ComputeCrosshairRect(in);
	EXPECT_EQ(1920, rect.x);
	EXPECT_EQ(540, rect.y);
}

TEST(ComputeCrosshairRect, OffsetsRelativeToAViewWindow)
{
	CrosshairRectInput in = FullView(16, 16);
	in.viewX = 100;
	in.viewY = 50;
	in.viewWidth = 800;
	in.viewHeight = 400;
	in.offsetXPercent = 5.0;
	in.offsetYPercent = 2.5;
	const CrosshairRect rect = ComputeCrosshairRect(in);
	EXPECT_EQ(100 + 400 + 40, rect.x);
	EXPECT_EQ(50 + 200 + 10, rect.y);
}

TEST(ComputeCrosshairRect, RoundsOffsetsToTheNearestPixel)
{
	CrosshairRectInput in = FullView(16, 16);
	in.viewWidth = 1366;
	in.offsetXPercent = 0.5;
	// [MobiusU9B] 0.5 % of 1366 is 6.83 pixels, which rounds to 7.
	EXPECT_EQ(683 + 7, ComputeCrosshairRect(in).x);
}

TEST(ComputeCrosshairRect, HandlesALargeImageAtTheLargestScale)
{
	CrosshairRectInput in = FullView(512, 512);
	in.scaleWithScreen = true;
	in.screenHeight = 4320;
	in.userScale = 4.0;
	in.grow = true;
	in.growSize = kPickupSize;
	// [MobiusU9B] 512 * 21.6 * 4 * (2 + 1/18) is about 90 900 pixels, which a 32-bit product would have overflowed.
	const CrosshairRect rect = ComputeCrosshairRect(in);
	EXPECT_GT(rect.width, 90000);
	EXPECT_EQ(rect.width, rect.height);
}
