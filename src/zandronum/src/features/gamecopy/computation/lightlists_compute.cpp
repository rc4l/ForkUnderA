// [Doruk-Aksoy] The subsectors the lights touch, as a bitset.
#include "features/gamecopy/computation/lightlists_compute.h"

namespace zx { namespace gamecopy {

void ComputeLitSubsectors(const int *touched, int count, int numSubsectors, std::vector<uint64_t> &bits)
{
	bits.assign(numSubsectors > 0 ? (size_t)(numSubsectors + 63) / 64 : 0, 0);
	for (int i = 0; i < count; i++)
	{
		const int s = touched[i];
		if (s >= 0 && s < numSubsectors) bits[s >> 6] |= 1ull << (s & 63);
	}
}

}} // namespace zx::gamecopy
