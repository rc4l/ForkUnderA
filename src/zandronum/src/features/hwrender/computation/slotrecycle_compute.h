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

}} // namespace zx::hwrender

#endif // ZX_SLOTRECYCLE_COMPUTE_H
