// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/gamecopy/computation/thinkerorder_compute.h"
#include "features/gamecopy/computation/changelog_compute.h"   // [Doruk-Aksoy] ComputeIndexIn

#include <cstddef>

namespace zx { namespace gamecopy {

int ComputeListBucket(uintptr_t list, uintptr_t settled, int settledCount, uintptr_t fresh, int freshCount, size_t size)
{
	const int i = ComputeIndexIn(list, settled, size, settledCount);
	if (i >= 0) return i;
	const int f = ComputeIndexIn(list, fresh, size, freshCount);
	return f >= 0 ? settledCount + f : -1;
}

int ComputeOrderAppend(OrderBuckets &b, int bucket, void *item)
{
	if (bucket < 0) return -1;
	if ((size_t)bucket >= b.items.size())
	{
		b.items.resize(bucket + 1);
		b.gone.resize(bucket + 1, 0);
	}
	b.items[bucket].push_back(item);
	return (int)b.items[bucket].size() - 1;
}

bool ComputeOrderRemove(OrderBuckets &b, int bucket, int slot, const void *item)
{
	if (bucket < 0 || (size_t)bucket >= b.items.size() || slot < 0 || (size_t)slot >= b.items[bucket].size() || b.items[bucket][slot] != item || item == NULL)
		return false;
	b.items[bucket][slot] = NULL;
	b.gone[bucket]++;
	return true;
}

bool ComputeOrderSparse(const OrderBuckets &b, int bucket)
{
	return bucket >= 0 && (size_t)bucket < b.items.size() && b.gone[bucket] * 2 > (int)b.items[bucket].size();
}

void ComputeOrderCompact(OrderBuckets &b, int bucket, const std::function<void(void *, int)> &moved)
{
	if (bucket < 0 || (size_t)bucket >= b.items.size()) return;
	std::vector<void *> &items = b.items[bucket];
	size_t kept = 0;
	for (size_t i = 0; i < items.size(); i++)
	{
		if (items[i] == NULL) continue;
		if (kept != i)
		{
			items[kept] = items[i];
			moved(items[kept], (int)kept);
		}
		kept++;
	}
	items.resize(kept);
	b.gone[bucket] = 0;
}

}} // namespace zx::gamecopy
