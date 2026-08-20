// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The plane cases, on the same terms as the wall cases.
//
// Two of these encode faults this renderer has actually shipped: a 3D floor's underside is a FLOOR
// plane seen from below, so every rule written as "floors face up" gets it backwards -- once as a
// culled surface and once as a surface that took no light at all because the side test found every
// lamp behind it. Both were invisible until someone stood in the one room that had them.

#include <gtest/gtest.h>

#include <math.h>

#include "features/surfaces/computation/planegeom_compute.h"

using namespace zx::surfaces;

namespace {

// ZDoom normalises a level floor at height h as c = -1, d = h.
SurfacePlane LevelFloor(float h)
{
	SurfacePlane p; p.a = 0.f; p.b = 0.f; p.c = -1.f; p.d = h;
	return p;
}

// ...and a ceiling the other way up.
SurfacePlane LevelCeiling(float h)
{
	SurfacePlane p; p.a = 0.f; p.b = 0.f; p.c = 1.f; p.d = -h;
	return p;
}

// A floor tilted along x, passing through (0,0,h), normalised the way P_AlignPlane leaves it.
SurfacePlane SlopedFloor(float h, float slope)
{
	const float len = sqrtf(slope * slope + 1.f);
	SurfacePlane p;
	p.a = slope / len; p.b = 0.f; p.c = -1.f / len;
	p.d = h / len;
	return p;
}

} // namespace

TEST(PlaneGeom, LevelPlanesReadTheirOwnHeight)
{
	EXPECT_FLOAT_EQ(64.f, ComputePlaneHeightAt(LevelFloor(64.f), 0.f, 0.f));
	EXPECT_FLOAT_EQ(64.f, ComputePlaneHeightAt(LevelFloor(64.f), 5000.f, -9000.f));
	EXPECT_FLOAT_EQ(192.f, ComputePlaneHeightAt(LevelCeiling(192.f), 1234.f, 5678.f));
}

TEST(PlaneGeom, ASlopeReadsDifferentlyAlongIt)
{
	const SurfacePlane s = SlopedFloor(0.f, 0.5f);
	EXPECT_NEAR(0.f, ComputePlaneHeightAt(s, 0.f, 0.f), 0.001f);
	// Tilted along x only: moving in y changes nothing, moving in x changes it linearly.
	EXPECT_NEAR(ComputePlaneHeightAt(s, 0.f, 0.f), ComputePlaneHeightAt(s, 0.f, 500.f), 0.001f);
	EXPECT_NEAR(2.f * ComputePlaneHeightAt(s, 100.f, 0.f), ComputePlaneHeightAt(s, 200.f, 0.f), 0.01f);
}

TEST(PlaneGeom, SlopedIsAskedOfTheEquationNotTheSector)
{
	EXPECT_FALSE(ComputePlaneIsSloped(LevelFloor(0.f)));
	EXPECT_FALSE(ComputePlaneIsSloped(LevelCeiling(128.f)));
	EXPECT_TRUE(ComputePlaneIsSloped(SlopedFloor(0.f, 0.25f)));
}

// A vertical plane has no height. Dividing by its c is how one malformed slope takes the whole
// level's geometry to infinity, so it answers rather than diverging.
TEST(PlaneGeom, AVerticalPlaneAnswersRatherThanDiverging)
{
	SurfacePlane vertical; vertical.a = 1.f; vertical.b = 0.f; vertical.c = 0.f; vertical.d = 0.f;
	EXPECT_FLOAT_EQ(0.f, ComputePlaneHeightAt(vertical, 100.f, 100.f));
	EXPECT_FALSE(ComputePlaneFacesViewer(vertical, 0.f, 0.f, 100.f));
}

TEST(PlaneGeom, AFloorFacesAViewerAboveIt)
{
	const SurfacePlane f = LevelFloor(0.f);
	EXPECT_TRUE(ComputePlaneFacesViewer(f, 0.f, 0.f, 41.f));
	EXPECT_FALSE(ComputePlaneFacesViewer(f, 0.f, 0.f, -41.f));
}

TEST(PlaneGeom, ACeilingFacesAViewerBelowIt)
{
	const SurfacePlane c = LevelCeiling(128.f);
	EXPECT_TRUE(ComputePlaneFacesViewer(c, 0.f, 0.f, 41.f));
	EXPECT_FALSE(ComputePlaneFacesViewer(c, 0.f, 0.f, 200.f));
}

// A viewer exactly in the plane is not looking at it from either side. Doom puts things exactly on
// floors constantly -- anything resting on one -- so this is the common case, not the edge.
TEST(PlaneGeom, LevelWithThePlaneIsNotFacing)
{
	EXPECT_FALSE(ComputePlaneFacesViewer(LevelFloor(64.f), 0.f, 0.f, 64.f));
}

// The normal is turned to the side the surface is SEEN from, which is the caller's knowledge.
TEST(PlaneGeom, NormalFollowsTheViewedSideNotThePlane)
{
	float fromAbove[3], fromBelow[3];
	ComputePlaneNormal(LevelFloor(0.f), false, fromAbove);
	ComputePlaneNormal(LevelFloor(0.f), true, fromBelow);
	EXPECT_FLOAT_EQ(-fromAbove[1], fromBelow[1]);
	EXPECT_NE(0.f, fromAbove[1]);
}

// [rc4l] The 3D floor case, which is why the flag exists at all.
//
// A 3D floor's walkable top is the CONTROL sector's ceiling plane, so its plane points down while
// the surface is looked at from above. Asked as "is this a floor" the answer is wrong; asked as
// "which side is it seen from" it is right, and the two shipped faults from getting this backwards
// were a surface culled entirely and a surface that took no dynamic light at all.
TEST(PlaneGeom, ACeilingPlaneSeenFromAboveGetsAnUpwardNormal)
{
	float n[3];
	// A ceiling plane (c positive) that is being looked at from above, as a 3D floor's top is.
	ComputePlaneNormal(LevelCeiling(64.f), true, n);
	EXPECT_LT(n[1], 0.f);   // turned to face the viewer above it rather than following the plane
	ComputePlaneNormal(LevelCeiling(64.f), false, n);
	EXPECT_GT(n[1], 0.f);
}

TEST(PlaneGeom, NormalsAreUnitLength)
{
	float n[3];
	ComputePlaneNormal(SlopedFloor(0.f, 0.75f), false, n);
	EXPECT_NEAR(1.f, sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]), 0.001f);
	// A degenerate plane still returns something usable rather than a zero vector that lights
	// nothing and sorts unpredictably.
	SurfacePlane zero; zero.a = zero.b = zero.c = zero.d = 0.f;
	ComputePlaneNormal(zero, false, n);
	EXPECT_NEAR(1.f, sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]), 0.001f);
}
