// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/hwrender/computation/materialindex_compute.h"

using namespace zx::hwrender;

namespace {

const void *Mat(int n) { return reinterpret_cast<const void *>((size_t)(n + 1) * 64); }

}

TEST(ComputeMaterialIndex, FindsWhatWasInsertedByMaterialAndTranslation)
{
	MaterialIndex index;
	EXPECT_EQ(-1, ComputeMaterialIndexFind(index, Mat(0), 0));
	ComputeMaterialIndexInsert(index, Mat(0), 0, 7);
	ComputeMaterialIndexInsert(index, Mat(0), 3, 8);
	// [rc4l] The white slot's material is NULL, a key like any other.
	ComputeMaterialIndexInsert(index, nullptr, 0, 0);
	EXPECT_EQ(7, ComputeMaterialIndexFind(index, Mat(0), 0));
	EXPECT_EQ(8, ComputeMaterialIndexFind(index, Mat(0), 3));
	EXPECT_EQ(0, ComputeMaterialIndexFind(index, nullptr, 0));
	EXPECT_EQ(-1, ComputeMaterialIndexFind(index, Mat(1), 0));
	// [rc4l] A key already there keeps its first value.
	ComputeMaterialIndexInsert(index, Mat(0), 0, 99);
	EXPECT_EQ(7, ComputeMaterialIndexFind(index, Mat(0), 0));
	EXPECT_EQ(3u, index.count);
}

TEST(ComputeMaterialIndex, GrowsPastHalfFullAndKeepsEveryKey)
{
	MaterialIndex index;
	for (int i = 0; i < 5000; i++) ComputeMaterialIndexInsert(index, Mat(i), i & 7, i);
	EXPECT_EQ(5000u, index.count);
	EXPECT_GE(index.buckets.size(), 10000u);
	for (int i = 0; i < 5000; i++) ASSERT_EQ(i, ComputeMaterialIndexFind(index, Mat(i), i & 7)) << i;
	EXPECT_EQ(-1, ComputeMaterialIndexFind(index, Mat(5000), 0));
}

TEST(ComputeMaterialIndex, ClearingKeepsTheBucketsAndForgetsTheKeys)
{
	MaterialIndex index;
	for (int i = 0; i < 100; i++) ComputeMaterialIndexInsert(index, Mat(i), 0, i);
	const size_t buckets = index.buckets.size();
	ComputeMaterialIndexClear(index);
	EXPECT_EQ(0u, index.count);
	EXPECT_EQ(buckets, index.buckets.size());
	EXPECT_EQ(-1, ComputeMaterialIndexFind(index, Mat(5), 0));
	ComputeMaterialIndexInsert(index, Mat(5), 0, 1);
	EXPECT_EQ(1, ComputeMaterialIndexFind(index, Mat(5), 0));
}

TEST(ComputeMaterialIndex, KeysThatShareABucketProbeOnward)
{
	MaterialIndex index;
	ComputeMaterialIndexInsert(index, Mat(0), 0, 1);
	const unsigned size = (unsigned)index.buckets.size();
	const unsigned home = ComputeMaterialIndexHash(Mat(0), 0, size);
	int other = 1;
	while (ComputeMaterialIndexHash(Mat(other), 0, size) != home) other++;
	ComputeMaterialIndexInsert(index, Mat(other), 0, 2);
	EXPECT_EQ(1, ComputeMaterialIndexFind(index, Mat(0), 0));
	EXPECT_EQ(2, ComputeMaterialIndexFind(index, Mat(other), 0));
	EXPECT_LT(ComputeMaterialIndexHash(Mat(0), 0, size), size);
}
