// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include "features/hwrender/computation/slotrecycle_compute.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace zx::hwrender;

// [Doruk-Aksoy] A pinned slot never gives way, nor one this reservation or the one before used, whose records may still be drawn.
TEST(SlotRecycle, OnlyAnUnpinnedSlotNeitherOfTheLastTwoReservationsUsedMayGiveWay)
{
	EXPECT_TRUE(ComputeMayRecycle({ false, 3 }, 5));
	EXPECT_FALSE(ComputeMayRecycle({ false, 4 }, 5)) << "the reservation before this one";
	EXPECT_FALSE(ComputeMayRecycle({ false, 5 }, 5)) << "this reservation";
	EXPECT_FALSE(ComputeMayRecycle({ true, 0 }, 5)) << "pinned";
	EXPECT_FALSE(ComputeMayRecycle({ false, 0 }, 1)) << "the first reservation has nothing old enough";
	EXPECT_FALSE(ComputeMayRecycle({ false, 0 }, 0));
}

// [Doruk-Aksoy] The least recently reserved go first, and among slots reserved together the lowest index does.
TEST(SlotRecycle, TheLeastRecentlyReservedGiveWayFirstLowestIndexAmongEquals)
{
	const std::vector<SlotTenure> slots = { { true, 0 }, { false, 7 }, { false, 2 }, { false, 9 }, { false, 2 }, { true, 1 }, { false, 8 } };
	EXPECT_EQ((std::vector<int>{ 2, 4, 1, 6 }), ComputeRecycleOrder(slots, 10));
	EXPECT_TRUE(ComputeRecycleOrder(slots, 3).empty()) << "every unpinned slot was used by one of the last two reservations";
	EXPECT_TRUE(ComputeRecycleOrder({}, 10).empty());
}

// [Doruk-Aksoy] A fresh binding writes every slot to the array's size; a reused one only those changed since it was filled.
TEST(SlotRecycle, AFreshFillWritesEverySlotAndAReusedOneOnlyWhatChanged)
{
	const std::vector<uint32_t> changed = { 1, 4, 2, 6, 3 };
	std::vector<unsigned> out;
	ComputeSlotsToWrite(changed, 3, true, 8, out);
	EXPECT_EQ((std::vector<unsigned>{ 0, 1, 2, 3, 4, 5, 6, 7 }), out);
	ComputeSlotsToWrite(changed, 3, false, 8, out);
	EXPECT_EQ((std::vector<unsigned>{ 1, 3 }), out) << "slots past the table keep the white they were filled with";
	ComputeSlotsToWrite(changed, 6, false, 8, out);
	EXPECT_TRUE(out.empty()) << "a fill already at the table's generation writes nothing";
	ComputeSlotsToWrite(changed, 0, false, 3, out);
	EXPECT_EQ((std::vector<unsigned>{ 0, 1, 2 }), out) << "never past the array's size";
}

// [Doruk-Aksoy] A piece's slot changed after the fill samples what the slot held before; a brightmap slot of 0 is no brightmap.
TEST(SlotRecycle, APiecesSlotsChangedAfterTheFillAreCounted)
{
	const std::vector<uint32_t> changed = { 1, 2, 5, 9 };
	EXPECT_EQ(0, ComputeSlotsChangedAfterFill(changed, 1, 0, 5));
	EXPECT_EQ(0, ComputeSlotsChangedAfterFill(changed, 1, 2, 5));
	EXPECT_EQ(1, ComputeSlotsChangedAfterFill(changed, 3, 0, 5));
	EXPECT_EQ(1, ComputeSlotsChangedAfterFill(changed, 1, 3, 5));
	EXPECT_EQ(2, ComputeSlotsChangedAfterFill(changed, 3, 3, 5));
	EXPECT_EQ(2, ComputeSlotsChangedAfterFill(changed, 7, 9, 5)) << "a slot past the table was never filled";
	EXPECT_EQ(0, ComputeSlotsChangedAfterFill(changed, -1, 0, 5));
}

// [Doruk-Aksoy] A full table whose least recently reserved key b the list still wants: found first, b is kept, and the missing x takes c, the one key the list does not want, where one pass would have given b's slot to x.
TEST(SlotRecycle, ReservingFindsEveryKeyBeforeMakingOneSoAMissingKeyNeverTakesAFoundOnesSlot)
{
	std::vector<char> keyOf = { 0, 'a', 'b', 'c' };
	std::vector<SlotTenure> tenure = { { true, 0 }, { false, 3 }, { false, 1 }, { false, 2 } };
	const uint32_t current = 5;
	const std::vector<char> wants = { 'a', 'x', 'b', 0 };
	std::string calls;
	auto find = [&](int k) {
		calls += 'f';
		if (wants[k] == 0) return 0;
		for (size_t s = 1; s < keyOf.size(); s++)
			if (keyOf[s] == wants[k]) { tenure[s].reserved = current; return (int)s; }
		return -1;
	};
	auto make = [&](int k) {
		calls += 'm';
		const int s = ComputeRecycleOrder(tenure, current).front();
		keyOf[s] = wants[k];
		tenure[s] = { false, current };
		return s;
	};
	std::vector<int> slots;
	ComputeReserveSlots((int)wants.size(), find, make, slots);
	EXPECT_EQ((std::vector<int>{ 1, 3, 2, 0 }), slots);
	EXPECT_EQ("ffffm", calls) << "every key found before any is made, and a key with nothing to find never made";
	EXPECT_EQ((std::vector<char>{ 0, 'a', 'b', 'x' }), keyOf);
}

// [Doruk-Aksoy] No keys, no calls.
TEST(SlotRecycle, ReservingNoKeysAsksNothing)
{
	std::vector<int> slots = { 7 };
	int calls = 0;
	ComputeReserveSlots(0, [&](int) { calls++; return 0; }, [&](int) { calls++; return 0; }, slots);
	EXPECT_TRUE(slots.empty());
	EXPECT_EQ(0, calls);
}

// [Doruk-Aksoy] A white slot tries again up to its bound, then stays as filled until its key changes and the count starts over.
TEST(SlotRecycle, AWhiteSlotRetriesUpToItsBound)
{
	int tries = 0;
	EXPECT_TRUE(ComputeRetryWhite(tries, 2));
	EXPECT_TRUE(ComputeRetryWhite(tries, 2));
	EXPECT_FALSE(ComputeRetryWhite(tries, 2));
	EXPECT_EQ(2, tries);
	tries = 0;
	EXPECT_TRUE(ComputeRetryWhite(tries, 2));
}
