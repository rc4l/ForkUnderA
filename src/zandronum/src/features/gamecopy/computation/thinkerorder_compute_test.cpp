// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/gamecopy/computation/thinkerorder_compute.h"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

using namespace zx::gamecopy;

namespace {
int a, b, c, d;
// [Doruk-Aksoy] A bucket's entries that are still there, in order.
std::vector<void *> Kept(const OrderBuckets &o, int bucket)
{
	std::vector<void *> out;
	for (void *item : o.items[bucket]) if (item != NULL) out.push_back(item);
	return out;
}
}

// [Doruk-Aksoy] Each settled list is its own index, each fresh list follows all of those, and any other address is no list, as a sentinel's own is not.
TEST(ThinkerOrderCompute, AListsAddressGivesItsBucket)
{
	const uintptr_t settled = 0x1000, fresh = 0x2000;
	EXPECT_EQ(ComputeListBucket(settled, settled, 4, fresh, 3, 8), 0);
	EXPECT_EQ(ComputeListBucket(settled + 24, settled, 4, fresh, 3, 8), 3);
	EXPECT_EQ(ComputeListBucket(fresh, settled, 4, fresh, 3, 8), 4);
	EXPECT_EQ(ComputeListBucket(fresh + 16, settled, 4, fresh, 3, 8), 6);
	EXPECT_EQ(ComputeListBucket(fresh + 24, settled, 4, fresh, 3, 8), -1) << "past the fresh lists";
	EXPECT_EQ(ComputeListBucket(0x3000, settled, 4, fresh, 3, 8), -1);
}

// [Doruk-Aksoy] Items keep the order their list added them in, each bucket its own, and a bucket below 0 holds nothing.
TEST(ThinkerOrderCompute, AppendsKeepTheListsOrder)
{
	OrderBuckets o;
	EXPECT_EQ(ComputeOrderAppend(o, 2, &a), 0);
	EXPECT_EQ(ComputeOrderAppend(o, 2, &b), 1);
	EXPECT_EQ(ComputeOrderAppend(o, 0, &c), 0);
	EXPECT_EQ(Kept(o, 2), (std::vector<void *>{ &a, &b }));
	EXPECT_EQ(Kept(o, 0), (std::vector<void *>{ &c }));
	EXPECT_TRUE(o.items[1].empty()) << "a bucket between two used ones is empty";
	EXPECT_EQ(ComputeOrderAppend(o, -1, &d), -1);
}

// [Doruk-Aksoy] A removed item leaves a gap the order skips, and only the item a slot holds can be removed from it.
TEST(ThinkerOrderCompute, RemovingLeavesAGap)
{
	OrderBuckets o;
	for (int *p : { &a, &b, &c }) ComputeOrderAppend(o, 0, p);
	EXPECT_TRUE(ComputeOrderRemove(o, 0, 1, &b));
	EXPECT_EQ(Kept(o, 0), (std::vector<void *>{ &a, &c }));
	EXPECT_EQ(o.gone[0], 1);
	EXPECT_FALSE(ComputeOrderRemove(o, 0, 1, &b)) << "already gone";
	EXPECT_FALSE(ComputeOrderRemove(o, 0, 0, &c)) << "another item's slot";
	EXPECT_FALSE(ComputeOrderRemove(o, 0, 5, &c)) << "past the bucket";
	EXPECT_FALSE(ComputeOrderRemove(o, 0, -1, &c));
	EXPECT_FALSE(ComputeOrderRemove(o, 3, 0, &a)) << "a bucket never used";
	EXPECT_FALSE(ComputeOrderRemove(o, -1, 0, &a)) << "a list the mirror does not keep, as a sentinel's";
	EXPECT_FALSE(ComputeOrderRemove(o, 0, 1, NULL));
	EXPECT_EQ(o.gone[0], 1);
}

// [Doruk-Aksoy] A bucket is closed up once more of it has left than stays, the order kept and each moved item told its new slot.
TEST(ThinkerOrderCompute, CompactingKeepsTheOrderAndTellsTheMoved)
{
	OrderBuckets o;
	for (int *p : { &a, &b, &c, &d }) ComputeOrderAppend(o, 1, p);
	ComputeOrderRemove(o, 1, 0, &a);
	ComputeOrderRemove(o, 1, 2, &c);
	EXPECT_FALSE(ComputeOrderSparse(o, 1)) << "half gone is not yet more than half";
	ComputeOrderRemove(o, 1, 1, &b);
	EXPECT_TRUE(ComputeOrderSparse(o, 1));
	std::vector<std::pair<void *, int>> moved;
	ComputeOrderAppend(o, 1, &a);
	ComputeOrderCompact(o, 1, [&](void *item, int slot) { moved.push_back(std::make_pair(item, slot)); });
	EXPECT_EQ(o.items[1], (std::vector<void *>{ &d, &a }));
	EXPECT_EQ(moved, (std::vector<std::pair<void *, int>>{ { &d, 0 }, { &a, 1 } }));
	EXPECT_EQ(o.gone[1], 0);
	EXPECT_FALSE(ComputeOrderSparse(o, 1));
	EXPECT_FALSE(ComputeOrderSparse(o, 7)) << "a bucket never used";
	EXPECT_FALSE(ComputeOrderSparse(o, -1));
	ComputeOrderCompact(o, 7, [&](void *, int) { ADD_FAILURE() << "nothing to move"; });
	ComputeOrderCompact(o, -1, [&](void *, int) { ADD_FAILURE() << "nothing to move"; });
}

// [Doruk-Aksoy] A bucket with nothing gone moves nothing when closed up.
TEST(ThinkerOrderCompute, CompactingAFullBucketMovesNothing)
{
	OrderBuckets o;
	for (int *p : { &a, &b }) ComputeOrderAppend(o, 0, p);
	ComputeOrderCompact(o, 0, [&](void *, int) { ADD_FAILURE() << "nothing moved"; });
	EXPECT_EQ(o.items[0], (std::vector<void *>{ &a, &b }));
}
