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

// [rc4l] The spin reads back as set, and a negative one is no spin at all.
TEST(JobPool, TheSpinReadsBackAndIsNeverNegative)
{
	JobPool pool(0, 50);
	EXPECT_EQ(50, pool.SpinMicros());
	pool.SetSpinMicros(300);
	EXPECT_EQ(300, pool.SpinMicros());
	pool.SetSpinMicros(-5);
	EXPECT_EQ(0, pool.SpinMicros());
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
	EXPECT_EQ(0, ComputeAutoWorkers(1, 7, 1));
	EXPECT_EQ(0, ComputeAutoWorkers(2, 7, 1));
	EXPECT_EQ(1, ComputeAutoWorkers(4, 7, 1));
	EXPECT_EQ(3, ComputeAutoWorkers(8, 7, 1));
	EXPECT_EQ(7, ComputeAutoWorkers(32, 7, 1));
	EXPECT_EQ(0, ComputeAutoWorkers(32, 0, 1));
	// [rc4l] The game thread takes a core of its own from the pool.
	EXPECT_EQ(2, ComputeAutoWorkers(8, 7, 2));
	EXPECT_EQ(0, ComputeAutoWorkers(4, 7, 2));
}

namespace {

std::atomic<int> s_started{ 0 }, s_ran{ 0 }, s_waited{ 0 };
std::atomic<int64_t> s_ranTicks{ 0 }, s_clock{ 0 };

// [rc4l] A clock that ticks once each time it is read, so every timed span is at least one tick.
int64_t Ticks() { return ++s_clock; }
void HeardStart(int) { s_started++; }
void HeardRun(int, int64_t start, int64_t end) { s_ran++; s_ranTicks += end - start; }
void HeardWait(int64_t start, int64_t end) { if (end >= start) s_waited++; }

const zx::jobs::JobObserver kHeard = { Ticks, HeardStart, HeardRun, HeardWait };

} // namespace

// [rc4l] features/telemetry hears each worker once, each lane's run of items with its time, and the caller's wait.
TEST(JobPool, TheObserverHearsWorkersRunsAndTheWait)
{
	JobPool pool(2, 0);
	zx::jobs::SetJobObserver(&kHeard);
	struct Slow { void operator()(int, int) const { std::this_thread::sleep_for(std::chrono::milliseconds(2)); } } slow;
	pool.ParallelFor(24, slow);
	pool.ParallelFor(24, slow);
	zx::jobs::SetJobObserver(nullptr);
	pool.ParallelFor(24, slow);
	EXPECT_EQ(s_waited.load(), 2);
	EXPECT_GE(s_ran.load(), 2);
	EXPECT_GT(s_ranTicks.load(), 0);
	EXPECT_GE(s_started.load(), 1);
	EXPECT_LE(s_started.load(), 2);
}

// [rc4l] The copy's change scans split 62,000 sides into chunks; every item must fall in exactly one.
TEST(JobPool, ChunksCoverEveryItemOnce)
{
	EXPECT_EQ(zx::jobs::ComputeChunkCount(0, 512), 0);
	EXPECT_EQ(zx::jobs::ComputeChunkCount(-3, 512), 0);
	EXPECT_EQ(zx::jobs::ComputeChunkCount(1, 512), 1);
	EXPECT_EQ(zx::jobs::ComputeChunkCount(512, 512), 1);
	EXPECT_EQ(zx::jobs::ComputeChunkCount(513, 512), 2);
	EXPECT_EQ(zx::jobs::ComputeChunkCount(10, 0), 1);
	int lo = -1, hi = -1;
	zx::jobs::ComputeChunkBounds(1030, 512, 2, lo, hi);
	EXPECT_EQ(lo, 1024);
	EXPECT_EQ(hi, 1030);
	zx::jobs::ComputeChunkBounds(1030, 512, 5, lo, hi);
	EXPECT_EQ(lo, hi);
	zx::jobs::ComputeChunkBounds(10, 0, 0, lo, hi);
	EXPECT_EQ(lo, 0);
	EXPECT_EQ(hi, 10);
	zx::jobs::ComputeChunkBounds(-4, 0, 0, lo, hi);
	EXPECT_EQ(hi, 0);

	JobPool pool(3, 0);
	std::vector<std::atomic<int>> seen(2049);
	struct Mark { std::vector<std::atomic<int>> &seen; void operator()(int a, int b, int) const { for (int i = a; i < b; i++) seen[i]++; } } mark = { seen };
	pool.ParallelChunks(2049, 100, mark);
	for (int i = 0; i < 2049; i++) EXPECT_EQ(seen[i].load(), 1) << i;
	pool.ParallelChunks(0, 100, mark);
}
