// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Tests for the channel lists kept by sound and by actor.
#include "gtest/gtest.h"
#include "features/sound-index/computation/soundindex_compute.h"

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace
{
struct Chan
{
	int id;
	ZxIndexLink link;
};

constexpr intptr_t kKeyA = 1;
constexpr intptr_t kKeyB = 2;

// [Doruk-Aksoy] The ids in key's list, head first.
std::vector<int> Ids(const ZxIndexHeads &heads, intptr_t key)
{
	std::vector<int> ids;
	for (ZxIndexLink *l = ComputeIndexFirst(heads, key); l != nullptr; l = l->next) ids.push_back(static_cast<Chan *>(l->owner)->id);
	return ids;
}

// [Doruk-Aksoy] Each key lists its own channels newest first, as the channel list does.
TEST(ComputeIndex, NewestFirstPerKey)
{
	ZxIndexHeads heads;
	Chan a{ 1, {} }, b{ 2, {} }, c{ 3, {} };
	ComputeIndexAdd(heads, kKeyA, &a.link, &a);
	ComputeIndexAdd(heads, kKeyB, &b.link, &b);
	ComputeIndexAdd(heads, kKeyA, &c.link, &c);
	EXPECT_EQ(Ids(heads, kKeyA), (std::vector<int>{ 3, 1 }));
	EXPECT_EQ(Ids(heads, kKeyB), (std::vector<int>{ 2 }));
	EXPECT_EQ(ComputeIndexFirst(heads, 99), nullptr);
}

// [Doruk-Aksoy] A channel already listed stays where it is instead of being listed twice.
TEST(ComputeIndex, AddingTwiceKeepsOnePlace)
{
	ZxIndexHeads heads;
	Chan a{ 1, {} }, b{ 2, {} };
	ComputeIndexAdd(heads, kKeyA, &a.link, &a);
	ComputeIndexAdd(heads, kKeyA, &b.link, &b);
	ComputeIndexAdd(heads, kKeyA, &a.link, &a);
	EXPECT_EQ(Ids(heads, kKeyA), (std::vector<int>{ 2, 1 }));
}

// [Doruk-Aksoy] Removing the head, the middle or the tail keeps the rest in order, and the key goes once its list is empty.
TEST(ComputeIndex, RemoveAnywhere)
{
	ZxIndexHeads heads;
	Chan a{ 1, {} }, b{ 2, {} }, c{ 3, {} }, d{ 4, {} };
	for (Chan *chan : { &a, &b, &c, &d }) ComputeIndexAdd(heads, kKeyA, &chan->link, chan);
	ComputeIndexRemove(heads, kKeyA, &c.link);
	EXPECT_EQ(Ids(heads, kKeyA), (std::vector<int>{ 4, 2, 1 }));
	ComputeIndexRemove(heads, kKeyA, &d.link);
	EXPECT_EQ(Ids(heads, kKeyA), (std::vector<int>{ 2, 1 }));
	ComputeIndexRemove(heads, kKeyA, &a.link);
	EXPECT_EQ(Ids(heads, kKeyA), (std::vector<int>{ 2 }));
	EXPECT_EQ(c.link.prev, nullptr);
	EXPECT_EQ(c.link.owner, nullptr);
	ComputeIndexRemove(heads, kKeyA, &b.link);
	EXPECT_TRUE(heads.empty());
}

// [Doruk-Aksoy] Removing a channel in no list changes nothing, and one moved to another key leaves its old key's list.
TEST(ComputeIndex, RemoveUnlistedAndMove)
{
	ZxIndexHeads heads;
	Chan a{ 1, {} }, b{ 2, {} };
	ComputeIndexRemove(heads, kKeyA, &a.link);
	EXPECT_TRUE(heads.empty());
	ComputeIndexAdd(heads, kKeyA, &a.link, &a);
	ComputeIndexAdd(heads, kKeyA, &b.link, &b);
	ComputeIndexRemove(heads, kKeyA, &a.link);
	ComputeIndexAdd(heads, kKeyB, &a.link, &a);
	EXPECT_EQ(Ids(heads, kKeyA), (std::vector<int>{ 2 }));
	EXPECT_EQ(Ids(heads, kKeyB), (std::vector<int>{ 1 }));
}

// [Doruk-Aksoy] Removing under a key whose list still holds others keeps that key, and heads stay valid as the map grows.
TEST(ComputeIndex, HeadsSurviveGrowth)
{
	ZxIndexHeads heads;
	std::vector<Chan> chans(2000);
	for (int i = 0; i < 2000; i++)
	{
		chans[i].id = i;
		chans[i].link = ZxIndexLink{};
		ComputeIndexAdd(heads, i % 1000, &chans[i].link, &chans[i]);
	}
	EXPECT_EQ(Ids(heads, 7), (std::vector<int>{ 1007, 7 }));
	ComputeIndexRemove(heads, 7, &chans[1007].link);
	EXPECT_EQ(Ids(heads, 7), (std::vector<int>{ 7 }));
	EXPECT_EQ(heads.size(), 1000u);
}
}
