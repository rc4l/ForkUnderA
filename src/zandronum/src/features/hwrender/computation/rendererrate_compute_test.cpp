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

// [rc4l] The counter's 8x8 glyphs were a fixed size in screen pixels, unreadable at high resolutions.
TEST(RendererRateCompute, TheCounterScalesFromTheTopRight)
{
	RateBox b = ComputeRateBox(1280, 800, 20, 1);
	EXPECT_EQ(1, b.scale);
	EXPECT_EQ(1280, b.virtualWidth);
	EXPECT_EQ(800, b.virtualHeight);
	EXPECT_EQ(1280 - 160, b.x);
	EXPECT_EQ(1280 - 160, b.boxLeft);
	EXPECT_EQ(8, b.boxHeight);
	b = ComputeRateBox(1280, 800, 20, 2);
	EXPECT_EQ(640, b.virtualWidth);
	EXPECT_EQ(400, b.virtualHeight);
	EXPECT_EQ(640 - 160, b.x);
	EXPECT_EQ(1280 - 320, b.boxLeft);
	EXPECT_EQ(16, b.boxHeight);
}

TEST(RendererRateCompute, AScaleThatDoesNotDivideTheScreenStillCoversTheText)
{
	const RateBox b = ComputeRateBox(1280, 800, 20, 3);
	EXPECT_EQ(426, b.virtualWidth);
	EXPECT_EQ(266, b.virtualHeight);
	EXPECT_EQ(426 - 160, b.x);
	EXPECT_EQ(266 * 1280 / 426, b.boxLeft);   // [rc4l] where the draw puts the first glyph
	EXPECT_EQ(25, b.boxHeight);               // [rc4l] 8 * 800 / 266 = 24.06, rounded up
}

TEST(RendererRateCompute, TheScaleIsAtLeastOneAndNoLargerThanFits)
{
	EXPECT_EQ(1, ComputeRateBox(1280, 800, 20, 0).scale);
	EXPECT_EQ(1, ComputeRateBox(1280, 800, 20, -4).scale);
	EXPECT_EQ(8, ComputeRateBox(1280, 800, 20, 8).scale);
	EXPECT_EQ(4, ComputeRateBox(640, 480, 20, 8).scale);    // [rc4l] 20 glyphs of 32 is the whole width
	EXPECT_EQ(2, ComputeRateBox(320, 20, 2, 8).scale);      // [rc4l] and as tall as the screen allows
}

