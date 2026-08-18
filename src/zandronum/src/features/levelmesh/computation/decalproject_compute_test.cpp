// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The cases a projected decal has to survive, stated as arithmetic rather than screenshots.
//
// The system this replaces was judged entirely by looking at it, which is why it took nine rounds:
// every change fixed the picture that was on screen and broke one that was not. These are the
// situations that kept coming back -- a corner, a floor under a wall mark, a pillar's far side, a
// grazing hit, a missile Doom never gave a vertical velocity to.

#include <gtest/gtest.h>

#include "features/levelmesh/computation/decalproject_compute.h"

#include <cmath>

using namespace zx::levelmesh;

namespace {

const float kSquareOnly = 0.f;          // accept anything not edge-on
const float kNoSkewLimit = -1.f;        // cosine floor low enough never to clamp

DecalBox MakeBox(float halfW, float halfH, float near_, float far_)
{
	DecalBox b = {};
	b.origin[0] = 0.f; b.origin[1] = 0.f; b.origin[2] = 0.f;
	b.right[0] = 1.f;  b.right[1] = 0.f; b.right[2] = 0.f;
	b.up[0] = 0.f;     b.up[1] = 0.f;    b.up[2] = 1.f;
	b.axis[0] = 0.f;   b.axis[1] = 1.f;  b.axis[2] = 0.f;
	b.halfW = halfW; b.halfH = halfH; b.near_ = near_; b.far_ = far_;
	return b;
}

float Len3(const float v[3]) { return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); }
float Dot(const float a[3], const float b[3]) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }

} // namespace

// ---------------------------------------------------------------------------------------------
// The basis: which way the picture is projected
// ---------------------------------------------------------------------------------------------

TEST(DecalProject, TheProjectionFollowsTheProjectile)
{
	// A rocket flying north-east into a wall facing south-west leaves an OBLIQUE mark, because that
	// is what an oblique hit does. Projecting along the wall's normal instead is the glued-quad
	// behaviour and is the reason every mark used to look identically head-on.
	const float vel[3] = { 1.f, 1.f, 0.f };
	const float n[3] = { -1.f, 0.f, 0.f };   // wall faces west, rocket comes from the west
	float right[3], up[3], axis[3];

	ASSERT_TRUE(BuildDecalBasis(vel, n, kNoSkewLimit, right, up, axis));

	EXPECT_NEAR(axis[0], std::sqrt(0.5f), 1e-5f);
	EXPECT_NEAR(axis[1], std::sqrt(0.5f), 1e-5f);
	EXPECT_NEAR(axis[2], 0.f, 1e-5f);
}

TEST(DecalProject, AMissileWithNoVelocityFacesTheSurfaceHeadOn)
{
	// [rc4l] Doom leaves velz at zero when autoaim is on, however far down the player is looking, so
	// a missile that lands on a floor can arrive with a velocity that has nothing perpendicular to
	// the surface in it at all. Projecting along it means projecting PARALLEL to the floor: the
	// mark is infinitely long and none of it is on the floor.
	const float vel[3] = { 0.f, 0.f, 0.f };
	const float floorN[3] = { 0.f, 0.f, 1.f };
	float right[3], up[3], axis[3];

	EXPECT_FALSE(BuildDecalBasis(vel, floorN, kNoSkewLimit, right, up, axis))
		<< "the caller has to be able to tell a fallback from a real direction";
	EXPECT_NEAR(axis[2], -1.f, 1e-5f) << "straight down into the floor";
}

TEST(DecalProject, TheAxesAreOrthonormal)
{
	// Everything downstream -- the clip, the texture coordinate, the normal offset -- assumes it.
	const float vel[3] = { 0.3f, -0.9f, -0.4f };
	const float n[3] = { -0.2f, 0.8f, 0.5f };
	float right[3], up[3], axis[3];

	BuildDecalBasis(vel, n, kNoSkewLimit, right, up, axis);

	EXPECT_NEAR(Len3(right), 1.f, 1e-4f);
	EXPECT_NEAR(Len3(up), 1.f, 1e-4f);
	EXPECT_NEAR(Len3(axis), 1.f, 1e-4f);
	EXPECT_NEAR(Dot(right, up), 0.f, 1e-4f);
	EXPECT_NEAR(Dot(right, axis), 0.f, 1e-4f);
	EXPECT_NEAR(Dot(up, axis), 0.f, 1e-4f);
}

