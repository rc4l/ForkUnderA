// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "features/acs-handles/computation/handles_compute.h"

using namespace zx::acshandles;

namespace {

int actors[8];
const void *A = &actors[0];
const void *B = &actors[1];
const void *C = &actors[2];
const void *D = &actors[3];

uint32_t Take(HandleTable &table, const void *actor)
{
	int released = 0;
	return table.Acquire(actor, released);
}

} // namespace

TEST(AcsHandles, HandlesPackSlotAndGenerationWithBit31Clear)
{
	EXPECT_EQ(ComputeHandle(1, 0), 1);
	EXPECT_EQ(ComputeHandle(5, 3), (3 << 20) | 5);
	EXPECT_EQ(ComputeHandle(kMaxSlot, kMaxGeneration), INT32_MAX);
	EXPECT_EQ(ComputeHandle(0, 4), 0);
	EXPECT_EQ(ComputeHandle(kMaxSlot + 1, 0), 0);
	EXPECT_EQ(ComputeHandle(1, kMaxGeneration + 1), 0);
	EXPECT_EQ(ComputeHandleSlot((7 << 20) | 9), 9u);
	EXPECT_EQ(ComputeHandleGeneration((7 << 20) | 9), 7u);
	EXPECT_EQ(ComputeHandleSlot(-5), 0u);
	EXPECT_EQ(ComputeHandleGeneration(0), 0u);
}

TEST(AcsHandles, AnActorResolvesThroughItsHandleUntilReleased)
{
	HandleTable table;
	EXPECT_EQ(Take(table, nullptr), 0u);
	const uint32_t slot = Take(table, A);
	ASSERT_EQ(slot, 1u);
	const int32_t handle = table.HandleOf(slot);
	EXPECT_EQ(table.Resolve(handle), A);
	EXPECT_EQ(table.Resolve(ComputeHandle(slot, 1)), nullptr);
	EXPECT_EQ(table.Resolve(0), nullptr);
	EXPECT_EQ(table.Resolve(ComputeHandle(40, 0)), nullptr);
	table.Release(slot);
	EXPECT_EQ(table.Resolve(handle), nullptr);
	EXPECT_EQ(table.HandleOf(slot), 0);
	EXPECT_EQ(table.HandleOf(0), 0);
	EXPECT_EQ(table.HandleOf(40), 0);
	table.Release(slot);
	table.Release(0);
	table.Release(40);
	EXPECT_EQ(table.CountState(kSlotFree), 1u);
}

// [Doruk-Aksoy] A freed slot comes back with the next generation, the oldest freed first.
TEST(AcsHandles, FreedSlotsComeBackOldestFirstWithANewGeneration)
{
	HandleTable table;
	const uint32_t a = Take(table, A), b = Take(table, B), c = Take(table, C);
	const int32_t oldB = table.HandleOf(b);
	table.Release(b);
	table.Release(a);
	EXPECT_EQ(Take(table, D), b);
	EXPECT_EQ(table.HandleOf(b), ComputeHandle(b, 1));
	EXPECT_EQ(table.Resolve(oldB), nullptr);
	EXPECT_EQ(Take(table, A), a);
	EXPECT_EQ(Take(table, B), 4u);
	EXPECT_EQ(table.CountState(kSlotLive), 4u);
	(void)c;
}

// [Doruk-Aksoy] A slot hands out each generation once, then retires, so no two handles it gave are equal.
TEST(AcsHandles, ASlotRetiresAfterItsLastGeneration)
{
	HandleTable table(1);
	std::set<int32_t> seen;
	for (uint32_t i = 0; i <= kMaxGeneration; ++i)
	{
		const uint32_t slot = Take(table, A);
		ASSERT_EQ(slot, 1u) << i;
		EXPECT_TRUE(seen.insert(table.HandleOf(slot)).second);
		table.Release(slot);
	}
	EXPECT_EQ(seen.size(), size_t(kMaxGeneration) + 1);
	EXPECT_EQ(table.CountState(kSlotRetired), 1u);
	EXPECT_EQ(Take(table, A), 0u);
	EXPECT_EQ(table.HandleOf(1), 0);
}

