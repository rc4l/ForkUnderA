// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/mouse-capture/computation/mousecapture_compute.h"

#include <gtest/gtest.h>

namespace {

TEST(MouseCapture, AMenuHasTheCursorAndTheClicks)
{
	const zx::MouseCapture c = zx::ComputeMouseCapture(false, false);
	EXPECT_FALSE(c.readMotion);
	EXPECT_TRUE(c.takeClicks);
	EXPECT_TRUE(c.showCursor);
}

TEST(MouseCapture, PlayWithThePointerHeldAimsAndShoots)
{
	const zx::MouseCapture c = zx::ComputeMouseCapture(true, true);
	EXPECT_TRUE(c.readMotion);
	EXPECT_TRUE(c.takeClicks);
	EXPECT_FALSE(c.showCursor);
}

// [rc4l] Escape drops a page's pointer lock mid-play: positions then are not motion, and the click that takes the pointer back is not a shot.
TEST(MouseCapture, PlayWithThePointerLostNeitherAimsNorShoots)
{
	const zx::MouseCapture c = zx::ComputeMouseCapture(true, false);
	EXPECT_FALSE(c.readMotion);
	EXPECT_FALSE(c.takeClicks);
	EXPECT_TRUE(c.showCursor);
}

} // namespace
