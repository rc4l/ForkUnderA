// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Each test below corresponds to a fault that shipped and was found from a screenshot.
// The names say which one, so a failure points at the symptom rather than at an abstraction.

#include <gtest/gtest.h>

#include <math.h>

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

// [rc4l] The normal has to face the side the surface is SEEN from, not the way its plane points.
//
// This is the winding bug's quieter twin. A wound-away surface disappears and someone reports it
// within a minute; a normal facing away just never takes a dynamic light, which is indistinguishable
// from a light being out of range. It shipped as a corridor ceiling on dbab02 that GL lit and the
// backend did not.

TEST(FlatNormal, AnOrdinarySurfaceIsLeftAlone)
{
	// A sector floor's plane points up and is seen from above; a sector ceiling's points down and is
	// seen from below. Neither needs touching, and touching either would break every lit room.
	EXPECT_FALSE(ComputeFlatNormalFlipped(/*viewedFromBelow=*/false, /*planeUp=*/1.f));
	EXPECT_FALSE(ComputeFlatNormalFlipped(/*viewedFromBelow=*/true,  /*planeUp=*/-1.f));
}

TEST(FlatNormal, A3DFloorsSurfacesAreFlipped)
{
	// A 3D floor is modelled from a control sector turned upside down: the walkable TOP is that
	// sector's ceiling plane, pointing down while you stand on it, and the UNDERSIDE is its floor
	// plane, pointing up while you look at it from below. Both disagree with the side they are seen
	// from, and both must be turned round.
	EXPECT_TRUE(ComputeFlatNormalFlipped(/*viewedFromBelow=*/false, /*planeUp=*/-1.f));
	EXPECT_TRUE(ComputeFlatNormalFlipped(/*viewedFromBelow=*/true,  /*planeUp=*/1.f));
}

TEST(FlatNormal, AgreesWithTheWindingForTheSameSurface)
{
	// The two decisions answer the same question -- which side is this surface presented from -- so
	// they must never disagree. When they did, a surface was drawn facing you and lit as though it
	// were facing away.
	for (int below = 0; below < 2; below++)
		for (int up = -1; up <= 1; up += 2)
		{
			const bool flipped = ComputeFlatNormalFlipped(below != 0, (float)up);
			const bool pointsDown = (up < 0) != flipped;
			EXPECT_EQ(below != 0, pointsDown)
				<< "seen from " << (below ? "below" : "above") << ", plane up " << up;
			// And both decisions depend on the SIDE alone, never on the plane -- which is the whole
			// content of the bug they share.
			EXPECT_EQ(ComputeFlatWindingReversed(below != 0),
			          ComputeFlatWindingReversed(pointsDown));
		}
}

TEST(FlatNormal, LeavesAVerticalPlaneAlone)
{
	// Not a flat at all, and its vertical component is zero -- flipping on the sign of a rounding
	// error would make the answer depend on which way the last bit fell.
	EXPECT_FALSE(ComputeFlatNormalFlipped(false, 0.f));
	EXPECT_FALSE(ComputeFlatNormalFlipped(true, 0.f));
}

// ---- the plane mapping as an affine map ----------------------------------------
//
// [rc4l] A horizon plane is sampled where a ray lands, so the six numbers must give exactly what ComputePlaneUV gives.

namespace {

void ExpectAffineMatches(const PlaneUVTransform &t, float x, float y)
{
	const PlaneUVAffine a = ComputePlaneUVAffine(t);
	float u, v;
	ComputePlaneUV(x, y, t, u, v);
	// [rc4l] Relative to the size of the answer, since at 30000 units out a coordinate runs to the hundreds.
	const float tol = 1e-5f * (1.f + fabsf(u) + fabsf(v));
	EXPECT_NEAR(a.ux * x + a.uy * y + a.u0, u, tol) << "at " << x << "," << y;
	EXPECT_NEAR(a.vx * x + a.vy * y + a.v0, v, tol) << "at " << x << "," << y;
}

} // namespace

TEST(PlaneUVAffine, IdentityIsOneTexturePerSixtyFourUnitsWithVReversed)
{
	const PlaneUVAffine a = ComputePlaneUVAffine(ComputeIdentityPlaneUV());
	EXPECT_FLOAT_EQ(a.ux, 1.f / 64.f);
	EXPECT_FLOAT_EQ(a.uy, 0.f);
	EXPECT_FLOAT_EQ(a.u0, 0.f);
	EXPECT_FLOAT_EQ(a.vx, 0.f);
	EXPECT_FLOAT_EQ(a.vy, -1.f / 64.f);
	EXPECT_FLOAT_EQ(a.v0, 0.f);
}

TEST(PlaneUVAffine, MatchesThePerVertexMappingFarFromTheOrigin)
{
	// [rc4l] wdiex02's water is sampled tens of thousands of units out, which is where it counts.
	const PlaneUVTransform id = ComputeIdentityPlaneUV();
	ExpectAffineMatches(id, -4623.f, -489.f);
	ExpectAffineMatches(id, -37391.f, 32279.f);
}

TEST(PlaneUVAffine, CarriesRotationScaleOffsetAndTextureSize)
{
	PlaneUVTransform t = ComputeIdentityPlaneUV();
	t.angleDegrees = 37.f;
	t.xscale = 2.f;
	t.yscale = 0.5f;
	t.xoffs = 48.f;
	t.yoffs = -20.f;
	t.texWidth = 128.f;
	t.texHeight = 32.f;
	ExpectAffineMatches(t, 0.f, 0.f);
	ExpectAffineMatches(t, 1234.f, -567.f);
	ExpectAffineMatches(t, -30000.f, 29000.f);
}

TEST(PlaneUVAffine, ALargeScrollOffsetDoesNotCostTheSlope)
{
	// [rc4l] A scroller's offset grows for as long as the level runs, and the slope must not lose precision to it.
	PlaneUVTransform t = ComputeIdentityPlaneUV();
	t.xoffs = 6400000.f;
	const PlaneUVAffine a = ComputePlaneUVAffine(t);
	EXPECT_FLOAT_EQ(a.ux, 1.f / 64.f);
	EXPECT_FLOAT_EQ(a.u0, 100000.f);
}

