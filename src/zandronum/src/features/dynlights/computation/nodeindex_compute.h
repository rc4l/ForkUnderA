// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A table from a pointer to a pointer: a relinking light's nodes by what they link to, so finding one is a lookup and not a walk of its list.

#ifndef ZX_NODEINDEX_COMPUTE_H
#define ZX_NODEINDEX_COMPUTE_H

#include <vector>

namespace zx { namespace dynlights {

// [rc4l] Open addressing with linear probing, emptied and reused per relink so it allocates only while it grows.
struct PointerIndex
{
	std::vector<const void *> keys;   // [rc4l] NULL for an empty slot
	std::vector<void *> values;
	unsigned mask;
	int used;
};

// [rc4l] Empty the index, with room for at least `expected` entries before it grows.
void ComputePointerIndexReset(PointerIndex &ix, int expected);

// [rc4l] Map non-null `key` to `value`, replacing what it mapped to before; the table doubles once half full.
void ComputePointerIndexPut(PointerIndex &ix, const void *key, void *value);

// [rc4l] What `key` maps to, NULL for a key never put since the last reset.
void *ComputePointerIndexFind(const PointerIndex &ix, const void *key);

}} // namespace zx::dynlights

#endif // ZX_NODEINDEX_COMPUTE_H
