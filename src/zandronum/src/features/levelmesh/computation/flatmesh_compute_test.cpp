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

// ---- render-style blend classification -------------------------------------
//
// Shared by sprites and decals. A decal is a small quad glued to a wall and its style decides
// whether a scorch mark burns the wall darker or a blood splat merely covers it.

TEST(FlatMeshCompute, ShadowStyleIsFuzz)
{
	EXPECT_EQ(ComputeStyleBlendMode(true, false, 1.0f), 3);
	// Shadow wins over everything, including an additive flag set at the same time.
	EXPECT_EQ(ComputeStyleBlendMode(true, true, 0.5f), 3);
}

TEST(FlatMeshCompute, AdditiveStyleIsAdditive)
{
	EXPECT_EQ(ComputeStyleBlendMode(false, true, 1.0f), 2);
}

TEST(FlatMeshCompute, AnOpaqueStyleIsOpaque)
{
	EXPECT_EQ(ComputeStyleBlendMode(false, false, 1.0f), 0);
}

TEST(FlatMeshCompute, APartlyFadedDecalIsTranslucent)
{
	// Decals fade out over their lifetime, so this is the common case, not the exception.
	EXPECT_EQ(ComputeStyleBlendMode(false, false, 0.4f), 1);
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

// [rc4l] WDI01: looking up through the town's stacked ceiling, every sky quad came from the stack's walk and went nowhere, so the sky was black.
TEST(FlatMeshCompute, TheWalkThroughAStackedViewFillsThatViewsSkyList)
{
	EXPECT_EQ(kSkyCaptureStack, ComputeSkyCaptureList(true, 2, 0));
	EXPECT_EQ(kSkyCaptureStack + 1, ComputeSkyCaptureList(true, 2, 1));
	EXPECT_EQ(kSkyCaptureNone, ComputeSkyCaptureList(true, 2, kMaxStackViews));  // [rc4l] a portal no slot took
	EXPECT_EQ(kSkyCaptureNone, ComputeSkyCaptureList(true, 3, 0));               // [rc4l] the skybox inside the stack
	EXPECT_EQ(kSkyCaptureNone, ComputeSkyCaptureList(false, 2, 0));              // [rc4l] a camera texture's portal
	EXPECT_EQ(kSkyCaptureMain, ComputeSkyCaptureList(true, 1, 0));
}

TEST(FlatMeshCompute, APassDrawsTheSkyListItsOwnWalkFilled)
{
	EXPECT_EQ(kSkyCaptureMain, ComputeSkyDrawList(false, -1));
	EXPECT_EQ(kSkyCaptureCamera, ComputeSkyDrawList(true, -1));
	EXPECT_EQ(kSkyCaptureCamera, ComputeSkyDrawList(true, 1));
	EXPECT_EQ(kSkyCaptureStack + 1, ComputeSkyDrawList(false, 1));
	EXPECT_EQ(kSkyCaptureMain, ComputeSkyDrawList(false, kMaxStackViews));
	for (int slot = 0; slot < kMaxStackViews; slot++)
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

// [rc4l] WDI01: the stacked view drew rooftops GL's walk through the stack never reached, as roof slabs over the courtyard's walls.
// [rc4l] WDI01's bathroom mirror reflected the room and not the player standing in front of it.
TEST(FlatMeshCompute, OnlyTheViewersSpriteFromTheMirrorTheyFaceIsKept)
{
	EXPECT_TRUE(ComputeTakesMirrorViewer(true, true, 2, true));
	EXPECT_FALSE(ComputeTakesMirrorViewer(true, true, 2, false));   // [rc4l] the room's sprites come from the player's walk
	EXPECT_FALSE(ComputeTakesMirrorViewer(false, true, 2, true));   // [rc4l] a skybox or stacked view is no mirror
	EXPECT_FALSE(ComputeTakesMirrorViewer(true, true, 3, true));    // [rc4l] a mirror seen in a mirror
	EXPECT_FALSE(ComputeTakesMirrorViewer(true, false, 2, true));   // [rc4l] a camera texture's mirror
}

TEST(FlatMeshCompute, AStackedViewMarksEachSubsectorItsWalkReached)
{
	EXPECT_EQ(0, ComputeStackVisWords(0));
	EXPECT_EQ(1, ComputeStackVisWords(16));
	EXPECT_EQ(2, ComputeStackVisWords(17));
	unsigned int bits[2] = { 0u, 0u };
	ComputeMarkStackVisible(bits, 0, 1);
	ComputeMarkStackVisible(bits, 16, 0);
	ComputeMarkStackVisible(bits, 16, 0);
	EXPECT_EQ(2u, bits[0]);     // [rc4l] subsector 0, second view
	EXPECT_EQ(1u, bits[1]);     // [rc4l] subsector 16, first view, set once
	EXPECT_TRUE(ComputeStackVisible(bits, 2, 0, 1));
	EXPECT_FALSE(ComputeStackVisible(bits, 2, 0, 0));
	EXPECT_TRUE(ComputeStackVisible(bits, 2, 16, 0));
	EXPECT_FALSE(ComputeStackVisible(bits, 2, 40, 0));        // [rc4l] past the words
	EXPECT_FALSE(ComputeStackVisible(bits, 2, -1, 0));
	EXPECT_FALSE(ComputeStackVisible(nullptr, 2, 0, 1));
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

// [rc4l] WDI01's rooftops: GL's clipper drops a roof's trim behind the walls in front of it, so a wall is kept or dropped by the seg it lies on.
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
