// [Doruk-Aksoy] The ACS string pool's growth after a collection.
#include "features/acs-strings/computation/stringpool_compute.h"

namespace zx { namespace acsstrings {

unsigned ComputeStringPoolGrowth(unsigned freed, unsigned size)
{
	return (unsigned long long)freed * 4 < size ? size : 0;
}

int ComputeTagCacheFind(const TagCache &cache, unsigned generation, unsigned index)
{
	if (cache.generation != generation || index >= cache.ids.size()) return -1;
	return cache.ids[index];
}

void ComputeTagCacheStore(TagCache &cache, unsigned generation, unsigned index, int id)
{
	if (cache.generation != generation)
	{
		cache.ids.assign(cache.ids.size(), -1);
		cache.generation = generation;
	}
	if (id == -1 || index >= kTagCacheMaxIndex) return;
	if (index >= cache.ids.size()) cache.ids.resize(index + 1, -1);
	cache.ids[index] = id;
}

}} // namespace zx::acsstrings
