// [Doruk-Aksoy] Tests for lightspot_compute: a light keeps its subsector only at the very point it was found for, and only while it still holds the one found.
#include <gtest/gtest.h>

#include "features/dynlights/computation/lightspot_compute.h"

using namespace zx::dynlights;

namespace {

int g_subA, g_subB;

// [Doruk-Aksoy] The same point with the subsector found there keeps it.
TEST(ComputeLightKeepsSubsector, SamePointSameSubsector)
{
	const LightSpot last{ 1024, -2048, &g_subA };
	EXPECT_TRUE(ComputeLightKeepsSubsector(last, 1024, -2048, &g_subA));
}

// [Doruk-Aksoy] A move in x or y, a subsector set by someone else, or no lookup yet each look it up again.
TEST(ComputeLightKeepsSubsector, AnythingElseLooksUp)
{
	const LightSpot last{ 1024, -2048, &g_subA };
	EXPECT_FALSE(ComputeLightKeepsSubsector(last, 1025, -2048, &g_subA));
	EXPECT_FALSE(ComputeLightKeepsSubsector(last, 1024, -2047, &g_subA));
	EXPECT_FALSE(ComputeLightKeepsSubsector(last, 1024, -2048, &g_subB));
	const LightSpot none{ 0, 0, nullptr };
	EXPECT_FALSE(ComputeLightKeepsSubsector(none, 0, 0, nullptr));
}

}
