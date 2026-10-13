// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/jobs/computation/jobpool_compute.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using zx::jobs::JobPool;
using zx::jobs::ComputeAutoWorkers;

namespace {

struct Tally
{
	std::vector<std::atomic<int> > runs;
	std::vector<std::atomic<int> > lanes;
	explicit Tally(int n) : runs(n), lanes(n) { for (int i = 0; i < n; i++) { runs[i] = 0; lanes[i] = -1; } }
};

void Count(void *user, int index, int lane)
{
	Tally *t = (Tally *)user;
	t->runs[index]++;
	t->lanes[index] = lane;
}

// [rc4l] Wait until every worker has spun out and gone to sleep, failing the test after two seconds.
bool AllAsleep(const JobPool &pool)
{
	for (int i = 0; i < 2000; i++)
	{
		if (pool.Sleeping() == pool.Workers()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return false;
}

} // namespace

TEST(JobPool, WithoutWorkersRunsEachIndexInOrderOnTheCaller)
{
	JobPool pool;
	EXPECT_EQ(0, pool.Workers());
	EXPECT_EQ(1, pool.Lanes());
	std::vector<int> order;
	auto f = [&order](int index, int lane) { order.push_back(index * 10 + lane); };
	pool.ParallelFor(4, f);
	ASSERT_EQ(4u, order.size());
	EXPECT_EQ(0, order[0]);
	EXPECT_EQ(10, order[1]);
	EXPECT_EQ(30, order[3]);
}

TEST(JobPool, EveryIndexRunsExactlyOnceOnSomeLane)
{
	JobPool pool(3, 100000);
	EXPECT_EQ(4, pool.Lanes());
	Tally t(1000);
	pool.ParallelFor(1000, Count, &t);
	for (int i = 0; i < 1000; i++)
	{
		EXPECT_EQ(1, t.runs[i].load()) << i;
		EXPECT_GE(t.lanes[i].load(), 0);
		EXPECT_LT(t.lanes[i].load(), 4);
	}
}

TEST(JobPool, NoIndexOrOneRunsOnTheCaller)
{
	JobPool pool(2, 100000);
	Tally t(1);
	pool.ParallelFor(0, Count, &t);
	EXPECT_EQ(0, t.runs[0].load());
	pool.ParallelFor(1, Count, &t);
	EXPECT_EQ(1, t.runs[0].load());
	EXPECT_EQ(0, t.lanes[0].load());
}

TEST(JobPool, SleepingWorkersWakeForTheNextJob)
{
	JobPool pool(2, 0);
	ASSERT_TRUE(AllAsleep(pool));
	std::atomic<int> sum(0);
	auto f = [&sum](int index, int) { std::this_thread::sleep_for(std::chrono::microseconds(200)); sum += index; };
	pool.ParallelFor(32, f);
	EXPECT_EQ(31 * 32 / 2, sum.load());
	ASSERT_TRUE(AllAsleep(pool));
	pool.ParallelFor(32, f);
	EXPECT_EQ(31 * 32, sum.load());
}

TEST(JobPool, BackToBackJobsEachFinishBeforeTheNext)
{
	JobPool pool(4, 100000);
	for (int round = 0; round < 500; round++)
	{
		std::atomic<int> ran(0);
		auto f = [&ran](int, int) { ran++; };
		pool.ParallelFor(8 + round % 5, f);
		ASSERT_EQ(8 + round % 5, ran.load()) << round;
	}
}

TEST(JobPool, ResizeReplacesTheWorkers)
{
	JobPool pool(1, 0);
	pool.Resize(3);
	EXPECT_EQ(3, pool.Workers());
	Tally t(64);
	pool.ParallelFor(64, Count, &t);
	for (int i = 0; i < 64; i++) EXPECT_EQ(1, t.runs[i].load());
	pool.Resize(0);
	EXPECT_EQ(0, pool.Workers());
	pool.ParallelFor(64, Count, &t);
	for (int i = 0; i < 64; i++) EXPECT_EQ(0, t.lanes[i].load());
}

TEST(JobPool, StopsWorkersWhetherSpinningOrAsleep)
{
	{
		JobPool spinning(2, 10000000);
	}
	{
		JobPool sleeping(2, 0);
		ASSERT_TRUE(AllAsleep(sleeping));
	}
	SUCCEED();
}

TEST(JobPool, AutoWorkersTakeHalfTheMachineLessTheCallerUpToTheCap)
{
	EXPECT_EQ(0, ComputeAutoWorkers(1, 7));
	EXPECT_EQ(0, ComputeAutoWorkers(2, 7));
	EXPECT_EQ(1, ComputeAutoWorkers(4, 7));
	EXPECT_EQ(3, ComputeAutoWorkers(8, 7));
	EXPECT_EQ(7, ComputeAutoWorkers(32, 7));
	EXPECT_EQ(0, ComputeAutoWorkers(32, 0));
}
