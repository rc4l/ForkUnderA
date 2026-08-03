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

// [rc4l] The Cocoa collapse. Apple exposes three profile constants, so the 4.1/4.0/3.3 chain has to
// fold onto them without asking the OS for the same pixel format twice.

TEST(CocoaGLProfile, CoreRequestsMapToTheWidestProfileThatFits)
{
	EXPECT_EQ(zx::ComputeCocoaGLProfile(zx::GLContextRequest{4, 1, true}), zx::kNSGLProfileCore41);
	EXPECT_EQ(zx::ComputeCocoaGLProfile(zx::GLContextRequest{4, 0, true}), zx::kNSGLProfileCore32);
	EXPECT_EQ(zx::ComputeCocoaGLProfile(zx::GLContextRequest{3, 3, true}), zx::kNSGLProfileCore32);
	EXPECT_EQ(zx::ComputeCocoaGLProfile(zx::GLContextRequest{3, 2, true}), zx::kNSGLProfileCore32);
	EXPECT_EQ(zx::ComputeCocoaGLProfile(zx::GLContextRequest{5, 0, true}), zx::kNSGLProfileCore41);
}

TEST(CocoaGLProfile, BelowThreeTwoHasNoCoreProfileOnApple)
{
	EXPECT_EQ(zx::ComputeCocoaGLProfile(zx::GLContextRequest{3, 1, true}), zx::kNSGLProfileLegacy);
	EXPECT_EQ(zx::ComputeCocoaGLProfile(zx::GLContextRequest{2, 1, true}), zx::kNSGLProfileLegacy);
}

TEST(CocoaGLProfile, ACompatibilityRequestIsAlwaysLegacy)
{
	// Legacy IS the compatibility profile on Apple, so even a high version number maps down.
	EXPECT_EQ(zx::ComputeCocoaGLProfile(zx::GLContextRequest{4, 1, false}), zx::kNSGLProfileLegacy);
	EXPECT_EQ(zx::ComputeCocoaGLProfile(zx::GLContextRequest{3, 0, false}), zx::kNSGLProfileLegacy);
}

TEST(CocoaGLProfile, CoreChainDedupesAndEndsAtLegacy)
{
	int chain[zx::kMaxCocoaGLProfiles];
	const int n = zx::ComputeCocoaGLProfileChain(true, chain, zx::kMaxCocoaGLProfiles);
	ASSERT_EQ(n, 3);
	EXPECT_EQ(chain[0], zx::kNSGLProfileCore41);
	EXPECT_EQ(chain[1], zx::kNSGLProfileCore32);   // 4.0 and 3.3 collapse to one entry
	EXPECT_EQ(chain[2], zx::kNSGLProfileLegacy);
}

TEST(CocoaGLProfile, CompatibilityChainIsJustLegacyOnce)
{
	// 3.0 and 2.1 both map to Legacy, and the trailing fallback must not add a third.
	int chain[zx::kMaxCocoaGLProfiles];
	const int n = zx::ComputeCocoaGLProfileChain(false, chain, zx::kMaxCocoaGLProfiles);
	ASSERT_EQ(n, 1);
	EXPECT_EQ(chain[0], zx::kNSGLProfileLegacy);
}

TEST(CocoaGLProfileChain, RejectsNullOrTooSmall)
{
	int chain[zx::kMaxCocoaGLProfiles];
	EXPECT_EQ(zx::ComputeCocoaGLProfileChain(true, nullptr, zx::kMaxCocoaGLProfiles), 0);
	EXPECT_EQ(zx::ComputeCocoaGLProfileChain(true, chain, zx::kMaxCocoaGLProfiles - 1), 0);
}

} // namespace