TEST(DecalProject, OnAWallThePictureStandsUp)
{
	// Scorch marks are not rolled to match the shot: a decal's up is the world's up.
	const float vel[3] = { 1.f, 0.f, 0.f };
	const float n[3] = { -1.f, 0.f, 0.f };
	float right[3], up[3], axis[3];

	BuildDecalBasis(vel, n, kNoSkewLimit, right, up, axis);

	EXPECT_NEAR(up[2], 1.f, 1e-5f);
}

TEST(DecalProject, OnAFloorThePicturePointsTheWayTheShotWasGoing)
{
	// Straight down at a floor, world up has nothing left in it once the axis is removed, so the
	// only meaningful direction is the travel across the surface. Without this the basis is
	// degenerate and the mark is drawn at whatever angle the arithmetic happened to leave.
	const float vel[3] = { 0.f, 6.f, -8.f };
	const float floorN[3] = { 0.f, 0.f, 1.f };
	float right[3], up[3], axis[3];

	BuildDecalBasis(vel, floorN, kNoSkewLimit, right, up, axis);

	EXPECT_NEAR(Len3(up), 1.f, 1e-4f);
	EXPECT_GT(up[1], 0.5f) << "the picture's up runs north, the way the missile was travelling";
	EXPECT_NEAR(Dot(up, axis), 0.f, 1e-4f);
}

TEST(DecalProject, AVerticalDropWithNoTravelStillGetsAUsableBasis)
{
	// A missile falling straight down: no horizontal travel to borrow a direction from. Any answer
	// is as good as any other, but it must be finite and unit-length rather than a zero vector.
	const float vel[3] = { 0.f, 0.f, -12.f };
	const float floorN[3] = { 0.f, 0.f, 1.f };
	float right[3], up[3], axis[3];

	BuildDecalBasis(vel, floorN, kNoSkewLimit, right, up, axis);

	EXPECT_NEAR(Len3(up), 1.f, 1e-4f);
	EXPECT_NEAR(Len3(right), 1.f, 1e-4f);
	EXPECT_NEAR(Dot(up, axis), 0.f, 1e-4f);
}

TEST(DecalProject, AGrazingHitIsTiltedBackToTheLimitAndKeepsItsDirection)
{
	// A rocket travelling almost along a wall projects a mark stretched by 1/cos, which runs away to
	// infinity as the hit flattens out. Clamping keeps it oblique -- the point of projecting at all
	// -- without letting one shot paint the whole corridor.
	const float n[3] = { -1.f, 0.f, 0.f };
	const float almostParallel[3] = { 0.05f, 1.f, 0.f };
	const float limit = 0.5f;   // 60 degrees off head-on
	float right[3], up[3], axis[3];

	BuildDecalBasis(almostParallel, n, limit, right, up, axis);

	const float back[3] = { -axis[0], -axis[1], -axis[2] };
	EXPECT_NEAR(Dot(back, n), limit, 1e-4f) << "tilted to exactly the limit, not past it";
	EXPECT_GT(axis[1], 0.f) << "and still skewed the way the rocket was actually going";
}

TEST(DecalProject, ASquareOnHitIsLeftAlone)
{
	const float n[3] = { -1.f, 0.f, 0.f };
	const float headOn[3] = { 1.f, 0.f, 0.f };
	float right[3], up[3], axis[3];

	BuildDecalBasis(headOn, n, 0.5f, right, up, axis);

	EXPECT_NEAR(axis[0], 1.f, 1e-5f);
	EXPECT_NEAR(axis[1], 0.f, 1e-5f);
}

