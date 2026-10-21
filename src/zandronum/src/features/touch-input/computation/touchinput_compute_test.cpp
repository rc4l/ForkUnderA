// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/touch-input/computation/touchinput_compute.h"

#include <gtest/gtest.h>

using namespace zx::touch;

TEST(TouchAxes, InsideTheDeadZoneTheStickIsCentred)
{
	const TouchAxes a = ComputeTouchAxes(5.f, -5.f, 64.f, 0.2f);
	EXPECT_FLOAT_EQ(a.x, 0.f);
	EXPECT_FLOAT_EQ(a.y, 0.f);
	const TouchAxes still = ComputeTouchAxes(0.f, 0.f, 64.f, 0.2f);
	EXPECT_FLOAT_EQ(still.x, 0.f);
	EXPECT_FLOAT_EQ(still.y, 0.f);
}

TEST(TouchAxes, PastTheDeadZoneTheTravelToTheRimIsTheWholeRange)
{
	// [rc4l] Halfway between the dead zone's edge (12.8) and the rim (64) along x reads half, and the rim reads one.
	const TouchAxes half = ComputeTouchAxes(38.4f, 0.f, 64.f, 0.2f);
	EXPECT_NEAR(half.x, 0.5f, 1e-5f);
	EXPECT_FLOAT_EQ(half.y, 0.f);
	const TouchAxes rim = ComputeTouchAxes(0.f, 64.f, 64.f, 0.2f);
	EXPECT_FLOAT_EQ(rim.x, 0.f);
	EXPECT_FLOAT_EQ(rim.y, 1.f);
}

TEST(TouchAxes, PastTheRimTheThumbReadsTheRimAlongItsOwnDirection)
{
	const TouchAxes far = ComputeTouchAxes(-300.f, 400.f, 64.f, 0.2f);
	EXPECT_NEAR(far.x, -0.6f, 1e-5f);
	EXPECT_NEAR(far.y, 0.8f, 1e-5f);
}

TEST(TouchAxes, NoRadiusOrNoDeadZoneRangeIsHandled)
{
	const TouchAxes none = ComputeTouchAxes(10.f, 10.f, 0.f, 0.2f);
	EXPECT_FLOAT_EQ(none.x, 0.f);
	// [rc4l] A dead zone of the whole radius leaves only the rim, read as one.
	const TouchAxes all = ComputeTouchAxes(100.f, 0.f, 64.f, 1.f);
	EXPECT_FLOAT_EQ(all.x, 1.f);
	const TouchAxes zero = ComputeTouchAxes(64.f, 0.f, 64.f, 0.f);
	EXPECT_FLOAT_EQ(zero.x, 1.f);
}

TEST(TouchLook, ADragBecomesMouseMotionWithUpPositive)
{
	int x = 0, y = 0;
	ComputeTouchLook(10.f, 4.f, 1.5f, false, x, y);
	EXPECT_EQ(x, 15);
	EXPECT_EQ(y, -6);
	ComputeTouchLook(-10.f, 4.f, 1.5f, true, x, y);
	EXPECT_EQ(x, -15);
	EXPECT_EQ(y, 6);
}
