// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Tests for the TID hash's bucket.
#include "gtest/gtest.h"
#include "features/tid-hash/computation/tidhash_compute.h"

#include <climits>
#include <set>

// [Doruk-Aksoy] Every TID lands in the table, negative and extreme ones included.
TEST(TidHashCompute, EveryTidLandsInRange)
{
	const int tids[] = { 0, 1, -1, 127, 128, kZxTidHashSize - 1, kZxTidHashSize, -kZxTidHashSize, INT_MAX, INT_MIN };
	for (int tid : tids)
	{
		EXPECT_GE(ComputeTidBucket(tid), 0) << tid;
		EXPECT_LT(ComputeTidBucket(tid), kZxTidHashSize) << tid;
	}
	EXPECT_EQ(ComputeTidBucket(-1), kZxTidHashSize - 1);
}

// [Doruk-Aksoy] One TID always maps to one bucket, so all actors sharing it keep one chain in insertion order.
TEST(TidHashCompute, OneTidOneBucket)
{
	EXPECT_EQ(ComputeTidBucket(5074), ComputeTidBucket(5074));
	EXPECT_EQ(ComputeTidBucket(5074 + kZxTidHashSize), ComputeTidBucket(5074));
}

// [Doruk-Aksoy] A run of consecutive TIDs as large as the table, as a mod numbers its monsters, fills every bucket once, where 128 buckets chained them 128 deep.
TEST(TidHashCompute, ConsecutiveTidsSpreadOverEveryBucket)
{
	std::set<int> buckets;
	for (int tid = 1000; tid < 1000 + kZxTidHashSize; tid++) buckets.insert(ComputeTidBucket(tid));
	EXPECT_EQ((int)buckets.size(), kZxTidHashSize);
}
