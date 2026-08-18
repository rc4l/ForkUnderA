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

TEST(DecalProject, FlippingMirrorsThePictureWithoutMovingIt)
{
	// [rc4l] DECALDEF's randomflipx mirrors the graphic so repeated marks do not look stamped. An
	// earlier version flipped by negating the quad's offset from the impact instead, which MOVED the
	// mark: a BFG's scorch and its glow got independent random flips and ended up as two marks side
	// by side. Negating the AXIS cannot move anything -- the box is symmetric about its own centre.
	float right[3] = { 1.f, 0.f, 0.f };
	float up[3] = { 0.f, 0.f, 1.f };

	ApplyDecalFlip(true, false, right, up);

	EXPECT_NEAR(right[0], -1.f, 1e-6f);
	EXPECT_NEAR(up[2], 1.f, 1e-6f) << "flipping across does not touch the up axis";

	ApplyDecalFlip(false, true, right, up);
	EXPECT_NEAR(right[0], -1.f, 1e-6f);
	EXPECT_NEAR(up[2], -1.f, 1e-6f);

	// Flipping twice is the identity, so a mark cannot drift by being flipped repeatedly.
	ApplyDecalFlip(true, true, right, up);
	EXPECT_NEAR(right[0], 1.f, 1e-6f);
	EXPECT_NEAR(up[2], 1.f, 1e-6f);
}

