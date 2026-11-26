// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-handles/computation/handles_compute.h"

namespace zx { namespace acshandles {

int32_t ComputeHandle(uint32_t slot, uint32_t generation)
{
	if (slot == 0 || slot > kMaxSlot || generation > kMaxGeneration)
		return 0;
	return static_cast<int32_t>((generation << kSlotBits) | slot);
}

uint32_t ComputeHandleSlot(int32_t handle)
{
	return handle > 0 ? static_cast<uint32_t>(handle) & kMaxSlot : 0;
}

uint32_t ComputeHandleGeneration(int32_t handle)
{
	return handle > 0 ? static_cast<uint32_t>(handle) >> kSlotBits : 0;
}

namespace {

// [Doruk-Aksoy] 2 adds each slot's key/value store; 1 still loads, with none.
constexpr uint32_t kChunkVersion = 2;

void Put(std::vector<uint8_t> &out, uint32_t value)
{
	for (int i = 0; i < 4; ++i)
		out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

void Put64(std::vector<uint8_t> &out, uint64_t value)
{
	Put(out, static_cast<uint32_t>(value));
	Put(out, static_cast<uint32_t>(value >> 32));
}

// [Doruk-Aksoy] Reads little-endian words from a chunk, failing once past its end.
struct Reader
{
	const uint8_t *data;
	size_t size;
	size_t at;
	bool ok;

	uint32_t Get()
	{
		if (!ok || size - at < 4)
		{
			ok = false;
			return 0;
		}
		uint32_t value = 0;
		for (int i = 0; i < 4; ++i)
			value |= uint32_t(data[at + i]) << (8 * i);
		at += 4;
		return value;
	}

	uint64_t Get64()
	{
		const uint64_t low = Get();
		return low | (uint64_t(Get()) << 32);
	}

	// [Doruk-Aksoy] A count no larger than the words left, so a damaged count fails rather than allocating.
	uint32_t Count(size_t wordsEach)
	{
		const uint32_t count = Get();
		if (ok && uint64_t(count) * wordsEach * 4 > size - at)
			ok = false;
		return ok ? count : 0;
	}
};

} // namespace

std::vector<uint8_t> ComputeEncodeTable(const TableState &state, const std::vector<std::string> &names)
{
	std::vector<uint8_t> out;
	Put(out, kChunkVersion);
	Put(out, static_cast<uint32_t>(state.slots.size()));
	for (const SlotEntry &entry : state.slots)
	{
		Put(out, entry.generation);
		Put(out, entry.state);
		Put(out, static_cast<uint32_t>(entry.map));
		Put(out, static_cast<uint32_t>(entry.data.size()));
		for (const DataPair &pair : entry.data)
		{
			Put(out, static_cast<uint32_t>(pair.key));
			Put(out, static_cast<uint32_t>(pair.value));
		}
	}
	Put(out, static_cast<uint32_t>(state.freeQueue.size()));
	for (uint32_t slot : state.freeQueue)
		Put(out, slot);
	const size_t visits = state.visitMaps.size() < state.visitTimes.size() ? state.visitMaps.size() : state.visitTimes.size();
	Put(out, static_cast<uint32_t>(visits));
	for (size_t i = 0; i < visits; ++i)
	{
		Put(out, static_cast<uint32_t>(state.visitMaps[i]));
		Put64(out, state.visitTimes[i]);
	}
	Put64(out, state.clock);
	Put(out, static_cast<uint32_t>(names.size()));
	for (const std::string &name : names)
	{
		Put(out, static_cast<uint32_t>(name.size()));
		out.insert(out.end(), name.begin(), name.end());
	}
	return out;
}

bool ComputeDecodeTable(const uint8_t *data, size_t size, TableState &state, std::vector<std::string> &names)
{
	Reader in{ data, size, 0, data != nullptr };
	state = TableState{};
	names.clear();
	const uint32_t version = in.Get();
	if (version != 1 && version != kChunkVersion)
		return false;
	const uint32_t slots = in.Count(version == 1 ? 3 : 4);
	for (uint32_t i = 0; i < slots; ++i)
	{
		SlotEntry entry{ 0, kSlotFree, 0, nullptr };
		entry.generation = in.Get();
		entry.state = static_cast<SlotState>(in.Get());
		entry.map = static_cast<int>(in.Get());
		const uint32_t pairs = version == 1 ? 0 : in.Count(2);
		for (uint32_t k = 0; k < pairs; ++k)
		{
			const int key = static_cast<int>(in.Get());
			entry.data.push_back(DataPair{ key, static_cast<int32_t>(in.Get()) });
		}
		state.slots.push_back(entry);
	}
	const uint32_t queued = in.Count(1);
	for (uint32_t i = 0; i < queued; ++i)
		state.freeQueue.push_back(in.Get());
	const uint32_t visits = in.Count(3);
	for (uint32_t i = 0; i < visits; ++i)
	{
		state.visitMaps.push_back(static_cast<int>(in.Get()));
		state.visitTimes.push_back(in.Get64());
	}
	state.clock = in.Get64();
	const uint32_t count = in.Count(1);
	for (uint32_t i = 0; i < count && in.ok; ++i)
	{
		const uint32_t length = in.Get();
		if (!in.ok || length > size - in.at)
		{
			in.ok = false;
			break;
		}
		names.emplace_back(reinterpret_cast<const char *>(data + in.at), length);
		in.at += length;
	}
	if (!in.ok)
	{
		state = TableState{};
		names.clear();
	}
	return in.ok;
}

HandleTable::HandleTable(uint32_t capacity)
	: capacity_(capacity > kMaxSlot ? kMaxSlot : capacity)
{
	Reset();
}

uint32_t HandleTable::Acquire(const void *actor, int &releasedMap)
{
	releasedMap = 0;
	if (actor == nullptr)
		return 0;
	if (free_.empty() && slots_.size() - 1 >= capacity_)
	{
		// [Doruk-Aksoy] A full table first gives back the least recently visited hub map's slots.
		const int oldest = OldestParkedMap();
		if (oldest != 0)
		{
			ReleaseParked(oldest);
			releasedMap = oldest;
		}
	}
	uint32_t slot = 0;
	if (!free_.empty())
	{
		slot = free_.front();
		free_.pop_front();
	}
	else if (slots_.size() - 1 < capacity_)
	{
		slot = static_cast<uint32_t>(slots_.size());
		slots_.push_back(SlotEntry{ 0, kSlotFree, 0, nullptr });
	}
	else
		return 0;
	SlotEntry &entry = slots_[slot];
	entry.state = kSlotLive;
	entry.actor = actor;
	entry.map = 0;
	return slot;
}

int32_t HandleTable::HandleOf(uint32_t slot) const
{
	if (slot == 0 || slot >= slots_.size())
		return 0;
	const SlotEntry &entry = slots_[slot];
	return entry.state == kSlotLive || entry.state == kSlotParked ? ComputeHandle(slot, entry.generation) : 0;
}

const void *HandleTable::Resolve(int32_t handle) const
{
	const uint32_t slot = ComputeHandleSlot(handle);
	if (slot == 0 || slot >= slots_.size())
		return nullptr;
	const SlotEntry &entry = slots_[slot];
	return entry.state == kSlotLive && entry.generation == ComputeHandleGeneration(handle) ? entry.actor : nullptr;
}

void HandleTable::Release(uint32_t slot)
{
	if (slot == 0 || slot >= slots_.size() || slots_[slot].state != kSlotLive)
		return;
	SlotEntry &entry = slots_[slot];
	entry.actor = nullptr;
	entry.map = 0;
	entry.data.clear();
	// [Doruk-Aksoy] A generation is never handed out twice, so a slot past its last one retires.
	if (entry.generation >= kMaxGeneration)
	{
		entry.state = kSlotRetired;
		return;
	}
	++entry.generation;
	entry.state = kSlotFree;
	free_.push_back(slot);
}

void HandleTable::Park(uint32_t slot, int map)
{
	if (slot == 0 || slot >= slots_.size() || slots_[slot].state != kSlotLive)
		return;
	SlotEntry &entry = slots_[slot];
	entry.state = kSlotParked;
	entry.actor = nullptr;
	entry.map = map;
	for (int known : visitMaps_)
		if (known == map)
			return;
	visitMaps_.push_back(map);
	visitTimes_.push_back(clock_);
}

bool HandleTable::Attach(uint32_t slot, const void *actor)
{
	if (slot == 0 || slot >= slots_.size() || actor == nullptr)
		return false;
	SlotEntry &entry = slots_[slot];
	if (entry.state == kSlotParked || (entry.state == kSlotLive && entry.actor == nullptr))
	{
		entry.state = kSlotLive;
		entry.actor = actor;
		entry.map = 0;
		return true;
	}
	return false;
}

void HandleTable::ReleaseUnattached()
{
	for (uint32_t slot = 1; slot < slots_.size(); ++slot)
		if (slots_[slot].state == kSlotLive && slots_[slot].actor == nullptr)
			Release(slot);
}

void HandleTable::Visit(int map)
{
	++clock_;
	for (size_t i = 0; i < visitMaps_.size(); ++i)
	{
		if (visitMaps_[i] == map)
		{
			visitTimes_[i] = clock_;
			return;
		}
	}
	visitMaps_.push_back(map);
	visitTimes_.push_back(clock_);
}

void HandleTable::ReleaseParked(int map)
{
	for (uint32_t slot = 1; slot < slots_.size(); ++slot)
	{
		SlotEntry &entry = slots_[slot];
		if (entry.state == kSlotParked && entry.map == map)
		{
			entry.state = kSlotLive;
			Release(slot);
		}
	}
	for (size_t i = 0; i < visitMaps_.size(); ++i)
	{
		if (visitMaps_[i] == map)
		{
			visitMaps_.erase(visitMaps_.begin() + i);
			visitTimes_.erase(visitTimes_.begin() + i);
			break;
		}
	}
}

void HandleTable::ReleaseAllParked()
{
	while (!visitMaps_.empty())
		ReleaseParked(visitMaps_.front());
	// [Doruk-Aksoy] A slot parked for a map the visit list lost goes too.
	for (uint32_t slot = 1; slot < slots_.size(); ++slot)
		if (slots_[slot].state == kSlotParked)
			ReleaseParked(slots_[slot].map);
}

void HandleTable::Reset()
{
	slots_.assign(1, SlotEntry{ 0, kSlotFree, 0, nullptr });
	free_.clear();
	visitMaps_.clear();
	visitTimes_.clear();
	clock_ = 0;
}

DataResult HandleTable::SetData(uint32_t slot, int key, int32_t value)
{
	if (slot == 0 || slot >= slots_.size() || slots_[slot].state != kSlotLive)
		return kDataNoSlot;
	std::vector<DataPair> &data = slots_[slot].data;
	for (DataPair &pair : data)
	{
		if (pair.key == key)
		{
			pair.value = value;
			return kDataSet;
		}
	}
	if (data.size() >= kMaxDataKeys)
		return kDataFull;
	data.push_back(DataPair{ key, value });
	return kDataSet;
}

bool HandleTable::GetData(uint32_t slot, int key, int32_t &value) const
{
	if (slot == 0 || slot >= slots_.size() || slots_[slot].state != kSlotLive)
		return false;
	for (const DataPair &pair : slots_[slot].data)
	{
		if (pair.key == key)
		{
			value = pair.value;
			return true;
		}
	}
	return false;
}

void HandleTable::ClearData(uint32_t slot, int key)
{
	if (slot == 0 || slot >= slots_.size() || slots_[slot].state != kSlotLive)
		return;
	std::vector<DataPair> &data = slots_[slot].data;
	for (size_t i = 0; i < data.size(); ++i)
	{
		if (data[i].key == key)
		{
			data.erase(data.begin() + i);
			return;
		}
	}
}

void HandleTable::ClearAllData(uint32_t slot)
{
	if (slot != 0 && slot < slots_.size() && slots_[slot].state == kSlotLive)
		slots_[slot].data.clear();
}

size_t HandleTable::CountState(SlotState state) const
{
	size_t count = 0;
	for (uint32_t slot = 1; slot < slots_.size(); ++slot)
		count += slots_[slot].state == state ? 1 : 0;
	return count;
}

int HandleTable::OldestParkedMap() const
{
	int oldest = 0;
	uint64_t oldestTime = 0;
	for (size_t i = 0; i < visitMaps_.size(); ++i)
	{
		bool parked = false;
		for (uint32_t slot = 1; slot < slots_.size() && !parked; ++slot)
			parked = slots_[slot].state == kSlotParked && slots_[slot].map == visitMaps_[i];
		if (parked && (oldest == 0 || visitTimes_[i] < oldestTime))
		{
			oldest = visitMaps_[i];
			oldestTime = visitTimes_[i];
		}
	}
	return oldest;
}

TableState HandleTable::Save() const
{
	TableState state;
	state.slots.assign(slots_.begin() + 1, slots_.end());
	for (SlotEntry &entry : state.slots)
		entry.actor = nullptr;
	state.freeQueue.assign(free_.begin(), free_.end());
	state.visitMaps = visitMaps_;
	state.visitTimes = visitTimes_;
	state.clock = clock_;
	return state;
}

void HandleTable::Load(const TableState &state)
{
	Reset();
	for (const SlotEntry &saved : state.slots)
	{
		if (slots_.size() - 1 >= capacity_)
			break;
		SlotEntry entry = saved;
		entry.actor = nullptr;
		if (entry.generation > kMaxGeneration || entry.state > kSlotRetired)
			entry = SlotEntry{ kMaxGeneration, kSlotRetired, 0, nullptr };
		slots_.push_back(entry);
	}
	std::vector<bool> queued(slots_.size(), false);
	for (uint32_t slot : state.freeQueue)
	{
		if (slot != 0 && slot < slots_.size() && slots_[slot].state == kSlotFree && !queued[slot])
		{
			free_.push_back(slot);
			queued[slot] = true;
		}
	}
	// [Doruk-Aksoy] A free slot the saved queue left out joins its end rather than being lost.
	for (uint32_t slot = 1; slot < slots_.size(); ++slot)
		if (slots_[slot].state == kSlotFree && !queued[slot])
			free_.push_back(slot);
	const size_t visits = state.visitMaps.size() < state.visitTimes.size() ? state.visitMaps.size() : state.visitTimes.size();
	visitMaps_.assign(state.visitMaps.begin(), state.visitMaps.begin() + visits);
	visitTimes_.assign(state.visitTimes.begin(), state.visitTimes.begin() + visits);
	clock_ = state.clock;
}

}} // namespace zx::acshandles
