// SPDX-License-Identifier: GPL-3.0-or-later

#include "features/handoff/computation/lumpcache_compute.h"

#include <gtest/gtest.h>

using zx::handoff::ComputeLumpCacheStale;

TEST(LumpCacheCompute, NothingKeptYetIsStale)
{
	EXPECT_TRUE(ComputeLumpCacheStale(-1, -1, -1, 0, 0, 0));
	EXPECT_TRUE(ComputeLumpCacheStale(-1, 2, 4096, 17, 2, 4096));
}

TEST(LumpCacheCompute, TheSameLumpFileAndLengthIsKept)
{
	EXPECT_FALSE(ComputeLumpCacheStale(17, 2, 4096, 17, 2, 4096));
	EXPECT_FALSE(ComputeLumpCacheStale(0, 0, 1, 0, 0, 1));
}

TEST(LumpCacheCompute, AnotherNumberFileOrLengthIsStale)
{
	EXPECT_TRUE(ComputeLumpCacheStale(17, 2, 4096, 18, 2, 4096));
	EXPECT_TRUE(ComputeLumpCacheStale(17, 2, 4096, 17, 3, 4096));
	EXPECT_TRUE(ComputeLumpCacheStale(17, 2, 4096, 17, 2, 4097));
}
