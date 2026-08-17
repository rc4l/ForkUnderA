// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Each test below corresponds to a fault that shipped and was found from a screenshot.
// The names say which one, so a failure points at the symptom rather than at an abstraction.

#include <gtest/gtest.h>

#include "features/levelmesh/computation/flatmesh_compute.h"

using namespace zx::levelmesh;

// ---- winding ---------------------------------------------------------------
//
// Two separate shipped faults: every ceiling in the level vanished, and later every 3D floor's
// walkable top surface vanished. Both were back-face culling deleting a surface wound for the wrong
// side, and both were silent.

TEST(FlatMeshCompute, SurfaceSeenFromAboveKeepsItsWinding)
{
	EXPECT_FALSE(ComputeFlatWindingReversed(false));
}

TEST(FlatMeshCompute, SurfaceSeenFromBelowIsReversed)
{
	EXPECT_TRUE(ComputeFlatWindingReversed(true));
}

TEST(FlatMeshCompute, TheTwoSidesAreOpposite)
{
	// The property that matters, stated directly: whatever the convention is, the two sides must
	// disagree. A version that returned the same answer for both deleted every ceiling.
	EXPECT_NE(ComputeFlatWindingReversed(true), ComputeFlatWindingReversed(false));
}

// ---- winding sign ----------------------------------------------------------

TEST(FlatMeshCompute, CounterClockwiseTriangleIsPositive)
{
	EXPECT_GT(ComputeTriangleWindingZ(0, 0, 1, 0, 0, 1), 0.f);
}

TEST(FlatMeshCompute, ClockwiseTriangleIsNegative)
{
	EXPECT_LT(ComputeTriangleWindingZ(0, 0, 0, 1, 1, 0), 0.f);
}

TEST(FlatMeshCompute, ReversingTwoVerticesFlipsTheSign)
{
	const float fwd = ComputeTriangleWindingZ(3, 1, 9, 2, 4, 7);
	const float rev = ComputeTriangleWindingZ(3, 1, 4, 7, 9, 2);
	EXPECT_FLOAT_EQ(fwd, -rev);
}

TEST(FlatMeshCompute, DegenerateTriangleIsZeroNotAWindingError)
{
	// A retired range is zeroed in place rather than freed, so collinear and all-identical vertices
	// both reach the verifier legitimately and must not be counted as wound either way.
	EXPECT_FLOAT_EQ(ComputeTriangleWindingZ(0, 0, 0, 0, 0, 0), 0.f);
	EXPECT_FLOAT_EQ(ComputeTriangleWindingZ(0, 0, 2, 2, 4, 4), 0.f);
}

// ---- blend classification --------------------------------------------------
//
// Flats were baked unconditionally opaque, so a translucent grate over a lava pit rendered solid.

TEST(FlatMeshCompute, FullyOpaqueIsOpaque)
{
	EXPECT_EQ(ComputeSurfaceBlendMode(false, 1.0f), 0);
}

TEST(FlatMeshCompute, AlphaOf255Of255IsStillOpaque)
{
	// The threshold has to let a fully opaque surface through as opaque, or every wall in the level
	// takes the sorted translucent path and stops writing depth.
	EXPECT_EQ(ComputeSurfaceBlendMode(false, 255.f / 255.f), 0);
}

TEST(FlatMeshCompute, AlphaJustBelowOpaqueIsTranslucent)
{
	EXPECT_EQ(ComputeSurfaceBlendMode(false, 254.f / 255.f), 1);
}

TEST(FlatMeshCompute, HalfAlphaIsTranslucent)
{
	EXPECT_EQ(ComputeSurfaceBlendMode(false, 0.5f), 1);
}

TEST(FlatMeshCompute, AdditiveWinsOverAlpha)
{
	// An additive surface is additive whatever its alpha says -- including at alpha 1, where the
	// alpha test alone would call it opaque.
	EXPECT_EQ(ComputeSurfaceBlendMode(true, 1.0f), 2);
	EXPECT_EQ(ComputeSurfaceBlendMode(true, 0.25f), 2);
}

// ---- coplanar overlap ------------------------------------------------------
//
// The mesh held both sides of every two-sided line, and coplanar quads built from different
// vertices stipple against each other. 1799 pairs on one map.

static MeshBox Wall(float x, float y0, float y1, float z0, float z1)
{
	MeshBox b = { x, x, y0, y1, z0, z1 };
	return b;
}

static MeshBox Flat(float x0, float x1, float y0, float y1, float z)
{
	MeshBox b = { x0, x1, y0, y1, z, z };
	return b;
}

