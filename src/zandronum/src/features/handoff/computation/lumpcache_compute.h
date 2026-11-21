// SPDX-License-Identifier: GPL-3.0-or-later
//
// [MGOOOOOO] When a lump's text kept from an earlier read must be read again, so a lookup a tic repeats asks the main thread to read it once.

#ifndef ZX_LUMPCACHE_COMPUTE_H
#define ZX_LUMPCACHE_COMPUTE_H

namespace zx { namespace handoff {

// [MGOOOOOO] True when nothing is kept yet (cachedLump below 0) or the archives now give the lump another number, file or length, as after a WAD change.
bool ComputeLumpCacheStale(int cachedLump, int cachedFile, int cachedLength, int lump, int file, int length);

}} // namespace zx::handoff

#endif
