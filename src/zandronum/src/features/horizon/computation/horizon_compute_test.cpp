// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Each case is a rule GLWall::DoHorizon or GLHorizonPortal encodes, at the wdiex02 numbers the bug was found at.

#include <gtest/gtest.h>

#include <math.h>

#include "features/horizon/computation/horizon_compute.h"

using namespace zx::horizon;

// ---- which lines -------------------------------------------------------------

TEST(HorizonCompute, LineHorizonIsAHorizon)
{
	EXPECT_TRUE(ComputeIsHorizonLine(9));
	EXPECT_TRUE(ComputeIsHorizonLine(kLineHorizonSpecial));
}

TEST(HorizonCompute, OtherSpecialsAreNot)
{
	// [rc4l] Line_Mirror (182) is the other line GL turns into a portal, and it is not this.
	EXPECT_FALSE(ComputeIsHorizonLine(0));
	EXPECT_FALSE(ComputeIsHorizonLine(182));
	EXPECT_FALSE(ComputeIsHorizonLine(8));
	EXPECT_FALSE(ComputeIsHorizonLine(10));
}

// ---- what each half is -------------------------------------------------------

TEST(HorizonCompute, WdiexHarbourIsSkyAboveAndWaterBelow)
{
	// [rc4l] wdiex02's horizon sectors: a sky ceiling over a water floor.
	const HorizonHalves h = ComputeHorizonHalves(false, true);
	EXPECT_TRUE(h.floorPlane);
	EXPECT_FALSE(h.ceilingPlane);
	EXPECT_TRUE(h.skyAbove);
}

TEST(HorizonCompute, AnIndoorHorizonIsTwoPlanes)
{
	const HorizonHalves h = ComputeHorizonHalves(false, false);
	EXPECT_TRUE(h.floorPlane);
	EXPECT_TRUE(h.ceilingPlane);
	EXPECT_FALSE(h.skyAbove);
}

TEST(HorizonCompute, ASkyFloorIsNeitherPlaneNorDrawnSky)
{
	const HorizonHalves h = ComputeHorizonHalves(true, false);
	EXPECT_FALSE(h.floorPlane);
	EXPECT_TRUE(h.ceilingPlane);
	EXPECT_FALSE(h.skyAbove);
}

// ---- how much of the line the window covers ----------------------------------

TEST(HorizonCompute, WindowSpansTheSectorWhenNeitherSideIsSky)
{
	const HorizonSpan s = ComputeHorizonWindowSpan(-504.f, 320.f, false, false);
	EXPECT_FLOAT_EQ(s.bottom, -504.f);
	EXPECT_FLOAT_EQ(s.top, 320.f);
}

TEST(HorizonCompute, ASkyCeilingStretchesTheWindowToTheSkyReach)
{
	// [rc4l] SkyNormal carries a sky ceiling up to 32768, and the line's sky has to meet it.
	const HorizonSpan s = ComputeHorizonWindowSpan(-504.f, 320.f, false, true);
	EXPECT_FLOAT_EQ(s.bottom, -504.f);
	EXPECT_FLOAT_EQ(s.top, kHorizonSkyReach);
}

TEST(HorizonCompute, ASkyFloorStretchesTheWindowDown)
{
	const HorizonSpan s = ComputeHorizonWindowSpan(-504.f, 320.f, true, false);
	EXPECT_FLOAT_EQ(s.bottom, -kHorizonSkyReach);
	EXPECT_FLOAT_EQ(s.top, 320.f);
}

// ---- where the eye splits it -------------------------------------------------

TEST(HorizonCompute, AnEyeInsideTheSectorSplitsAtItsOwnHeight)
{
	// [rc4l] rc4l's vantage: viewz -445.35 over a horizon sector from -504 to 320.
	EXPECT_FLOAT_EQ(ComputeHorizonSplit(-445.35f, -504.f, 320.f), -445.35f);
}

TEST(HorizonCompute, AnEyeAboveTheCeilingSeesTheFloorAllTheWayUp)
{
	EXPECT_FLOAT_EQ(ComputeHorizonSplit(500.f, -504.f, 320.f), 320.f);
}

TEST(HorizonCompute, AnEyeBelowTheFloorSeesTheCeilingAllTheWayDown)
{
	EXPECT_FLOAT_EQ(ComputeHorizonSplit(-600.f, -504.f, 320.f), -504.f);
}

// ---- where a ray meets the plane ---------------------------------------------

