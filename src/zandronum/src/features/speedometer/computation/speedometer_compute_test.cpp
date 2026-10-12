// [MobiusU9B] Tests for the speedometer computations, covering every branch of speedometer_compute.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "features/speedometer/computation/speedometer_compute.h"

#include <gtest/gtest.h>

#include <limits>

using zx::speedometer::ComputeSpeedometerClampPercent;
using zx::speedometer::ComputeSpeedometerColor;
using zx::speedometer::ComputeSpeedometerDisplayValue;
using zx::speedometer::ComputeSpeedometerSpeed;
using zx::speedometer::ComputeSpeedometerTextPos;
using zx::speedometer::TextPos;

namespace
{
	const int kTicRate = 35;
	const double kNaN = std::numeric_limits<double>::quiet_NaN();
}

TEST(ComputeSpeedometerSpeed, IsZeroAtRest)
{
	EXPECT_DOUBLE_EQ(0.0, ComputeSpeedometerSpeed(0.0, 0.0, 0.0, false, kTicRate));
}

TEST(ComputeSpeedometerSpeed, ConvertsUnitsPerTicToUnitsPerSecond)
{
	// [MobiusU9B] A 3-4-5 triangle keeps the expected value exact: 5 units per tic is 175 per second.
	EXPECT_DOUBLE_EQ(175.0, ComputeSpeedometerSpeed(3.0, 4.0, 0.0, false, kTicRate));
}

TEST(ComputeSpeedometerSpeed, IgnoresVerticalSpeedByDefault)
{
	EXPECT_DOUBLE_EQ(175.0, ComputeSpeedometerSpeed(3.0, -4.0, -40.0, false, kTicRate));
}

TEST(ComputeSpeedometerSpeed, IncludesVerticalSpeedWhenAsked)
{
	// [MobiusU9B] 2-3-6 is a Pythagorean quadruple, so the 3D length is exactly 7 units per tic.
	EXPECT_DOUBLE_EQ(245.0, ComputeSpeedometerSpeed(2.0, 3.0, -6.0, true, kTicRate));
}

TEST(ComputeSpeedometerSpeed, CountsAFallFromRestOnlyIn3D)
{
	EXPECT_DOUBLE_EQ(0.0, ComputeSpeedometerSpeed(0.0, 0.0, -10.0, false, kTicRate));
	EXPECT_DOUBLE_EQ(350.0, ComputeSpeedometerSpeed(0.0, 0.0, -10.0, true, kTicRate));
}

TEST(ComputeSpeedometerDisplayValue, RoundsToNearest)
{
	EXPECT_EQ(583, ComputeSpeedometerDisplayValue(583.4));
	EXPECT_EQ(584, ComputeSpeedometerDisplayValue(583.5));
	EXPECT_EQ(1, ComputeSpeedometerDisplayValue(0.5));
	EXPECT_EQ(0, ComputeSpeedometerDisplayValue(0.49));
}

TEST(ComputeSpeedometerDisplayValue, ShowsZeroForZeroNegativeAndNaN)
{
	EXPECT_EQ(0, ComputeSpeedometerDisplayValue(0.0));
	EXPECT_EQ(0, ComputeSpeedometerDisplayValue(-12.0));
	EXPECT_EQ(0, ComputeSpeedometerDisplayValue(kNaN));
}

TEST(ComputeSpeedometerDisplayValue, ClampsRunawaySpeeds)
{
	EXPECT_EQ(99998, ComputeSpeedometerDisplayValue(99998.0));
	EXPECT_EQ(99999, ComputeSpeedometerDisplayValue(99999.0));
	EXPECT_EQ(99999, ComputeSpeedometerDisplayValue(1e300));
	EXPECT_EQ(99999, ComputeSpeedometerDisplayValue(std::numeric_limits<double>::infinity()));
}

TEST(ComputeSpeedometerClampPercent, PassesInRangeValuesThrough)
{
	EXPECT_DOUBLE_EQ(0.5, ComputeSpeedometerClampPercent(0.5));
	EXPECT_DOUBLE_EQ(62.5, ComputeSpeedometerClampPercent(62.5));
	EXPECT_DOUBLE_EQ(100.0, ComputeSpeedometerClampPercent(100.0));
}

