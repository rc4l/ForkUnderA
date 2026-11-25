// [Doruk-Aksoy] Tests for lightheight_compute, each expected height what UZDoom's UpdateLocation gives, in raw 16.16 units.
#include <gtest/gtest.h>

#include <cstdint>

#include "features/dynlights/computation/lightheight_compute.h"

using namespace zx::dynlights;

namespace {

constexpr int64_t kUnit = int64_t(1) << 16;
constexpr int64_t kMargin = 5 * kUnit;

}

// [Doruk-Aksoy] A light between floor + 5 and ceiling - 5 stays where it is, the bounds themselves included.
TEST(ComputeLightZ, InsideTheBandStays)
{
	EXPECT_EQ(ComputeLightZ(64 * kUnit, 0, 128 * kUnit, kMargin, false), 64 * kUnit);
	EXPECT_EQ(ComputeLightZ(5 * kUnit, 0, 128 * kUnit, kMargin, false), 5 * kUnit);
	EXPECT_EQ(ComputeLightZ(123 * kUnit, 0, 128 * kUnit, kMargin, false), 123 * kUnit);
}

// [Doruk-Aksoy] A light on or under the floor is lifted to 5 above it, one at or over the ceiling lowered to 5 below.
TEST(ComputeLightZ, KeptOffFloorAndCeiling)
{
	EXPECT_EQ(ComputeLightZ(0, 0, 128 * kUnit, kMargin, false), 5 * kUnit);
	EXPECT_EQ(ComputeLightZ(-40 * kUnit, -32 * kUnit, 128 * kUnit, kMargin, false), -27 * kUnit);
	EXPECT_EQ(ComputeLightZ(128 * kUnit, 0, 128 * kUnit, kMargin, false), 123 * kUnit);
	EXPECT_EQ(ComputeLightZ(4 * kUnit + 1, 0, 128 * kUnit, kMargin, false), 5 * kUnit);
}

// [Doruk-Aksoy] Where floor and ceiling are under 10 apart the floor wins, as UZDoom tests it first.
TEST(ComputeLightZ, NarrowGapFavoursTheFloor)
{
	EXPECT_EQ(ComputeLightZ(2 * kUnit, 0, 8 * kUnit, kMargin, false), 5 * kUnit);
	EXPECT_EQ(ComputeLightZ(7 * kUnit, 0, 8 * kUnit, kMargin, false), 3 * kUnit);
}

// [Doruk-Aksoy] A +NOINTERACTION actor's light is never moved.
TEST(ComputeLightZ, NoInteractionIsLeftAlone)
{
	EXPECT_EQ(ComputeLightZ(0, 0, 128 * kUnit, kMargin, true), 0);
	EXPECT_EQ(ComputeLightZ(200 * kUnit, 0, 128 * kUnit, kMargin, true), 200 * kUnit);
}
