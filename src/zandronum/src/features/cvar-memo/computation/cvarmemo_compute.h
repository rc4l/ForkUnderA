// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Pure rules for a cvar lookup remembered by its name's pointer, which a script passes the same each call, valid for one generation of the cvar list.
#ifndef ZX_CVARMEMO_COMPUTE_H
#define ZX_CVARMEMO_COMPUTE_H

constexpr int kZxCVarMemoSlots = 256;

// [Doruk-Aksoy] One slot per pointer hash, holding the last name pointer stored there and what it found, all under one generation.
struct ZxCVarMemo
{
	const void *key[kZxCVarMemoSlots] = {};
	void *value[kZxCVarMemoSlots] = {};
	unsigned generation = 0;
};

// [Doruk-Aksoy] The slot a name pointer goes in.
int ComputeCVarMemoSlot(const void *key);

// [Doruk-Aksoy] What key found under generation, or NULL when it was not stored or the generation has moved on.
void *ComputeCVarMemoFind(const ZxCVarMemo &memo, const void *key, unsigned generation);

// [Doruk-Aksoy] Remembers what key found, first forgetting every slot when the generation has moved on.
void ComputeCVarMemoStore(ZxCVarMemo &memo, const void *key, void *value, unsigned generation);

#endif
