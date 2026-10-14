// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/dynlights/computation/nodeindex_compute.h"

#include <stddef.h>
#include <stdint.h>

namespace zx { namespace dynlights {

namespace {

unsigned Slot(const PointerIndex &ix, const void *key)
{
	return (unsigned)(((unsigned long long)(uintptr_t)key * 0x9E3779B97F4A7C15ull) >> 32) & ix.mask;
}

void Insert(PointerIndex &ix, const void *key, void *value)
{
	unsigned i = Slot(ix, key);
	while (ix.keys[i] != NULL && ix.keys[i] != key) i = (i + 1) & ix.mask;
	if (ix.keys[i] == NULL) { ix.keys[i] = key; ix.used++; }
	ix.values[i] = value;
}

} // namespace

void ComputePointerIndexReset(PointerIndex &ix, int expected)
{
	unsigned size = 16;
	while (size < (unsigned)expected * 2) size *= 2;
	if (ix.keys.size() < size) { ix.keys.resize(size); ix.values.resize(size); }
	size = (unsigned)ix.keys.size();
	ix.keys.assign(size, NULL);
	ix.mask = size - 1;
	ix.used = 0;
}

void ComputePointerIndexPut(PointerIndex &ix, const void *key, void *value)
{
	if ((unsigned)(ix.used + 1) * 2 > ix.mask + 1)
	{
		const std::vector<const void *> keys(ix.keys);
		const std::vector<void *> values(ix.values);
		const unsigned size = (ix.mask + 1) * 2;
		ix.keys.assign(size, NULL);
		ix.values.resize(size);
		ix.mask = size - 1;
		ix.used = 0;
		for (size_t i = 0; i < keys.size(); i++) if (keys[i] != NULL) Insert(ix, keys[i], values[i]);
	}
	Insert(ix, key, value);
}

void *ComputePointerIndexFind(const PointerIndex &ix, const void *key)
{
	if (ix.keys.empty()) return NULL;
	for (unsigned i = Slot(ix, key); ix.keys[i] != NULL; i = (i + 1) & ix.mask)
		if (ix.keys[i] == key) return ix.values[i];
	return NULL;
}

}} // namespace zx::dynlights
