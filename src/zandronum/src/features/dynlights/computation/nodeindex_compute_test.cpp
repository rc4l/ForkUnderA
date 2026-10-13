// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/dynlights/computation/nodeindex_compute.h"

#include <gtest/gtest.h>

#include <vector>

using namespace zx::dynlights;

TEST(NodeIndex, AnUnbuiltIndexFindsNothing)
{
	PointerIndex ix = {};
	int x = 0;
	EXPECT_EQ(NULL, ComputePointerIndexFind(ix, &x));
}

// [rc4l] A light's subsectors, sides and sector, each found by what it links to.
TEST(NodeIndex, PutKeysAreFoundAndOthersAreNot)
{
	PointerIndex ix = {};
	ComputePointerIndexReset(ix, 4);
	int targets[3] = {}, nodes[3] = {}, other = 0;
	for (int i = 0; i < 3; i++) ComputePointerIndexPut(ix, &targets[i], &nodes[i]);
	for (int i = 0; i < 3; i++) EXPECT_EQ(&nodes[i], ComputePointerIndexFind(ix, &targets[i]));
	EXPECT_EQ(NULL, ComputePointerIndexFind(ix, &other));
	ComputePointerIndexPut(ix, &targets[1], &nodes[2]);
	EXPECT_EQ(&nodes[2], ComputePointerIndexFind(ix, &targets[1]));
	EXPECT_EQ(3, ix.used);
}

TEST(NodeIndex, AResetForgetsEverything)
{
	PointerIndex ix = {};
	ComputePointerIndexReset(ix, 4);
	int t = 0, n = 0;
	ComputePointerIndexPut(ix, &t, &n);
	ComputePointerIndexReset(ix, 1);
	EXPECT_EQ(NULL, ComputePointerIndexFind(ix, &t));
	EXPECT_EQ(0, ix.used);
}

// [rc4l] A light that reaches far past the guess: the table grows and keeps every entry.
TEST(NodeIndex, ItGrowsPastWhatItWasSizedFor)
{
	PointerIndex ix = {};
	ComputePointerIndexReset(ix, 2);
	EXPECT_EQ(15u, ix.mask);
	std::vector<int> targets(1000), nodes(1000);
	for (int i = 0; i < 1000; i++) ComputePointerIndexPut(ix, &targets[i], &nodes[i]);
	EXPECT_EQ(2047u, ix.mask);
	for (int i = 0; i < 1000; i++) EXPECT_EQ(&nodes[i], ComputePointerIndexFind(ix, &targets[i]));
	ComputePointerIndexReset(ix, 10);
	EXPECT_EQ(2047u, ix.mask);
	EXPECT_EQ(NULL, ComputePointerIndexFind(ix, &targets[5]));
}