// ---------------------------------------------------------------------------------------------
// Which surfaces receive the mark
// ---------------------------------------------------------------------------------------------

TEST(DecalProject, TheFarSideOfAPillarGetsNothing)
{
	// [rc4l] The failure this prevents is specific and was reported as "this entire quadrant is
	// missing": a box round a pillar's edge contains BOTH of its faces, and without a facing test
	// the face nobody shot at prints a full mirrored copy of the mark.
	const float axis[3] = { 1.f, 0.f, 0.f };      // travelling east
	const float facingMe[3] = { -1.f, 0.f, 0.f }; // the west face, which was hit
	const float facingAway[3] = { 1.f, 0.f, 0.f };// the east face, round the back

	EXPECT_TRUE(AcceptSurfaceForDecal(facingMe, axis, kSquareOnly));
	EXPECT_FALSE(AcceptSurfaceForDecal(facingAway, axis, kSquareOnly));
}

TEST(DecalProject, TheFloorUnderAWallMarkDoesReceiveIt)
{
	// The case the old system could never give enough of the picture to. A rocket angled down into
	// the base of a wall is travelling into the floor as well, so the floor is a legitimate
	// receiver and takes however much of the box covers it -- no budget, no leftover.
	const float axis[3] = { 0.f, 0.707f, -0.707f };
	const float floorN[3] = { 0.f, 0.f, 1.f };
	const float wallN[3] = { 0.f, -1.f, 0.f };

	EXPECT_TRUE(AcceptSurfaceForDecal(floorN, axis, 0.05f));
	EXPECT_TRUE(AcceptSurfaceForDecal(wallN, axis, 0.05f));
}

TEST(DecalProject, AWallExactlyEdgeOnToTheProjectionIsDropped)
{
	// Its footprint is a zero-area sliver of infinitely stretched texture: a bright line down a
	// wall nobody shot.
	const float axis[3] = { 0.f, 1.f, 0.f };
	const float edgeOn[3] = { 1.f, 0.f, 0.f };

	EXPECT_FALSE(AcceptSurfaceForDecal(edgeOn, axis, 0.05f));
}

// ---------------------------------------------------------------------------------------------
// Where the box sits
// ---------------------------------------------------------------------------------------------

TEST(DecalProject, TheBoxIsAdvancedOntoTheGeometryByTheProjectilesRadius)
{
	// [rc4l] Doom explodes a missile when its BOUNDING BOX touches a line, so the missile's centre
	// is a radius short of the surface. Centring the box there puts half of it in the air in front
	// of the wall, and on a corner it can miss the face that was actually struck.
	const float pos[3] = { 100.f, 200.f, 40.f };
	const float axis[3] = { 1.f, 0.f, 0.f };
	float origin[3];

	DecalOriginFromImpact(pos, axis, 11.f, origin);   // MT_ROCKET has radius 11

	EXPECT_NEAR(origin[0], 111.f, 1e-4f);
	EXPECT_NEAR(origin[1], 200.f, 1e-4f);
	EXPECT_NEAR(origin[2], 40.f, 1e-4f);
}

// ---------------------------------------------------------------------------------------------
// The clip
// ---------------------------------------------------------------------------------------------

TEST(DecalProject, AWallAcrossTheBoxIsCutToTheBoxsWidthAndHeight)
{
	// The box is 16 wide and 16 tall, looking north; the wall is a big quad in the x-z plane at
	// y = 0. What comes back is the 32x32 window, not the wall.
	const DecalBox box = MakeBox(16.f, 16.f, 16.f, 16.f);
	const float wall[12] = {
		-100.f, 0.f, -100.f,
		 100.f, 0.f, -100.f,
		 100.f, 0.f,  100.f,
		-100.f, 0.f,  100.f,
	};
	float out[64 * 3];

	const int n = ClipPolygonToDecalBox(wall, 4, box, out, 64);

	ASSERT_EQ(n, 4);
	for (int i = 0; i < n; i++)
	{
		EXPECT_LE(std::fabs(out[i*3 + 0]), 16.f + 1e-4f);
		EXPECT_LE(std::fabs(out[i*3 + 1]), 16.f + 1e-4f);
		EXPECT_NEAR(out[i*3 + 2], 0.f, 1e-4f) << "the wall is at the box's own depth";
	}
}

