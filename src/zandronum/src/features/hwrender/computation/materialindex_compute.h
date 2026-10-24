// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A material and translation looked up in constant time: the slot table's index and the texture cache's, which a walk over thousands of Complex Doom sprite frames for every slot of every refill made 11% of a fight's frame.

#ifndef ZX_MATERIALINDEX_COMPUTE_H
#define ZX_MATERIALINDEX_COMPUTE_H

#include <vector>

namespace zx { namespace hwrender {

// [rc4l] Open addressing on (material, translation) to a value, its buckets doubled past half full; a NULL material is a key like any other.
struct MaterialIndex
{
	struct Entry { const void *material; int translation; int value; };
	std::vector<Entry> buckets;   // [rc4l] a power of two of them, value below 0 for an empty one
	unsigned count = 0;
};

// [rc4l] The bucket a key starts its probe at in a table of size buckets, a power of two.
unsigned ComputeMaterialIndexHash(const void *material, int translation, unsigned size);

// [rc4l] Empties the index, keeping its buckets.
void ComputeMaterialIndexClear(MaterialIndex &index);

// [rc4l] Maps the key to value (0 or more), the first mapping kept if the key is already there.
void ComputeMaterialIndexInsert(MaterialIndex &index, const void *material, int translation, int value);

// [rc4l] The key's value, -1 when absent.
int ComputeMaterialIndexFind(const MaterialIndex &index, const void *material, int translation);

}} // namespace zx::hwrender

#endif // ZX_MATERIALINDEX_COMPUTE_H
