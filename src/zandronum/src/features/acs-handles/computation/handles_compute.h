// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_ACSHANDLES_COMPUTE_H
#define ZX_ACSHANDLES_COMPUTE_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace zx { namespace acshandles {

// [Doruk-Aksoy] A handle is bit 31 clear, a generation in bits 30-20 and a slot in bits 19-0.
constexpr int kSlotBits = 20;
constexpr int kGenerationBits = 11;
constexpr uint32_t kMaxSlot = (uint32_t(1) << kSlotBits) - 1;
constexpr uint32_t kMaxGeneration = (uint32_t(1) << kGenerationBits) - 1;

// [Doruk-Aksoy] The handle for `slot` at `generation`, or 0 when the slot is 0 or either is out of range.
int32_t ComputeHandle(uint32_t slot, uint32_t generation);

// [Doruk-Aksoy] The slot and generation a handle names; a negative handle names slot 0.
uint32_t ComputeHandleSlot(int32_t handle);
uint32_t ComputeHandleGeneration(int32_t handle);

enum SlotState : uint8_t
{
	kSlotFree,		// [Doruk-Aksoy] Waiting in the free queue.
	kSlotLive,		// [Doruk-Aksoy] Holding an actor of the current level.
	kSlotParked,	// [Doruk-Aksoy] Holding an actor of a hub map the player left, until it comes back.
	kSlotRetired,	// [Doruk-Aksoy] Its generation ran out, so it is never given out again.
};

// [Doruk-Aksoy] One of the values a script attached to an actor, its key a name number.
struct DataPair
{
	int key;
	int32_t value;
};

// [Doruk-Aksoy] The most keys one actor carries.
constexpr size_t kMaxDataKeys = 64;

struct SlotEntry
{
	uint32_t generation;
	SlotState state;
	int map;				// [Doruk-Aksoy] The hub map a parked slot waits for.
	const void *actor;		// [Doruk-Aksoy] A live slot's actor; not saved.
	std::vector<DataPair> data;	// [Doruk-Aksoy] Its actor's key/value store, in the order keys were first set.
};

enum DataResult
{
	kDataSet,
	kDataFull,		// [Doruk-Aksoy] A new key past kMaxDataKeys.
	kDataNoSlot,	// [Doruk-Aksoy] The slot holds no live actor.
};

// [Doruk-Aksoy] Everything a savegame keeps of the table: each slot past 0 in order, the free queue, and each hub map's last visit.
struct TableState
{
	std::vector<SlotEntry> slots;
	std::vector<uint32_t> freeQueue;
	std::vector<int> visitMaps;
	std::vector<uint64_t> visitTimes;
	uint64_t clock;
};

// [Doruk-Aksoy] The savegame chunk for a table whose map and data keys are 1-based indices into `names`, since name numbers change between runs.
std::vector<uint8_t> ComputeEncodeTable(const TableState &state, const std::vector<std::string> &names);

// [Doruk-Aksoy] A chunk read back, false when it is too short or of another version.
bool ComputeDecodeTable(const uint8_t *data, size_t size, TableState &state, std::vector<std::string> &names);

// [Doruk-Aksoy] Every map key in `state` passed through `remap`, 0 staying 0.
template <class Remap>
void ComputeRemapMaps(TableState &state, Remap remap)
{
	for (SlotEntry &entry : state.slots)
		if (entry.map != 0)
			entry.map = remap(entry.map);
	for (int &map : state.visitMaps)
		map = remap(map);
}

// [Doruk-Aksoy] Every data key in `state` passed through `remap`.
template <class Remap>
void ComputeRemapDataKeys(TableState &state, Remap remap)
{
	for (SlotEntry &entry : state.slots)
		for (DataPair &pair : entry.data)
			pair.key = remap(pair.key);
}

// [Doruk-Aksoy] One machine's handle table over opaque actor pointers: slots given out oldest first, retired when their generation runs out, parked while their hub map is away.
class HandleTable
{
public:
	// [Doruk-Aksoy] `capacity` is the highest slot handed out, kMaxSlot unless a test wants a smaller table.
	explicit HandleTable(uint32_t capacity = kMaxSlot);

	// [Doruk-Aksoy] A slot for `actor`, or 0 when none is left; `releasedMap` names the hub map whose slots were released to make room, else 0.
	uint32_t Acquire(const void *actor, int &releasedMap);

	// [Doruk-Aksoy] The handle a live or parked slot answers to now, or 0.
	int32_t HandleOf(uint32_t slot) const;

	// [Doruk-Aksoy] The actor a handle names, or null once it is gone, parked or another generation's.
	const void *Resolve(int32_t handle) const;

	// [Doruk-Aksoy] The slot's actor is gone: its handles go stale and the slot waits in the queue, or retires.
	void Release(uint32_t slot);

	// [Doruk-Aksoy] The slot's actor leaves with hub map `map`, which keeps it until the map comes back.
	void Park(uint32_t slot, int map);

	// [Doruk-Aksoy] A saved actor comes back on `slot`, live again; false when the slot was released meanwhile.
	bool Attach(uint32_t slot, const void *actor);

	// [Doruk-Aksoy] After a load, every live slot no actor came back for released.
	void ReleaseUnattached();

	// [Doruk-Aksoy] Hub map `map` was entered now, for the release-oldest valve.
	void Visit(int map);

	// [Doruk-Aksoy] Every slot parked for `map`, or for every map, released.
	void ReleaseParked(int map);
	void ReleaseAllParked();

	// [Doruk-Aksoy] Every slot free, every generation 0: a new game, where no variable can hold a handle.
	void Reset();

	// [Doruk-Aksoy] The live slot's value under `key` set, a new key going last.
	DataResult SetData(uint32_t slot, int key, int32_t value);

	// [Doruk-Aksoy] The live slot's value under `key` into `value`, false when unset.
	bool GetData(uint32_t slot, int key, int32_t &value) const;

	// [Doruk-Aksoy] The live slot's key `key` removed, or all its keys.
	void ClearData(uint32_t slot, int key);
	void ClearAllData(uint32_t slot);

	uint32_t Capacity() const { return capacity_; }
	// [Doruk-Aksoy] How many slots past 0 are in `state`.
	size_t CountState(SlotState state) const;

	TableState Save() const;
	// [Doruk-Aksoy] The table as saved, its live slots waiting for Attach.
	void Load(const TableState &state);

private:
	uint32_t capacity_;
	std::vector<SlotEntry> slots_;	// [Doruk-Aksoy] Index 0 is never used.
	std::deque<uint32_t> free_;
	std::vector<int> visitMaps_;
	std::vector<uint64_t> visitTimes_;
	uint64_t clock_;

	int OldestParkedMap() const;
};

}} // namespace zx::acshandles

#endif
