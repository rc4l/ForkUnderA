// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Each thinker list mirrored as an array in its order, so the actors are read in thinker order from arrays instead of a walk down the lists, one dependent load an actor.
#ifndef ZX_THINKERORDER_COMPUTE_H
#define ZX_THINKERORDER_COMPUTE_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace zx { namespace gamecopy {

// [Doruk-Aksoy] The bucket of the list at address list: the i-th of settledCount lists from settled is i, the i-th of freshCount lists from fresh is settledCount + i (each size bytes), any other -1.
int ComputeListBucket(uintptr_t list, uintptr_t settled, int settledCount, uintptr_t fresh, int freshCount, size_t size);

// [Doruk-Aksoy] Each bucket's entries in its list's order, NULL where one has left, and how many have.
struct OrderBuckets
{
	std::vector<std::vector<void *>> items;
	std::vector<int> gone;
};

// [Doruk-Aksoy] item appended to bucket's end, as a list's AddTail puts it; its slot, or -1 for a bucket below 0.
int ComputeOrderAppend(OrderBuckets &b, int bucket, void *item);
// [Doruk-Aksoy] item taken out of bucket's slot, as a list's Remove takes it; false when that slot does not hold it.
bool ComputeOrderRemove(OrderBuckets &b, int bucket, int slot, const void *item);
// [Doruk-Aksoy] Whether a bucket holds more left entries than kept ones, worth closing up.
bool ComputeOrderSparse(const OrderBuckets &b, int bucket);
// [Doruk-Aksoy] A bucket's left entries closed up, each kept one that moved given its new slot through moved.
void ComputeOrderCompact(OrderBuckets &b, int bucket, const std::function<void(void *, int)> &moved);

// [Doruk-Aksoy] Slots [lo, hi) of one bucket, a piece of a walk across buckets that lanes take apart.
struct OrderChunk
{
	int bucket, lo, hi;
};
// [Doruk-Aksoy] The slots of each bucket named, in the order named, cut into chunks of at most chunk slots, none for an empty or missing bucket.
void ComputeOrderChunks(const OrderBuckets &b, const std::vector<int> &buckets, int chunk, std::vector<OrderChunk> &out);

}} // namespace zx::gamecopy

#endif // ZX_THINKERORDER_COMPUTE_H
