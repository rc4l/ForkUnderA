// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/cvar-memo/computation/cvarmemo_compute.h"

#include <cstdint>

int ComputeCVarMemoSlot(const void *key)
{
	const uintptr_t k = reinterpret_cast<uintptr_t>(key);
	return int(((k >> 3) ^ (k >> 11)) & (kZxCVarMemoSlots - 1));
}

void *ComputeCVarMemoFind(const ZxCVarMemo &memo, const void *key, unsigned generation)
{
	if (memo.generation != generation) return nullptr;
	const int slot = ComputeCVarMemoSlot(key);
	return memo.key[slot] == key ? memo.value[slot] : nullptr;
}

void ComputeCVarMemoStore(ZxCVarMemo &memo, const void *key, void *value, unsigned generation)
{
	if (memo.generation != generation)
	{
		for (int i = 0; i < kZxCVarMemoSlots; i++)
		{
			memo.key[i] = nullptr;
			memo.value[i] = nullptr;
		}
		memo.generation = generation;
	}
	const int slot = ComputeCVarMemoSlot(key);
	memo.key[slot] = key;
	memo.value[slot] = value;
}
