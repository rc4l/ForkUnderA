// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>
#include "features/glportal/computation/glportal_compute.h"

using namespace zx::glportal;

// [rc4l] A stacked room wider than the room under it overflowed the stack: each stacked view drew the next first, at DrawScene recursion 0 every time.
TEST(GLPortalCompute, AStackedViewIsDrawnFirstOnlyInThePlayersOwnScene)
{
	EXPECT_TRUE(ComputeDrawsSkyPortalFirst(true, 0, 1));
	EXPECT_FALSE(ComputeDrawsSkyPortalFirst(true, 0, 2));   // [rc4l] inside a portal drawn first
	EXPECT_FALSE(ComputeDrawsSkyPortalFirst(true, 1, 2));   // [rc4l] inside a portal EndFrame drew
	EXPECT_FALSE(ComputeDrawsSkyPortalFirst(true, 1, 1));
}

TEST(GLPortalCompute, ASkyWithoutADepthBufferIsDrawnFirstAtAnyDepth)
{
	EXPECT_TRUE(ComputeDrawsSkyPortalFirst(false, 0, 1));
	EXPECT_TRUE(ComputeDrawsSkyPortalFirst(false, 0, 5));
	EXPECT_TRUE(ComputeDrawsSkyPortalFirst(false, 3, 4));
}
