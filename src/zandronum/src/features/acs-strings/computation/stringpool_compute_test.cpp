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

// [Doruk-Aksoy] A literal's pool id is found again under the generation it was cached in, and nothing is found before.
TEST(TagCache, FindsWhatItCached)
{
	TagCache cache;
	EXPECT_EQ(-1, ComputeTagCacheFind(cache, 1, 7));
	ComputeTagCacheStore(cache, 1, 7, 0x12340005);
	ComputeTagCacheStore(cache, 1, 2, 0x12340009);
	EXPECT_EQ(0x12340005, ComputeTagCacheFind(cache, 1, 7));
	EXPECT_EQ(0x12340009, ComputeTagCacheFind(cache, 1, 2));
	EXPECT_EQ(-1, ComputeTagCacheFind(cache, 1, 3));
	EXPECT_EQ(-1, ComputeTagCacheFind(cache, 1, 100));
}

// [Doruk-Aksoy] A purge, clear or load moves the pool's generation on, which forgets every id cached before it.
TEST(TagCache, ANewGenerationForgets)
{
	TagCache cache;
	ComputeTagCacheStore(cache, 1, 7, 0x12340005);
	ComputeTagCacheStore(cache, 1, 2, 0x12340009);
	EXPECT_EQ(-1, ComputeTagCacheFind(cache, 2, 7));
	ComputeTagCacheStore(cache, 2, 7, 0x12340001);
	EXPECT_EQ(0x12340001, ComputeTagCacheFind(cache, 2, 7));
	EXPECT_EQ(-1, ComputeTagCacheFind(cache, 2, 2));
}

// [Doruk-Aksoy] A failed add (-1) and an index past the cache's bound are never kept.
TEST(TagCache, FailuresAndHugeIndexesAreNotKept)
{
	TagCache cache;
	ComputeTagCacheStore(cache, 1, 4, -1);
	EXPECT_EQ(-1, ComputeTagCacheFind(cache, 1, 4));
	ComputeTagCacheStore(cache, 1, kTagCacheMaxIndex, 0x12340005);
	EXPECT_EQ(-1, ComputeTagCacheFind(cache, 1, kTagCacheMaxIndex));
	EXPECT_TRUE(cache.ids.size() <= kTagCacheMaxIndex);
}
