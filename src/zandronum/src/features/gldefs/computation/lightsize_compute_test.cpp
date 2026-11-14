// [Doruk-Aksoy] Tests for lightsize_compute, each expected value what UZDoom's clamp((int)v, 1, 1024) gives for the same GLDEFS value.
#include <gtest/gtest.h>

#include <limits>

#include "features/gldefs/computation/lightsize_compute.h"

using namespace zx::gldefs;

// [Doruk-Aksoy] Sizes inside 1 to 1024 stand, including the ones past 255 a byte used to cut.
TEST(ComputeLightSize, KeepsSizesUpTo1024)
{
	EXPECT_EQ(ComputeLightSize(1), 1);
	EXPECT_EQ(ComputeLightSize(64), 64);
	EXPECT_EQ(ComputeLightSize(255), 255);
	EXPECT_EQ(ComputeLightSize(256), 256);
	EXPECT_EQ(ComputeLightSize(600), 600);
	EXPECT_EQ(ComputeLightSize(1024), 1024);
}

// [Doruk-Aksoy] Zero and below become 1, past 1024 becomes 1024.
TEST(ComputeLightSize, ClampsToUZDoomsRange)
{
	EXPECT_EQ(ComputeLightSize(0), 1);
	EXPECT_EQ(ComputeLightSize(-50), 1);
	EXPECT_EQ(ComputeLightSize(1025), 1024);
	EXPECT_EQ(ComputeLightSize(100000), 1024);
}

// [Doruk-Aksoy] A scale is 255ths, cut toward zero as UZDoom's int cast cuts it.
TEST(ComputeSectorLightScale, ScalesBy255)
{
	EXPECT_EQ(ComputeSectorLightScale(1.f), 255);
	EXPECT_EQ(ComputeSectorLightScale(0.5f), 127);
	EXPECT_EQ(ComputeSectorLightScale(2.f), 510);
	EXPECT_EQ(ComputeSectorLightScale(4.f), 1020);
}

// [Doruk-Aksoy] Below 1/255, past 1024/255, huge and NaN scales land inside 1 to 1024.
TEST(ComputeSectorLightScale, ClampsToUZDoomsRange)
{
	EXPECT_EQ(ComputeSectorLightScale(0.f), 1);
	EXPECT_EQ(ComputeSectorLightScale(-1.f), 1);
	EXPECT_EQ(ComputeSectorLightScale(1.f / 255.f), 1);
	EXPECT_EQ(ComputeSectorLightScale(5.f), 1024);
	EXPECT_EQ(ComputeSectorLightScale(1e30f), 1024);
	EXPECT_EQ(ComputeSectorLightScale(std::numeric_limits<float>::quiet_NaN()), 1);
}
