// [Doruk-Aksoy] Tests for decalbounds_compute: the rectangle a deferred mark is drawn over.
#include <gtest/gtest.h>

#include "features/hwrender/computation/decalbounds_compute.h"
#include "features/hwrender/computation/backendview_compute.h"   // [Doruk-Aksoy] ComputeInvertMatrix4

using namespace zx::hwrender;

namespace {

const float kEye[3] = { 0, 0, 0 };

// [Doruk-Aksoy] A projection as this backend makes one, depth 0 at the near plane (1) and 1 at the far (100), the eye at the origin looking down -z.
const float kN = 1.f, kF = 100.f;
const float kVulkan[16] = { 1, 0, 0, 0,   0, 1, 0, 0,   0, 0, kF / (kN - kF), -1,   0, 0, kN * kF / (kN - kF), 0 };

// [Doruk-Aksoy] kVulkan seen from eye e: the projection times a translation by -e, column-major.
void FromEye(const float e[3], float out[16])
{
	for (int i = 0; i < 12; i++) out[i] = kVulkan[i];
	for (int k = 0; k < 4; k++) out[12 + k] = kVulkan[k] * -e[0] + kVulkan[4 + k] * -e[1] + kVulkan[8 + k] * -e[2] + kVulkan[12 + k];
}

} // namespace

// [Doruk-Aksoy] A rectangle's pixels are its share of the target, and one off the screen covers none.
TEST(DecalBounds, PixelsAndTheScreensEdges)
{
	const float centre[3] = { 0, 0, -10 };
	DecalScreenRect r = ComputeDecalClippedRect(centre, 1.f, kEye, kVulkan);
	EXPECT_NEAR((2.0 / 9.0) * 0.5 * 100.0 * (2.0 / 9.0) * 0.5 * 50.0, ComputeDecalRectPixels(r, 100.f, 50.f), 1e-3);
	const float right[3] = { 9.5f, 0, -10 };
	r = ComputeDecalClippedRect(right, 1.f, kEye, kVulkan);
	EXPECT_TRUE(ComputeDecalRectShows(r));
	EXPECT_FLOAT_EQ(1.f, r.hi[0]);
	const float gone[3] = { 40.f, 0, -10 };
	r = ComputeDecalClippedRect(gone, 1.f, kEye, kVulkan);
	EXPECT_FALSE(ComputeDecalRectShows(r));
	EXPECT_DOUBLE_EQ(0.0, ComputeDecalRectPixels(r, 100.f, 100.f));
	const float below[3] = { 0, -40.f, -10 };
	r = ComputeDecalClippedRect(below, 1.f, kEye, kVulkan);
	EXPECT_FALSE(ComputeDecalRectShows(r));
	EXPECT_DOUBLE_EQ(0.0, ComputeDecalRectPixels(r, 100.f, 100.f));
	const float around[3] = { 0, 0, -0.5f };
	r = ComputeDecalClippedRect(around, 1.f, kEye, kVulkan);
	EXPECT_DOUBLE_EQ(200.0 * 100.0, ComputeDecalRectPixels(r, 200.f, 100.f));
}

// [Doruk-Aksoy] A mark rides its plane: on a level floor at height 32 (C = 1, D = -32), 4 above it is 36.
TEST(DecalBounds, AMarkRidesItsPlane)
{
	const float centre[3] = { 5, 0, 7 };
	const float level[4] = { 0, 0, -32, 1 };
	EXPECT_FLOAT_EQ(36.f, ComputeDecalRiddenHeight(centre, level, 4.f));
	// [Doruk-Aksoy] A slope rising one in x: z = x + 32 at x = 5 is 37.
	const float slope[4] = { -1, 0, -32, 1 };
	EXPECT_FLOAT_EQ(37.f, ComputeDecalRiddenHeight(centre, slope, 0.f));
}

// [Doruk-Aksoy] The eye comes back out of the inverse of a projection looking from it, and a matrix with no eye says so.
TEST(DecalBounds, TheEyeComesOutOfTheInverse)
{
	const float e[3] = { 5, -2, 3 };
	float mvp[16], inv[16], eye[3];
	FromEye(e, mvp);
	ASSERT_TRUE(ComputeInvertMatrix4(mvp, inv));
	ASSERT_TRUE(ComputeDecalEye(inv, eye));
	EXPECT_NEAR(5.f, eye[0], 1e-3f);
	EXPECT_NEAR(-2.f, eye[1], 1e-3f);
	EXPECT_NEAR(3.f, eye[2], 1e-3f);
	const float identity[16] = { 1, 0, 0, 0,   0, 1, 0, 0,   0, 0, 1, 0,   0, 0, 0, 1 };
	EXPECT_FALSE(ComputeDecalEye(identity, eye));
}

