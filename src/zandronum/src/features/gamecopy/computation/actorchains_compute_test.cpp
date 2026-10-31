// [Doruk-Aksoy] Tests for actorchains_compute: links made from one walk of the actor list match a walk of every sector's chain.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "features/gamecopy/computation/actorchains_compute.h"
#include "features/gamecopy/computation/recordlinks_compute.h"

using namespace zx::gamecopy;

namespace {

// [Doruk-Aksoy] An actor as its sector chain holds it, with its place in the list when it is listed.
struct Node
{
	Node *next = nullptr;
	Node **prev = nullptr;
	int place = -1;
};

// [Doruk-Aksoy] A sector, its chain head among other members as the game's is.
struct Sector
{
	int before[3];
	Node *thinglist = nullptr;
	double after[2];
};

uintptr_t Addr(const void *p) { return reinterpret_cast<uintptr_t>(p); }

int HeadOf(const Node &n, const std::vector<Sector> &sectors)
{
	return ComputeChainHead(Addr(n.prev), Addr(&sectors[0].thinglist), sizeof(Sector), (int)sectors.size());
}

// [Doruk-Aksoy] The links a walk of every sector's chain makes, listed nodes only.
void Reference(const std::vector<Sector> &sectors, int listed, std::vector<int> &firstIn, std::vector<int> &next)
{
	firstIn.resize(sectors.size());
	next.assign(listed, -1);
	RecordLinks links;
	for (size_t s = 0; s < sectors.size(); s++)
	{
		links.Begin(firstIn[s]);
		for (Node *n = sectors[s].thinglist; n != nullptr; n = n->next)
			if (n->place >= 0) links.Add(n->place, next[n->place]);
	}
}

// [Doruk-Aksoy] Puts node n at the head of sector s's chain, as LinkToWorld does.
void Link(Node &n, Sector &s)
{
	n.next = s.thinglist;
	if (n.next != nullptr) n.next->prev = &n.next;
	n.prev = &s.thinglist;
	s.thinglist = &n;
}

} // namespace

// [Doruk-Aksoy] A back-link names the sector it heads, and anything else is an actor's or no chain at all.
TEST(ActorChains, ABackLinkNamesTheSectorItHeads)
{
	std::vector<Sector> sectors(4);
	const uintptr_t first = Addr(&sectors[0].thinglist);
	EXPECT_EQ(kChainNone, ComputeChainHead(0, first, sizeof(Sector), 4));
	EXPECT_EQ(kChainNone, ComputeChainHead(kUnlinkedPrev, first, sizeof(Sector), 4));
	EXPECT_EQ(0, ComputeChainHead(first, first, sizeof(Sector), 4));
	EXPECT_EQ(2, ComputeChainHead(Addr(&sectors[2].thinglist), first, sizeof(Sector), 4));
	EXPECT_EQ(kChainAfterActor, ComputeChainHead(Addr(&sectors[2].after[0]), first, sizeof(Sector), 4));
	EXPECT_EQ(kChainAfterActor, ComputeChainHead(first - sizeof(Sector), first, sizeof(Sector), 4));
	EXPECT_EQ(kChainAfterActor, ComputeChainHead(first + 4 * sizeof(Sector), first, sizeof(Sector), 4));
	EXPECT_EQ(kChainAfterActor, ComputeChainHead(first, first, 0, 4));
	EXPECT_EQ(kChainAfterActor, ComputeChainHead(first, first, sizeof(Sector), 0));
}

// [Doruk-Aksoy] Chains with unlisted actors inside them and listed actors in no chain link as a walk of every sector does.
TEST(ActorChains, LinksFromTheListMatchAWalkOfEverySector)
{
	unsigned seed = 11;
	auto next = [&seed]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
	for (int round = 0; round < 30; round++)
	{
		std::vector<Sector> sectors(1 + next() % 40);
		std::vector<Node> nodes(next() % 200);
		std::vector<Node *> order;
		for (Node &n : nodes)
		{
			if (next() % 5 != 0) Link(n, sectors[next() % sectors.size()]);
			// [Doruk-Aksoy] Some never linked, some taken out again as UnlinkFromWorld leaves them.
			else if (next() % 2 == 0) n.prev = reinterpret_cast<Node **>(kUnlinkedPrev);
			if (next() % 6 != 0) order.push_back(&n);
		}
		// [Doruk-Aksoy] The list in an order of its own, as thinker order is not chain order.
		for (size_t i = order.size(); i > 1; i--) std::swap(order[i - 1], order[next() % i]);
		// [Doruk-Aksoy] An unlisted head is the one case the walk of the list cannot see, so every head here is listed.
		for (Sector &s : sectors)
			if (s.thinglist != nullptr && std::find(order.begin(), order.end(), s.thinglist) == order.end()) order.push_back(s.thinglist);
		for (size_t r = 0; r < order.size(); r++) order[r]->place = (int)r;
		std::vector<int> head;
		std::vector<Node *> chainNext;
		for (Node *n : order)
		{
			head.push_back(HeadOf(*n, sectors));
			chainNext.push_back(n->next);
		}
		std::vector<int> firstIn, links, wantFirst, wantNext;
		const bool whole = ComputeChainLinks(head, chainNext, (int)sectors.size(), [](Node *n) { return n->place; }, [](Node *n) { return n->next; }, firstIn, links);
		Reference(sectors, (int)order.size(), wantFirst, wantNext);
		EXPECT_TRUE(whole) << "round " << round;
		EXPECT_EQ(wantFirst, firstIn) << "round " << round;
		EXPECT_EQ(wantNext, links) << "round " << round;
	}
}

// [Doruk-Aksoy] A chain whose head is not listed cannot be linked from the list, and says so.
TEST(ActorChains, AnUnlistedHeadIsNoticed)
{
	std::vector<Sector> sectors(2);
	Node a, b, c;
	Link(c, sectors[1]);
	Link(b, sectors[1]);
	Link(a, sectors[1]);
	b.place = 0;
	c.place = 1;
	const std::vector<int> head = { HeadOf(b, sectors), HeadOf(c, sectors) };
	const std::vector<Node *> chainNext = { b.next, c.next };
	std::vector<int> firstIn, next;
	EXPECT_FALSE(ComputeChainLinks(head, chainNext, 2, [](Node *n) { return n->place; }, [](Node *n) { return n->next; }, firstIn, next));
	EXPECT_EQ((std::vector<int>{ -1, -1 }), firstIn);
	EXPECT_EQ((std::vector<int>{ 1, -1 }), next);
}

// [Doruk-Aksoy] A head past the sectors is left out and noticed, and no sectors make no first links.
TEST(ActorChains, AHeadPastTheSectorsIsLeftOut)
{
	const std::vector<int> head = { 3, kChainNone };
	const std::vector<Node *> chainNext = { nullptr, nullptr };
	std::vector<int> firstIn, next;
	EXPECT_FALSE(ComputeChainLinks(head, chainNext, 2, [](Node *n) { return n->place; }, [](Node *n) { return n->next; }, firstIn, next));
	EXPECT_EQ((std::vector<int>{ -1, -1 }), firstIn);
	EXPECT_TRUE(ComputeChainLinks(std::vector<int>{ kChainNone }, chainNext, 0, [](Node *n) { return n->place; }, [](Node *n) { return n->next; }, firstIn, next));
	EXPECT_TRUE(firstIn.empty());
	EXPECT_EQ((std::vector<int>{ -1 }), next);
}
