// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/computation/framestall_compute.h"

#include <gtest/gtest.h>

using namespace zx::frame;

// [rc4l] A stall burns what it was asked for, and nothing for zero or a negative.
TEST(FrameStall, AStallBurnsWhatItWasAskedAndNegativesNone)
{
	EXPECT_EQ(1500, ComputeStallUs(1500));
	EXPECT_EQ(0, ComputeStallUs(0));
	EXPECT_EQ(0, ComputeStallUs(-7));
}
