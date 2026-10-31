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

}} // namespace zx::gamecopy
