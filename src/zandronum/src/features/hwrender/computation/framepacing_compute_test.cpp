// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/framepacing_compute.h"

#include <gtest/gtest.h>

using zx::hwrender::ComputeFenceWaitValue;
using zx::hwrender::ComputePresentInterval;

TEST(FramePacing, TheFirstFramesWaitForNothing)
{
	EXPECT_EQ(0ull, ComputeFenceWaitValue(1, 2));
	EXPECT_EQ(0ull, ComputeFenceWaitValue(2, 2));
}

TEST(FramePacing, ALaterFrameWaitsForTheOneThatManyBack)
{
	EXPECT_EQ(1ull, ComputeFenceWaitValue(3, 2));
	EXPECT_EQ(98ull, ComputeFenceWaitValue(100, 2));
}

TEST(FramePacing, NoneAheadMeansNoPacing)
{
	EXPECT_EQ(0ull, ComputeFenceWaitValue(100, 0));
}

TEST(FramePacing, ANegativeAheadIsPacingOff)
{
	EXPECT_EQ(0ull, ComputeFenceWaitValue(10, -3));
}

TEST(FramePacing, VsyncPresentsEveryRefresh)
{
	EXPECT_EQ(1, ComputePresentInterval(true, false));
	EXPECT_EQ(0, ComputePresentInterval(false, false));
}

// [rc4l] Asking a page's canvas for 0 changed nothing on screen and remade its swapchain on the first frame, which Chrome on Linux then failed to give a texture.
TEST(FramePacing, APageAlwaysPresentsAtItsCompositorsPace)
{
	EXPECT_EQ(1, ComputePresentInterval(false, true));
	EXPECT_EQ(1, ComputePresentInterval(true, true));
}
