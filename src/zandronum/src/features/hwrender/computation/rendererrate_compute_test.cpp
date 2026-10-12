// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>
#include <string.h>
#include "features/hwrender/computation/rendererrate_compute.h"

using namespace zx::hwrender;

// [rc4l] Queue #40: both windows read the same loop's rate, so each now shows its own renderer's frame time.
TEST(RendererRateCompute, AWholeSecondBecomesWhatIsShown)
{
	RendererRate r = ComputeRateStart();
	EXPECT_FALSE(r.seen);
	ComputeRateFrame(r, 1000, 2.0);
	ComputeRateFrame(r, 1400, 3.0);
	ComputeRateFrame(r, 1800, 4.0);
	EXPECT_EQ(0, r.shownMs);   // [rc4l] the second is still under way
	ComputeRateFrame(r, 2100, 5.0);
	EXPECT_EQ(3, r.shownMs);
	EXPECT_EQ(333, r.shownFps);   // [rc4l] three frames of 3 ms each allow 333 a second
	EXPECT_EQ(1, r.frames);
	EXPECT_DOUBLE_EQ(5.0, r.busyMs);
}

TEST(RendererRateCompute, AnEmptyOrZeroCostSecondLeavesNoRateBehind)
{
	RendererRate r = ComputeRateStart();
	ComputeRateFrame(r, 5000, 0.0);
	ComputeRateFrame(r, 6000, -1.0);   // [rc4l] a clock that stepped back counts as nothing
	EXPECT_EQ(0, r.shownMs);
	EXPECT_EQ(0, r.shownFps);
	EXPECT_DOUBLE_EQ(0.0, r.busyMs);
}

TEST(RendererRateCompute, ARendererIsLiveWhileItDrewWithinTheSecond)
{
	RendererRate r = ComputeRateStart();
	EXPECT_FALSE(ComputeRateLive(r, 10));
	ComputeRateFrame(r, 3000, 1.0);
	EXPECT_TRUE(ComputeRateLive(r, 3999));
	EXPECT_FALSE(ComputeRateLive(r, 4000));
}

TEST(RendererRateCompute, TheLineNamesTheRendererAndFitsItsBuffer)
{
	char buf[40];
	EXPECT_EQ(18, ComputeRateText(buf, sizeof(buf), "GL", 3, 310));
	EXPECT_STREQ("GL  3 ms (310 fps)", buf);
	char small[8];
	EXPECT_EQ(7, ComputeRateText(small, sizeof(small), "VK", 12, 83));
	EXPECT_STREQ("VK 12 m", small);
	EXPECT_EQ(0, ComputeRateText(NULL, 0, "VK", 1, 1));
	EXPECT_EQ(0, ComputeRateText(buf, 0, "VK", 1, 1));
}
