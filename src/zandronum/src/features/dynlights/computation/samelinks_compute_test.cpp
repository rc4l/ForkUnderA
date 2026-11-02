// [Doruk-Aksoy] Tests for samelinks_compute: a relink is skipped only when its walk reached the very targets the last one linked, in its order.
#include <gtest/gtest.h>

#include <vector>

#include "features/dynlights/computation/samelinks_compute.h"

using namespace zx::dynlights;

namespace {

// [Doruk-Aksoy] A reached target as the walk notes it: where it links and which of the light's lists it goes in.
struct Reached
{
	const void *linkto;
	int kind;
};

const void *LinkOf(const Reached &r) { return r.linkto; }

} // namespace

// [Doruk-Aksoy] What was kept compares equal to the walk it was kept from, and nothing to nothing.
TEST(SameLinks, AKeptWalkIsTheSame)
{
	int a, b, c;
	const std::vector<Reached> walk = { { &a, 0 }, { &b, 1 }, { &c, 2 }, { &a, 0 } };
	std::vector<const void *> linked = { &c };
	ComputeKeepLinks(walk, LinkOf, linked);
	EXPECT_EQ((std::vector<const void *>{ &a, &b, &c, &a }), linked);
	EXPECT_TRUE(ComputeSameLinks(linked, walk, LinkOf));
	EXPECT_TRUE(ComputeSameLinks(std::vector<const void *>(), std::vector<Reached>(), LinkOf));
}

// [Doruk-Aksoy] A target more, fewer, another or the same ones in another order is a change.
TEST(SameLinks, AnyDifferenceIsAChange)
{
	int a, b, c;
	const std::vector<const void *> linked = { &a, &b };
	EXPECT_FALSE(ComputeSameLinks(linked, std::vector<Reached>{ { &a, 0 }, { &b, 0 }, { &c, 0 } }, LinkOf));
	EXPECT_FALSE(ComputeSameLinks(linked, std::vector<Reached>{ { &a, 0 } }, LinkOf));
	EXPECT_FALSE(ComputeSameLinks(linked, std::vector<Reached>{ { &a, 0 }, { &c, 0 } }, LinkOf));
	EXPECT_FALSE(ComputeSameLinks(linked, std::vector<Reached>{ { &b, 0 }, { &a, 0 } }, LinkOf));
	EXPECT_FALSE(ComputeSameLinks(linked, std::vector<Reached>(), LinkOf));
}
