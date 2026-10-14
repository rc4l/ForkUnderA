// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/jobs/computation/sharedarray_compute.h"
#include "features/jobs/computation/jobpool_compute.h"

#include <gtest/gtest.h>

using zx::jobs::SharedArray;
using zx::jobs::JobPool;

TEST(SharedArray, HoldsWhatIsStoredAndFillsAndClears)
{
	SharedArray<unsigned> a;
	EXPECT_EQ(0u, a.Size());
	a.Resize(4, 7);
	EXPECT_EQ(4u, a.Size());
	EXPECT_EQ(7u, a.Load(3));
	a.Store(1, 9);
	EXPECT_EQ(9u, a.Load(1));
	a.Or(1, 0x10);
	EXPECT_EQ(0x19u, a.Load(1));
	a.Fill(2);
	EXPECT_EQ(2u, a.Load(1));
	a.Clear();
	EXPECT_EQ(0u, a.Size());
}

TEST(SharedArray, LowerWithinKeepsTheSmallestOfAnEpochAndReplacesAnOlderOne)
{
	const unsigned long long epoch = 0xff00000000000000ull;
	SharedArray<unsigned long long> a;
	a.Resize(1, 0);
	// [rc4l] An element from another epoch takes the new value even when it is larger.
	a.LowerWithin(0, 0x0100000000000050ull, epoch);
	EXPECT_EQ(0x0100000000000050ull, a.Load(0));
	a.LowerWithin(0, 0x0100000000000060ull, epoch);
	EXPECT_EQ(0x0100000000000050ull, a.Load(0));
	a.LowerWithin(0, 0x0100000000000040ull, epoch);
	EXPECT_EQ(0x0100000000000040ull, a.Load(0));
	a.LowerWithin(0, 0x0200000000000070ull, epoch);
	EXPECT_EQ(0x0200000000000070ull, a.Load(0));
}

TEST(SharedArray, LanesRacingLeaveTheSmallestKeyAndEveryBit)
{
	JobPool pool(7, 100000);
	SharedArray<unsigned long long> key;
	SharedArray<unsigned> bits;
	key.Resize(1, 0);
	bits.Resize(1, 0);
	auto f = [&](int index, int)
	{
		key.LowerWithin(0, (1ull << 40) | (unsigned long long)(1000 - index), 0xffffff0000000000ull);
		bits.Or(0, 1u << (index % 32));
	};
	pool.ParallelFor(1000, f);
	EXPECT_EQ((1ull << 40) | 1ull, key.Load(0));
	EXPECT_EQ(0xffffffffu, bits.Load(0));
}
