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
// [rc4l] WDI07's waterfall: re-baked at another WFALL frame, its base texture is still the batch's, so it is patched rather than rebuilding the scene four times a second.
// [rc4l] Keys scattered through the list come together, each group in index order, so a base texture is asked once for all its batches.
TEST(BatchCompute, GroupsGatherEqualKeysWhereverTheyStand)
{
	const uint64_t keys[6] = { 7, 3, 7, 9, 3, 7 };
	std::vector<int> order;
	std::vector<BatchRun> groups;
	ComputeGroupByKey(keys, 6, order, groups);
	const int want[6] = { 1, 4, 0, 2, 5, 3 };
	ASSERT_EQ(6u, order.size());
	for (int i = 0; i < 6; i++) EXPECT_EQ(want[i], order[i]) << i;
	ASSERT_EQ(3u, groups.size());
	EXPECT_EQ(0, groups[0].start); EXPECT_EQ(2, groups[0].count);
	EXPECT_EQ(2, groups[1].start); EXPECT_EQ(3, groups[1].count);
	EXPECT_EQ(5, groups[2].start); EXPECT_EQ(1, groups[2].count);
	ComputeGroupByKey(keys, 0, order, groups);
	EXPECT_TRUE(order.empty());
	EXPECT_TRUE(groups.empty());
}

TEST(BatchCompute, AnotherFrameOfTheSameBaseStaysButANewTextureDoesNot)
{
	int frame1, frame2, base, other;
	EXPECT_TRUE(ComputeStaysInBatch(&frame1, &base, &frame1, &base));
	EXPECT_TRUE(ComputeStaysInBatch(&frame2, &base, &frame1, &base));
	EXPECT_FALSE(ComputeStaysInBatch(&frame2, &other, &frame1, &base));   // [rc4l] a pressed switch
	EXPECT_FALSE(ComputeStaysInBatch(&frame2, nullptr, &frame1, nullptr));
	EXPECT_TRUE(ComputeStaysInBatch(&frame1, nullptr, &frame1, nullptr));
}
} // namespace
