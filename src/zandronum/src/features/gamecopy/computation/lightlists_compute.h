// [Doruk-Aksoy] Each subsector's run of lights in one list, made from the subsectors the lights touch rather than a walk of every subsector.
#ifndef ZX_LIGHTLISTS_COMPUTE_H
#define ZX_LIGHTLISTS_COMPUTE_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "features/jobs/computation/joinparts_compute.h"

namespace zx { namespace gamecopy {

// [Doruk-Aksoy] The subsectors each lane's lights touch as a bitset over numSubsectors, repeats and indices out of range ignored.
void ComputeLitSubsectors(const std::vector<zx::jobs::Part<int>> &touched, int numSubsectors, std::vector<uint64_t> &bits);

// [Doruk-Aksoy] Each run of subsectors [lo, hi) appended to out in subsector order, its first counted from out's start: a lit one's from lightsOf(s, out), an unlit one empty; first and count already hold every subsector.
template <class LightsOf>
void ComputeSubsectorRuns(const std::vector<uint64_t> &bits, int lo, int hi, std::vector<int> &first, std::vector<int> &count, std::vector<int> &out, LightsOf lightsOf)
{
	for (int s = lo; s < hi; s++)
	{
		first[s] = (int)out.size();
		if ((size_t)(s >> 6) < bits.size() && ((bits[s >> 6] >> (s & 63)) & 1)) lightsOf(s, out);
		count[s] = (int)out.size() - first[s];
	}
}

// [Doruk-Aksoy] Each subsector's first moved to where the part of its chunk of chunk subsectors landed once the parts were joined, starts[k] chunk k's.
void ComputeShiftRuns(const std::vector<int> &starts, int chunk, std::vector<int> &first);

}} // namespace zx::gamecopy

#endif