// [Doruk-Aksoy] In front of the eye the cut rectangle is the cube's, as the uncut one was.
TEST(DecalBounds, InFrontTheCutRectangleIsTheCubes)
{
	const float centre[3] = { 0, 0, -10 };
	const DecalScreenRect r = ComputeDecalClippedRect(centre, 1.f, kEye, kVulkan);
	EXPECT_FALSE(r.whole);
	EXPECT_TRUE(ComputeDecalRectShows(r));
	EXPECT_NEAR(-1.f / 9.f, r.lo[0], 1e-5f);
	EXPECT_NEAR(1.f / 9.f, r.hi[1], 1e-5f);
}

// [Doruk-Aksoy] Behind the eye a mark covers nothing, where the vertex stage's own rule took the whole screen.
TEST(DecalBounds, BehindTheEyeCoversNothing)
{
	const float behind[3] = { 0, 0, 10 };
	const DecalScreenRect r = ComputeDecalClippedRect(behind, 1.f, kEye, kVulkan);
	EXPECT_FALSE(r.whole);
	EXPECT_FALSE(ComputeDecalRectShows(r));
}

// [Doruk-Aksoy] Across the near plane only the part in front counts: ahead it spans the screen's width, beside the eye it is off the screen.
TEST(DecalBounds, AcrossTheNearPlaneOnlyThePartInFront)
{
	const float ahead[3] = { 0, 0, -1.5f };
	DecalScreenRect r = ComputeDecalClippedRect(ahead, 1.f, kEye, kVulkan);
	EXPECT_FALSE(r.whole);
	EXPECT_TRUE(ComputeDecalRectShows(r));
	EXPECT_NEAR(-1.f, r.lo[0], 1e-5f);
	EXPECT_NEAR(1.f, r.hi[0], 1e-5f);
	const float beside[3] = { 3, 0, 0 };
	r = ComputeDecalClippedRect(beside, 1.f, kEye, kVulkan);
	EXPECT_FALSE(ComputeDecalRectShows(r));
}

// [Doruk-Aksoy] With the eye inside the cube the mark may be anywhere on screen; a wide one in front is clamped to it.
TEST(DecalBounds, InsideIsWholeAndWideIsClamped)
{
	const float around[3] = { 0, 0, -0.5f };
	DecalScreenRect r = ComputeDecalClippedRect(around, 1.f, kEye, kVulkan);
	EXPECT_TRUE(r.whole);
	EXPECT_TRUE(ComputeDecalRectShows(r));
	const float wide[3] = { 0, 0, -3.5f };
	r = ComputeDecalClippedRect(wide, 2.f, kEye, kVulkan);
	EXPECT_FALSE(r.whole);
	EXPECT_FLOAT_EQ(-1.f, r.lo[0]);
	EXPECT_FLOAT_EQ(1.f, r.hi[0]);
	EXPECT_FLOAT_EQ(-1.f, r.lo[1]);
	EXPECT_FLOAT_EQ(1.f, r.hi[1]);
}

// [Doruk-Aksoy] A projection whose near plane lies behind its eye cuts nothing there, rather than dividing by it.
TEST(DecalBounds, ANearPlaneBehindTheEyeIsNotDividedBy)
{
	const float odd[16] = { 1, 0, 0, 0,   0, 1, 0, 0,   0, 0, -1, -1,   0, 0, 0, -5 };
	const float far[3] = { 100, 100, 100 };
	const float centre[3] = { 0, 0, 0 };
	const DecalScreenRect r = ComputeDecalClippedRect(centre, 1.f, far, odd);
	EXPECT_FALSE(r.whole);
}

// [Doruk-Aksoy] The paint radius is the run-out radius once it reaches past the picture's corners, else the box's reach.
TEST(DecalBounds, ThePaintRadius)
{
	const float axisU[3] = { 1.f / 8.f, 0, 0 }, axisV[3] = { 0, 1.f / 6.f, 0 };
	const float runsOut[4] = { 4, 20, 14, 0 };
	EXPECT_FLOAT_EQ(14.f, ComputeDecalPaintRadius(runsOut, axisU, axisV));
	const float noRunOut[4] = { 0, 20, 10, 0 };
	EXPECT_FLOAT_EQ(20.f, ComputeDecalPaintRadius(noRunOut, axisU, axisV));
	const float shallow[4] = { 0, 5, 10, 0 };
	EXPECT_FLOAT_EQ(10.f, ComputeDecalPaintRadius(shallow, axisU, axisV));
	const float none[3] = { 0, 0, 0 };
	EXPECT_FLOAT_EQ(20.f, ComputeDecalPaintRadius(runsOut, none, none));
}
