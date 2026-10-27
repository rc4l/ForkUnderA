// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] A count many threads add to at once: each adds to a slot of its own, on its own cache line, and a read sums them, so the adds never fight over one line.

#ifndef ZX_TELEMETRY_SHARDEDCOUNTER_COMPUTE_H
#define ZX_TELEMETRY_SHARDEDCOUNTER_COMPUTE_H

#include <atomic>

namespace zx { namespace telemetry {

// [Doruk-Aksoy] The slot the `n`th thread to count takes, among `slots`.
inline int ComputeCounterSlot(unsigned n, int slots) { return (int)(n % (unsigned)slots); }

// [Doruk-Aksoy] kSlots counts a cache line apart; constant-initialised, so a static constructor's add before any other code runs counts too.
template <int kSlots> struct ShardedCounter
{
	struct alignas(64) Slot { std::atomic<long long> value{ 0 }; };
	Slot slots[kSlots];

	void Add(int slot, long long n) { slots[slot].value.fetch_add(n, std::memory_order_relaxed); }
	long long Sum() const
	{
		long long total = 0;
		for (const Slot &s : slots) total += s.value.load(std::memory_order_relaxed);
		return total;
	}
};

}} // namespace zx::telemetry

#endif // ZX_TELEMETRY_SHARDEDCOUNTER_COMPUTE_H
