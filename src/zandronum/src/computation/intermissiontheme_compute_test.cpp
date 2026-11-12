// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/intermissiontheme_compute.h"

#include <gtest/gtest.h>

using zx::ComputeIntermissionThemeOrder;
using zx::ComputePlaysIntermissionTheme;

TEST(IntermissionTheme, ATeamsThemeBringsItsOwnTrack)
{
	EXPECT_EQ(ComputeIntermissionThemeOrder(true, 4, 2), 4);
	EXPECT_EQ(ComputeIntermissionThemeOrder(false, 4, 2), 2);
}

TEST(IntermissionTheme, OnlyTheMapsThemeNeedsItsLump)
{
	EXPECT_TRUE(ComputePlaysIntermissionTheme(false, false));
	EXPECT_TRUE(ComputePlaysIntermissionTheme(true, true));
	EXPECT_FALSE(ComputePlaysIntermissionTheme(true, false));
}
