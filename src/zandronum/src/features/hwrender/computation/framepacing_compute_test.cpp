// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/framepacing_compute.h"

#include <gtest/gtest.h>

using zx::hwrender::ComputeFenceWaitValue;
using zx::hwrender::ComputePresentInterval;
using zx::hwrender::ComputeSwapchainImages;

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

// [Doruk-Aksoy] Three images under FIFO let the CPU queue two frames, and at 144 Hz it settled into 1.1/5.7/13.9 ms frames that each showed for one refresh: a judder on screen.
TEST(FramePacing, VsyncAsksForTwoImagesSoNoSecondFrameQueues)
{
	EXPECT_EQ(2, ComputeSwapchainImages(true, false));
}

// [Doruk-Aksoy] With two, mailbox had no image free to draw into until the next vblank and ran no faster than the monitor.
TEST(FramePacing, MailboxKeepsThreeImages)
{
	EXPECT_EQ(3, ComputeSwapchainImages(false, false));
}

TEST(FramePacing, APageCanvasKeepsThreeImagesWhateverVsyncSays)
{
	EXPECT_EQ(3, ComputeSwapchainImages(true, true));
	EXPECT_EQ(3, ComputeSwapchainImages(false, true));
}

// [Doruk-Aksoy] fua_debug_swapchain_images 3 brings back the old count under vsync, so the vsyncpacing scenario can show it fails there.
TEST(FramePacing, AForcedCountWinsOverVsync)
{
	EXPECT_EQ(3, ComputeSwapchainImages(true, false, 3));
	EXPECT_EQ(2, ComputeSwapchainImages(false, false, 2));
}

TEST(FramePacing, ForcingNothingOrLessIsTheVsyncChoice)
{
	EXPECT_EQ(2, ComputeSwapchainImages(true, false, 0));
	EXPECT_EQ(3, ComputeSwapchainImages(false, false, -1));
}

TEST(FramePacing, APageCanvasIgnoresAForcedCount)
{
	EXPECT_EQ(3, ComputeSwapchainImages(true, true, 2));
}
