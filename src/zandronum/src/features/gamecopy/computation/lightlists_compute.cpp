// [Doruk-Aksoy] The subsectors the lights touch, as a bitset.
#include "features/gamecopy/computation/lightlists_compute.h"

namespace zx { namespace gamecopy {

void ComputeLitSubsectors(const std::vector<zx::jobs::Part<int>> &touched, int numSubsectors, std::vector<uint64_t> &bits)
{
	bits.assign(numSubsectors > 0 ? (size_t)(numSubsectors + 63) / 64 : 0, 0);
	for (const zx::jobs::Part<int> &part : touched)
		for (const int s : part.items)
			if (s >= 0 && s < numSubsectors) bits[s >> 6] |= 1ull << (s & 63);
}

void ComputeShiftRuns(const std::vector<int> &starts, int chunk, std::vector<int> &first)
{
	if (chunk <= 0) return;
	for (size_t s = 0; s < first.size(); s++)
	{
		const size_t k = s / (size_t)chunk;
		if (k < starts.size()) first[s] += starts[k];
	}
}

}} // namespace zx::gamecopy
