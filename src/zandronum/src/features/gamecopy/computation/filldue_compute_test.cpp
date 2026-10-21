// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gamecopy/computation/filldue_compute.h"

#include <gtest/gtest.h>

using zx::gamecopy::ComputeFillDue;

TEST(FillDue, OnceATic)
{
	EXPECT_TRUE(ComputeFillDue(10, 9, false));
	EXPECT_FALSE(ComputeFillDue(10, 10, false));
}

TEST(FillDue, TheFirstFrameOfALevelFills)
{
	EXPECT_TRUE(ComputeFillDue(0, -1, false));
	EXPECT_TRUE(ComputeFillDue(57, -1, false));
}

TEST(FillDue, AClientFillsEveryFrame)
{
	EXPECT_TRUE(ComputeFillDue(10, 10, true));
}
