// [Doruk-Aksoy] Each sector's actors linked from what one walk of the actor list notes of each, its sector chain's next and whether it heads a sector, rather than a second walk of every sector's chain.
#ifndef ZX_ACTORCHAINS_COMPUTE_H
#define ZX_ACTORCHAINS_COMPUTE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace zx { namespace gamecopy {

// [Doruk-Aksoy] What an actor's chain back-link says: in no chain, or in one after another actor (a sector index otherwise).
const int kChainNone = -2;
const int kChainAfterActor = -1;

// [Doruk-Aksoy] The back-link UnlinkFromWorld leaves on an actor it took out of its sector's chain.
const uintptr_t kUnlinkedPrev = 0xBeefCafe;

// [Doruk-Aksoy] The sector whose chain head prev is, given the first sector's head and the sectors' stride, or what else it points at.
int ComputeChainHead(uintptr_t prev, uintptr_t firstHead, size_t stride, int numSectors);

// [Doruk-Aksoy] Each sector's first listed actor and each listed actor's next listed one in its chain, from each place's head (ComputeChainHead's) and chain next (as many), placeOf naming a node's place or -1 and nextOf its own next; false when a chained actor was neither a head nor any listed actor's next, as under an unlisted head, which only a walk of the sectors finds.
template <class Node, class PlaceOf, class NextOf>
bool ComputeChainLinks(const std::vector<int> &head, const std::vector<Node *> &chainNext, int numSectors, PlaceOf placeOf, NextOf nextOf,
	std::vector<int> &firstIn, std::vector<int> &next)
{
	firstIn.assign(numSectors > 0 ? numSectors : 0, -1);
	next.assign(head.size(), -1);
	size_t chained = 0, linked = 0;
	for (size_t r = 0; r < head.size(); r++)
	{
		if (head[r] == kChainNone) continue;
		chained++;
		if (head[r] >= 0 && head[r] < numSectors)
		{
			firstIn[head[r]] = (int)r;
			linked++;
		}
		for (Node *n = chainNext[r]; n != nullptr; n = nextOf(n))
		{
			const int q = placeOf(n);
			if (q < 0) continue;
			next[r] = q;
			linked++;
			break;
		}
	}
	return linked == chained;
}

}} // namespace zx::gamecopy

#endif
