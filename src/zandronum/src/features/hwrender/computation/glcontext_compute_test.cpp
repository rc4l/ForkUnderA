// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/hwrender/computation/glcontext_compute.h"

#include <gtest/gtest.h>

namespace
{

TEST(GLContext, CoreChainIsHighestFirstAllCore)
{
	zx::GLContextRequest reqs[zx::kMaxGLContextRequests];
	const int n = zx::ComputeGLContextRequests(true, reqs, zx::kMaxGLContextRequests);
	ASSERT_EQ(n, 3);
	EXPECT_EQ(reqs[0].major, 4);
	EXPECT_EQ(reqs[0].minor, 1);
	EXPECT_EQ(reqs[1].major, 4);
	EXPECT_EQ(reqs[1].minor, 0);
	EXPECT_EQ(reqs[2].major, 3);
	EXPECT_EQ(reqs[2].minor, 3);
	for (int i = 0; i < n; ++i)
	{
		EXPECT_TRUE(reqs[i].coreProfile);
	}
	// Strictly descending so the driver hands back the best it supports.
	EXPECT_GT(reqs[0].major * 10 + reqs[0].minor, reqs[1].major * 10 + reqs[1].minor);
	EXPECT_GT(reqs[1].major * 10 + reqs[1].minor, reqs[2].major * 10 + reqs[2].minor);
}

TEST(GLContext, CompatChainIsHighestFirstAllCompat)
{
	zx::GLContextRequest reqs[zx::kMaxGLContextRequests];
	const int n = zx::ComputeGLContextRequests(false, reqs, zx::kMaxGLContextRequests);
	ASSERT_EQ(n, 2);
	EXPECT_EQ(reqs[0].major, 3);
	EXPECT_EQ(reqs[0].minor, 0);
	EXPECT_EQ(reqs[1].major, 2);
	EXPECT_EQ(reqs[1].minor, 1);
	for (int i = 0; i < n; ++i)
	{
		EXPECT_FALSE(reqs[i].coreProfile);
	}
}

TEST(GLContext, RejectsNullOrTooSmallBuffer)
{
	EXPECT_EQ(zx::ComputeGLContextRequests(true, nullptr, zx::kMaxGLContextRequests), 0);

	zx::GLContextRequest reqs[zx::kMaxGLContextRequests];
	EXPECT_EQ(zx::ComputeGLContextRequests(true, reqs, zx::kMaxGLContextRequests - 1), 0);
}

} // namespace
