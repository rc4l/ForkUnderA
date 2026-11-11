// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/name-memo/computation/namememo_compute.h"

#include <cstdint>

ZxNameMemo &ComputeNameMemoFor(std::unique_ptr<ZxNameMemo> &holder)
{
	if (!holder) holder.reset(new ZxNameMemo());
	return *holder;
}

int ComputeNameMemoSlot(const void *key)
{
	const uintptr_t k = reinterpret_cast<uintptr_t>(key);
	return int(((k >> 3) ^ (k >> 11)) & (kZxNameMemoSlots - 1));
}

void *ComputeNameMemoFind(const ZxNameMemo &memo, const void *key, unsigned generation)
{
	if (memo.generation != generation) return nullptr;
	const int slot = ComputeNameMemoSlot(key);
	return memo.key[slot] == key ? memo.value[slot] : nullptr;
}

void ComputeNameMemoStore(ZxNameMemo &memo, const void *key, void *value, unsigned generation)
{
	if (memo.generation != generation)
	{
		for (int i = 0; i < kZxNameMemoSlots; i++)
		{
			memo.key[i] = nullptr;
			memo.value[i] = nullptr;
		}
		memo.generation = generation;
	}
	const int slot = ComputeNameMemoSlot(key);
	memo.key[slot] = key;
	memo.value[slot] = value;
}
