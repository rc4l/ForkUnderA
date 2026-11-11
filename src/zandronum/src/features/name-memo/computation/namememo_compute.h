// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Pure rules for a lookup by name remembered by the name's pointer, which a script passes the same each call, valid for one generation of what it looks up.
#ifndef ZX_NAMEMEMO_COMPUTE_H
#define ZX_NAMEMEMO_COMPUTE_H

#include <memory>

constexpr int kZxNameMemoSlots = 4096;

// [Doruk-Aksoy] One slot per pointer hash, holding the last name pointer stored there and what it found, all under one generation.
struct ZxNameMemo
{
	const void *key[kZxNameMemoSlots] = {};
	void *value[kZxNameMemoSlots] = {};
	unsigned generation = 0;
};

// [Doruk-Aksoy] The memo a thread keeps in holder, made on its first lookup, so only threads that look names up pay its 64 KB.
ZxNameMemo &ComputeNameMemoFor(std::unique_ptr<ZxNameMemo> &holder);

// [Doruk-Aksoy] The slot a name pointer goes in.
int ComputeNameMemoSlot(const void *key);

// [Doruk-Aksoy] What key found under generation, or NULL when it was not stored or the generation has moved on.
void *ComputeNameMemoFind(const ZxNameMemo &memo, const void *key, unsigned generation);

// [Doruk-Aksoy] Remembers what key found, first forgetting every slot when the generation has moved on.
void ComputeNameMemoStore(ZxNameMemo &memo, const void *key, void *value, unsigned generation);

#endif
