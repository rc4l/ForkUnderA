// [Doruk-Aksoy] Each sector's actors linked from what one walk of the actor list notes of each, its sector chain's next and whether it heads a sector, rather than a second walk of every sector's chain.
#ifndef ZX_ACTORCHAINS_COMPUTE_H
#define ZX_ACTORCHAINS_COMPUTE_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "features/jobs/computation/joinparts_compute.h"   // [Doruk-Aksoy] Padded

namespace zx { namespace gamecopy {

// [Doruk-Aksoy] What an actor's chain back-link says: in no chain, or in one after another actor (a sector index otherwise).
const int kChainNone = -2;
const int kChainAfterActor = -1;

// [Doruk-Aksoy] The back-link UnlinkFromWorld leaves on an actor it took out of its sector's chain.
const uintptr_t kUnlinkedPrev = 0xBeefCafe;

// [Doruk-Aksoy] The sector whose chain head prev is, given the first sector's head and the sectors' stride, or what else it points at.
int ComputeChainHead(uintptr_t prev, uintptr_t firstHead, size_t stride, int numSectors);

// [Doruk-Aksoy] How many places of a range are in a chain and how many of those are linked.
struct ChainCount
{
	size_t chained = 0, linked = 0;
};

// [Doruk-Aksoy] A range's count padded, so ranges counted on other threads never share a line.
typedef zx::jobs::Padded<ChainCount> RangeCount;

// [Doruk-Aksoy] firstIn and next sized for numSectors and places and every link cleared, before ComputeChainLinksIn fills them.
void ComputeChainLinksBegin(size_t places, int numSectors, std::vector<int> &firstIn, std::vector<int> &next);

// [Doruk-Aksoy] Whether every chained place of the ranges counted was linked, as ComputeChainLinks answers.
bool ComputeChainLinksWhole(const std::vector<RangeCount> &counts);

// [Doruk-Aksoy] ComputeChainLinks' links of places [lo, hi) alone, counted into count, so ranges apart can be linked on threads of their own.
template <class Node, class PlaceOf, class NextOf>
void ComputeChainLinksIn(size_t lo, size_t hi, const std::vector<int> &head, const std::vector<Node *> &chainNext, int numSectors, PlaceOf placeOf, NextOf nextOf,
	std::vector<int> &firstIn, std::vector<int> &next, ChainCount &count)
{
	for (size_t r = lo; r < hi; r++)
	{
		if (head[r] == kChainNone) continue;
		count.chained++;
		if (head[r] >= 0 && head[r] < numSectors)
		{
			firstIn[head[r]] = (int)r;
			count.linked++;
		}
		for (Node *n = chainNext[r]; n != nullptr; n = nextOf(n))
		{
			const int q = placeOf(n);
			if (q < 0) continue;
			next[r] = q;
			count.linked++;
			break;
		}
	}
}

// [Doruk-Aksoy] Each sector's first listed actor and each listed actor's next listed one in its chain, from each place's head (ComputeChainHead's) and chain next (as many), placeOf naming a node's place or -1 and nextOf its own next; false when a chained actor was neither a head nor any listed actor's next, as under an unlisted head, which only a walk of the sectors finds.
template <class Node, class PlaceOf, class NextOf>
bool ComputeChainLinks(const std::vector<int> &head, const std::vector<Node *> &chainNext, int numSectors, PlaceOf placeOf, NextOf nextOf,
	std::vector<int> &firstIn, std::vector<int> &next)
{
	ComputeChainLinksBegin(head.size(), numSectors, firstIn, next);
	std::vector<RangeCount> counts(1);
	ComputeChainLinksIn(0, head.size(), head, chainNext, numSectors, placeOf, nextOf, firstIn, next, counts[0].value);
	return ComputeChainLinksWhole(counts);
}

}} // namespace zx::gamecopy

#endif
