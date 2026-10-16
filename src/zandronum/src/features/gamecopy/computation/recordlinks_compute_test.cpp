// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gamecopy/computation/recordlinks_compute.h"

#include <gtest/gtest.h>

#include <vector>

using namespace zx::gamecopy;

namespace {

// [rc4l] A list read back by following its links from first.
std::vector<int> Walk(int first, const std::vector<int> &next)
{
	std::vector<int> out;
	for (int r = first; r >= 0; r = next[r]) out.push_back(r);
	return out;
}

} // namespace

TEST(RecordLinks, ListsHoldTheirRecordsInTheOrderAdded)
{
	std::vector<int> next(6, 99);
	int a = 99, b = 99;
	RecordLinks links;
	links.Begin(a);
	links.Add(4, next[4]);
	links.Add(1, next[1]);
	links.Add(3, next[3]);
	links.Begin(b);
	links.Add(0, next[0]);
	EXPECT_EQ(Walk(a, next), (std::vector<int>{ 4, 1, 3 }));
	EXPECT_EQ(Walk(b, next), (std::vector<int>{ 0 }));
}

TEST(RecordLinks, AListNothingIsAddedToIsEmpty)
{
	int first = 7;
	RecordLinks links;
	links.Begin(first);
	EXPECT_EQ(first, -1);
}

TEST(RecordLinks, ARecordNamesOneOfTheCount)
{
	EXPECT_TRUE(ComputeHoldsRecord(0, 3));
	EXPECT_TRUE(ComputeHoldsRecord(2, 3));
	EXPECT_FALSE(ComputeHoldsRecord(3, 3));
	EXPECT_FALSE(ComputeHoldsRecord(-1, 3));
	EXPECT_FALSE(ComputeHoldsRecord(0, 0));
}
