// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "features/telemetry/computation/shardedcounter_compute.h"

using namespace zx::telemetry;

// [Doruk-Aksoy] Threads take slots in turn and wrap past the last.
TEST(ShardedCounter, ThreadsTakeSlotsInTurn)
{
	EXPECT_EQ(0, ComputeCounterSlot(0, 64));
	EXPECT_EQ(5, ComputeCounterSlot(5, 64));
	EXPECT_EQ(0, ComputeCounterSlot(64, 64));
	EXPECT_EQ(3, ComputeCounterSlot(67, 64));
}

// [Doruk-Aksoy] Each slot sits on a cache line of its own, and the sum is every slot's adds.
TEST(ShardedCounter, SlotsAreALineApartAndSumTogether)
{
	static_assert(sizeof(ShardedCounter<4>::Slot) == 64, "one slot to a cache line");
	static ShardedCounter<4> c;
	EXPECT_EQ(0, c.Sum());
	c.Add(0, 2);
	c.Add(3, 5);
	c.Add(3, -1);
	EXPECT_EQ(6, c.Sum());
}

// [Doruk-Aksoy] Threads adding at once, several to a slot, lose nothing.
TEST(ShardedCounter, ConcurrentAddsAreAllCounted)
{
	static ShardedCounter<4> c;
	std::vector<std::thread> threads;
	for (int t = 0; t < 8; t++)
		threads.emplace_back([t]() { for (int i = 0; i < 10000; i++) c.Add(ComputeCounterSlot((unsigned)t, 4), 1); });
	for (std::thread &t : threads) t.join();
	EXPECT_EQ(80000, c.Sum());
}