TEST(DecalProject, AWallBehindTheBoxIsRejectedEntirely)
{
	// The far plane is what stops a mark printing through a wall onto whatever is in the next room.
	// The old system had no such plane and a mark on a thin wall appeared on both sides of it.
	const DecalBox box = MakeBox(16.f, 16.f, 8.f, 8.f);
	const float farWall[12] = {
		-100.f, 40.f, -100.f,
		 100.f, 40.f, -100.f,
		 100.f, 40.f,  100.f,
		-100.f, 40.f,  100.f,
	};
	float out[64 * 3];

	EXPECT_EQ(ClipPolygonToDecalBox(farWall, 4, box, out, 64), 0);
}

TEST(DecalProject, ASurfaceInsideTheBoxSurvivesWhole)
{
	// A small floor patch entirely within the box must come back unchanged, or every decal loses its
	// edges to a clip that should not have touched them.
	DecalBox box = MakeBox(64.f, 64.f, 64.f, 64.f);
	const float patch[12] = {
		-8.f,  -8.f, 0.f,
		 8.f,  -8.f, 0.f,
		 8.f,   8.f, 0.f,
		-8.f,   8.f, 0.f,
	};
	float out[64 * 3];

	const int n = ClipPolygonToDecalBox(patch, 4, box, out, 64);

	EXPECT_EQ(n, 4);
}

TEST(DecalProject, APolygonThatCrossesACornerOfTheBoxGainsVertices)
{
	// Sutherland-Hodgman turning a triangle into a quad -- the ordinary case at a corner, and the
	// reason the output buffer must be bigger than the input.
	const DecalBox box = MakeBox(10.f, 10.f, 10.f, 10.f);
	const float tri[9] = {
		-100.f, 0.f, 0.f,
		 100.f, 0.f, 0.f,
		   0.f, 0.f, 100.f,
	};
	float out[64 * 3];

	const int n = ClipPolygonToDecalBox(tri, 3, box, out, 64);

	EXPECT_GE(n, 4);
	for (int i = 0; i < n; i++)
	{
		EXPECT_LE(std::fabs(out[i*3 + 0]), 10.f + 1e-4f);
		EXPECT_LE(std::fabs(out[i*3 + 1]), 10.f + 1e-4f);
	}
}

TEST(DecalProject, TheCornerCase)
{
	// [rc4l] The one that broke the old system: a mark at the meeting of a wall and the floor.
	//
	// Both surfaces are inside one box, so both are clipped from ONE projection. Their pieces meet
	// exactly at the corner line with no seam to get wrong, because the seam is not a decision --
	// it is where the two clipped polygons happen to end, and both end at the same place.
	DecalBox box = MakeBox(24.f, 24.f, 24.f, 24.f);
	box.origin[2] = 0.f;

	const float wall[12] = {          // y = 12, standing up
		-100.f, 12.f, -50.f,  100.f, 12.f, -50.f,  100.f, 12.f, 50.f,  -100.f, 12.f, 50.f,
	};
	const float floor[12] = {         // z = -12, lying flat
		-100.f, -100.f, -12.f,  100.f, -100.f, -12.f,  100.f, 100.f, -12.f,  -100.f, 100.f, -12.f,
	};
	float wallOut[64 * 3], floorOut[64 * 3];

	const int nw = ClipPolygonToDecalBox(wall, 4, box, wallOut, 64);
	const int nf = ClipPolygonToDecalBox(floor, 4, box, floorOut, 64);

	ASSERT_GE(nw, 3);
	ASSERT_GE(nf, 3);

	// The wall's piece reaches down to the box's bottom edge and the floor's piece reaches out to
	// the box's far edge: between them they cover the whole picture, which is what "seamless" means
	// when it is arithmetic instead of an opinion.
	float wallLowest = 1e9f, floorFurthest = -1e9f;
	for (int i = 0; i < nw; i++) wallLowest = std::fmin(wallLowest, wallOut[i*3 + 1]);
	for (int i = 0; i < nf; i++) floorFurthest = std::fmax(floorFurthest, floorOut[i*3 + 2]);

	EXPECT_NEAR(wallLowest, -24.f, 1e-3f) << "the wall's share runs to the bottom of the picture";
	EXPECT_NEAR(floorFurthest, 24.f, 1e-3f) << "the floor's share runs to the far edge of the box";
}