TEST(PlaneUVAffine, ACanvasFlipsV)
{
	PlaneUVTransform t = ComputeIdentityPlaneUV();
	t.hasCanvas = true;
	const PlaneUVAffine a = ComputePlaneUVAffine(t);
	EXPECT_FLOAT_EQ(a.vy, 1.f / 64.f);
	ExpectAffineMatches(t, 300.f, -700.f);
}

// [rc4l] wdiexsec's reflective floor re-walked the scene from under it, and that walk's four sky quads replaced the player's eighty-eight.
TEST(FlatMeshCompute, OnlyThePlayersOuterWalkFillsThePlayersSkyList)
{
	EXPECT_EQ(kSkyCaptureMain, ComputeSkyCaptureList(true, 1, -1));
	EXPECT_EQ(kSkyCaptureNone, ComputeSkyCaptureList(true, 2, -1));    // [rc4l] a mirror or skybox re-entry
	EXPECT_EQ(kSkyCaptureNone, ComputeSkyCaptureList(true, 4, -1));
	EXPECT_EQ(kSkyCaptureCamera, ComputeSkyCaptureList(false, 1, -1)); // [rc4l] a camera texture's own view
	EXPECT_EQ(kSkyCaptureNone, ComputeSkyCaptureList(false, 2, -1));
}

// [rc4l] A walk through a line mirror, however deep, fills the mirrors' list, unless it is a stacked view's or a camera texture's.
TEST(FlatMeshCompute, AMirrorsWalkFillsTheMirrorsSkyList)
{
	EXPECT_EQ(kSkyCaptureMirror, ComputeSkyCaptureList(true, 2, -1, true));
	EXPECT_EQ(kSkyCaptureMirror, ComputeSkyCaptureList(true, 4, -1, true));
	EXPECT_EQ(kSkyCaptureStack, ComputeSkyCaptureList(true, 2, 0, true));
	EXPECT_EQ(kSkyCaptureNone, ComputeSkyCaptureList(false, 2, -1, true));
	EXPECT_EQ(kSkyCaptureMain, ComputeSkyCaptureList(true, 1, -1, true));
	EXPECT_FALSE(ComputeTakesSkyViewpoint(kSkyCaptureMirror, false));
}

// [rc4l] A camera texture and a stacked view paint other skies from their own lists, a mirror from the player's and the mirrors', the player's view from its own.
TEST(FlatMeshCompute, APassPaintsOtherSkiesFromTheListsItsViewSees)
{
	int lists[2] = { -9, -9 };
	EXPECT_EQ(1, ComputeSkyLookLists(true, 2, true, lists));
	EXPECT_EQ(kSkyCaptureCamera, lists[0]);
	EXPECT_EQ(1, ComputeSkyLookLists(false, 2, true, lists));
	EXPECT_EQ(kSkyCaptureStack + 2, lists[0]);
	EXPECT_EQ(2, ComputeSkyLookLists(false, -1, true, lists));
	EXPECT_EQ(kSkyCaptureMain, lists[0]);
	EXPECT_EQ(kSkyCaptureMirror, lists[1]);
	EXPECT_EQ(1, ComputeSkyLookLists(false, -1, false, lists));
	EXPECT_EQ(kSkyCaptureMain, lists[0]);
}

// [rc4l] WDI01: looking up through the town's stacked ceiling, every sky quad came from the stack's walk and went nowhere, so the sky was black.
TEST(FlatMeshCompute, TheWalkThroughAStackedViewFillsThatViewsSkyList)
{
	EXPECT_EQ(kSkyCaptureStack, ComputeSkyCaptureList(true, 2, 0));
	EXPECT_EQ(kSkyCaptureStack + 1, ComputeSkyCaptureList(true, 2, 1));
	EXPECT_EQ(kSkyCaptureNone, ComputeSkyCaptureList(true, 3, 0));               // [rc4l] the skybox inside the stack
	EXPECT_EQ(kSkyCaptureNone, ComputeSkyCaptureList(false, 2, 0));              // [rc4l] a camera texture's portal
	EXPECT_EQ(kSkyCaptureMain, ComputeSkyCaptureList(true, 1, 0));
}

// [rc4l] WDI07: the clocktower's fan clouds stand where only the walk through the town's stacked sky reaches, and only the player's walk was kept.
TEST(FlatMeshCompute, WhatAStackedViewsWalkDrawsBelongsToThatView)
{
	EXPECT_EQ(0, ComputeStackCaptureSlot(true, 2, 0));
	EXPECT_EQ(1, ComputeStackCaptureSlot(true, 2, 1));
	EXPECT_EQ(-1, ComputeStackCaptureSlot(true, 1, 0));                // [rc4l] the player's own walk
	EXPECT_EQ(-1, ComputeStackCaptureSlot(true, 2, -1));               // [rc4l] a mirror or skybox walk
	EXPECT_EQ(-1, ComputeStackCaptureSlot(true, 3, 0));                // [rc4l] a portal inside the stacked view
	EXPECT_EQ(-1, ComputeStackCaptureSlot(false, 2, 0));               // [rc4l] a camera texture's portal
}

TEST(FlatMeshCompute, APassDrawsTheSkyListItsOwnWalkFilled)
{
	EXPECT_EQ(kSkyCaptureMain, ComputeSkyDrawList(false, -1));
	EXPECT_EQ(kSkyCaptureCamera, ComputeSkyDrawList(true, -1));
	EXPECT_EQ(kSkyCaptureCamera, ComputeSkyDrawList(true, 1));
	EXPECT_EQ(kSkyCaptureStack + 1, ComputeSkyDrawList(false, 1));
	for (int slot = 0; slot < 8; slot++)
		EXPECT_EQ(ComputeSkyCaptureList(true, 2, slot), ComputeSkyDrawList(false, slot));
}

// [rc4l] WDI01: with no sky in the player's own walk, the backend fell back to the sector's ceiling "skybox", which is the stack's anchor, and rendered the sky from inside the town.
TEST(FlatMeshCompute, AStackedViewsSkyboxStandsInOnlyForAPlayerWalkThatNamedNone)
{
	EXPECT_TRUE(ComputeTakesSkyViewpoint(kSkyCaptureMain, false));
	EXPECT_TRUE(ComputeTakesSkyViewpoint(kSkyCaptureMain, true));
	EXPECT_TRUE(ComputeTakesSkyViewpoint(kSkyCaptureStack, false));
	EXPECT_FALSE(ComputeTakesSkyViewpoint(kSkyCaptureStack + 1, true));
	EXPECT_FALSE(ComputeTakesSkyViewpoint(kSkyCaptureCamera, false));
	EXPECT_FALSE(ComputeTakesSkyViewpoint(kSkyCaptureNone, false));
}