TEST(HorizonCompute, ARayDownThroughTheLineLandsOnTheFloorBeyondIt)
{
	// [rc4l] Eye 64 above the floor, through a point 32 above it 100 units away, so the floor is met 200 units out.
	const HorizonHit h = ComputeHorizonRayHit(0.f, 0.f, 64.f, 100.f, 0.f, 32.f, 0.f, kHorizonHalfExtent);
	EXPECT_TRUE(h.withinSquare);
	EXPECT_FLOAT_EQ(h.x, 200.f);
	EXPECT_FLOAT_EQ(h.y, 0.f);
	EXPECT_FLOAT_EQ(h.z, 0.f);
}

TEST(HorizonCompute, ARayUpThroughTheLineLandsOnTheCeilingBeyondIt)
{
	const HorizonHit h = ComputeHorizonRayHit(10.f, 20.f, 0.f, 10.f, 70.f, 64.f, 128.f, kHorizonHalfExtent);
	EXPECT_TRUE(h.withinSquare);
	EXPECT_FLOAT_EQ(h.x, 10.f);
	EXPECT_FLOAT_EQ(h.y, 120.f);
	EXPECT_FLOAT_EQ(h.z, 128.f);
}

TEST(HorizonCompute, ANearlyLevelRayStopsAtTheEdgeOfGLsSquare)
{
	// [rc4l] Barely below the eye the floor is 6.4 million units out, so the sample stops at GL's 32768 edge.
	const HorizonHit h = ComputeHorizonRayHit(0.f, 0.f, 64.f, 1000.f, 1000.f, 63.99f, 0.f, 32768.f);
	EXPECT_FALSE(h.withinSquare);
	EXPECT_NEAR(h.x, 32768.f, 0.01f);
	EXPECT_NEAR(h.y, 32768.f, 0.01f);
	EXPECT_FLOAT_EQ(h.z, 0.f);
}

TEST(HorizonCompute, TheSquareIsMeasuredFromTheEyeNotTheOrigin)
{
	const HorizonHit h = ComputeHorizonRayHit(-4623.f, -489.f, -445.f, -4623.f, -1489.f, -445.01f,
		-504.f, 32768.f);
	EXPECT_FALSE(h.withinSquare);
	EXPECT_FLOAT_EQ(h.x, -4623.f);
	EXPECT_NEAR(h.y, -489.f - 32768.f, 0.01f);
}

TEST(HorizonCompute, ALevelRayGoesStraightToTheHorizon)
{
	// [rc4l] Exactly at eye height the ray never meets the floor, which is the horizon line itself.
	const HorizonHit h = ComputeHorizonRayHit(0.f, 0.f, 64.f, 0.f, 50.f, 64.f, 0.f, 32768.f);
	EXPECT_FALSE(h.withinSquare);
	EXPECT_FLOAT_EQ(h.x, 0.f);
	EXPECT_FLOAT_EQ(h.y, 32768.f);
	EXPECT_FLOAT_EQ(h.z, 0.f);
}

TEST(HorizonCompute, ARayLeavingThePlaneAlsoGoesToTheHorizon)
{
	const HorizonHit h = ComputeHorizonRayHit(0.f, 0.f, 64.f, -30.f, 0.f, 80.f, 0.f, 32768.f);
	EXPECT_FALSE(h.withinSquare);
	EXPECT_FLOAT_EQ(h.x, -32768.f);
	EXPECT_FLOAT_EQ(h.y, 0.f);
}

TEST(HorizonCompute, AnEyeOnThePlaneNeverReachesIt)
{
	const HorizonHit h = ComputeHorizonRayHit(0.f, 0.f, 0.f, 10.f, 0.f, -5.f, 0.f, 32768.f);
	EXPECT_FALSE(h.withinSquare);
	EXPECT_FLOAT_EQ(h.x, 32768.f);
}

TEST(HorizonCompute, AVerticalRayThatMissesHasNoHeadingAndStaysOverTheEye)
{
	// [rc4l] Straight up toward a floor: no heading to follow, so the sample stays beneath the eye.
	const HorizonHit h = ComputeHorizonRayHit(5.f, 7.f, 64.f, 5.f, 7.f, 100.f, 0.f, 32768.f);
	EXPECT_TRUE(h.withinSquare);
	EXPECT_FLOAT_EQ(h.x, 5.f);
	EXPECT_FLOAT_EQ(h.y, 7.f);
	EXPECT_FLOAT_EQ(h.z, 0.f);
}

