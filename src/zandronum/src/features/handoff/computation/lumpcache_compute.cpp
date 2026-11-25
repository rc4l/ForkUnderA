// SPDX-License-Identifier: GPL-3.0-or-later

#include "features/handoff/computation/lumpcache_compute.h"

namespace zx { namespace handoff {

bool ComputeLumpCacheStale(int cachedLump, int cachedFile, int cachedLength, int lump, int file, int length)
{
	return cachedLump < 0 || cachedLump != lump || cachedFile != file || cachedLength != length;
}

}} // namespace zx::handoff
