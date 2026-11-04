// [Doruk-Aksoy] Tests for relinkslack_compute: a moving light keeps its surfaces only within the slack of where its last relink put it.
#include <gtest/gtest.h>

#include <cstdint>

#include "features/dynlights/computation/relinkslack_compute.h"

using namespace zx::dynlights;

namespace {

constexpr int64_t kUnit = int64_t(1) << 16;
constexpr int64_t kSlack = 16 * kUnit;
const LinkSpot kLinked{ 1000 * kUnit, -500 * kUnit, true };

// [Doruk-Aksoy] Up to the slack on either axis, either way, keeps the surfaces.
TEST(ComputeLightKeepsLinks, WithinTheSlack)
{
	EXPECT_TRUE(ComputeLightKeepsLinks(kLinked, kLinked.x, kLinked.y, kSlack));
	EXPECT_TRUE(ComputeLightKeepsLinks(kLinked, kLinked.x + kSlack, kLinked.y - kSlack, kSlack));
	EXPECT_TRUE(ComputeLightKeepsLinks(kLinked, kLinked.x - kSlack, kLinked.y + kSlack, kSlack));
}

// [Doruk-Aksoy] Past the slack on either axis, no slack, or no relink yet each relink.
TEST(ComputeLightKeepsLinks, EverythingElseRelinks)
{
	EXPECT_FALSE(ComputeLightKeepsLinks(kLinked, kLinked.x + kSlack + 1, kLinked.y, kSlack));
	EXPECT_FALSE(ComputeLightKeepsLinks(kLinked, kLinked.x, kLinked.y - kSlack - 1, kSlack));
	EXPECT_FALSE(ComputeLightKeepsLinks(kLinked, kLinked.x + 1, kLinked.y, 0));
	EXPECT_FALSE(ComputeLightKeepsLinks(kLinked, kLinked.x + 1, kLinked.y, -kSlack));
	const LinkSpot never{ 0, 0, false };
	EXPECT_FALSE(ComputeLightKeepsLinks(never, 0, 0, kSlack));
}

}
