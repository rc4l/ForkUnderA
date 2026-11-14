// [Doruk-Aksoy] Tests for lightintensity_compute, each expected value UZDoom's colour scale for the same carrier.
#include <gtest/gtest.h>

#include "features/dynlights/computation/lightintensity_compute.h"

using namespace zx::dynlights;

// [Doruk-Aksoy] Without +LIGHTMULTALPHA a fading carrier's light stays as bright as GLDEFS says.
TEST(ComputeLightIntensity, WithoutTheFlagAlphaIsIgnored)
{
	EXPECT_FLOAT_EQ(ComputeLightIntensity(1.f, false, 0.25f), 1.f);
	EXPECT_FLOAT_EQ(ComputeLightIntensity(2.f, false, 0.f), 2.f);
}

// [Doruk-Aksoy] With it the light is scaled by the alpha, on top of the GLDEFS intensity.
TEST(ComputeLightIntensity, WithTheFlagAlphaScales)
{
	EXPECT_FLOAT_EQ(ComputeLightIntensity(1.f, true, 0.25f), 0.25f);
	EXPECT_FLOAT_EQ(ComputeLightIntensity(2.f, true, 0.5f), 1.f);
	EXPECT_FLOAT_EQ(ComputeLightIntensity(1.f, true, 1.f), 1.f);
	EXPECT_FLOAT_EQ(ComputeLightIntensity(1.f, true, 0.f), 0.f);
}