// [rc4l] WDI01's bathroom mirror reflected the room and not the player standing in front of it, and WDI12's left out the hand dryer behind the player.
TEST(FlatMeshCompute, WhatAMirrorsWalkDrawsBelongsToThatMirror)
{
	EXPECT_TRUE(ComputeMirrorCaptures(1, true, 2));
	EXPECT_FALSE(ComputeMirrorCaptures(0, true, 2));     // [rc4l] a skybox or stacked view is no mirror
	EXPECT_FALSE(ComputeMirrorCaptures(1, true, 3));     // [rc4l] a mirror inside a stacked view or skybox
	EXPECT_FALSE(ComputeMirrorCaptures(1, true, 1));
	EXPECT_FALSE(ComputeMirrorCaptures(1, false, 2));    // [rc4l] a camera texture's mirror
	EXPECT_EQ(5776, ComputeMirrorViewId(1, 5776, -1, 9000));
}

// [rc4l] A mirror seen in a mirror is a view of its own, as GL walks every reflection: named past the level's lines by its path from the player.
TEST(FlatMeshCompute, AReflectionInAReflectionIsAViewOfItsOwn)
{
	EXPECT_TRUE(ComputeMirrorCaptures(2, true, 3));
	EXPECT_TRUE(ComputeMirrorCaptures(4, true, 5));
	EXPECT_FALSE(ComputeMirrorCaptures(2, true, 4));
	EXPECT_EQ(9000, ComputeMirrorViewId(2, 5776, 0, 9000));
	EXPECT_EQ(9003, ComputeMirrorViewId(3, 5776, 3, 9000));
	std::vector<std::vector<int> > paths;
	const int ab[2] = { 10, 20 }, ba[2] = { 20, 10 }, aba[3] = { 10, 20, 10 };
	EXPECT_EQ(-1, ComputeFindMirrorPath(paths, ab, 2));
	EXPECT_EQ(0, ComputeAddMirrorPath(paths, ab, 2));
	EXPECT_EQ(1, ComputeAddMirrorPath(paths, ba, 2));
	EXPECT_EQ(0, ComputeAddMirrorPath(paths, ab, 2));
	EXPECT_EQ(2, ComputeAddMirrorPath(paths, aba, 3));
	EXPECT_EQ(0, ComputeFindMirrorPath(paths, aba, 2));   // [rc4l] its first two lines are ab
	EXPECT_EQ(1, ComputeFindMirrorPath(paths, ba, 2));
}

// [rc4l] A traced ray in the first reflection of line 10 that bounces off line 20 is in view ab's walk, and off line 10 again in aba's.
TEST(FlatMeshCompute, ATracedRayStepsFromViewToViewByTheMirrorItBouncesOff)
{
	std::vector<std::vector<int> > paths;
	const int ab[2] = { 10, 20 }, aba[3] = { 10, 20, 10 }, orphan[3] = { 30, 40, 30 }, one[1] = { 50 };
	ComputeAddMirrorPath(paths, ab, 2);
	ComputeAddMirrorPath(paths, aba, 3);
	ComputeAddMirrorPath(paths, orphan, 3);   // [rc4l] its outer view was never walked, so no ray reaches it
	ComputeAddMirrorPath(paths, one, 1);      // [rc4l] a first reflection is where a ray starts, not a step
	std::vector<int> steps(1, 99);
	ComputeMirrorSteps(paths, 9000, steps);
	const int want[6] = { 10, 20, 9000, 9000, 10, 9001 };
	ASSERT_EQ(6u, steps.size());
	for (int i = 0; i < 6; i++) EXPECT_EQ(want[i], steps[i]) << i;
}

// [rc4l] WDI01: the stacked view drew rooftops GL's walk through the stack never reached, as roof slabs over the courtyard's walls.
TEST(FlatMeshCompute, AStackedViewMarksEachSubsectorItsWalkReached)
{
	// [rc4l] Eight stacked views make ten slots an id, which the bit positions below are counted in.
	const int W = ComputeWalkSlots(8);
	EXPECT_EQ(10, W);
	EXPECT_EQ(0, ComputeStackVisWords(0, W));
	EXPECT_EQ(1, ComputeStackVisWords(3, W));
	EXPECT_EQ(2, ComputeStackVisWords(4, W));
	unsigned int bits[4] = { 0u, 0u, 0u, 0u };
	ComputeMarkStackVisible(bits, 0, 1, W);
	ComputeMarkStackVisible(bits, 12, 0, W);
	ComputeMarkStackVisible(bits, 12, 0, W);
	EXPECT_EQ(2u, bits[0]);              // [rc4l] subsector 0, second slot
	EXPECT_EQ(1u << 24, bits[3]);        // [rc4l] subsector 12, first slot (bit 120), set once
	EXPECT_TRUE(ComputeStackVisible(bits, 4, 0, 1, W));
	EXPECT_FALSE(ComputeStackVisible(bits, 4, 0, 0, W));
	EXPECT_TRUE(ComputeStackVisible(bits, 4, 12, 0, W));
	EXPECT_FALSE(ComputeStackVisible(bits, 4, 12, 8, W));
	EXPECT_FALSE(ComputeStackVisible(bits, 4, 40, 0, W));        // [rc4l] past the words
	EXPECT_FALSE(ComputeStackVisible(bits, 4, -1, 0, W));
	EXPECT_FALSE(ComputeStackVisible(nullptr, 4, 0, 1, W));
	// [rc4l] Clearing one slot leaves the others.
	ComputeMarkStackVisible(bits, 12, 1, W);
	ComputeClearStackVisible(bits, 12, 0, W);
	EXPECT_FALSE(ComputeStackVisible(bits, 4, 12, 0, W));
	EXPECT_TRUE(ComputeStackVisible(bits, 4, 12, 1, W));
	// [rc4l] A pass draws what its walk drew, and everything when it has no walk or the piece no id, as the scene shader culls.
	EXPECT_TRUE(ComputePassDraws(bits, 4, 12, 1, W));
	EXPECT_FALSE(ComputePassDraws(bits, 4, 12, 0, W));
	EXPECT_TRUE(ComputePassDraws(bits, 4, 12, -1, W));
	EXPECT_TRUE(ComputePassDraws(bits, 4, -1, 0, W));
	EXPECT_FALSE(ComputePassDraws(nullptr, 0, 3, 0, W));   // [rc4l] a slot with no bits uploaded drew nothing
}

