// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gamecopy/computation/polyblend_compute.h"

#include <gtest/gtest.h>

using zx::gamecopy::ComputeAlongLine;
using zx::gamecopy::ComputeOnLine;

TEST(PolyBlend, TheEndsAreZeroAndOne)
{
	EXPECT_DOUBLE_EQ(0.0, ComputeAlongLine(0, 0, 100, 0, 0, 0));
	EXPECT_DOUBLE_EQ(1.0, ComputeAlongLine(0, 0, 100, 0, 100, 0));
	EXPECT_DOUBLE_EQ(0.25, ComputeAlongLine(0, 0, 0, 400, 0, 100));
}

TEST(PolyBlend, ALineOfNoLengthIsItsStart)
{
	EXPECT_DOUBLE_EQ(0.0, ComputeAlongLine(5, 5, 5, 5, 9, 9));
}

TEST(PolyBlend, ARiderFollowsTheMovedLine)
{
	// [rc4l] A split a third of the way along a line that slid 30 along x and turned upright.
	const double t = ComputeAlongLine(0, 0, 300, 0, 100, 0);
	long long x, y;
	ComputeOnLine(30, 0, 30, 300, t, x, y);
	EXPECT_EQ(30, x);
	EXPECT_EQ(100, y);
}

TEST(PolyBlend, TheEndsLandExactly)
{
	long long x, y;
	ComputeOnLine(7, -3, 1000003, 999999, 0.0, x, y);
	EXPECT_EQ(7, x);
	EXPECT_EQ(-3, y);
	ComputeOnLine(7, -3, 1000003, 999999, 1.0, x, y);
	EXPECT_EQ(1000003, x);
	EXPECT_EQ(999999, y);
}
