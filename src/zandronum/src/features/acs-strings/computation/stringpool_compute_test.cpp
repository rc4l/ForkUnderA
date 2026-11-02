// [Doruk-Aksoy] Tests for stringpool_compute: a collection that frees little doubles the pool's room, one that frees enough leaves it.
#include <gtest/gtest.h>

#include "features/acs-strings/computation/stringpool_compute.h"

using namespace zx::acsstrings;

// [Doruk-Aksoy] DnD's case, ~119 of a pool of thousands freed, doubles the room; a quarter or more freed adds none.
TEST(StringPool, AFewFreedDoublesTheRoom)
{
	EXPECT_EQ(2000u, ComputeStringPoolGrowth(119, 2000));
	EXPECT_EQ(2000u, ComputeStringPoolGrowth(0, 2000));
	EXPECT_EQ(2000u, ComputeStringPoolGrowth(499, 2000));
	EXPECT_EQ(0u, ComputeStringPoolGrowth(500, 2000));
	EXPECT_EQ(0u, ComputeStringPoolGrowth(2000, 2000));
}

// [Doruk-Aksoy] An empty pool has no room to double, and a count near the type's top does not wrap.
TEST(StringPool, EdgesNeitherGrowNorWrap)
{
	EXPECT_EQ(0u, ComputeStringPoolGrowth(0, 0));
	EXPECT_EQ(0u, ComputeStringPoolGrowth(0x40000000u, 0x80000000u));
	EXPECT_EQ(0x80000000u, ComputeStringPoolGrowth(0x1fffffffu, 0x80000000u));
}