// ---------------------------------------------------------------------------------------------
// How deep the box has to be
// ---------------------------------------------------------------------------------------------

TEST(DecalProject, ASquareOnHitNeedsNoDepthBeyondTheSpread)
{
	// Nothing is slanted, so the only depth is the reach that lets the mark carry onto the floor in
	// front of the wall. Ask for none and there is none.
	float near_ = 0.f, far_ = 0.f;

	ComputeDecalBoxDepth(20.f, 1.f, 0.6f, near_, far_);

	EXPECT_NEAR(near_, 12.f, 1e-4f);
	EXPECT_NEAR(far_, 4.f, 1e-4f) << "forward only far enough not to print through a thin wall";
}

TEST(DecalProject, ATiltedHitGetsEXACTLYTheDepthItsOwnSlantNeeds)
{
	// [rc4l] The bug this pins down: a 45-degree hit lays its picture across a band of depth as deep
	// as the picture is big, and a shallower box cuts a straight edge through the middle of the mark.
	// It came out as a hard-edged wedge of scorch beside a corner, which reads as a rendering fault
	// rather than as a clip, and cost a round of guessing before the numbers were printed.
	const float size = 20.f, cos45 = 0.70710678f;
	float near_ = 0.f, far_ = 0.f;

	ComputeDecalBoxDepth(size, cos45, 0.6f, near_, far_);

	EXPECT_NEAR(near_, size, 1e-3f) << "tan(45) is 1, so the slant is the picture's own size";
	EXPECT_GE(far_, size) << "and it slants BOTH ways from the contact point";
}

TEST(DecalProject, TheSlantWinsWhenItIsDeeperThanTheSpread)
{
	float shallowNear = 0.f, shallowFar = 0.f, steepNear = 0.f, steepFar = 0.f;

	ComputeDecalBoxDepth(20.f, 0.95f, 0.6f, shallowNear, shallowFar);   // barely tilted
	ComputeDecalBoxDepth(20.f, 0.40f, 0.6f, steepNear, steepFar);       // strongly tilted

	EXPECT_GT(steepNear, shallowNear);
	EXPECT_GT(steepFar, shallowFar);
	EXPECT_NEAR(shallowNear, 12.f, 1e-4f) << "a barely tilted hit still gets the spread";
}

TEST(DecalProject, AProjectionAlongTheSurfaceIsFlooredRatherThanInfinite)
{
	// tan goes to infinity as the hit flattens out. The skew clamp keeps real hits away from this,
	// but a guard here is what stops a bad caller asking for a box the size of the map.
	float near_ = 0.f, far_ = 0.f;

	ComputeDecalBoxDepth(20.f, 0.f, 0.6f, near_, far_);

	EXPECT_LT(near_, 200.f);
	EXPECT_GT(near_, 0.f);
	EXPECT_TRUE(near_ == near_) << "and never a NaN";
}

// ---------------------------------------------------------------------------------------------
// Running out instead of stopping dead
// ---------------------------------------------------------------------------------------------

