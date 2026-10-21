// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>
#include "features/levelmesh/computation/linequery_compute.h"

using namespace zx::levelmesh;

TEST(LineQueryCompute, AKeyArgumentGivesTheTextAfterItsEqualsSign)
{
	EXPECT_STREQ("WFALL", ComputeKeyArgValue("texture=WFALL", "texture"));
	EXPECT_STREQ("", ComputeKeyArgValue("tag=", "tag"));
	EXPECT_EQ(nullptr, ComputeKeyArgValue("tagged=3", "tag"));   // [rc4l] a longer key that starts the same
	EXPECT_EQ(nullptr, ComputeKeyArgValue("tag", "tag"));
	EXPECT_EQ(nullptr, ComputeKeyArgValue("limit=8", "tag"));
}

// [rc4l] WDI07's waterfall cycles WFALL1..WFALL4, written in whatever case the map used.
TEST(LineQueryCompute, ATexturePrefixMatchesEveryFrameInAnyCase)
{
	EXPECT_TRUE(ComputeHasNamePrefix("WFALL3", "wfall"));
	EXPECT_TRUE(ComputeHasNamePrefix("WFALL3", ""));
	EXPECT_FALSE(ComputeHasNamePrefix("WFAL", "WFALL"));   // [rc4l] shorter than the prefix
	EXPECT_FALSE(ComputeHasNamePrefix("BRICK1", "WFALL"));
}