// [rc4l] A level's slots and sky lists follow its stacked views: a level with none has the player's and the skybox's walks alone.
TEST(FlatMeshCompute, ALevelHasASlotAndASkyListPerStackedView)
{
	EXPECT_EQ(2, ComputeWalkSlots(0));
	EXPECT_EQ(2, ComputeWalkSlots(-3));
	EXPECT_EQ(35, ComputeWalkSlots(33));
	EXPECT_EQ(kSkyCaptureStack, ComputeSkyCaptureLists(0));
	EXPECT_EQ(kSkyCaptureStack, ComputeSkyCaptureLists(-1));
	EXPECT_EQ(kSkyCaptureStack + 33, ComputeSkyCaptureLists(33));
	EXPECT_EQ(kFirstStackWalkSlot, ComputeStackWalkSlot(0));
	EXPECT_EQ(kFirstStackWalkSlot + 32, ComputeStackWalkSlot(32));
	EXPECT_EQ(-1, ComputeStackWalkSlot(-1));
}

TEST(FlatMeshCompute, AWallsProbeStepsOntoItsOwnSideAndAFlatsStaysAtItsCentre)
{
	float x, y;
	EXPECT_TRUE(ComputePieceProbe(100.f, 50.f, 0.f, 0.f, -2.f, x, y));    // [rc4l] a wall facing south, normal not unit
	EXPECT_FLOAT_EQ(100.f, x);
	EXPECT_FLOAT_EQ(49.f, y);
	EXPECT_FALSE(ComputePieceProbe(100.f, 50.f, 0.3f, 0.9f, 0.f, x, y));  // [rc4l] a gentle slope is still a flat
	EXPECT_FLOAT_EQ(100.f, x);
	EXPECT_FLOAT_EQ(50.f, y);
	EXPECT_FALSE(ComputePieceProbe(100.f, 50.f, 0.f, 0.f, 0.f, x, y));    // [rc4l] no normal at all
	EXPECT_FLOAT_EQ(100.f, x);
	EXPECT_FLOAT_EQ(50.f, y);
}

// [rc4l] WDI07's clocktower roof: an ARTILE05 floor leaning (-0.96, 0.29, 0) was placed as a wall, by a sidedef its stacked view's walk never drew, and culled.
TEST(FlatMeshCompute, ASteepFloorIsStillAFlatAndOnlyAnUprightSurfaceIsAWall)
{
	float x, y;
	EXPECT_FALSE(ComputePieceProbe(100.f, 50.f, -0.96f, 0.29f, 0.f, x, y));
	EXPECT_FLOAT_EQ(100.f, x);
	EXPECT_FLOAT_EQ(50.f, y);
	EXPECT_FALSE(ComputePieceProbe(100.f, 50.f, 0.f, -0.3f, 0.95f, x, y));  // [rc4l] a steep ceiling
	EXPECT_TRUE(ComputePieceProbe(100.f, 50.f, 1.f, 0.001f, 0.f, x, y));    // [rc4l] a wall whose normal carries rounding
	EXPECT_FLOAT_EQ(101.f, x);
	EXPECT_FLOAT_EQ(50.f, y);
}

namespace { struct V3 { float x, y, z; }; }

// [rc4l] One average for the stacked view's placement, the translucent sort and the patch report, which each kept their own loop.
TEST(FlatMeshCompute, APiecesCentroidIsItsVerticesMeanAndAnOverrunIsRefused)
{
	const V3 v[4] = { { 0, 0, 0 }, { 2, 4, 6 }, { 4, 8, 12 }, { 100, 100, 100 } };
	float x = -1.f, y = -1.f, z = -1.f;
	ASSERT_TRUE(ComputeCentroid(v, 4, 0, 3, x, y, z));
	EXPECT_FLOAT_EQ(2.f, x);
	EXPECT_FLOAT_EQ(4.f, y);
	EXPECT_FLOAT_EQ(6.f, z);
	ASSERT_TRUE(ComputeCentroid(v, 4, 3, 1, x, y, z));
	EXPECT_FLOAT_EQ(100.f, x);
	EXPECT_FALSE(ComputeCentroid(v, 4, 2, 3, x, y, z));                  // [rc4l] runs off the end
	EXPECT_FALSE(ComputeCentroid(v, 4, 5, 1, x, y, z));                  // [rc4l] starts past it
	EXPECT_FALSE(ComputeCentroid(v, 4, 0, 0, x, y, z));
	EXPECT_FALSE(ComputeCentroid((const V3 *)nullptr, 4, 0, 1, x, y, z));
	EXPECT_FLOAT_EQ(100.f, x);                                            // [rc4l] a refusal leaves the answer alone
}

// [rc4l] WDI01: looking over a low roof, GL showed the stacked rooftops through its stencil's cap where the backend, with no cap, showed sky.
TEST(FlatMeshCompute, AStacksCapCoversTheMapOverheadOrUnderfoot)
{
	float minX = 0.f, maxX = 0.f, minY = 0.f, maxY = 0.f, x, y, z;
	for (int k = 0; k < 6; k++)
	{
		ComputeStackCapCorner(true, k, x, y, z);
		EXPECT_FLOAT_EQ(32767.f, z);
		minX = x < minX ? x : minX; maxX = x > maxX ? x : maxX;
		minY = y < minY ? y : minY; maxY = y > maxY ? y : maxY;
	}
	EXPECT_FLOAT_EQ(-32767.f, minX); EXPECT_FLOAT_EQ(32767.f, maxX);
	EXPECT_FLOAT_EQ(-32767.f, minY); EXPECT_FLOAT_EQ(32767.f, maxY);
	ComputeStackCapCorner(false, 0, x, y, z);
	EXPECT_FLOAT_EQ(-32767.f, z);
	float x2, y2, z2;
	ComputeStackCapCorner(true, 3, x2, y2, z2);   // [rc4l] the two triangles share their diagonal
	ComputeStackCapCorner(true, 0, x, y, z);
	EXPECT_FLOAT_EQ(x, x2); EXPECT_FLOAT_EQ(y, y2);
}

