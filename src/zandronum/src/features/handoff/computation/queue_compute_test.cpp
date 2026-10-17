// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/handoff/computation/queue_compute.h"

#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

namespace {

struct Seen { std::vector<std::string> names; std::vector<size_t> depths; };

void Note(const zx::QueueBase &q, void *user)
{
	Seen *s = (Seen *)user;
	s->names.push_back(q.Name());
	s->depths.push_back(q.Depth());
}

} // namespace

TEST(Queue, ItemsDrainOldestFirstAndTheDepthEmpties)
{
	zx::Queue<std::string> q("console text");
	q.Push("a");
	q.Push("b");
	EXPECT_EQ(2u, q.Depth());
	std::vector<std::string> got;
	EXPECT_EQ(2u, q.Drain([&](const std::string &s) { got.push_back(s); }));
	ASSERT_EQ(2u, got.size());
	EXPECT_EQ("a", got[0]);
	EXPECT_EQ("b", got[1]);
	EXPECT_EQ(0u, q.Depth());
	EXPECT_EQ(2u, q.HighWater());
	EXPECT_EQ(0u, q.Drain([&](const std::string &) { FAIL(); }));
}

TEST(Queue, ATakerMayPushAgainAndThoseWaitForTheNextDrain)
{
	zx::Queue<int> q("effects");
	q.Push(1);
	int taken = 0;
	q.Drain([&](int v) { taken += v; q.Push(v + 1); });
	EXPECT_EQ(1, taken);
	EXPECT_EQ(1u, q.Depth());
	q.Drain([&](int v) { taken += v; });
	EXPECT_EQ(3, taken);
}

TEST(Queue, EveryLiveQueueIsListedWithItsDepth)
{
	Seen before;
	zx::QueueBase::ForEach(Note, &before);
	{
		zx::Queue<int> q("commands");
		q.Push(5);
		Seen during;
		zx::QueueBase::ForEach(Note, &during);
		ASSERT_EQ(before.names.size() + 1, during.names.size());
		EXPECT_EQ("commands", during.names.back());
		EXPECT_EQ(1u, during.depths.back());
	}
	Seen after;
	zx::QueueBase::ForEach(Note, &after);
	EXPECT_EQ(before.names.size(), after.names.size());
}

TEST(Queue, TwoThreadsPushingLoseNothing)
{
	zx::Queue<int> q("both threads");
	std::thread a([&] { for (int i = 0; i < 5000; i++) q.Push(1); });
	std::thread b([&] { for (int i = 0; i < 5000; i++) q.Push(2); });
	a.join();
	b.join();
	int sum = 0;
	EXPECT_EQ(10000u, q.Drain([&](int v) { sum += v; }));
	EXPECT_EQ(15000, sum);
}
