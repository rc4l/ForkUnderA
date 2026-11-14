// [Doruk-Aksoy] Tests for attenuate_compute, each expected value what UZDoom's FLightDefaults and AddLightDefaults give for the same GLDEFS.
#include <gtest/gtest.h>

#include <limits>

#include "features/gldefs/computation/attenuate_compute.h"

using namespace zx::gldefs;

// [Doruk-Aksoy] A light's own attenuate tag wins over the level's flag either way.
TEST(ComputeLightAttenuated, OwnTagWins)
{
	EXPECT_TRUE(ComputeLightAttenuated(1, false));
	EXPECT_TRUE(ComputeLightAttenuated(1, true));
	EXPECT_FALSE(ComputeLightAttenuated(0, true));
	EXPECT_FALSE(ComputeLightAttenuated(0, false));
}

// [Doruk-Aksoy] A light without the tag follows MAPINFO attenuatelights.
TEST(ComputeLightAttenuated, UnsetFollowsTheLevel)
{
	EXPECT_TRUE(ComputeLightAttenuated(kAttenuateUnset, true));
	EXPECT_FALSE(ComputeLightAttenuated(kAttenuateUnset, false));
}

// [Doruk-Aksoy] attenuate 1 and an unset tag are both scaled, cut toward zero as UZDoom's int cast cuts it.
TEST(ComputeLightSizeFactor, ScalesAttenuatedAndUnset)
{
	EXPECT_EQ(ComputeLightSizeFactor(64, 1, 2.0), 128);
	EXPECT_EQ(ComputeLightSizeFactor(64, kAttenuateUnset, 2.0), 128);
	EXPECT_EQ(ComputeLightSizeFactor(65, 1, 0.5), 32);
	EXPECT_EQ(ComputeLightSizeFactor(1000, 1, 1.5), 1500);
	EXPECT_EQ(ComputeLightSizeFactor(64, 1, 1.0), 64);
}

// [Doruk-Aksoy] attenuate 0 keeps its size under any factor.
TEST(ComputeLightSizeFactor, AttenuateZeroKeepsItsSize)
{
	EXPECT_EQ(ComputeLightSizeFactor(64, 0, 2.0), 64);
	EXPECT_EQ(ComputeLightSizeFactor(64, 0, 0.0), 64);
}

// [Doruk-Aksoy] A size of 0 (a sector light without a scale) stays 0, as do a zero, negative or NaN factor.
TEST(ComputeLightSizeFactor, ZeroNegativeAndNaN)
{
	EXPECT_EQ(ComputeLightSizeFactor(0, 1, 2.0), 0);
	EXPECT_EQ(ComputeLightSizeFactor(64, 1, 0.0), 0);
	EXPECT_EQ(ComputeLightSizeFactor(64, 1, -2.0), 0);
	EXPECT_EQ(ComputeLightSizeFactor(64, 1, std::numeric_limits<double>::quiet_NaN()), 0);
}

// [Doruk-Aksoy] A huge factor is held at 2^20 instead of overflowing.
TEST(ComputeLightSizeFactor, HugeFactorIsHeld)
{
	EXPECT_EQ(ComputeLightSizeFactor(1024, 1, 1e30), 1 << 20);
	EXPECT_EQ(ComputeLightSizeFactor(1024, 1, 1024.0), 1 << 20);
}
