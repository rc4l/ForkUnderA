// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/netids/computation/netidsearch_compute.h"

namespace zx { namespace netids {

namespace {

// [rc4l] The first set bit in [lo, hi), or hi when there is none.
unsigned FirstSet(const uint64_t *bits, unsigned lo, unsigned hi)
{
	while (lo < hi)
	{
		uint64_t word = bits[lo >> 6] >> (lo & 63);
		if (word == 0) { lo = (lo | 63) + 1; continue; }
		while ((word & 1) == 0) { word >>= 1; lo++; }
		return lo < hi ? lo : hi;
	}
	return hi;
}

} // namespace

void ComputeMarkId(uint64_t *bits, unsigned id, bool free)
{
	const uint64_t bit = 1ull << (id & 63);
	if (free) bits[id >> 6] |= bit;
	else bits[id >> 6] &= ~bit;
}

bool ComputeIdFree(const uint64_t *bits, unsigned id)
{
	return ((bits[id >> 6] >> (id & 63)) & 1) != 0;
}

unsigned ComputeNextFreeId(const uint64_t *bits, unsigned from)
{
	const unsigned after = FirstSet(bits, from + 1, kIdCount);
	if (after < kIdCount) return after;
	return FirstSet(bits, 1, from);
}

}} // namespace zx::netids