TEST(DecalProject, ASliceKeepsOnlyItsOwnDepth)
{
	// A floor stretching away from the wall spans the whole depth of the box. Sliced, each piece can
	// be drawn at its own strength -- which is the difference between a mark that runs out and a mark
	// that ends in a straight line across open floor.
	const float floorStrip[12] = {
		-10.f, 0.f, -30.f,
		 10.f, 0.f, -30.f,
		 10.f, 0.f,   0.f,
		-10.f, 0.f,   0.f,
	};
	float out[72 * 3];

	const int n = ClipLocalPolygonToDepthBand(floorStrip, 4, -20.f, -10.f, out, 72);

	ASSERT_GE(n, 3);
	for (int i = 0; i < n; i++)
	{
		EXPECT_GE(out[i*3 + 2], -20.f - 1e-4f);
		EXPECT_LE(out[i*3 + 2], -10.f + 1e-4f);
	}
}

TEST(DecalProject, SlicesCoverTheWholePolygonBetweenThem)
{
	// No gaps and no overlaps: a strip cut into pieces has to add back up, or the ramp has holes in
	// it and the holes are visible as bands of bare wall.
	const float strip[12] = {
		-10.f, 0.f, -40.f,  10.f, 0.f, -40.f,  10.f, 0.f, 0.f,  -10.f, 0.f, 0.f,
	};
	float out[72 * 3];
	float lowest = 1e9f, highest = -1e9f;
	int slicesWithArea = 0;

	for (int k = 0; k < 4; k++)
	{
		const float lo = -40.f + 10.f * k, hi = lo + 10.f;
		const int n = ClipLocalPolygonToDepthBand(strip, 4, lo, hi, out, 72);
		if (n < 3) continue;
		slicesWithArea++;
		for (int i = 0; i < n; i++)
		{
			lowest = std::fmin(lowest, out[i*3 + 2]);
			highest = std::fmax(highest, out[i*3 + 2]);
		}
	}

	EXPECT_EQ(slicesWithArea, 4);
	EXPECT_NEAR(lowest, -40.f, 1e-3f);
	EXPECT_NEAR(highest, 0.f, 1e-3f);
}

TEST(DecalProject, ASliceOutsideThePolygonIsEmpty)
{
	const float strip[12] = {
		-10.f, 0.f, -5.f,  10.f, 0.f, -5.f,  10.f, 0.f, 0.f,  -10.f, 0.f, 0.f,
	};
	float out[72 * 3];

	EXPECT_EQ(ClipLocalPolygonToDepthBand(strip, 4, -40.f, -20.f, out, 72), 0);
}

TEST(DecalProject, TheSurfaceThatWasHitIsNeverFaded)
{
	// [rc4l] The mark on the wall you shot is the picture, at the size the decal asked for. Fading it
	// would be second-guessing the artist, and it is also the case that must not change when the
	// run-out is added, or every mark in the game gets quietly weaker.
	const float centre[3] = { 0.f, 0.f, 0.f };
	const float edgeOfPicture[3] = { 12.f, 16.f, 0.f };   // radius 20, exactly the picture's own

	EXPECT_NEAR(DecalRadialFade(centre, 20.f, 60.f), 1.f, 1e-6f);
	EXPECT_NEAR(DecalRadialFade(edgeOfPicture, 20.f, 60.f), 1.f, 1e-6f);
}

TEST(DecalProject, ReachingBeyondThePictureRunsOutSmoothly)
{
	const float justOutside[3] = { 0.f, 0.f, -21.f };
	const float halfway[3] = { 0.f, 0.f, -40.f };
	const float atTheLimit[3] = { 0.f, 0.f, -60.f };
	const float beyond[3] = { 0.f, 0.f, -90.f };

	const float a = DecalRadialFade(justOutside, 20.f, 60.f);
	const float b = DecalRadialFade(halfway, 20.f, 60.f);

	EXPECT_LT(a, 1.f);
	EXPECT_GT(a, 0.9f) << "and it leaves the picture's edge gently, not with a crease";
	EXPECT_LT(b, a);
	EXPECT_NEAR(DecalRadialFade(atTheLimit, 20.f, 60.f), 0.f, 1e-6f);
	EXPECT_NEAR(DecalRadialFade(beyond, 20.f, 60.f), 0.f, 1e-6f);
}

