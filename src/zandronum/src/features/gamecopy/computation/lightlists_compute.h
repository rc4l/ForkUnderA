// [Doruk-Aksoy] Each subsector's run of lights in one list, made from the subsectors the lights touch rather than a walk of every subsector.
#ifndef ZX_LIGHTLISTS_COMPUTE_H
#define ZX_LIGHTLISTS_COMPUTE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace zx { namespace gamecopy {

// [Doruk-Aksoy] The touched subsectors as a bitset over numSubsectors, repeats and indices out of range ignored.
void ComputeLitSubsectors(const int *touched, int count, int numSubsectors, std::vector<uint64_t> &bits);

// [Doruk-Aksoy] Each subsector's run in out, in subsector order: a lit one's from lightsOf(s, out), an unlit one empty at the end so far, as a walk of every subsector makes them.
template <class LightsOf>
void ComputeSubsectorRuns(const std::vector<uint64_t> &bits, int numSubsectors, std::vector<int> &first, std::vector<int> &count, std::vector<int> &out,
	LightsOf lightsOf)
{
	first.resize(numSubsectors > 0 ? numSubsectors : 0);
	count.resize(first.size());
	out.clear();
	for (int s = 0; s < numSubsectors; s++)
	{
		first[s] = (int)out.size();
		if ((size_t)(s >> 6) < bits.size() && ((bits[s >> 6] >> (s & 63)) & 1)) lightsOf(s, out);
		count[s] = (int)out.size() - first[s];
	}
}

}} // namespace zx::gamecopy

#endif