// ---- which slice each half draws ---------------------------------------------

TEST(HorizonCompute, EachHalfDrawsOnlyItsSideOfTheEye)
{
	const HorizonSpan f = ComputeHorizonHalfSpan(false, -504.f, 320.f, -445.f);
	const HorizonSpan c = ComputeHorizonHalfSpan(true, -504.f, 320.f, -445.f);
	EXPECT_FLOAT_EQ(f.bottom, -504.f);
	EXPECT_FLOAT_EQ(f.top, -445.f);
	EXPECT_FLOAT_EQ(c.bottom, -445.f);
	EXPECT_FLOAT_EQ(c.top, 320.f);
}

TEST(HorizonCompute, AnEyeAboveTheCeilingLeavesTheCeilingHalfEmpty)
{
	const HorizonSpan f = ComputeHorizonHalfSpan(false, -504.f, 320.f, 900.f);
	const HorizonSpan c = ComputeHorizonHalfSpan(true, -504.f, 320.f, 900.f);
	EXPECT_FLOAT_EQ(f.top, 320.f);
	EXPECT_FLOAT_EQ(c.top - c.bottom, 0.f);
}

TEST(HorizonCompute, AnEyeBelowTheFloorLeavesTheFloorHalfEmpty)
{
	const HorizonSpan f = ComputeHorizonHalfSpan(false, -504.f, 320.f, -900.f);
	EXPECT_FLOAT_EQ(f.top - f.bottom, 0.f);
}

// ---- what the plane is lit with ------------------------------------------------

TEST(HorizonCompute, AFullbrightFlatIsLitFullAndIgnoresExtraLight)
{
	int level = -1, rel = -1;
	ComputeHorizonLightInputs(true, 96, 32, level, rel);
	EXPECT_EQ(level, 255);
	EXPECT_EQ(rel, 0);
}

TEST(HorizonCompute, AnOrdinaryFlatTakesTheSectorLightAndExtraLight)
{
	int level = -1, rel = -1;
	ComputeHorizonLightInputs(false, 96, 32, level, rel);
	EXPECT_EQ(level, 96);
	EXPECT_EQ(rel, 32);
}

// ---- which side a window faces -----------------------------------------------

TEST(HorizonCompute, AWindowFacesTheSectorOnItsRight)
{
	// [rc4l] wdiex02's line 30450 runs west along y = -2240 with the harbour to its north.
	EXPECT_TRUE(ComputeHorizonFacesEye(-4352.f, -2240.f, -6400.f, -2240.f, -4623.f, -489.f));
	EXPECT_FALSE(ComputeHorizonFacesEye(-4352.f, -2240.f, -6400.f, -2240.f, -4623.f, -3000.f));
	EXPECT_FALSE(ComputeHorizonFacesEye(-4352.f, -2240.f, -6400.f, -2240.f, -5000.f, -2240.f));
}

// ---- the eye of a pass, and what it can see ------------------------------------

namespace {

// [rc4l] BuildMVP's projection: Vulkan depth, near 5, far 65536, column-major.
void Projection(float *p)
{
	for (int i = 0; i < 16; i++) p[i] = 0.f;
	const float f = 1.f / tanf(37.f * 3.14159265f / 180.f), zn = 5.f, zf = 65536.f;
	p[0] = f / (16.f / 9.f); p[5] = f; p[10] = zf / (zn - zf); p[11] = -1.f; p[14] = zf * zn / (zn - zf);
}

void Multiply(const float *a, const float *b, float *out)
{
	for (int c = 0; c < 4; c++)
		for (int r = 0; r < 4; r++)
		{
			float s = 0.f;
			for (int k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k];
			out[c * 4 + r] = s;
		}
}

// [rc4l] A camera at (ex, ey, ez) turned yawDeg about the up axis, looking down its -z.
void ViewProjection(float ex, float ey, float ez, float yawDeg, float *mvp)
{
	const float a = yawDeg * 3.14159265f / 180.f, ca = cosf(a), sa = sinf(a);
	float rot[16] = { ca, 0.f, sa, 0.f,  0.f, 1.f, 0.f, 0.f,  -sa, 0.f, ca, 0.f,  0.f, 0.f, 0.f, 1.f };
	float tr[16] = { 1.f, 0.f, 0.f, 0.f,  0.f, 1.f, 0.f, 0.f,  0.f, 0.f, 1.f, 0.f,  -ex, -ey, -ez, 1.f };
	float view[16], proj[16];
	Multiply(rot, tr, view);
	Projection(proj);
	Multiply(proj, view, mvp);
}

} // namespace

