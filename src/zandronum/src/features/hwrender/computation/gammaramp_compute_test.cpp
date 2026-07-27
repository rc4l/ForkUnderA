// [rc4l] Tests for the gamma-ramp helpers (hwrender video layer).
#include "gtest/gtest.h"
#include "features/hwrender/computation/gammaramp_compute.h"

using namespace hwrender;

namespace
{
TEST(GammaRamp, IdentityIsLinearFullRange)
{
	unsigned short r[256];
	ComputeIdentityRamp(r);
	EXPECT_EQ(r[0], 0);
	EXPECT_EQ(r[255], 65535);
	EXPECT_EQ(r[128], 128 * 257);
	// [rc4l] Strictly increasing.
	for (int i = 1; i < 256; i++)
		EXPECT_GT(r[i], r[i - 1]);
}

TEST(GammaRamp, GammaOneMatchesIdentityEndpointsAndMonotone)
{
	unsigned short r[256];
	ComputeGammaRamp(1.0f, r);
	EXPECT_EQ(r[0], 0);
	EXPECT_EQ(r[255], 65535);
	for (int i = 1; i < 256; i++)
		EXPECT_GE(r[i], r[i - 1]);
}

TEST(GammaRamp, GammaAboveOneBrightensMidtones)
{
	unsigned short g1[256], g2[256];
	ComputeGammaRamp(1.0f, g1);
	ComputeGammaRamp(2.0f, g2);
	// [rc4l] Brighter gamma lifts the midpoint but keeps the endpoints pinned.
	EXPECT_GT(g2[128], g1[128]);
	EXPECT_EQ(g2[0], 0);
	EXPECT_EQ(g2[255], 65535);
}

TEST(GammaRamp, NonPositiveGammaFallsBackToIdentity)
{
	unsigned short ramp[256], id[256];
	ComputeIdentityRamp(id);

	ComputeGammaRamp(0.0f, ramp);
	for (int i = 0; i < 256; i++)
		EXPECT_EQ(ramp[i], id[i]);

	ComputeGammaRamp(-3.0f, ramp);
	for (int i = 0; i < 256; i++)
		EXPECT_EQ(ramp[i], id[i]);
}
} // namespace
