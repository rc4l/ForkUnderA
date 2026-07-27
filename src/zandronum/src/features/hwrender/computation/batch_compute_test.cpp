// [rc4l] Tests for draw-run coalescing (hwrender).
#include "gtest/gtest.h"
#include "features/hwrender/computation/batch_compute.h"

using namespace hwrender;

namespace
{
TEST(Batch, EmptyIsZeroRuns)
{
	BatchRun runs[4];
	EXPECT_EQ(ComputeBatchRuns(nullptr, 0, runs, 4), 0);
}

TEST(Batch, AllSameKeyIsOneRun)
{
	const uint64_t keys[] = {5, 5, 5};
	BatchRun runs[4];
	EXPECT_EQ(ComputeBatchRuns(keys, 3, runs, 4), 1);
	EXPECT_EQ(runs[0].start, 0);
	EXPECT_EQ(runs[0].count, 3);
}

TEST(Batch, AllDistinctKeysAreSeparateRuns)
{
	const uint64_t keys[] = {1, 2, 3};
	BatchRun runs[4];
	EXPECT_EQ(ComputeBatchRuns(keys, 3, runs, 4), 3);
	for (int i = 0; i < 3; i++)
	{
		EXPECT_EQ(runs[i].start, i);
		EXPECT_EQ(runs[i].count, 1);
	}
}

TEST(Batch, MixedRunsCoalesceAdjacentEqual)
{
	const uint64_t keys[] = {1, 1, 2, 3, 3, 3};
	BatchRun runs[8];
	ASSERT_EQ(ComputeBatchRuns(keys, 6, runs, 8), 3);
	EXPECT_EQ(runs[0].start, 0); EXPECT_EQ(runs[0].count, 2);
	EXPECT_EQ(runs[1].start, 2); EXPECT_EQ(runs[1].count, 1);
	EXPECT_EQ(runs[2].start, 3); EXPECT_EQ(runs[2].count, 3);
}

TEST(Batch, TruncatesToMaxRunsButReportsWritten)
{
	const uint64_t keys[] = {1, 2, 3, 4};
	BatchRun runs[2];
	// [rc4l] Only 2 slots for 4 runs -> writes 2, returns 2.
	EXPECT_EQ(ComputeBatchRuns(keys, 4, runs, 2), 2);
	EXPECT_EQ(runs[0].start, 0);
	EXPECT_EQ(runs[1].start, 1);
}
} // namespace