// [rc4l] WDI01: a window's glass, a middle texture with see-through texels, came out of the map bake solid.
TEST(FlatMeshCompute, ATwoSidedMiddleIsTranslucentByItsLineOrItsTexture)
{
	EXPECT_TRUE(ComputeMidTextureTranslucent(1.f, false, true));     // [rc4l] glass in the texture
	EXPECT_TRUE(ComputeMidTextureTranslucent(0.5f, false, false));   // [rc4l] a translucent line
	EXPECT_TRUE(ComputeMidTextureTranslucent(1.f, true, false));     // [rc4l] an additive one
	EXPECT_FALSE(ComputeMidTextureTranslucent(1.f, false, false));
	EXPECT_FALSE(ComputeMidTextureTranslucent(0.f, true, false));    // [rc4l] alpha 0 skips GL's linedef block
	EXPECT_TRUE(ComputeMidTextureTranslucent(0.f, false, true));
}

// [rc4l] A stacked ceiling the map bake drew solid hid the stacked view's walls behind it, which GL never draws.
TEST(FlatMeshCompute, AStackedPlaneTakesItsOwnAlphaAndAnyOtherWhatItsReflectionLeaves)
{
	EXPECT_FLOAT_EQ(0.f, ComputePlaneAlpha(true, 0.f, 0.f));
	EXPECT_FLOAT_EQ(0.5f, ComputePlaneAlpha(true, 0.5f, 0.25f));   // [rc4l] a translucent stack ignores the reflection
	EXPECT_FLOAT_EQ(1.f, ComputePlaneAlpha(false, 0.f, 0.f));
	EXPECT_FLOAT_EQ(0.75f, ComputePlaneAlpha(false, 1.f, 0.25f));
}

// [rc4l] WDI01's rooftops: GL's clipper drops a roof's trim behind the walls in front of it, so a wall is kept or dropped by the seg it lies on.
TEST(FlatMeshCompute, APointsDistanceToASegmentStopsAtItsEnds)
{
	EXPECT_FLOAT_EQ(0.f, ComputeSegmentDistance2(5.f, 0.f, 0.f, 0.f, 10.f, 0.f));
	EXPECT_FLOAT_EQ(9.f, ComputeSegmentDistance2(5.f, 3.f, 0.f, 0.f, 10.f, 0.f));
	EXPECT_FLOAT_EQ(25.f, ComputeSegmentDistance2(-3.f, 4.f, 0.f, 0.f, 10.f, 0.f));   // [rc4l] past the start
	EXPECT_FLOAT_EQ(4.f, ComputeSegmentDistance2(12.f, 0.f, 0.f, 0.f, 10.f, 0.f));    // [rc4l] past the end
	EXPECT_FLOAT_EQ(2.f, ComputeSegmentDistance2(1.f, 1.f, 0.f, 0.f, 0.f, 0.f));      // [rc4l] a seg with no length
}

// [rc4l] wdiexsec: looking up from under the pool, the water's underside took the light under it, the script's blue, where GL lights a top plane from above.
TEST(FlatMeshCompute, AFacesLightComesFromItsPlaneNotFromTheViewer)
{
	EXPECT_FALSE(ComputeFFFaceLight(true, false, false, false).lightBelow);   // [rc4l] top, from above
	EXPECT_FALSE(ComputeFFFaceLight(true, true, false, false).lightBelow);    // [rc4l] top, from below
	EXPECT_TRUE(ComputeFFFaceLight(false, true, false, false).lightBelow);    // [rc4l] bottom, from below
	EXPECT_TRUE(ComputeFFFaceLight(false, false, false, false).lightBelow);   // [rc4l] bottom, from above
}

TEST(FlatMeshCompute, AFogFaceReadsItsLightFromTheOtherSide)
{
	EXPECT_TRUE(ComputeFFFaceLight(true, true, true, false).lightBelow);
	EXPECT_FALSE(ComputeFFFaceLight(false, false, true, false).lightBelow);
}

// [rc4l] GL gives FF_FIX's model light to a bottom plane seen from above and to nothing else.
TEST(FlatMeshCompute, OnlyABottomSeenFromAboveTakesTheFixedModelLight)
{
	EXPECT_TRUE(ComputeFFFaceLight(false, false, false, true).modelLight);
	EXPECT_FALSE(ComputeFFFaceLight(true, true, false, true).modelLight);
	EXPECT_FALSE(ComputeFFFaceLight(true, false, false, true).modelLight);
	EXPECT_FALSE(ComputeFFFaceLight(false, true, false, true).modelLight);
	EXPECT_FALSE(ComputeFFFaceLight(false, false, false, false).modelLight);
}

// [rc4l] wdiexsec: a barrel drawn in the player's scene was wiped by a mirror walk, and gating on portal depth then dropped it, since GL draws the player's scene at depth 1.
TEST(FlatMeshCompute, OnlyThePlayersOutermostSceneOwnsTheFramesCapture)
{
	EXPECT_TRUE(ComputeOwnsFrameCapture(true, 1));
	EXPECT_TRUE(ComputeOwnsFrameCapture(true, 0));
	EXPECT_FALSE(ComputeOwnsFrameCapture(true, 2));
	EXPECT_FALSE(ComputeOwnsFrameCapture(false, 1));
}

// [rc4l] WDI04's corner room: subsector 3796 has the nodebuilder's sector 385 and render sector 164, and only 164's floor is where GL draws it.
TEST(FlatMeshCompute, AFlatIsDrawnWithItsRenderSector)
{
	EXPECT_EQ(164, ComputeFlatSector(164, 385));
	EXPECT_EQ(7, ComputeFlatSector(7, 7));
	EXPECT_EQ(385, ComputeFlatSector(-1, 385));
}

