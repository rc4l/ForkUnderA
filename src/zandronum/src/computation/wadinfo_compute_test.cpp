// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/wadinfo_compute.h"

#include <gtest/gtest.h>

using namespace zx::wadinfo;

TEST(WadInfoIndexInRange, OnlyAnIndexBelowTheCountNamesAnEntry)
{
	EXPECT_TRUE(ComputeIndexInRange(0, 3));
	EXPECT_TRUE(ComputeIndexInRange(2, 3));
	EXPECT_FALSE(ComputeIndexInRange(3, 3));
	EXPECT_FALSE(ComputeIndexInRange(0, 0));
}

TEST(WadInfoIndexInRange, ANegativeOrHugeIndexNamesNone)
{
	EXPECT_FALSE(ComputeIndexInRange(-1, 3));
	EXPECT_FALSE(ComputeIndexInRange(0x40000000, 3));
}

TEST(WadInfoParentWad, AWadInsideAnotherNamesItsParent)
{
	EXPECT_EQ(ComputeParentWadInfo(4, 5), 4);
}

TEST(WadInfoParentWad, ATopLevelWadHasNoParent)
{
	EXPECT_EQ(ComputeParentWadInfo(4, 4), -1);
}