TEST(AcsHandles, AFullTableWithNothingParkedGivesNoSlot)
{
	HandleTable table(2);
	EXPECT_EQ(table.Capacity(), 2u);
	EXPECT_EQ(HandleTable(kMaxSlot + 5).Capacity(), kMaxSlot);
	Take(table, A);
	Take(table, B);
	int released = 7;
	EXPECT_EQ(table.Acquire(C, released), 0u);
	EXPECT_EQ(released, 0);
}

// [Doruk-Aksoy] Leaving a hub map parks its slots; coming back attaches the same actors under the same handles.
TEST(AcsHandles, AParkedSlotKeepsItsHandleForItsActorsReturn)
{
	HandleTable table;
	const uint32_t slot = Take(table, A);
	const int32_t handle = table.HandleOf(slot);
	table.Park(slot, 10);
	EXPECT_EQ(table.Resolve(handle), nullptr);
	EXPECT_EQ(table.HandleOf(slot), handle);
	// [Doruk-Aksoy] The level unloading destroys the parked actor, which releases nothing.
	table.Release(slot);
	EXPECT_EQ(table.CountState(kSlotParked), 1u);
	EXPECT_TRUE(table.Attach(slot, B));
	EXPECT_EQ(table.Resolve(handle), B);
	EXPECT_FALSE(table.Attach(slot, C));
	EXPECT_FALSE(table.Attach(slot, nullptr));
	EXPECT_FALSE(table.Attach(0, C));
	EXPECT_FALSE(table.Attach(40, C));
	table.Park(0, 10);
	table.Park(40, 10);
	table.Release(slot);
	table.Park(slot, 10);
	EXPECT_EQ(table.CountState(kSlotParked), 0u);
	EXPECT_FALSE(table.Attach(slot, C));
}

// [Doruk-Aksoy] With no free slot left, the least recently visited hub map's slots make room, then the next oldest's.
TEST(AcsHandles, AFullTableReleasesTheOldestVisitedHubMapFirst)
{
	HandleTable table(2);
	table.Visit(10);
	table.Visit(20);
	const uint32_t a = Take(table, A), b = Take(table, B);
	const int32_t handleA = table.HandleOf(a);
	table.Park(a, 10);
	table.Park(b, 20);
	table.Visit(20);
	int released = 0;
	EXPECT_EQ(table.Acquire(C, released), a);
	EXPECT_EQ(released, 10);
	EXPECT_EQ(table.HandleOf(a), ComputeHandle(a, 1));
	EXPECT_FALSE(table.Attach(a, A));
	EXPECT_EQ(table.Resolve(handleA), nullptr);
	EXPECT_EQ(table.CountState(kSlotParked), 1u);
	EXPECT_EQ(table.Acquire(D, released), b);
	EXPECT_EQ(released, 20);
	EXPECT_EQ(table.Acquire(A, released), 0u);
	EXPECT_EQ(released, 0);
}

// [Doruk-Aksoy] A map parked without a recorded visit still counts, as the oldest.
TEST(AcsHandles, AParkedMapWithoutAVisitIsOldest)
{
	HandleTable table(2);
	table.Visit(20);
	const uint32_t a = Take(table, A), b = Take(table, B);
	table.Park(b, 20);
	table.Park(a, 30);
	table.Visit(20);
	int released = 0;
	EXPECT_EQ(table.Acquire(C, released), a);
	EXPECT_EQ(released, 30);
}