// [rc4l] A stacked view or a mirror draws the sprites its own walk took, as GL's does, and the frame's other passes never draw them.
TEST(FlatMeshCompute, ASpriteIsDrawnOnlyInThePassWhoseWalkTookIt)
{
	EXPECT_TRUE(ComputeDrawsDynamicPiece(-1, -1, -1, -1, -1, -1));
	EXPECT_TRUE(ComputeDrawsDynamicPiece(-1, 1, -1, -1, 1, -1));
	EXPECT_FALSE(ComputeDrawsDynamicPiece(-1, 1, -1, -1, -1, -1));   // [rc4l] the player's view
	EXPECT_FALSE(ComputeDrawsDynamicPiece(-1, 1, -1, -1, 0, -1));    // [rc4l] the other stacked view
	EXPECT_FALSE(ComputeDrawsDynamicPiece(-1, -1, -1, -1, 0, -1));   // [rc4l] the player's walk's sprites are not this view's
	EXPECT_TRUE(ComputeDrawsDynamicPiece(5776, -1, -1, 5776, -1, -1));
	EXPECT_FALSE(ComputeDrawsDynamicPiece(5776, -1, -1, -1, -1, -1));    // [rc4l] the player's view never shows the viewer
	EXPECT_FALSE(ComputeDrawsDynamicPiece(5765, -1, -1, 5776, -1, -1));  // [rc4l] WDI07: another mirror's viewer, faced to its camera
	EXPECT_FALSE(ComputeDrawsDynamicPiece(-1, -1, -1, 5776, -1, -1));    // [rc4l] the mirror draws its own walk's room, not the player's
}

// [rc4l] A decal on a mirror's face waits for the pass over that mirror's surface, which draws nothing else, as RenderMirrorSurface follows the sheen with DoDrawDecals.
TEST(FlatMeshCompute, AMirrorsDecalIsDrawnOnlyOverItsSurface)
{
	EXPECT_TRUE(ComputeDrawsDynamicPiece(-1, -1, 12, -1, -1, 12));
	EXPECT_FALSE(ComputeDrawsDynamicPiece(-1, -1, 12, -1, -1, -1));   // [rc4l] the player's world pass, which the surface would cover
	EXPECT_FALSE(ComputeDrawsDynamicPiece(-1, -1, 12, -1, -1, 13));   // [rc4l] another mirror's surface
	EXPECT_FALSE(ComputeDrawsDynamicPiece(-1, -1, -1, -1, -1, 12));   // [rc4l] a sprite is the world's
	EXPECT_FALSE(ComputeDrawsDynamicPiece(12, -1, 12, -1, -1, 12));   // [rc4l] one the reflection's own walk took
}

// [rc4l] The same subsector 3796: a plane modelled on 164 or 385 is its own and goes in GL's solid pass, one modelled on a 3D floor's control sector is masked.
TEST(FlatMeshCompute, AFlatIsItsSubsectorsOwnUnlessA3DFloorModelsIt)
{
	EXPECT_TRUE(ComputeIsOwnFlat(164, 385, 164));
	EXPECT_TRUE(ComputeIsOwnFlat(385, 385, 164));
	EXPECT_TRUE(ComputeIsOwnFlat(-1, 385, 164));
	EXPECT_FALSE(ComputeIsOwnFlat(900, 385, 164));
}

// [rc4l] WDI07 rendered four washroom mirrors from across the map every frame, half the frame's world passes, though GL's walk reached none of them.
TEST(FlatMeshCompute, AMirrorIsRenderedOnlyWhenItCanBeSeen)
{
	EXPECT_TRUE(ComputeRendersMirror(true, false, false));
	EXPECT_FALSE(ComputeRendersMirror(false, false, true));   // [rc4l] GL's walk is the authority while it runs
	EXPECT_TRUE(ComputeRendersMirror(false, true, true));
	EXPECT_FALSE(ComputeRendersMirror(false, true, false));
}

// [rc4l] gl_flats.cpp's two arms: an absolute floor light gives way to a band no sector casts, an absolute ceiling light never does, and a sector's own band is never taken from itself.
TEST(FlatMeshCompute, APlaneTakesTheBandLightByProcessSectorsRule)
{
	EXPECT_TRUE(ComputePlaneTakesBandLight(false, false, true, false));
	EXPECT_TRUE(ComputePlaneTakesBandLight(true, false, true, false));
	EXPECT_FALSE(ComputePlaneTakesBandLight(false, false, true, true));   // [rc4l] its own band
	EXPECT_FALSE(ComputePlaneTakesBandLight(true, true, false, false));
	EXPECT_FALSE(ComputePlaneTakesBandLight(false, true, true, false));
	EXPECT_TRUE(ComputePlaneTakesBandLight(false, true, false, false));
}

// [rc4l] WDI18's forest: the player's walk marks its own slot, a stacked view's walk its view's, and a camera's or a mirror's none.
TEST(FlatMeshCompute, EachWalkMarksItsOwnSlot)
{
	EXPECT_EQ(ComputeStackWalkSlot(1), ComputeWalkSlot(false, true, 1, false));
	EXPECT_EQ(-1, ComputeWalkSlot(false, true, -1, true));     // [rc4l] a stacked view no slot took
	EXPECT_EQ(kMainWalkSlot, ComputeWalkSlot(false, false, -1, true));
	EXPECT_EQ(-1, ComputeWalkSlot(false, false, -1, false));
	EXPECT_EQ(kSkyboxWalkSlot, ComputeWalkSlot(true, true, 1, false));    // [rc4l] a stacked view's sky is the same skybox
	unsigned int bits[1] = { 0u };
	const int W = ComputeWalkSlots(8);
	ComputeMarkStackVisible(bits, 1, kMainWalkSlot, W);
	EXPECT_TRUE(ComputeStackVisible(bits, 1, 1, kMainWalkSlot, W));
	EXPECT_FALSE(ComputeStackVisible(bits, 1, 1, kSkyboxWalkSlot, W));
}

