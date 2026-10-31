// [Doruk-Aksoy] The seen bits a fill keeps for the sectors or lines of one kind: the last fill's, the changed ones' live bits over them, and what the frames marked since over those.
#ifndef ZX_SEENMERGE_COMPUTE_H
#define ZX_SEENMERGE_COMPUTE_H

#include <utility>
#include <vector>

namespace zx { namespace gamecopy {

// [Doruk-Aksoy] bits updated in place, each index out of range ignored.
void ComputeSeenMerge(std::vector<unsigned char> &bits, const std::vector<std::pair<int, unsigned char>> &changed, const std::vector<int> &marked);

}} // namespace zx::gamecopy

#endif
