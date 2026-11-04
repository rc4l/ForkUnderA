// [Doruk-Aksoy] How much room the ACS string pool gains after a collection, so a pool that stays nearly full is not collected again a few strings later.
#ifndef ZX_STRINGPOOL_COMPUTE_H
#define ZX_STRINGPOOL_COMPUTE_H

#include <vector>

namespace zx { namespace acsstrings {

// [Doruk-Aksoy] Entries to add to the room of a pool of size entries after a collection freed freed of them: size when under a quarter came free, so the next collection waits for that many new strings, else none.
unsigned ComputeStringPoolGrowth(unsigned freed, unsigned size);

// [Doruk-Aksoy] A module's string literals as the pool ids PCD_TAGSTRING gave them, valid for one generation of the pool: ids only change when strings are purged, cleared or loaded.
struct TagCache
{
	std::vector<int> ids;
	unsigned generation = 0;
};

// [Doruk-Aksoy] Literal indexes from here on are not cached, so a malformed index cannot grow the cache without bound.
constexpr unsigned kTagCacheMaxIndex = 1u << 16;

// [Doruk-Aksoy] The pool id cached for the module's literal index under the pool's generation, or -1 when there is none.
int ComputeTagCacheFind(const TagCache &cache, unsigned generation, unsigned index);

// [Doruk-Aksoy] Caches id for index, forgetting every id first when the pool's generation has moved on; a failed id (-1) or an index from kTagCacheMaxIndex on is not kept.
void ComputeTagCacheStore(TagCache &cache, unsigned generation, unsigned index, int id);

}} // namespace zx::acsstrings

#endif