// [rc4l] AOW03's tower: the sky portal draws only what GL's skybox walk reached, a stacked view what its own walk did, any other pass everything.
TEST(FlatMeshCompute, APassDrawsWhatItsOwnWalkDrew)
{
	EXPECT_EQ(ComputeStackWalkSlot(1), ComputePassWalkSlot(true, 1, false, false));
	EXPECT_EQ(-1, ComputePassWalkSlot(true, -1, false, true));
	EXPECT_EQ(kSkyboxWalkSlot, ComputePassWalkSlot(true, -1, true, true));
	EXPECT_EQ(-1, ComputePassWalkSlot(true, -1, true, false));    // [rc4l] no skybox walk this frame, so the reach test stands in
	EXPECT_EQ(-1, ComputePassWalkSlot(false, 1, true, true));
	EXPECT_FALSE(ComputeWalkBitsNeeded(0, 0, false));
	EXPECT_TRUE(ComputeWalkBitsNeeded(1, 0, false));
	EXPECT_TRUE(ComputeWalkBitsNeeded(0, 2, false));
	EXPECT_TRUE(ComputeWalkBitsNeeded(0, 0, true));
}

// [rc4l] Standalone: the player's pass draws only what features/visibility's walk reached, and only the player's pass does.
TEST(FlatMeshCompute, ThePlayersPassDrawsWhatTheVisibilityWalkReached)
{
	EXPECT_EQ(kMainWalkSlot, ComputePassWalkSlot(true, -1, false, false, true));
	EXPECT_EQ(ComputeStackWalkSlot(1), ComputePassWalkSlot(true, 1, false, false, true));          // [rc4l] a stacked view keeps its own walk
	EXPECT_EQ(-1, ComputePassWalkSlot(true, -1, true, false, true));         // [rc4l] and the sky portal its own rule
	EXPECT_EQ(-1, ComputePassWalkSlot(false, -1, false, false, true));       // [rc4l] no bits uploaded, nothing culled
	EXPECT_TRUE(ComputeWalkBitsNeeded(0, 0, false, true));
}

// [rc4l] AOW03's lake (queue #35): crouched under its fake floor the eye is below, and every height transfer then shows its underwater planes.
TEST(FlatMeshCompute, TheViewAreaIsWhereTheEyeStandsAgainstTheTransfer)
{
	EXPECT_EQ(kViewAreaBelow, ComputeViewArea(true, -35.5f, -26.f, 776.f, false));
	EXPECT_EQ(kViewAreaBelow, ComputeViewArea(true, -26.f, -26.f, 776.f, false));     // [rc4l] at the surface is under it
	EXPECT_EQ(kViewAreaNormal, ComputeViewArea(true, -15.f, -26.f, 776.f, false));
	EXPECT_EQ(kViewAreaAbove, ComputeViewArea(true, 800.f, -26.f, 776.f, false));
	EXPECT_EQ(kViewAreaNormal, ComputeViewArea(true, 800.f, -26.f, 776.f, true));     // [rc4l] a floor-only transfer has no above
	EXPECT_EQ(kViewAreaNormal, ComputeViewArea(false, -35.5f, -26.f, 776.f, false));
}

// [rc4l] The player's walk's holes go to the scene shader until it is full, and a stacked view's or a camera's are painted as sky.
TEST(FlatMeshCompute, OnlyThePlayersSkyHolesGoToTheShader)
{
	EXPECT_TRUE(ComputeSkyHoleToShader(kSkyCaptureMain, 0));
	EXPECT_TRUE(ComputeSkyHoleToShader(kSkyCaptureMain, kMaxSkyHoles - 1));
	EXPECT_FALSE(ComputeSkyHoleToShader(kSkyCaptureMain, kMaxSkyHoles));
	EXPECT_FALSE(ComputeSkyHoleToShader(kSkyCaptureStack, 0));
	EXPECT_FALSE(ComputeSkyHoleToShader(kSkyCaptureCamera, 0));
}

// [rc4l] WDI18's linedef 14495: a pillar beyond a bare one-sided wall is behind its hole, the yard in front of it and anything over, under or beside the hole's span is not.
TEST(FlatMeshCompute, APointIsBehindASkyHoleOnlyThroughItsSpan)
{
	const float zfloor[2] = { 0.f, 0.f }, zceil[2] = { 256.f, 128.f };
	float hole[kSkyHoleFloats];
	ComputeSkyHoleRecord(0.f, 512.f, 768.f, 512.f, zfloor, zceil, hole);
	EXPECT_EQ(768.f, hole[2]); EXPECT_EQ(512.f, hole[3]);
	EXPECT_EQ(0.f, hole[4]); EXPECT_EQ(256.f, hole[5]); EXPECT_EQ(128.f, hole[7]);
	const float eye[3] = { 384.f, 41.f, 256.f };            // [rc4l] mesh space: map x, height, map y
	const float pillar[3] = { 384.f, 32.f, 800.f };
	const float yard[3] = { 384.f, 0.f, 400.f };
	const float onTheLine[3] = { 384.f, 32.f, 512.f };
	const float overIt[3] = { 384.f, 2000.f, 800.f };
	const float underIt[3] = { 384.f, -2000.f, 800.f };
	const float pastItsEnd[3] = { 1500.f, 32.f, 800.f };
	const float pastItsStart[3] = { -800.f, 32.f, 800.f };
	const float behindTheEye[3] = { 384.f, 32.f, -400.f };
	EXPECT_TRUE(ComputeBehindSkyHole(eye, pillar, hole));
	EXPECT_FALSE(ComputeBehindSkyHole(eye, yard, hole));
	EXPECT_FALSE(ComputeBehindSkyHole(eye, onTheLine, hole));
	EXPECT_FALSE(ComputeBehindSkyHole(eye, overIt, hole));
	EXPECT_FALSE(ComputeBehindSkyHole(eye, underIt, hole));
	EXPECT_FALSE(ComputeBehindSkyHole(eye, pastItsEnd, hole));
	EXPECT_FALSE(ComputeBehindSkyHole(eye, pastItsStart, hole));
	EXPECT_FALSE(ComputeBehindSkyHole(eye, behindTheEye, hole));
	// [rc4l] The ceiling falls from 256 to 128 along the wall, so a point seen high through its low end passes over it.
	const float lowEndHigh[3] = { 700.f, 400.f, 800.f };
	const float highEndHigh[3] = { 64.f, 400.f, 800.f };
	EXPECT_FALSE(ComputeBehindSkyHole(eye, lowEndHigh, hole));
	EXPECT_TRUE(ComputeBehindSkyHole(eye, highEndHigh, hole));
	// [rc4l] A line from the eye running along the wall never meets it.
	const float alongIt[kSkyHoleFloats] = { 100.f, 0.f, 100.f, 512.f, 0.f, 256.f, 0.f, 256.f };
	const float eye2[3] = { 100.f, 41.f, -100.f };
	const float ahead[3] = { 100.f, 41.f, 900.f };
	EXPECT_FALSE(ComputeBehindSkyHole(eye2, ahead, alongIt));
}

