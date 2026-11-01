// [Doruk-Aksoy] The seen bits a fill keeps for the sectors or lines of one kind: the last fill's, the changed ones' live bits over them, and what the frames marked since over those.
#ifndef ZX_SEENMERGE_COMPUTE_H
#define ZX_SEENMERGE_COMPUTE_H

#include <utility>
#include <vector>

namespace zx { namespace gamecopy {

// [Doruk-Aksoy] bits updated in place, each index out of range ignored.
void ComputeSeenMerge(std::vector<unsigned char> &bits, const std::vector<std::pair<int, unsigned char>> &changed, const std::vector<int> &marked);

// [Doruk-Aksoy] Where a fill takes the sectors' and lines' seen bits from: the live map read whole, the tic's end scan, or the fill's own whole compare of them.
enum SeenSource { kSeenReadWhole, kSeenFromScan, kSeenFromFill };

// [Doruk-Aksoy] sized when the bits held are of this map's sizes, subsectorsScanned when the tic's end read this map's subsectors.
SeenSource ComputeSeenSource(bool scanned, bool sized, bool subsectorsScanned);

}} // namespace zx::gamecopy

#endif
