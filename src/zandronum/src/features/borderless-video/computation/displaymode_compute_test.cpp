// [rc4l] Tests for displaymode_compute. 100% coverage of the pure decision logic.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "displaymode_compute.h"
#include "gtest/gtest.h"

using namespace zx;

TEST(DisplayMode, WindowKindForFullscreen)
{
	EXPECT_EQ(WINDOW_NORMAL,             WindowKindForFullscreen(false));
	EXPECT_EQ(WINDOW_BORDERLESS_DESKTOP, WindowKindForFullscreen(true));
}

TEST(DisplayMode, BorderlessRectCoversDisplay)
{
	int x = -1, y = -1, w = -1, h = -1;
	BorderlessWindowRect(0, 0, 1920, 1080, &x, &y, &w, &h);
	EXPECT_EQ(0, x);   EXPECT_EQ(0, y);
	EXPECT_EQ(1920, w); EXPECT_EQ(1080, h);
}

TEST(DisplayMode, BorderlessRectHonorsSecondMonitorOrigin)
{
	int x = 0, y = 0, w = 0, h = 0;
	// a second monitor offset to the right of a 1920-wide primary
	BorderlessWindowRect(1920, 120, 2560, 1440, &x, &y, &w, &h);
	EXPECT_EQ(1920, x); EXPECT_EQ(120, y);
	EXPECT_EQ(2560, w); EXPECT_EQ(1440, h);
}

TEST(DisplayMode, BorderlessRectToleratesNullOutParams)
{
	// must not crash if a caller only wants some fields
	BorderlessWindowRect(0, 0, 800, 600, nullptr, nullptr, nullptr, nullptr);
	int w = 0;
	BorderlessWindowRect(0, 0, 800, 600, nullptr, nullptr, &w, nullptr);
	EXPECT_EQ(800, w);
}
