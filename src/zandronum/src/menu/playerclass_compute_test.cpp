// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "menu/playerclass_compute.h"

#include <gtest/gtest.h>

using namespace zx::playerclass;

namespace {

// [rc4l] Four classes, the second and fourth forbidden.
bool SomeAllowed(int c) { return c == 0 || c == 2; }
bool AllAllowed(int) { return true; }
bool NoneAllowed(int) { return false; }

}

TEST(PlayerClassMenu, PreviewSkipsForbiddenClasses)
{
	EXPECT_EQ(2, ComputeNextPreviewClass(0, 4, SomeAllowed));
	EXPECT_EQ(0, ComputeNextPreviewClass(2, 4, SomeAllowed));
	EXPECT_EQ(2, ComputeNextPreviewClass(1, 4, SomeAllowed));
	EXPECT_EQ(1, ComputeNextPreviewClass(0, 4, AllAllowed));
	EXPECT_EQ(0, ComputeNextPreviewClass(3, 4, AllAllowed));
}

TEST(PlayerClassMenu, PreviewWithNoneAllowedStepsOnInsteadOfSpinning)
{
	EXPECT_EQ(1, ComputeNextPreviewClass(0, 4, NoneAllowed));
	EXPECT_EQ(0, ComputeNextPreviewClass(3, 4, NoneAllowed));
}

TEST(PlayerClassMenu, PreviewStartsFromTheFirstClass)
{
	EXPECT_EQ(0, ComputeNextPreviewClass(-1, 4, SomeAllowed));
	EXPECT_EQ(0, ComputeNextPreviewClass(9, 4, SomeAllowed));
	EXPECT_EQ(0, ComputeNextPreviewClass(0, 0, SomeAllowed));
}

TEST(PlayerClassMenu, RowsCountOnlyAllowedClasses)
{
	EXPECT_EQ(-1, ComputeClassOfMenuRow(0, false, 4, SomeAllowed));
	EXPECT_EQ(0, ComputeClassOfMenuRow(1, false, 4, SomeAllowed));
	EXPECT_EQ(2, ComputeClassOfMenuRow(2, false, 4, SomeAllowed));
	EXPECT_EQ(0, ComputeClassOfMenuRow(0, true, 4, SomeAllowed));
	EXPECT_EQ(2, ComputeClassOfMenuRow(1, true, 4, SomeAllowed));
}

TEST(PlayerClassMenu, ARowPastTheListNamesNoClass)
{
	EXPECT_EQ(NO_CLASS, ComputeClassOfMenuRow(3, false, 4, SomeAllowed));
	EXPECT_EQ(NO_CLASS, ComputeClassOfMenuRow(2, true, 4, SomeAllowed));
	EXPECT_EQ(NO_CLASS, ComputeClassOfMenuRow(0, true, 4, NoneAllowed));
}

TEST(PlayerClassMenu, AClassShowsAtItsAllowedRow)
{
	EXPECT_EQ(1, ComputeMenuRowOfClass(0, false, 4, SomeAllowed));
	EXPECT_EQ(2, ComputeMenuRowOfClass(2, false, 4, SomeAllowed));
	EXPECT_EQ(0, ComputeMenuRowOfClass(0, true, 4, SomeAllowed));
	EXPECT_EQ(1, ComputeMenuRowOfClass(2, true, 4, SomeAllowed));
}

TEST(PlayerClassMenu, RandomAndLeftOutClassesShowTheFirstRow)
{
	EXPECT_EQ(0, ComputeMenuRowOfClass(-1, false, 4, SomeAllowed));
	EXPECT_EQ(0, ComputeMenuRowOfClass(-1, true, 4, SomeAllowed));
	EXPECT_EQ(0, ComputeMenuRowOfClass(1, false, 4, SomeAllowed));
	EXPECT_EQ(0, ComputeMenuRowOfClass(4, false, 4, SomeAllowed));
}

TEST(PlayerClassMenu, OpeningReplacesAForbiddenClass)
{
	EXPECT_TRUE(ComputeClassNeedsReplacing(1, false, 4, SomeAllowed));
	EXPECT_TRUE(ComputeClassNeedsReplacing(4, false, 4, SomeAllowed));
	EXPECT_FALSE(ComputeClassNeedsReplacing(2, true, 4, SomeAllowed));
}

TEST(PlayerClassMenu, OpeningReplacesRandomOnlyWhereItIsNotOffered)
{
	EXPECT_TRUE(ComputeClassNeedsReplacing(-1, true, 4, SomeAllowed));
	EXPECT_FALSE(ComputeClassNeedsReplacing(-1, false, 4, SomeAllowed));
}
