// [Doruk-Aksoy] The seen bits a fill keeps.
#include "features/gamecopy/computation/seenmerge_compute.h"

namespace zx { namespace gamecopy {

void ComputeSeenMerge(std::vector<unsigned char> &bits, const std::vector<std::pair<int, unsigned char>> &changed, const std::vector<int> &marked)
{
	const int n = (int)bits.size();
	for (const std::pair<int, unsigned char> &c : changed) if (c.first >= 0 && c.first < n) bits[c.first] = c.second;
	for (int i : marked) if (i >= 0 && i < n) bits[i] = 1;
}

}} // namespace zx::gamecopy