TEST(AcsHandles, LeavingTheHubReleasesEveryParkedSlot)
{
	HandleTable table;
	const uint32_t a = Take(table, A), b = Take(table, B), c = Take(table, C);
	table.Park(a, 10);
	table.Park(b, 20);
	table.Park(c, 10);
	table.ReleaseParked(20);
	EXPECT_EQ(table.CountState(kSlotParked), 2u);
	EXPECT_EQ(table.CountState(kSlotFree), 1u);
	table.ReleaseParked(99);
	table.ReleaseAllParked();
	EXPECT_EQ(table.CountState(kSlotParked), 0u);
	EXPECT_EQ(table.CountState(kSlotFree), 3u);
}

TEST(AcsHandles, ResetStartsEveryGenerationOver)
{
	HandleTable table;
	const uint32_t a = Take(table, A);
	table.Release(a);
	Take(table, B);
	table.Park(Take(table, C), 10);
	table.Reset();
	EXPECT_EQ(table.CountState(kSlotLive) + table.CountState(kSlotParked) + table.CountState(kSlotFree), 0u);
	EXPECT_EQ(table.HandleOf(Take(table, D)), 1);
}

// [Doruk-Aksoy] A savegame keeps generations, the free queue's order, parked slots and visits; live slots wait for their actors.
TEST(AcsHandles, ASavedTableComesBackAsItWas)
{
	HandleTable table;
	const uint32_t a = Take(table, A), b = Take(table, B), c = Take(table, C), d = Take(table, D);
	table.Release(c);
	table.Release(a);
	table.Visit(10);
	table.Park(b, 10);
	const int32_t handleD = table.HandleOf(d);
	const TableState saved = table.Save();
	EXPECT_EQ(saved.slots.size(), 4u);
	EXPECT_EQ(saved.slots[3].actor, nullptr);

	HandleTable loaded;
	loaded.Load(saved);
	EXPECT_EQ(loaded.Resolve(handleD), nullptr);
	EXPECT_TRUE(loaded.Attach(d, D));
	EXPECT_EQ(loaded.Resolve(handleD), D);
	EXPECT_EQ(Take(loaded, A), c);
	EXPECT_EQ(Take(loaded, B), a);
	EXPECT_EQ(loaded.CountState(kSlotParked), 1u);
	loaded.ReleaseUnattached();
	EXPECT_EQ(loaded.CountState(kSlotLive), 3u);

	HandleTable unattached;
	unattached.Load(saved);
	unattached.ReleaseUnattached();
	EXPECT_EQ(unattached.Resolve(handleD), nullptr);
	EXPECT_EQ(unattached.CountState(kSlotLive), 0u);
}

// [Doruk-Aksoy] A damaged save loads what makes sense: bad entries retire, bad queue entries drop, a missed free slot is queued.
TEST(AcsHandles, ADamagedSaveLoadsSafely)
{
	TableState state;
	state.slots = {
		SlotEntry{ 0, kSlotFree, 0, nullptr },
		SlotEntry{ kMaxGeneration + 1, kSlotLive, 0, nullptr },
		SlotEntry{ 2, static_cast<SlotState>(9), 0, nullptr },
		SlotEntry{ 1, kSlotFree, 0, nullptr },
		SlotEntry{ 0, kSlotParked, 99, nullptr },
		SlotEntry{ 0, kSlotFree, 0, nullptr },
	};
	state.freeQueue = { 4, 0, 40, 2, 4 };
	state.visitMaps = { 10, 20 };
	state.visitTimes = { 3 };
	state.clock = 5;
	HandleTable table(5);
	table.Load(state);
	EXPECT_EQ(table.CountState(kSlotRetired), 2u);
	EXPECT_EQ(Take(table, A), 4u);
	EXPECT_EQ(Take(table, B), 1u);
	EXPECT_EQ(table.CountState(kSlotParked), 1u);
	table.ReleaseAllParked();
	EXPECT_EQ(table.CountState(kSlotParked), 0u);
	EXPECT_EQ(table.Save().visitMaps.size(), 0u);

	TableState uneven;
	uneven.visitTimes = { 1, 2 };
	uneven.visitMaps = { 30 };
	uneven.clock = 2;
	table.Load(uneven);
	EXPECT_EQ(table.Save().visitTimes.size(), 1u);
}

