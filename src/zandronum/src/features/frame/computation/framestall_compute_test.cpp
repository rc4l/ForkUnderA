// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/computation/framestall_compute.h"

#include <gtest/gtest.h>

using namespace zx::frame;

// [rc4l] The tic stall reproduces a heavy map's tic frame on a light one; the every-frame stall spreads the same cost evenly as the control.
TEST(FrameStall, TheTicStallLandsOnlyOnFramesThatRanATic)
{
	EXPECT_EQ(1500, ComputeFrameStallUs(true, 1500, 0));
	EXPECT_EQ(0, ComputeFrameStallUs(false, 1500, 0));
}

TEST(FrameStall, TheEveryFrameStallLandsAlwaysAndAddsToTheTicStall)
{
	EXPECT_EQ(50, ComputeFrameStallUs(false, 1500, 50));
	EXPECT_EQ(1550, ComputeFrameStallUs(true, 1500, 50));
}

TEST(FrameStall, NegativeStallsCountAsNone)
{
	EXPECT_EQ(0, ComputeFrameStallUs(true, -5, -7));
}