TEST(HorizonCompute, TheEyeComesBackOutOfTheMatrix)
{
	float mvp[16], eye[3];
	ViewProjection(-4623.f, -445.f, -489.f, 0.f, mvp);
	ASSERT_TRUE(ComputeEyeFromViewProjection(mvp, eye));
	EXPECT_NEAR(eye[0], -4623.f, 0.05f);
	EXPECT_NEAR(eye[1], -445.f, 0.05f);
	EXPECT_NEAR(eye[2], -489.f, 0.05f);
}

TEST(HorizonCompute, TheEyeSurvivesATurn)
{
	float mvp[16], eye[3];
	ViewProjection(3584.f, 516.f, -2560.f, 116.f, mvp);
	ASSERT_TRUE(ComputeEyeFromViewProjection(mvp, eye));
	EXPECT_NEAR(eye[0], 3584.f, 0.05f);
	EXPECT_NEAR(eye[1], 516.f, 0.05f);
	EXPECT_NEAR(eye[2], -2560.f, 0.05f);
}

TEST(HorizonCompute, AMatrixWithNoEyeSaysSo)
{
	float zero[16] = { 0.f };
	float eye[3];
	EXPECT_FALSE(ComputeEyeFromViewProjection(zero, eye));
}

TEST(HorizonCompute, AQuadAheadIsVisible)
{
	float mvp[16];
	ViewProjection(0.f, 0.f, 0.f, 0.f, mvp);
	const float q[4][3] = { { -50, -20, -300 }, { -50, 20, -300 }, { 50, 20, -300 }, { 50, -20, -300 } };
	EXPECT_TRUE(ComputeQuadMayBeVisible(mvp, q));
}

TEST(HorizonCompute, AQuadSpanningTheViewIsVisibleThoughEveryCornerIsOffScreen)
{
	// [rc4l] A horizon line is long: both ends off screen, its middle straight ahead.
	float mvp[16];
	ViewProjection(0.f, 0.f, 0.f, 0.f, mvp);
	const float q[4][3] = { { -9000, -20, -300 }, { -9000, 20, -300 }, { 9000, 20, -300 }, { 9000, -20, -300 } };
	EXPECT_TRUE(ComputeQuadMayBeVisible(mvp, q));
}

TEST(HorizonCompute, AQuadBehindTheEyeIsNot)
{
	float mvp[16];
	ViewProjection(0.f, 0.f, 0.f, 0.f, mvp);
	const float q[4][3] = { { -50, -20, 300 }, { -50, 20, 300 }, { 50, 20, 300 }, { 50, -20, 300 } };
	EXPECT_FALSE(ComputeQuadMayBeVisible(mvp, q));
}

TEST(HorizonCompute, AQuadOffToEachSideIsNot)
{
	float mvp[16];
	ViewProjection(0.f, 0.f, 0.f, 0.f, mvp);
	const float left[4][3]  = { { -900, -20, -100 }, { -900, 20, -100 }, { -800, 20, -100 }, { -800, -20, -100 } };
	const float right[4][3] = { { 800, -20, -100 }, { 800, 20, -100 }, { 900, 20, -100 }, { 900, -20, -100 } };
	const float below[4][3] = { { -5, -900, -100 }, { -5, -800, -100 }, { 5, -800, -100 }, { 5, -900, -100 } };
	const float above[4][3] = { { -5, 800, -100 }, { -5, 900, -100 }, { 5, 900, -100 }, { 5, 800, -100 } };
	EXPECT_FALSE(ComputeQuadMayBeVisible(mvp, left));
	EXPECT_FALSE(ComputeQuadMayBeVisible(mvp, right));
	EXPECT_FALSE(ComputeQuadMayBeVisible(mvp, below));
	EXPECT_FALSE(ComputeQuadMayBeVisible(mvp, above));
}

TEST(HorizonCompute, AQuadBeyondTheFarPlaneIsNot)
{
	float mvp[16];
	ViewProjection(0.f, 0.f, 0.f, 0.f, mvp);
	const float q[4][3] = { { -50, -20, -90000 }, { -50, 20, -90000 }, { 50, 20, -90000 }, { 50, -20, -90000 } };
	EXPECT_FALSE(ComputeQuadMayBeVisible(mvp, q));
}
