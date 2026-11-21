// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] The bindless material array's slots given to new keys once the table is full: which slot gives way, and which slots a fill must write.

#ifndef ZX_SLOTRECYCLE_COMPUTE_H
#define ZX_SLOTRECYCLE_COMPUTE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace zx { namespace hwrender {

// [Doruk-Aksoy] What decides whether a slot may give way: pinned (the white one, or a baked piece's), and the dynamic reservation that last used it.
struct SlotTenure
{
	bool pinned;
	uint32_t reserved;
};

// [Doruk-Aksoy] Whether a slot may give way while reservation `current` runs: unpinned, and used by neither this reservation nor the one before, whose records may still be drawn.
bool ComputeMayRecycle(const SlotTenure &s, uint32_t current);

// [Doruk-Aksoy] Every slot that may give way, least recently reserved first and the lowest index first among equals.
std::vector<int> ComputeRecycleOrder(const std::vector<SlotTenure> &slots, uint32_t current);

// [Doruk-Aksoy] The slots a fill writes on a binding last filled at generation `filledGen`: all `count` when `fresh`, else those changed since, a slot past the table counting as the white it was filled with.
void ComputeSlotsToWrite(const std::vector<uint32_t> &changedGen, uint32_t filledGen, bool fresh, unsigned count, std::vector<unsigned> &out);

// [Doruk-Aksoy] How many of a piece's slots, its material's and its brightmap's (0 for none), changed after the fill at generation `filledGen`, and so sample what the slot held before.
int ComputeSlotsChangedAfterFill(const std::vector<uint32_t> &changedGen, int matSlot, int brightSlot, uint32_t filledGen);

// [Doruk-Aksoy] Whether a slot that came back white is filled again at the next fill, counting the try, up to `bound` tries for its key.
bool ComputeRetryWhite(int &tries, int bound);

// [Doruk-Aksoy] The slots of `count` keys found before any is made, so making one never takes a slot another key of the list holds: `find(k)` returns key k's slot, 0 for a key with nothing to find or -1 when missing, marking a found one as used, and `make(k)` then gives each missing key one.
template <class Find, class Make>
void ComputeReserveSlots(int count, Find find, Make make, std::vector<int> &slots)
{
	slots.resize(count > 0 ? (size_t)count : 0);
	for (int k = 0; k < count; k++) slots[k] = find(k);
	for (int k = 0; k < count; k++)
		if (slots[k] < 0) slots[k] = make(k);
}

// [Doruk-Aksoy] Whether slot is met for the first time in reservation `serial`, stamping it so the rest of that reservation reads false; a slot of 0 or less always reads true.
bool ComputeFirstInReservation(std::vector<unsigned int> &stamps, int slot, unsigned int serial);

// [Doruk-Aksoy] A direct-mapped memo of one value per (pointer, number) key, kept for one pass: a key asked again in the same pass reads what the first ask computed, and a collision computes again.
template <class V>
class ComputePassMemo
{
public:
	explicit ComputePassMemo(int bits = 10) : m_mask(((size_t)1 << bits) - 1), m_entries(m_mask + 1) {}
	template <class Compute>
	V Get(const void *a, int b, unsigned int pass, Compute compute)
	{
		const uintptr_t p = (uintptr_t)a;
		Entry &e = m_entries[(size_t)((p >> 4) ^ (p >> 14) ^ ((unsigned int)b * 2654435761u)) & m_mask];
		if (e.pass == pass && e.a == a && e.b == b) return e.value;
		e.a = a; e.b = b; e.pass = pass; e.value = compute();
		return e.value;
	}

private:
	struct Entry { const void *a = nullptr; int b = 0; unsigned int pass = 0; V value = V(); };
	size_t m_mask;
	std::vector<Entry> m_entries;
};

}} // namespace zx::hwrender

#endif // ZX_SLOTRECYCLE_COMPUTE_H
