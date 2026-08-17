// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Named after the symptom each case prevents. Both of these shipped wrong once.

#include <gtest/gtest.h>

#include "features/levelmesh/computation/flatdecal_compute.h"

using namespace zx::levelmesh;

// ---- which plane of a 3D floor -------------------------------------------------
//
// Shipped backwards, with a comment explaining the inversion the wrong way round, and the decals
// then sat on the wrong surface.

TEST(FlatDecalCompute, AShotLandingOnA3DFloorMarksItsTop)
{
	// F3DFloor::top is the CONTROL sector's ceiling plane, and that is the surface you walk on.
	EXPECT_TRUE(ComputeDecalUsesTopPlane(false));
}

TEST(FlatDecalCompute, AShotHittingTheUndersideMarksItsBottom)
{
	EXPECT_FALSE(ComputeDecalUsesTopPlane(true));
}

TEST(FlatDecalCompute, TheTwoCasesDisagree)
{
	// Whatever the convention, a walkable surface and an underside cannot be the same plane. A
	// version returning the same answer for both put every decal on one face of the floor.
	EXPECT_NE(ComputeDecalUsesTopPlane(true), ComputeDecalUsesTopPlane(false));
}

// ---- height ---------------------------------------------------------------------

TEST(FlatDecalCompute, AStationarySurfaceKeepsTheDecalWhereItLanded)
{
	// The common case: nothing has moved, so the only change is the offset that lifts the quad
	// clear of the surface.
	EXPECT_FLOAT_EQ(ComputeDecalHeight(192.f, 192.f, 192.f, false, 0.05f, 1.f), 192.05f);
}

TEST(FlatDecalCompute, ThePlanesOwnHeightIsNeverUsedDirectly)
{
	// This is the bug that drew decals 192 units underground. A shot landing on a 3D floor at 192
	// while the sector's floor sits at 0 must still be drawn at 192 -- the plane height is only ever
	// consulted as a DIFFERENCE.
	EXPECT_FLOAT_EQ(ComputeDecalHeight(192.f, 0.f, 0.f, false, 0.f, 1.f), 192.f);
	EXPECT_FLOAT_EQ(ComputeDecalHeight(192.f, 64.f, 64.f, false, 0.f, 1.f), 192.f);
	EXPECT_FLOAT_EQ(ComputeDecalHeight(192.f, -300.f, -300.f, false, 0.f, 1.f), 192.f);
}

TEST(FlatDecalCompute, ARisingLiftCarriesItsDecalsUp)
{
	// Shot at 64 on a platform that was at 64 when the shot landed and has since risen 40 units.
	EXPECT_FLOAT_EQ(ComputeDecalHeight(64.f, 64.f, 104.f, false, 0.f, 1.f), 104.f);
}

TEST(FlatDecalCompute, ADescendingLiftCarriesThemDown)
{
	EXPECT_FLOAT_EQ(ComputeDecalHeight(64.f, 64.f, 24.f, false, 0.f, 1.f), 24.f);
}

TEST(FlatDecalCompute, ACeilingDecalIsOffsetDownwardsIntoTheRoom)
{
	// "Clear of the surface" is downward for a ceiling. Offsetting the same way as a floor would
	// push it up INTO the ceiling and hide it.
	EXPECT_FLOAT_EQ(ComputeDecalHeight(128.f, 128.f, 128.f, true, 0.05f, 1.f), 127.95f);
}

TEST(FlatDecalCompute, TheOffsetGoesOppositeWaysForTheTwoSides)
{
	const float floorZ = ComputeDecalHeight(100.f, 100.f, 100.f, false, 2.f, 1.f);
	const float ceilZ  = ComputeDecalHeight(100.f, 100.f, 100.f, true, 2.f, 1.f);
	EXPECT_GT(floorZ, 100.f);
	EXPECT_LT(ceilZ, 100.f);
	EXPECT_FLOAT_EQ(floorZ - 100.f, 100.f - ceilZ);
}

TEST(FlatDecalCompute, MovementAndOffsetCompose)
{
	// Both terms at once, on a ceiling that was at the hit height and has since descended 10 units.
	EXPECT_FLOAT_EQ(ComputeDecalHeight(128.f, 128.f, 118.f, true, 0.05f, 1.f), 117.95f);
}

// ---- only track a plane that IS the surface --------------------------------
//
// Measured on dbab02's spawn: a shot landing at 128 on geometry the trace does not report as a 3D
// floor (trace.ffloor is NULL), while the plane consulted read 128 when the shot landed and 0 when
// the frame drew. Tracking that plane teleported the decal 128 units down.

TEST(FlatDecalCompute, APlaneAtTheHitHeightIsTheSurface)
{
	EXPECT_TRUE(ComputeDecalTracksPlane(128.f, 128.f, 1.f));
	EXPECT_TRUE(ComputeDecalTracksPlane(128.f, 128.4f, 1.f));
	EXPECT_TRUE(ComputeDecalTracksPlane(128.f, 127.6f, 1.f));
}

TEST(FlatDecalCompute, APlaneNowhereNearTheHitIsADifferentSurface)
{
	EXPECT_FALSE(ComputeDecalTracksPlane(128.f, 0.f, 1.f));
	EXPECT_FALSE(ComputeDecalTracksPlane(0.f, 128.f, 1.f));
}

TEST(FlatDecalCompute, ADecalOnAPlaneItDidNotLandOnDoesNotMoveWithIt)
{
	// Hit at 128 on a 3D floor while the sector's own floor sits at 0. That plane is not the
	// surface, so however far it travels the decal must not follow it -- this is the case that drew
	// decals 128 units underground.
	EXPECT_FLOAT_EQ(ComputeDecalHeight(128.f, 0.f, 0.f, false, 0.f, 1.f), 128.f);
	EXPECT_FLOAT_EQ(ComputeDecalHeight(128.f, 0.f, 40.f, false, 0.f, 1.f), 128.f);
}

TEST(FlatDecalCompute, ATrackedDecalStillRidesItsLift)
{
	// The guard must not cost the feature it was added for: a plane that WAS at the hit height still
	// carries its decals.
	EXPECT_FLOAT_EQ(ComputeDecalHeight(64.f, 64.f, 104.f, false, 0.f, 1.f), 104.f);
}
