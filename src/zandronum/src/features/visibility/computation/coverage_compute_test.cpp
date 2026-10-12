// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/visibility/computation/coverage_compute.h"

#include <gtest/gtest.h>

using namespace zx::visibility;

TEST(Coverage, ANodeIsCoveredWhenAnySubsectorBelowItIs)
{
	// [rc4l] Node 2 is the root over nodes 0 (subsectors 0, 1) and 1 (subsectors 2, 3).
	const CoverageNode nodes[3] = { { { ~0, ~1 } }, { { ~2, ~3 } }, { { 0, 1 } } };
	const unsigned char subs[4] = { 0, 0, 0, 1 };
	unsigned char covered[3] = { 9, 9, 9 };
	EXPECT_TRUE(ComputeNodeCoverage(nodes, 2, subs, covered));
	EXPECT_EQ(covered[0], 0);
	EXPECT_EQ(covered[1], 1);
	EXPECT_EQ(covered[2], 1);
	const unsigned char none[4] = { 0, 0, 0, 0 };
	EXPECT_FALSE(ComputeNodeCoverage(nodes, 2, none, covered));
	EXPECT_EQ(covered[1], 0);
	const unsigned char first[4] = { 1, 0, 0, 0 };
	EXPECT_TRUE(ComputeNodeCoverage(nodes, 2, first, covered));   // [rc4l] the front alone, and still both sides walked
	EXPECT_EQ(covered[0], 1);
	EXPECT_EQ(covered[1], 0);
}
