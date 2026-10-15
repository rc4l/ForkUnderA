// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/queryring_compute.h"

#include <gtest/gtest.h>

#include <vector>

using zx::hwrender::QueryRing;

TEST(QueryRing, SlotsAreReadOldestFirstAndOnlyOnceTheirResultIsIn)
{
	QueryRing ring;
	EXPECT_EQ(ring.Begin(), 0);
	EXPECT_EQ(ring.Begin(), 1);
	EXPECT_EQ(ring.Begin(), 2);
	std::vector<int> read;
	// [rc4l] Slot 0 is in, slot 1 is not, so slot 2 waits behind it though it is in.
	EXPECT_EQ(ring.Collect([&](int s) { if (s == 1) return false; read.push_back(s); return true; }), 1);
	EXPECT_EQ(read, std::vector<int>{ 0 });
	EXPECT_EQ(ring.Collect([&](int s) { read.push_back(s); return true; }), 2);
	EXPECT_EQ(read, (std::vector<int>{ 0, 1, 2 }));
	EXPECT_EQ(ring.Collect([](int) { return true; }), 0);
}

TEST(QueryRing, AFullRingSkipsTimingUntilASlotIsReadAndWrapsAround)
{
	QueryRing ring;
	for (int i = 0; i < QueryRing::kSlots; i++) EXPECT_EQ(ring.Begin(), i);
	EXPECT_EQ(ring.Begin(), -1);
	EXPECT_EQ(ring.Collect([](int s) { return s == 0; }), 1);
	EXPECT_EQ(ring.Begin(), 0);
	EXPECT_EQ(ring.Begin(), -1);
}

TEST(QueryRing, TicksAtTheirFrequencyAreMilliseconds)
{
	EXPECT_DOUBLE_EQ(zx::hwrender::ComputeQueryMs(1500000, 1000000000), 1.5);
	EXPECT_DOUBLE_EQ(zx::hwrender::ComputeQueryMs(5, 0), -1.0);
}
