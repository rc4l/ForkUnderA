// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/hwrender/computation/slotrecycle_compute.h"

#include <algorithm>
#include <cstddef>

namespace zx { namespace hwrender {

bool ComputeMayRecycle(const SlotTenure &s, uint32_t current)
{
	return !s.pinned && s.reserved + 1u < current;
}

std::vector<int> ComputeRecycleOrder(const std::vector<SlotTenure> &slots, uint32_t current)
{
	std::vector<int> order;
	for (size_t i = 0; i < slots.size(); i++)
		if (ComputeMayRecycle(slots[i], current)) order.push_back((int)i);
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return slots[a].reserved < slots[b].reserved; });
	return order;
}

void ComputeSlotsToWrite(const std::vector<uint32_t> &changedGen, uint32_t filledGen, bool fresh, unsigned count, std::vector<unsigned> &out)
{
	out.clear();
	for (unsigned i = 0; i < count; i++)
		if (fresh || (i < changedGen.size() && changedGen[i] > filledGen)) out.push_back(i);
}

int ComputeSlotsChangedAfterFill(const std::vector<uint32_t> &changedGen, int matSlot, int brightSlot, uint32_t filledGen)
{
	auto changed = [&](int slot) { return (size_t)slot >= changedGen.size() || changedGen[slot] > filledGen; };
	return (matSlot >= 0 && changed(matSlot) ? 1 : 0) + (brightSlot > 0 && changed(brightSlot) ? 1 : 0);
}

bool ComputeRetryWhite(int &tries, int bound)
{
	if (tries >= bound) return false;
	tries++;
	return true;
}

}} // namespace zx::hwrender
