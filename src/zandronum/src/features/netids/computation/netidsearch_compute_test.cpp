// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include <random>
#include <vector>
#include "features/netids/computation/netidsearch_compute.h"

using namespace zx::netids;

namespace {

// [rc4l] IDList::getNewID's walk as it was, entry by entry, which the bitmap search must agree with.
// [Kaori-Chani] lowest is where the wrap lands, the player range sitting below it.
unsigned WalkedNextFreeId(const std::vector<bool> &free, unsigned from, unsigned lowest = 1)
{
	unsigned at = from;
	do
	{
		at = at == kIdCount - 1 ? lowest : at + 1;
		if (at == from) return from;
	} while (!free[at]);
	return at;
}

struct Ids
{
	std::vector<uint64_t> bits = std::vector<uint64_t>(kIdWords, 0);
	std::vector<bool> free = std::vector<bool>(kIdCount, false);
	void Mark(unsigned id, bool isFree) { ComputeMarkId(&bits[0], id, isFree); free[id] = isFree; }
	unsigned Next(unsigned from, unsigned lowest = 1) const
	{
		return ComputeNextFreeId(&bits[0], from, lowest);
	}
};

} // namespace

TEST(NetIdSearch, MarkingSetsAndClearsOneBit)
{
	Ids ids;
	ids.Mark(65, true);
	EXPECT_EQ(2ull, ids.bits[1]);
	ids.Mark(64, true);
	EXPECT_EQ(3ull, ids.bits[1]);
	ids.Mark(65, false);
	EXPECT_EQ(1ull, ids.bits[1]);
	EXPECT_EQ(0ull, ids.bits[0]);
	EXPECT_TRUE(ComputeIdFree(&ids.bits[0], 64));
	EXPECT_FALSE(ComputeIdFree(&ids.bits[0], 65));
	EXPECT_FALSE(ComputeIdFree(&ids.bits[0], 0));
}

TEST(NetIdSearch, TheNextFreeIdIsTheFirstAfterFrom)
{
	Ids ids;
	for (unsigned id : { 5u, 9u, 200u, 70000u % kIdCount }) ids.Mark(id, true);
	EXPECT_EQ(9u, ids.Next(5));
	EXPECT_EQ(200u, ids.Next(9));      // [rc4l] across empty words
	EXPECT_EQ(5u, ids.Next(1));
	EXPECT_EQ(9u, ids.Next(6));
}

TEST(NetIdSearch, TheSearchGoesRoundPast65535ToOne)
{
	Ids ids;
	ids.Mark(3, true);
	ids.Mark(65535, true);
	EXPECT_EQ(65535u, ids.Next(60000));
	EXPECT_EQ(3u, ids.Next(65535));
	ids.Mark(65535, false);
	EXPECT_EQ(3u, ids.Next(100));
}

// [rc4l] nuts.wad with Complex Doom: 70,000 actors fill every id, and the walk read all 65,535 entries on each spawn to find none.
TEST(NetIdSearch, AFullTableAnswersFromItself)
{
	Ids ids;
	EXPECT_EQ(7u, ids.Next(7));
	ids.Mark(7, true);
	EXPECT_EQ(7u, ids.Next(7));   // [rc4l] only from itself is free, which the walk does not count
	EXPECT_EQ(7u, ids.Next(65535));
}

TEST(NetIdSearch, IdZeroIsNeverHandedOut)
{
	Ids ids;
	ids.Mark(0, true);
	ids.Mark(10, true);
	EXPECT_EQ(10u, ids.Next(65535));
	ids.Mark(10, false);
	EXPECT_EQ(65535u, ids.Next(65535));
}

TEST(NetIdSearch, ASetBitPastTheEndOfTheRangeIsNotTaken)
{
	Ids ids;
	ids.Mark(40, true);
	EXPECT_EQ(40u, ids.Next(30));
	EXPECT_EQ(40u, ids.Next(40));   // [rc4l] 1 up to 40 holds no free id short of 40 itself
	ids.Mark(20, true);
	EXPECT_EQ(20u, ids.Next(40));
}

TEST(NetIdSearch, AgreesWithTheWalkOnRandomTables)
{
	std::mt19937 rng(1234);
	for (int round = 0; round < 40; round++)
	{
		Ids ids;
		const unsigned density = round % 4 == 0 ? 2 : round % 4 == 1 ? 50 : round % 4 == 2 ? 2000 : 65000;
		std::uniform_int_distribution<unsigned> pick(0, kIdCount - 1), roll(0, 65535);
		for (unsigned id = 0; id < kIdCount; id++) if (roll(rng) < density) ids.Mark(id, true);
		for (int probe = 0; probe < 200; probe++)
		{
			const unsigned from = 1 + pick(rng) % (kIdCount - 1);
			ASSERT_EQ(WalkedNextFreeId(ids.free, from), ids.Next(from)) << "round " << round << " from " << from;
		}
	}
}

// [Kaori-Chani] The wrap lands on lowest, not 1, so the ids reserved for players are never handed
// out however free they are marked.
TEST(NetIdSearch, TheWrapSkipsTheReservedRange)
{
	const unsigned firstDynamic = 65;   // MAXPLAYERS + 1
	Ids ids;
	for (unsigned id = 1; id <= 64; ++id) ids.Mark(id, true);
	ids.Mark(70, true);
	ids.Mark(900, true);

	// Above the range it behaves as before.
	EXPECT_EQ(900u, ids.Next(70, firstDynamic));
	// Wrapping from the top skips 1..64 and finds the first dynamic id that is free.
	EXPECT_EQ(70u, ids.Next(65535, firstDynamic));
	// With nothing free above the range, the search returns from itself rather than a player's id.
	Ids bare;
	for (unsigned id = 1; id <= 64; ++id) bare.Mark(id, true);
	EXPECT_EQ(5000u, bare.Next(5000, firstDynamic));
	// And the reference walk agrees with the bitmap, bound for bound.
	EXPECT_EQ(WalkedNextFreeId(ids.free, 65535, firstDynamic), ids.Next(65535, firstDynamic));
}