// [Doruk-Aksoy] The savegame chunk carries every field and the maps' names, and a damaged one is refused whole.
TEST(AcsHandles, TheSaveChunkRoundTripsAndRefusesDamage)
{
	TableState state;
	state.slots = { SlotEntry{ 3, kSlotParked, 2, nullptr }, SlotEntry{ 7, kSlotFree, 0, nullptr }, SlotEntry{ kMaxGeneration, kSlotRetired, 0, nullptr } };
	state.freeQueue = { 2 };
	state.visitMaps = { 2, 1 };
	state.visitTimes = { 9, (uint64_t(1) << 40) + 5 };
	state.clock = (uint64_t(3) << 33) + 1;
	const std::vector<std::string> names = { "MAP01", "MAP02" };
	const std::vector<uint8_t> chunk = ComputeEncodeTable(state, names);

	TableState back;
	std::vector<std::string> backNames;
	ASSERT_TRUE(ComputeDecodeTable(chunk.data(), chunk.size(), back, backNames));
	EXPECT_EQ(backNames, names);
	ASSERT_EQ(back.slots.size(), 3u);
	EXPECT_EQ(back.slots[0].generation, 3u);
	EXPECT_EQ(back.slots[0].state, kSlotParked);
	EXPECT_EQ(back.slots[0].map, 2);
	EXPECT_EQ(back.slots[2].state, kSlotRetired);
	EXPECT_EQ(back.freeQueue, state.freeQueue);
	EXPECT_EQ(back.visitMaps, state.visitMaps);
	EXPECT_EQ(back.visitTimes, state.visitTimes);
	EXPECT_EQ(back.clock, state.clock);

	for (size_t cut = 0; cut < chunk.size(); ++cut)
	{
		EXPECT_FALSE(ComputeDecodeTable(chunk.data(), cut, back, backNames)) << cut;
		EXPECT_TRUE(back.slots.empty() && backNames.empty()) << cut;
	}
	std::vector<uint8_t> wrongVersion = chunk;
	wrongVersion[0] = 2;
	EXPECT_FALSE(ComputeDecodeTable(wrongVersion.data(), wrongVersion.size(), back, backNames));
	EXPECT_FALSE(ComputeDecodeTable(nullptr, 0, back, backNames));
	std::vector<uint8_t> hugeCount = chunk;
	hugeCount[7] = 0x7F;
	EXPECT_FALSE(ComputeDecodeTable(hugeCount.data(), hugeCount.size(), back, backNames));
}

TEST(AcsHandles, AnUnevenVisitListSavesItsPairs)
{
	TableState state;
	state.visitMaps = { 1, 2 };
	state.visitTimes = { 4 };
	state.clock = 0;
	TableState back;
	std::vector<std::string> names;
	const std::vector<uint8_t> chunk = ComputeEncodeTable(state, {});
	ASSERT_TRUE(ComputeDecodeTable(chunk.data(), chunk.size(), back, names));
	EXPECT_EQ(back.visitMaps, std::vector<int>{ 1 });
	EXPECT_EQ(back.visitTimes, std::vector<uint64_t>{ 4 });
}

TEST(AcsHandles, RemapTouchesEveryMapKeyButZero)
{
	TableState state;
	state.slots = { SlotEntry{ 0, kSlotParked, 5, nullptr }, SlotEntry{ 0, kSlotFree, 0, nullptr } };
	state.visitMaps = { 5, 6 };
	ComputeRemapMaps(state, [](int map) { return map * 10; });
	EXPECT_EQ(state.slots[0].map, 50);
	EXPECT_EQ(state.slots[1].map, 0);
	EXPECT_EQ(state.visitMaps, (std::vector<int>{ 50, 60 }));
}