TEST(DecalProject, TheRunOutIsContinuousAROUNDACorner)
{
	// [rc4l] The seam this replaces: fading per SURFACE gives each face of a corner its own constant,
	// so a step appears exactly down the join. Distance from the impact does not know a corner is
	// there, so two points either side of one get near-identical strength.
	const float onTheWall[3] = { 0.f, -24.9f, -0.5f };
	const float onTheFloor[3] = { 0.f, -25.0f, -0.6f };

	const float wall = DecalRadialFade(onTheWall, 20.f, 60.f);
	const float floor = DecalRadialFade(onTheFloor, 20.f, 60.f);

	EXPECT_NEAR(wall, floor, 0.02f) << "no step where the two surfaces meet";
}

// ---------------------------------------------------------------------------------------------
// The texture coordinate
// ---------------------------------------------------------------------------------------------

TEST(DecalProject, TheCentreOfTheBoxIsTheCentreOfThePicture)
{
	const DecalBox box = MakeBox(16.f, 16.f, 16.f, 16.f);
	const float centre[3] = { 0.f, 0.f, 0.f };
	float u, v;

	DecalUV(centre, box, u, v);

	EXPECT_NEAR(u, 0.5f, 1e-5f);
	EXPECT_NEAR(v, 0.5f, 1e-5f);
}

TEST(DecalProject, ThePictureIsNotUpsideDown)
{
	// v runs down, because a texture's first row is its top. Getting this wrong flips every decal
	// vertically, which on a symmetric scorch mark is invisible and on a lettered one is not.
	const DecalBox box = MakeBox(16.f, 16.f, 16.f, 16.f);
	const float highUp[3] = { 0.f, 16.f, 0.f };
	float u, v;

	DecalUV(highUp, box, u, v);

	EXPECT_NEAR(v, 0.f, 1e-5f) << "the top of the box is the top row of the texture";
}

TEST(DecalProject, TheEdgesOfTheBoxAreTheEdgesOfThePicture)
{
	const DecalBox box = MakeBox(16.f, 8.f, 4.f, 4.f);
	float u, v;

	const float rightEdge[3] = { 16.f, 0.f, 0.f };
	DecalUV(rightEdge, box, u, v);
	EXPECT_NEAR(u, 1.f, 1e-5f);

	const float leftEdge[3] = { -16.f, 0.f, 0.f };
	DecalUV(leftEdge, box, u, v);
	EXPECT_NEAR(u, 0.f, 1e-5f);

	const float bottom[3] = { 0.f, -8.f, 0.f };
	DecalUV(bottom, box, u, v);
	EXPECT_NEAR(v, 1.f, 1e-5f);
}

TEST(DecalProject, FlippingMirrorsThePictureWithoutMovingIt)
{
	// [rc4l] DECALDEF's randomflipx mirrors the graphic. An earlier version flipped by negating the
	// quad's offset from the impact instead, which MOVED the mark: a BFG's scorch and its glow got
	// independent random flips and ended up as two marks side by side. In a texture coordinate that
	// cannot happen -- the geometry is the box either way.
	float u = 0.25f, v = 0.75f;

	DecalFlipUV(true, false, u, v);
	EXPECT_NEAR(u, 0.75f, 1e-6f);
	EXPECT_NEAR(v, 0.75f, 1e-6f);

	DecalFlipUV(false, true, u, v);
	EXPECT_NEAR(u, 0.75f, 1e-6f);
	EXPECT_NEAR(v, 0.25f, 1e-6f);

	// The centre is the fixed point of both flips, so a centred mark never moves.
	float cu = 0.5f, cv = 0.5f;
	DecalFlipUV(true, true, cu, cv);
	EXPECT_NEAR(cu, 0.5f, 1e-6f);
	EXPECT_NEAR(cv, 0.5f, 1e-6f);
}
