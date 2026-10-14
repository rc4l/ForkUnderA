// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>
#include <cmath>
#include "features/hwrender/computation/mirrorsheen_compute.h"

using namespace zx::hwrender;

namespace {
const float kIdentity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
}

// [rc4l] GL's view carries the 1.2 pixel stretch on heights, so its normal matrix shrinks them by as much rather than repeating the view.
TEST(MirrorSheenCompute, TheNormalMatrixIsTheInverseTransposeOfTheView)
{
	float n[9];
	ASSERT_TRUE(ComputeNormalMatrix(kIdentity, n));
	for (int i = 0; i < 9; i++) EXPECT_FLOAT_EQ(i % 4 == 0 ? 1.f : 0.f, n[i]) << i;
	const float stretched[16] = { 1, 0, 0, 0, 0, 1.2f, 0, 0, 0, 0, 1, 0, 5, 6, 7, 1 };
	ASSERT_TRUE(ComputeNormalMatrix(stretched, n));
	EXPECT_FLOAT_EQ(1.f / 1.2f, n[4]);
	const float turned[16] = { 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };   // [rc4l] 90 degrees about z
	ASSERT_TRUE(ComputeNormalMatrix(turned, n));
	EXPECT_FLOAT_EQ(1.f, n[1]);
	EXPECT_FLOAT_EQ(-1.f, n[3]);
	const float flat[16] = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	EXPECT_FALSE(ComputeNormalMatrix(flat, n));
}

// [rc4l] main.vp's SPHEREMAP: a ray straight back off the glass lands in the middle of mirror.png, and one glancing right lands right of it.
TEST(MirrorSheenCompute, TheSphereMapIsTheReflectedRaysProjection)
{
	float s, t;
	const float ahead[3] = { 0.f, 0.f, -10.f }, facing[3] = { 0.f, 0.f, 3.f };
	ComputeSphereMapUV(ahead, facing, s, t);
	EXPECT_FLOAT_EQ(0.5f, s);
	EXPECT_FLOAT_EQ(0.5f, t);
	const float right[3] = { 1.f, 0.f, -1.f };
	ComputeSphereMapUV(right, facing, s, t);
	EXPECT_NEAR(0.6913f, s, 1e-4f);
	EXPECT_FLOAT_EQ(0.5f, t);
	const float none[3] = { 0.f, 0.f, 0.f };   // [rc4l] a normal with no length reflects nothing
	ComputeSphereMapUV(right, none, s, t);
	EXPECT_NEAR(0.5f + 0.7071068f / (2.f * std::sqrt(0.5f + 0.0857864f)), s, 1e-4f);
}

// [rc4l] WDI08's ward mirror: GL added the sheen at a tenth of the wall's light, and the backend drew the reflection bare.
TEST(MirrorSheenCompute, TheBlockCarriesTheViewTheEyeNormalAndATenthOfTheLight)
{
	const float rgb[3] = { 0.5f, 0.6f, 0.7f };
	float out[kMirrorSheenFloats];
	ComputeMirrorSheenConstants(kIdentity, 0.f, -2.f, rgb, -1.f, 1.f, -0.001f, true, 4, 37, out);
	for (int i = 0; i < 16; i++) EXPECT_FLOAT_EQ(kIdentity[i], out[i]) << i;
	EXPECT_FLOAT_EQ(0.f, out[16]);
	EXPECT_FLOAT_EQ(0.f, out[17]);
	EXPECT_FLOAT_EQ(-1.f, out[18]);
	EXPECT_FLOAT_EQ(37.f, out[19]);   // [rc4l] the mirror's line, the view a traced ray from it starts in
	EXPECT_FLOAT_EQ(0.6f, out[21]);
	EXPECT_FLOAT_EQ(kMirrorSheenAlpha, out[23]);
	EXPECT_FLOAT_EQ(-1.f, out[24]);
	EXPECT_FLOAT_EQ(1.f, out[25]);
	EXPECT_FLOAT_EQ(-0.001f, out[26]);
	EXPECT_FLOAT_EQ(4.f, out[27]);   // [rc4l] r_mirror_recursions, how many mirrors deep the reflections go
	ComputeMirrorSheenConstants(kIdentity, 1.f, 0.f, rgb, -1.f, 0.f, 0.f, false, 4, 0, out);
	EXPECT_FLOAT_EQ(1.f, out[16]);
	EXPECT_FLOAT_EQ(0.f, out[23]);   // [rc4l] r_mirror_envmap off, or the mirror outside GL's walk
	const float flat[16] = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	ComputeMirrorSheenConstants(flat, 1.f, 0.f, rgb, -1.f, 0.f, 0.f, true, 4, 0, out);
	EXPECT_FLOAT_EQ(0.f, out[16]);
}
