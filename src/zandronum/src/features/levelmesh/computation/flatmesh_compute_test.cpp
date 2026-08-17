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

// ---- plane texture transform -----------------------------------------------
//
// The mesh baked (x/64, -y/64) and stopped, so scrolling floors did not scroll. The same matrix
// carries four other things, and the widest of them is silent: any flat whose texture is not 64x64
// tiled at the wrong rate everywhere, in every map, and reads as "the texture looks slightly off".

TEST(FlatMeshCompute, IdentityTransformIsThePlainMapping)
{
	float u = 0, v = 0;
	ComputePlaneUV(128.f, 256.f, ComputeIdentityPlaneUV(), u, v);
	EXPECT_FLOAT_EQ(u, 128.f / 64.f);
	EXPECT_FLOAT_EQ(v, -256.f / 64.f);   // V runs the other way
}

TEST(FlatMeshCompute, A128WideTextureTilesAtHalfTheRate)
{
	// The bug nobody reported: without the 64/width term a 128-wide flat tiles twice as fast as GL.
	PlaneUVTransform t = ComputeIdentityPlaneUV();
	t.texWidth = 128.f;
	t.texHeight = 128.f;
	float u = 0, v = 0;
	ComputePlaneUV(128.f, 128.f, t, u, v);
	EXPECT_FLOAT_EQ(u, 1.0f);    // 128 units across a 128-texel texture is exactly one tile
	EXPECT_FLOAT_EQ(v, -1.0f);
}

TEST(FlatMeshCompute, OffsetIsInTextureWidthsNotMapUnits)
{
	// A scrolling floor animates xoffs. Half a 64-texel texture is 0.5 in UV; half a 128-texel one
	// is the same 0.5 for twice the map distance, which is why the divisor is the texture size.
	PlaneUVTransform t = ComputeIdentityPlaneUV();
	t.xoffs = 32.f;
	float u = 0, v = 0;
	ComputePlaneUV(0.f, 0.f, t, u, v);
	EXPECT_FLOAT_EQ(u, 0.5f);

	t = ComputeIdentityPlaneUV();
	t.texWidth = 128.f;
	t.xoffs = 64.f;
	ComputePlaneUV(0.f, 0.f, t, u, v);
	EXPECT_FLOAT_EQ(u, 0.5f);
}

TEST(FlatMeshCompute, ScaleAppliesAfterTheOffset)
{
	// Order matters and is easy to get backwards: the plane's own scale multiplies the ALREADY
	// offset coordinate. Scaling first would move a scrolled floor to the wrong place.
	PlaneUVTransform t = ComputeIdentityPlaneUV();
	t.xoffs = 32.f;      // 0.5 in UV
	t.xscale = 2.f;
	float u = 0, v = 0;
	ComputePlaneUV(0.f, 0.f, t, u, v);
	EXPECT_FLOAT_EQ(u, 1.0f);   // (0 + 0.5) * 2, not 0 * 2 + 0.5
}

TEST(FlatMeshCompute, RotationTurnsTheTextureAndIsNegated)
{
	// GL negates the plane angle before building the matrix, so a 90-degree plane angle rotates the
	// coordinate by -90. Taking the sign from the map instead flips every rotated flat.
	PlaneUVTransform t = ComputeIdentityPlaneUV();
	t.angleDegrees = 90.f;
	float u = 0, v = 0;
	ComputePlaneUV(64.f, 0.f, t, u, v);   // untransformed this is (1, 0)
	EXPECT_NEAR(u, 0.f, 1e-5f);
	EXPECT_NEAR(v, -1.f, 1e-5f);          // rotated by -90, not +90
}

TEST(FlatMeshCompute, ACanvasTextureHasItsVScaleNegated)
{
	// A camera texture is stored upside down; GL compensates in this same matrix.
	PlaneUVTransform t = ComputeIdentityPlaneUV();
	t.hasCanvas = true;
	float u = 0, v = 0;
	ComputePlaneUV(0.f, 64.f, t, u, v);
	EXPECT_FLOAT_EQ(v, 1.0f);   // would be -1 without the negation
}

TEST(FlatMeshCompute, AZeroSizedTextureFallsBackToSixtyFour)
{
	// Division by a zero texture size would put infinities in the vertex buffer, where they are
	// silent until an entire batch renders as nothing.
	PlaneUVTransform t = ComputeIdentityPlaneUV();
	t.texWidth = 0.f;
	t.texHeight = 0.f;
	float u = 0, v = 0;
	ComputePlaneUV(64.f, 64.f, t, u, v);
	EXPECT_FLOAT_EQ(u, 1.0f);
	EXPECT_FLOAT_EQ(v, -1.0f);
}

// ---- wall blend classification ---------------------------------------------
//
// A pane of frosted glass rendered as a solid white slab: its transparency lives in the texture's
// alpha channel, so the wall's own alpha is 1 and an alpha-based rule calls it opaque.

TEST(FlatMeshCompute, AWallInTheTranslucentListBlendsEvenAtFullAlpha)
{
	// This is the frosted glass. Alpha says opaque; the engine's own routing says otherwise, and the
	// engine is right.
	EXPECT_EQ(ComputeWallBlendMode(true, false, 1.0f), 1);
}

TEST(FlatMeshCompute, AnOrdinaryWallIsOpaque)
{
	EXPECT_EQ(ComputeWallBlendMode(false, false, 1.0f), 0);
}

TEST(FlatMeshCompute, AWallWithLowAlphaBlendsWithoutBeingInTheList)
{
	// A 3D floor's side inherits the rover's alpha and is not necessarily routed as translucent.
	EXPECT_EQ(ComputeWallBlendMode(false, false, 0.5f), 1);
}

TEST(FlatMeshCompute, AnAdditiveWallIsAdditiveWhicheverListItIsIn)
{
	EXPECT_EQ(ComputeWallBlendMode(true, true, 1.0f), 2);
	EXPECT_EQ(ComputeWallBlendMode(false, true, 1.0f), 2);
}
