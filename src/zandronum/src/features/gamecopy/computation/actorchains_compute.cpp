// [Doruk-Aksoy] Which sector's chain an actor's back-link heads.
#include "features/gamecopy/computation/actorchains_compute.h"

namespace zx { namespace gamecopy {

int ComputeChainHead(uintptr_t prev, uintptr_t firstHead, size_t stride, int numSectors)
{
	if (prev == 0 || prev == kUnlinkedPrev) return kChainNone;
	if (prev < firstHead || stride == 0 || (prev - firstHead) % stride != 0) return kChainAfterActor;
	const uintptr_t s = (prev - firstHead) / stride;
	return s < (uintptr_t)(numSectors > 0 ? numSectors : 0) ? (int)s : kChainAfterActor;
}

void ComputeChainLinksBegin(size_t places, int numSectors, std::vector<int> &firstIn, std::vector<int> &next)
{
	firstIn.assign(numSectors > 0 ? numSectors : 0, -1);
	next.assign(places, -1);
}

bool ComputeChainLinksWhole(const std::vector<RangeCount> &counts)
{
	size_t chained = 0, linked = 0;
	for (const RangeCount &c : counts)
	{
		chained += c.value.chained;
		linked += c.value.linked;
	}
	return linked == chained;
}

}} // namespace zx::gamecopy