// [rc4l] A stamp for a plain floor at height 0, lit 160, opaque.
static FlatStamp PlainFloorStamp()
{
	FlatStamp s = {};
	s.pc = 65536; s.xscale = s.yscale = 65536;
	s.material = &s; s.baseTex = &s;
	s.lightLevel = 160; s.alpha = 65536;
	return s;
}

// [rc4l] AOW03's lights change 56 sectors a tic, and each rebuilt every floor in them though only the shade moved.
TEST(FlatMesh, ALightChangeReshadesThePieceAndKeepsItsVertices)
{
	const FlatStamp was = PlainFloorStamp();
	FlatStamp now = was;
	EXPECT_EQ(kFlatSame, ComputeFlatChange(true, was, now, false));
	now.lightLevel = 192;
	EXPECT_EQ(kFlatReshade, ComputeFlatChange(true, was, now, false));
	now = was; now.extraLight = 8;
	EXPECT_EQ(kFlatReshade, ComputeFlatChange(true, was, now, false));
	now = was; now.lightColor = 0xff0000; now.fadeColor = 0x202020;
	EXPECT_EQ(kFlatReshade, ComputeFlatChange(true, was, now, false));
	now = was; now.desaturation = 10; now.blendFactor = 2;
	EXPECT_EQ(kFlatReshade, ComputeFlatChange(true, was, now, false));
	now = was; now.alpha = 32768; now.renderStyle = 2;
	EXPECT_EQ(kFlatReshade, ComputeFlatChange(true, was, now, false));
}

TEST(FlatMesh, AMovedPlaneOrANewScaleAngleOrTextureRebuildsTheVertices)
{
	const FlatStamp was = PlainFloorStamp();
	FlatStamp now = was; now.pd = -64 * 65536;
	EXPECT_EQ(kFlatRebuild, ComputeFlatChange(true, was, now, false));
	now = was; now.pa = 100; now.pb = 100;
	EXPECT_EQ(kFlatRebuild, ComputeFlatChange(true, was, now, false));
	now = was; now.dz = -1.f;
	EXPECT_EQ(kFlatRebuild, ComputeFlatChange(true, was, now, false));
	now = was; now.xscale = 2 * 65536;
	EXPECT_EQ(kFlatRebuild, ComputeFlatChange(true, was, now, false));
	now = was; now.yscale = 2 * 65536;
	EXPECT_EQ(kFlatRebuild, ComputeFlatChange(true, was, now, false));
	now = was; now.angle = 0x40000000u;
	EXPECT_EQ(kFlatRebuild, ComputeFlatChange(true, was, now, false));
	int other = 0;
	now = was; now.material = &other;
	EXPECT_EQ(kFlatRebuild, ComputeFlatChange(true, was, now, false));
	now = was; now.baseTex = &other;
	EXPECT_EQ(kFlatRebuild, ComputeFlatChange(true, was, now, false));
	now = was; now.pb = 1;
	EXPECT_EQ(kFlatRebuild, ComputeFlatChange(true, was, now, false));
	now = was; now.pc = 1;
	EXPECT_EQ(kFlatRebuild, ComputeFlatChange(true, was, now, false));
}

TEST(FlatMesh, AFlatNeverStoredIsBuilt)
{
	const FlatStamp was = PlainFloorStamp();
	EXPECT_EQ(kFlatRebuild, ComputeFlatChange(false, was, was, false));
}

// [rc4l] AOW03's Scroll_Floor sectors rebuilt their floors every frame while only the offset moved.
TEST(FlatMesh, AMovedOffsetScrollsThePieceFromItsBakedVertices)
{
	const FlatStamp baked = PlainFloorStamp();
	FlatStamp now = baked; now.xoffs = 65536;
	EXPECT_EQ(kFlatRescroll, ComputeFlatChange(true, baked, now, false));
	now = baked; now.yoffs = 1; now.lightLevel = 200;
	EXPECT_EQ(kFlatRescroll, ComputeFlatChange(true, baked, now, false));
	now = baked; now.xoffs = 65536; now.angle = 0x40000000u;
	EXPECT_EQ(kFlatRebuild, ComputeFlatChange(true, baked, now, false));
}

TEST(FlatMesh, AScrollThatReturnsToTheBakedOffsetIsUndoneNotLeftBehind)
{
	const FlatStamp baked = PlainFloorStamp();
	EXPECT_EQ(kFlatRescroll, ComputeFlatChange(true, baked, baked, true));
	EXPECT_EQ(kFlatRebuild, ComputeFlatChange(false, baked, baked, true));
}

// [rc4l] The wall bake and the decals each worked out a two-sided middle's blend from the linedef.
TEST(FlatMeshCompute, ATwoSidedMiddleBlendsByItsLinedef)
{
	MidTextureBlend b = ComputeMidTextureBlend(0.5f, false, false);
	EXPECT_FLOAT_EQ(0.5f, b.alpha);
	EXPECT_FALSE(b.additive);
	EXPECT_TRUE(b.translucent);
	EXPECT_EQ(1, b.blendMode);
	b = ComputeMidTextureBlend(0.75f, true, false);
	EXPECT_TRUE(b.additive);
	EXPECT_EQ(2, b.blendMode);
	// [rc4l] No alpha is opaque and never additive, ML_ADDTRANS or not, unless the texture is see-through.
	b = ComputeMidTextureBlend(0.f, true, false);
	EXPECT_FLOAT_EQ(1.f, b.alpha);
	EXPECT_FALSE(b.additive);
	EXPECT_FALSE(b.translucent);
	EXPECT_EQ(0, b.blendMode);
	b = ComputeMidTextureBlend(1.f, false, true);
	EXPECT_TRUE(b.translucent);
	EXPECT_EQ(1, b.blendMode);
}
