// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/materialindex_compute.h"

#include <cstddef>

namespace zx { namespace hwrender {

namespace {

const unsigned kFirstBuckets = 64;

// [rc4l] Where a key's probe ends: its own bucket, or the empty one it would take.
unsigned Probe(const MaterialIndex &index, const void *material, int translation)
{
	const unsigned size = (unsigned)index.buckets.size();
	unsigned i = ComputeMaterialIndexHash(material, translation, size);
	while (index.buckets[i].value >= 0 && !(index.buckets[i].material == material && index.buckets[i].translation == translation))
		i = (i + 1) & (size - 1);
	return i;
}

void Grow(MaterialIndex &index)
{
	std::vector<MaterialIndex::Entry> old;
	old.swap(index.buckets);
	index.buckets.assign(old.empty() ? kFirstBuckets : old.size() * 2, MaterialIndex::Entry{ nullptr, 0, -1 });
	index.count = 0;
	for (const MaterialIndex::Entry &e : old)
		if (e.value >= 0) ComputeMaterialIndexInsert(index, e.material, e.translation, e.value);
}

}

unsigned ComputeMaterialIndexHash(const void *material, int translation, unsigned size)
{
	unsigned long long h = (unsigned long long)(size_t)material;
	h ^= (unsigned long long)(unsigned)translation * 0x9E3779B97F4A7C15ull;
	h ^= h >> 29; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 32;
	return (unsigned)(h & (size - 1));
}

void ComputeMaterialIndexClear(MaterialIndex &index)
{
	for (MaterialIndex::Entry &e : index.buckets) e.value = -1;
	index.count = 0;
}

void ComputeMaterialIndexInsert(MaterialIndex &index, const void *material, int translation, int value)
{
	if ((index.count + 1) * 2 > index.buckets.size()) Grow(index);
	MaterialIndex::Entry &e = index.buckets[Probe(index, material, translation)];
	if (e.value >= 0) return;
	e = MaterialIndex::Entry{ material, translation, value };
	index.count++;
}

int ComputeMaterialIndexFind(const MaterialIndex &index, const void *material, int translation)
{
	if (index.buckets.empty()) return -1;
	return index.buckets[Probe(index, material, translation)].value;
}

}} // namespace zx::hwrender
