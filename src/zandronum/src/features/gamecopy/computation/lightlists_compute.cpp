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
	// [Doruk-Aksoy] Chunk by chunk rather than a division a subsector, and a chunk whose part landed at the start left alone.
	const size_t n = first.size(), step = (size_t)chunk;
	for (size_t k = 0, lo = 0; k < starts.size() && lo < n; k++, lo += step)
	{
		const int shift = starts[k];
		if (shift == 0) continue;
		const size_t hi = lo + step < n ? lo + step : n;
		for (size_t s = lo; s < hi; s++) first[s] += shift;
	}
}

}} // namespace zx::gamecopy
