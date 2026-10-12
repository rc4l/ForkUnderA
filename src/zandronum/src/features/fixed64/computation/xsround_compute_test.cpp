// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/fixed64/computation/xsround_compute.h"

#include <gtest/gtest.h>

using namespace zx;

TEST(XsRound, FixedPointRoundsToNearestWithTiesToEven)
{
	EXPECT_EQ(ComputeXsFix(1.0, 16), 65536);
	EXPECT_EQ(ComputeXsFix(-1.0, 16), -65536);
	EXPECT_EQ(ComputeXsFix(0.5 / 65536.0, 16), 0);      // [rc4l] a tie goes to the even neighbour
	EXPECT_EQ(ComputeXsFix(1.5 / 65536.0, 16), 2);
	EXPECT_EQ(ComputeXsFix(0.25, 30), 1 << 28);
}

TEST(XsRound, AnUnsignedRoundingTakesTiesUpAndWrapsNegatives)
{
	EXPECT_EQ(ComputeXsRoundToUInt(0.5), 1u);
	EXPECT_EQ(ComputeXsRoundToUInt(2.5), 3u);
	EXPECT_EQ(ComputeXsRoundToUInt(-1.0), 0xFFFFFFFFu);
	EXPECT_EQ(ComputeXsRoundToUInt(1073741824.0), 0x40000000u);
}

TEST(XsRound, FloorRoundsDownEitherSideOfZero)
{
	EXPECT_EQ(ComputeXsFloor(2.75), 2);
	EXPECT_EQ(ComputeXsFloor(3.0), 3);
	EXPECT_EQ(ComputeXsFloor(-0.25), -1);
	EXPECT_EQ(ComputeXsFloor(-2.0), -2);
}
