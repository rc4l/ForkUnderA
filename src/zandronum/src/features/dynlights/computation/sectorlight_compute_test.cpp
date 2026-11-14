// [Doruk-Aksoy] Tests for sectorlight_compute, each expected radius what UZDoom's clamp(lightlevel * intensity / 8, 0, 255) gives.
#include <gtest/gtest.h>

#include "features/dynlights/computation/sectorlight_compute.h"

using namespace zx::dynlights;

// [Doruk-Aksoy] With no scale the radius is the sector's light level.
TEST(ComputeSectorLightRadius, NoScaleIsTheLightLevel)
{
	EXPECT_FLOAT_EQ(ComputeSectorLightRadius(0, 0), 0.f);
	EXPECT_FLOAT_EQ(ComputeSectorLightRadius(0, 64), 64.f);
	EXPECT_FLOAT_EQ(ComputeSectorLightRadius(0, 200), 200.f);
	EXPECT_FLOAT_EQ(ComputeSectorLightRadius(0, 255), 255.f);
}

// [Doruk-Aksoy] The argument is eighths of the light level: 8 keeps it, 4 halves it, 1 is an eighth.
TEST(ComputeSectorLightRadius, ArgumentIsEighths)
{
	EXPECT_FLOAT_EQ(ComputeSectorLightRadius(8, 200), 200.f);
	EXPECT_FLOAT_EQ(ComputeSectorLightRadius(4, 200), 100.f);
	EXPECT_FLOAT_EQ(ComputeSectorLightRadius(1, 200), 25.f);
	EXPECT_FLOAT_EQ(ComputeSectorLightRadius(12, 100), 150.f);
}

// [Doruk-Aksoy] GLDEFS scale 1.0 is argument 255, almost 32 times the level, so any lit sector reaches the 255 cap.
TEST(ComputeSectorLightRadius, ClampsAt255)
{
	EXPECT_FLOAT_EQ(ComputeSectorLightRadius(255, 64), 255.f);
	EXPECT_FLOAT_EQ(ComputeSectorLightRadius(16, 200), 255.f);
	EXPECT_FLOAT_EQ(ComputeSectorLightRadius(0, 300), 255.f);
}

// [Doruk-Aksoy] A negative argument or level, which ACS or a map can give, lands on 0.
TEST(ComputeSectorLightRadius, NegativeIsZero)
{
	EXPECT_FLOAT_EQ(ComputeSectorLightRadius(-8, 200), 0.f);
	EXPECT_FLOAT_EQ(ComputeSectorLightRadius(8, -40), 0.f);
}