TEST(ComputeSpeedometerClampPercent, ClampsToTheEnds)
{
	EXPECT_DOUBLE_EQ(0.0, ComputeSpeedometerClampPercent(0.0));
	EXPECT_DOUBLE_EQ(0.0, ComputeSpeedometerClampPercent(-5.0));
	EXPECT_DOUBLE_EQ(100.0, ComputeSpeedometerClampPercent(250.0));
}

TEST(ComputeSpeedometerClampPercent, TreatsNaNAsZero)
{
	EXPECT_DOUBLE_EQ(0.0, ComputeSpeedometerClampPercent(kNaN));
}

TEST(ComputeSpeedometerColor, KeepsValidColors)
{
	EXPECT_EQ(0, ComputeSpeedometerColor(0, 22, 9));
	EXPECT_EQ(6, ComputeSpeedometerColor(6, 22, 9));
	EXPECT_EQ(21, ComputeSpeedometerColor(21, 22, 9));
}

TEST(ComputeSpeedometerColor, FallsBackOutsideTheRange)
{
	// [MobiusU9B] A negative range would index before the font's colour table, so it must never reach the draw call.
	EXPECT_EQ(9, ComputeSpeedometerColor(-1, 22, 9));
	EXPECT_EQ(9, ComputeSpeedometerColor(22, 22, 9));
	EXPECT_EQ(9, ComputeSpeedometerColor(1000, 22, 9));
}

namespace
{
	// [MobiusU9B] A 640x480 virtual screen with a 60x10 box and 40-wide text, so the box has 580x470 of free space.
	TextPos PlaceAt(double xPct, double yPct)
	{
		return ComputeSpeedometerTextPos(xPct, yPct, 640, 480, 60, 10, 40);
	}
}

TEST(ComputeSpeedometerTextPos, TopLeftPutsTheBoxFlushInTheCorner)
{
	const TextPos pos = PlaceAt(0.0, 0.0);
	EXPECT_EQ(20, pos.x);
	EXPECT_EQ(0, pos.y);
}

TEST(ComputeSpeedometerTextPos, BottomRightKeepsTheWholeBoxOnScreen)
{
	const TextPos pos = PlaceAt(100.0, 100.0);
	EXPECT_EQ(640 - 40, pos.x);
	EXPECT_EQ(480 - 10, pos.y);
}

TEST(ComputeSpeedometerTextPos, MiddleCentresTheBox)
{
	const TextPos pos = PlaceAt(50.0, 50.0);
	EXPECT_EQ(290 + 60 - 40, pos.x);
	EXPECT_EQ(235, pos.y);
}

TEST(ComputeSpeedometerTextPos, RoundsFractionalPositions)
{
	// [MobiusU9B] 0.1% of 580 is 0.58 and of 470 is 0.47, which round to 1 and 0.
	const TextPos pos = PlaceAt(0.1, 0.1);
	EXPECT_EQ(1 + 20, pos.x);
	EXPECT_EQ(0, pos.y);
}

TEST(ComputeSpeedometerTextPos, ClampsOutOfRangePercentages)
{
	const TextPos low = PlaceAt(-50.0, kNaN);
	EXPECT_EQ(20, low.x);
	EXPECT_EQ(0, low.y);
	const TextPos high = PlaceAt(500.0, 500.0);
	EXPECT_EQ(600, high.x);
	EXPECT_EQ(470, high.y);
}

TEST(ComputeSpeedometerTextPos, RightAlignsSoTheSuffixStaysPut)
{
	// [MobiusU9B] Narrower and wider readings share a right edge, which is what stops proportional digits jittering.
	const TextPos narrow = ComputeSpeedometerTextPos(50.0, 50.0, 640, 480, 60, 10, 25);
	const TextPos wide = ComputeSpeedometerTextPos(50.0, 50.0, 640, 480, 60, 10, 55);
	EXPECT_EQ(narrow.x + 25, wide.x + 55);
	EXPECT_EQ(narrow.y, wide.y);
}

TEST(ComputeSpeedometerTextPos, NeverPushesWideTextOffTheLeftEdge)
{
	const TextPos pos = ComputeSpeedometerTextPos(0.0, 0.0, 640, 480, 60, 10, 90);
	EXPECT_EQ(0, pos.x);
}

TEST(ComputeSpeedometerTextPos, PinsABoxLargerThanTheScreenToTheTopLeft)
{
	const TextPos pos = ComputeSpeedometerTextPos(100.0, 100.0, 50, 8, 60, 10, 60);
	EXPECT_EQ(0, pos.x);
	EXPECT_EQ(0, pos.y);
}