TEST(FlatMeshCompute, TheRealCaseFromDbab02)
{
	// Verbatim from the map: one wall spanning z 24..128 and another spanning 88..128 in the plane
	// x = -64, same y extent. This is the pair that produced the checkerboard seam.
	EXPECT_TRUE(ComputeCoplanarOverlap(Wall(-64, 192, 256, 88, 128),
	                                   Wall(-64, 192, 256, 24, 128), 0.05f));
}

TEST(FlatMeshCompute, WallsInDifferentPlanesDoNotOverlap)
{
	EXPECT_FALSE(ComputeCoplanarOverlap(Wall(-64, 192, 256, 24, 128),
	                                    Wall(-32, 192, 256, 24, 128), 0.05f));
}

TEST(FlatMeshCompute, AdjacentFloorsSharingAnEdgeAreNotAnOverlap)
{
	// Every floor in a level touches its neighbours. A predicate that calls this an overlap reports
	// most of the map and is therefore useless.
	EXPECT_FALSE(ComputeCoplanarOverlap(Flat(0, 64, 0, 64, 0),
	                                    Flat(64, 128, 0, 64, 0), 0.05f));
}

TEST(FlatMeshCompute, StackedWallsMeetingAtAHeightAreNotAnOverlap)
{
	// The top and bottom halves of one split wall share a horizontal edge and nothing else.
	EXPECT_FALSE(ComputeCoplanarOverlap(Wall(-64, 192, 256, 0, 64),
	                                    Wall(-64, 192, 256, 64, 128), 0.05f));
}

TEST(FlatMeshCompute, FloorsAtDifferentHeightsDoNotOverlap)
{
	EXPECT_FALSE(ComputeCoplanarOverlap(Flat(0, 64, 0, 64, 0),
	                                    Flat(0, 64, 0, 64, 16), 0.05f));
}

TEST(FlatMeshCompute, TwoFloorsInTheSamePlaceDoOverlap)
{
	EXPECT_TRUE(ComputeCoplanarOverlap(Flat(0, 64, 0, 64, 0),
	                                   Flat(32, 96, 32, 96, 0), 0.05f));
}

TEST(FlatMeshCompute, TwoBoxesThatAreNotFlatAtAllAreNotCoplanar)
{
	// Solid volumes can intersect without being a rendering fault; only surfaces fight.
	MeshBox a = { 0, 64, 0, 64, 0, 64 };
	MeshBox b = { 32, 96, 32, 96, 32, 96 };
	EXPECT_FALSE(ComputeCoplanarOverlap(a, b, 0.05f));
}

TEST(FlatMeshCompute, SeparationIsTestedOnEveryAxis)
{
	// One case per axis, so a missing early-out cannot pass by luck on the others.
	EXPECT_FALSE(ComputeCoplanarOverlap(Flat(0, 64, 0, 64, 0), Flat(200, 264, 0, 64, 0), 0.05f));
	EXPECT_FALSE(ComputeCoplanarOverlap(Flat(0, 64, 0, 64, 0), Flat(0, 64, 200, 264, 0), 0.05f));
	EXPECT_FALSE(ComputeCoplanarOverlap(Flat(0, 64, 0, 64, 0), Flat(0, 64, 0, 64, 200), 0.05f));
}

// ---- winding consistency across the level ----------------------------------

TEST(FlatMeshCompute, OppositeAndUnanimousIsConsistent)
{
	EXPECT_TRUE(ComputeWindingConsistent(1200, 0, 0, 830));
	EXPECT_TRUE(ComputeWindingConsistent(0, 1200, 830, 0));
}

TEST(FlatMeshCompute, BothGroupsWindingTheSameWayIsNotConsistent)
{
	// This is the state that deleted every ceiling: floors and ceilings wound alike, so one cull
	// mode could not keep both.
	EXPECT_FALSE(ComputeWindingConsistent(1200, 0, 830, 0));
	EXPECT_FALSE(ComputeWindingConsistent(0, 1200, 0, 830));
}

TEST(FlatMeshCompute, AMixtureWithinOneGroupIsNotConsistent)
{
	// This is the 3D floor case: most surfaces seen from above wound one way, and the 3D floor tops
	// -- wound by their plane's normal instead of their viewing side -- wound the other.
	EXPECT_FALSE(ComputeWindingConsistent(1100, 100, 0, 830));
	EXPECT_FALSE(ComputeWindingConsistent(1200, 0, 30, 800));
}

TEST(FlatMeshCompute, AnAbsentGroupIsVacuouslyConsistent)
{
	// A level with no ceilings, or a mesh baked before any were seen, is not a fault.
	EXPECT_TRUE(ComputeWindingConsistent(1200, 0, 0, 0));
	EXPECT_TRUE(ComputeWindingConsistent(0, 0, 0, 830));
	EXPECT_TRUE(ComputeWindingConsistent(0, 0, 0, 0));
}
