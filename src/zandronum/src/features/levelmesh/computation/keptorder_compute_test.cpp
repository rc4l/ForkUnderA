// [Doruk-Aksoy] Tests for keptorder_compute: each key finds the entry it had last pass while the list keeps its order, and a cache that gets it wrong only works an entry out again.
#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "features/levelmesh/computation/keptorder_compute.h"

using namespace zx::levelmesh;

namespace {

// [Doruk-Aksoy] An entry: its key and what was worked out for it, here the pass it was made in.
struct Entry
{
	int key = -1;
	int made = -1;
};

// [Doruk-Aksoy] One pass over keys, each entry made afresh noted with pass; how many were found.
int Pass(KeptOrder<Entry> &kept, const std::vector<int> &keys, int pass, size_t window = 8)
{
	int hits = 0;
	ComputeKeptBegin(kept);
	for (const int key : keys)
	{
		bool found = false;
		Entry &e = ComputeKeptFind(kept, key, window, [](const Entry &x) { return x.key; }, found);
		if (found) { hits++; EXPECT_EQ(key, e.key); continue; }
		e.key = key;
		e.made = pass;
	}
	ComputeKeptEnd(kept);
	return hits;
}

std::vector<int> Keys(const KeptOrder<Entry> &kept)
{
	std::vector<int> out;
	for (const Entry &e : kept.entries) out.push_back(e.key);
	return out;
}

} // namespace

// [Doruk-Aksoy] The same list twice finds every entry where it was.
TEST(KeptOrder, TheSameListFindsEveryEntry)
{
	KeptOrder<Entry> kept;
	EXPECT_EQ(0, Pass(kept, { 1, 2, 3 }, 0));
	EXPECT_EQ(3, Pass(kept, { 1, 2, 3 }, 1));
	EXPECT_EQ((std::vector<int>{ 1, 2, 3 }), Keys(kept));
	for (const Entry &e : kept.entries) EXPECT_EQ(0, e.made);
}

// [Doruk-Aksoy] Items dropped from the front and the middle and new ones appended keep the rest found and moved down.
TEST(KeptOrder, DropsAndAppendsKeepTheRest)
{
	KeptOrder<Entry> kept;
	Pass(kept, { 1, 2, 3, 4, 5 }, 0);
	EXPECT_EQ(3, Pass(kept, { 2, 4, 5, 6, 7 }, 1));
	EXPECT_EQ((std::vector<int>{ 2, 4, 5, 6, 7 }), Keys(kept));
	EXPECT_EQ(0, kept.entries[0].made);
	EXPECT_EQ(0, kept.entries[2].made);
	EXPECT_EQ(1, kept.entries[3].made);
	EXPECT_EQ(1, kept.entries[4].made);
	EXPECT_EQ(5, Pass(kept, { 2, 4, 5, 6, 7 }, 2));
}

// [Doruk-Aksoy] A new key in the gap a drop left takes that room.
TEST(KeptOrder, ANewKeyTakesTheRoomADropLeft)
{
	KeptOrder<Entry> kept;
	Pass(kept, { 1, 2, 3 }, 0);
	EXPECT_EQ(2, Pass(kept, { 2, 8, 3 }, 1));
	EXPECT_EQ((std::vector<int>{ 2, 8, 3 }), Keys(kept));
	EXPECT_EQ(1, kept.entries[1].made);
	EXPECT_EQ(0, kept.entries[2].made);
}

// [Doruk-Aksoy] A new key before entries not yet passed is made in the spare, so the rest are still found, and the next pass keeps it.
TEST(KeptOrder, ANewKeyBeforeEntriesNotYetPassedGetsTheSpare)
{
	KeptOrder<Entry> kept;
	Pass(kept, { 1, 2, 3, 4 }, 0);
	EXPECT_EQ(4, Pass(kept, { 9, 1, 2, 3, 4 }, 1));
	EXPECT_EQ((std::vector<int>{ 1, 2, 3, 4 }), Keys(kept));
	EXPECT_EQ(9, kept.spare.key);
	EXPECT_EQ(1, kept.spare.made);
	EXPECT_EQ(1, Pass(kept, { 1, 5, 6 }, 2));
	EXPECT_EQ((std::vector<int>{ 1 }), Keys(kept));
	EXPECT_EQ(1, Pass(kept, { 1, 5, 6 }, 3));
	EXPECT_EQ((std::vector<int>{ 1, 5, 6 }), Keys(kept));
}

// [Doruk-Aksoy] A key further ahead than the window is made afresh, and the entries before it let go once the pass ends.
TEST(KeptOrder, AKeyPastTheWindowIsMadeAgain)
{
	KeptOrder<Entry> kept;
	Pass(kept, { 1, 2, 3, 4 }, 0);
	EXPECT_EQ(0, Pass(kept, { 4 }, 1, 2));
	EXPECT_TRUE(kept.entries.empty());
	EXPECT_EQ(0, Pass(kept, { 4 }, 2));
	EXPECT_EQ((std::vector<int>{ 4 }), Keys(kept));
	EXPECT_EQ(2, kept.entries[0].made);
	EXPECT_EQ(0, Pass(kept, {}, 3));
	EXPECT_TRUE(kept.entries.empty());
}

// [Doruk-Aksoy] Lists that only drop and append, at random, find every key the cache held, and keep their entries in the list's order.
TEST(KeptOrder, RandomDropsAndAppendsFindEveryCachedKey)
{
	unsigned seed = 7;
	auto next = [&seed]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
	KeptOrder<Entry> kept;
	std::vector<int> keys;
	int fresh = 0;
	for (int pass = 0; pass < 60; pass++)
	{
		std::vector<int> now;
		for (const int k : keys) if (next() % 4 != 0) now.push_back(k);
		const int added = (int)(next() % 6);
		for (int i = 0; i < added; i++) now.push_back(fresh++);
		const std::vector<int> before = Keys(kept);
		int cached = 0;
		for (const int k : now) for (const int b : before) if (k == b) cached++;
		EXPECT_EQ(cached, Pass(kept, now, pass, before.size() + 1)) << "pass " << pass;
		// [Doruk-Aksoy] What the cache holds is the list with some keys left out, never another order.
		const std::vector<int> after = Keys(kept);
		size_t at = 0;
		for (const int k : now) if (at < after.size() && after[at] == k) at++;
		EXPECT_EQ(after.size(), at) << "pass " << pass;
		EXPECT_GE(after.size(), (size_t)cached) << "pass " << pass;
		keys = now;
	}
}
