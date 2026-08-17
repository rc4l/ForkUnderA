// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The projected decal's geometry, pinned.
//
// Every one of these covers something that has actually been wrong, most of it more than once, and
// none of it announced itself: a decal on the wrong plane, an offset graphic landing half a graphic
// away, a box pointing into the wall instead of out of it, a mark dragged into a streak by a corner.
// A screenshot settles those slowly and only for the corner you happened to be standing in.
//
// The unwrap tests are the load-bearing ones. They are written as PROPERTIES -- continuity across a
// join, identity on the original surface, direction of travel -- rather than as expected numbers,
// because the property is what the feature is and a number is only one sample of it.

#include "features/levelmesh/computation/decalvolume_compute.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace zx::levelmesh;

namespace {

// A wall decal's frame: U along the wall (east), V straight up, N out of the wall (north).
DecalFrame WallFrame(float halfW, float halfH, float halfD)
{
	const float u[3] = { 1.f, 0.f, 0.f };
	const float v[3] = { 0.f, 0.f, 1.f };
	const float n[3] = { 0.f, 1.f, 0.f };
	DecalFrame f{};
	EXPECT_TRUE(ComputeDecalBasis(u, v, n, halfW, halfH, halfD, f));
	return f;
}

const float kUp[3]      = { 0.f, 0.f,  1.f };
const float kOutOfWall[3] = { 0.f, 1.f, 0.f };

} // namespace

// ---------------------------------------------------------------------------------------------
// The box

TEST(DecalBoxDepth, NeverShallowerThanAStep)
{
	// A bullet hole is a few units across, and its box still has to survive a floor that steps or
	// slopes under it. At eight, marks on anything but dead-flat ground came out clipped.
	EXPECT_FLOAT_EQ(24.f, ComputeDecalBoxDepth(2.f, 3.f));
	EXPECT_FLOAT_EQ(24.f, ComputeDecalBoxDepth(0.f, 0.f));
}

TEST(DecalBoxDepth, GrowsWithTheMarkSoItCanCarryRoundACorner)
{
	// Past a join a decal continues for at most its own remaining width or height, so the box is
	// sized from the larger of the two. Smaller would cut the wrap short.
	EXPECT_FLOAT_EQ(64.f, ComputeDecalBoxDepth(64.f, 10.f));
	EXPECT_FLOAT_EQ(64.f, ComputeDecalBoxDepth(10.f, 64.f));
	EXPECT_FLOAT_EQ(80.f, ComputeDecalBoxDepth(80.f, 80.f));
}

TEST(DecalBasis, RejectsADegenerateBox)
{
	const float u[3] = { 1.f, 0.f, 0.f }, v[3] = { 0.f, 0.f, 1.f }, n[3] = { 0.f, 1.f, 0.f };
	DecalFrame f{};
	EXPECT_FALSE(ComputeDecalBasis(u, v, n, 0.f, 8.f, 24.f, f));
	EXPECT_FALSE(ComputeDecalBasis(u, v, n, 8.f, -1.f, 24.f, f));
	EXPECT_FALSE(ComputeDecalBasis(u, v, n, 8.f, 8.f, 0.f, f));
}

TEST(DecalBasis, PutsTheBoxEdgeAtExactlyOne)
{
	// The whole point of pre-dividing: the inside test is one dot product per axis with nothing left
	// to divide, so a point on the face of the box must land on 1.0 and not 0.999.
	const DecalFrame f = WallFrame(16.f, 8.f, 24.f);
	const float atEdge[3] = { 16.f, 24.f, 8.f };   // +U*halfW, +N*halfD, +V*halfH
	float local[3];
	ComputeDecalLocal(f, atEdge, local);
	EXPECT_FLOAT_EQ(1.f, local[0]);
	EXPECT_FLOAT_EQ(1.f, local[1]);
	EXPECT_FLOAT_EQ(1.f, local[2]);
}

// ---------------------------------------------------------------------------------------------
// The wall's axes

TEST(WallDecalAxes, NormalPointsOutOfTheFaceTheDecalStuckTo)
{
	// A linedef's front side faces to the RIGHT of v1->v2. For a line running east, that is south.
	// Backwards here points the box INTO the wall, and the decal lands on whatever is behind it.
	float along[2], normal[2];
	ASSERT_TRUE(ComputeWallDecalAxes(64.f, 0.f, /*backSide=*/false, along, normal));
	EXPECT_FLOAT_EQ(1.f, along[0]);
	EXPECT_FLOAT_EQ(0.f, along[1]);
	EXPECT_FLOAT_EQ(0.f, normal[0]);
	EXPECT_FLOAT_EQ(-1.f, normal[1]);

	ASSERT_TRUE(ComputeWallDecalAxes(64.f, 0.f, /*backSide=*/true, along, normal));
	EXPECT_FLOAT_EQ(0.f, normal[0]);
	EXPECT_FLOAT_EQ(1.f, normal[1]);
}

TEST(WallDecalAxes, AreUnitLengthAndPerpendicular)
{
	float along[2], normal[2];
	ASSERT_TRUE(ComputeWallDecalAxes(30.f, -40.f, false, along, normal));
	EXPECT_NEAR(1.f, std::sqrt(along[0] * along[0] + along[1] * along[1]), 1e-6f);
	EXPECT_NEAR(1.f, std::sqrt(normal[0] * normal[0] + normal[1] * normal[1]), 1e-6f);
	EXPECT_NEAR(0.f, along[0] * normal[0] + along[1] * normal[1], 1e-6f);
}

TEST(WallDecalAxes, RefusesAZeroLengthLinedef)
{
	// A malformed map can contain one, and dividing by its length paints the whole screen.
	float along[2], normal[2];
	EXPECT_FALSE(ComputeWallDecalAxes(0.f, 0.f, false, along, normal));
}

// ---------------------------------------------------------------------------------------------
// Anchor to centre

TEST(DecalAnchorOffset, IsNothingForAGraphicOffsetAtItsMiddle)
{
	EXPECT_FLOAT_EQ(0.f, ComputeDecalAlongOffset(16.f, 16.f, false));
	EXPECT_FLOAT_EQ(0.f, ComputeDecalUpOffset(8.f, 8.f, false));
}

TEST(DecalAnchorOffset, MovesTheCentreForAnOffsetGraphic)
{
	// leftOffset 0 means the graphic hangs from its left edge, so its middle is a half-width to the
	// right of the anchor. Ignoring this put offset decals half a graphic from where GL draws them.
	EXPECT_FLOAT_EQ(16.f, ComputeDecalAlongOffset(16.f, 0.f, false));
	// topOffset 0 means it hangs from its top edge, so its middle is a half-height BELOW the anchor.
	EXPECT_FLOAT_EQ(-8.f, ComputeDecalUpOffset(8.f, 0.f, false));
}

TEST(DecalAnchorOffset, MirrorsWhenTheGraphicIsFlipped)
{
	// A flipped graphic is drawn mirrored, so its offset is measured from the other edge. The two
	// offsets must come out symmetric about the anchor.
	const float plain   = ComputeDecalAlongOffset(16.f, 4.f, false);
	const float flipped = ComputeDecalAlongOffset(16.f, 4.f, true);
	EXPECT_FLOAT_EQ(12.f, plain);
	EXPECT_FLOAT_EQ(-12.f, flipped);

	EXPECT_FLOAT_EQ(-ComputeDecalUpOffset(8.f, 2.f, false), ComputeDecalUpOffset(8.f, 2.f, true));
}

// ---------------------------------------------------------------------------------------------
// The unwrap
//
// A wall decal 32 wide and 16 tall, centred 16 above the floor, on a wall running east with its face
// pointing north. So the floor is exactly one half-height below the decal's centre: the mark reaches
// the join and has half its height left to spend on the floor.

TEST(DecalUnwrap, ChangesNothingOnTheSurfaceItWasShotAt)
{
	// local.z is zero there, so the carry is zero. If this ever stops holding, every decal in the
	// game shifts, which is worth one assertion.
	const DecalFrame f = WallFrame(16.f, 8.f, 24.f);
	const float rel[3] = { 8.f, 0.f, -4.f };
	float local[3];
	ComputeDecalLocal(f, rel, local);
	float u = 0.f, v = 0.f;
	ASSERT_TRUE(ComputeDecalUnwrapUV(f, local, kOutOfWall, u, v));
	EXPECT_FLOAT_EQ(local[0] * 0.5f + 0.5f, u);
	EXPECT_FLOAT_EQ(local[1] * 0.5f + 0.5f, v);
}

TEST(DecalUnwrap, IsContinuousAcrossTheJoin)
{
	// The point AT the corner, reached two ways: down the wall, and along the floor from zero
	// distance out. Both must give the same coordinate, or the mark shows a seam at every corner.
	const DecalFrame f = WallFrame(16.f, 8.f, 24.f);

	const float onWallAtFloor[3] = { 0.f, 0.f, -8.f };      // 8 down = the join
	float lw[3]; ComputeDecalLocal(f, onWallAtFloor, lw);
	float uw = 0.f, vw = 0.f;
	ASSERT_TRUE(ComputeDecalUnwrapUV(f, lw, kOutOfWall, uw, vw));

	const float onFloorAtWall[3] = { 0.f, 0.f, -8.f };      // same point, now seen as floor
	float lf[3]; ComputeDecalLocal(f, onFloorAtWall, lf);
	float uf = 0.f, vf = 0.f;
	ASSERT_TRUE(ComputeDecalUnwrapUV(f, lf, kUp, uf, vf));

	EXPECT_NEAR(uw, uf, 1e-5f);
	EXPECT_NEAR(vw, vf, 1e-5f);
}

TEST(DecalUnwrap, CarriesOntoTheFloorByTheDistanceTravelled)
{
	// Four units out along the floor is four units further into the picture -- the same scale as the
	// wall, which is what "unstretched" means. A plain projection would leave v unchanged here, and
	// that unchanging v IS the dragged column.
	const DecalFrame f = WallFrame(16.f, 8.f, 24.f);

	const float atJoin[3] = { 0.f, 0.f, -8.f };
	const float outOnFloor[3] = { 0.f, 4.f, -8.f };
	float lj[3], lo[3];
	ComputeDecalLocal(f, atJoin, lj);
	ComputeDecalLocal(f, outOnFloor, lo);

	float uj, vj, uo, vo;
	ASSERT_TRUE(ComputeDecalUnwrapUV(f, lj, kUp, uj, vj));
	ASSERT_FALSE(ComputeDecalUnwrapUV(f, lo, kUp, uo, vo));   // 12 of 8 used: past the end

	// v runs downward past the join at the wall's own scale: 4 world units of an 8-unit half-height.
	EXPECT_NEAR(vj - 4.f / 8.f * 0.5f, vo, 1e-5f);
	EXPECT_FLOAT_EQ(uj, uo);   // across the wall is unaffected by walking away from it
}

TEST(DecalUnwrap, CarriesTheOtherWayForACeiling)
{
	// Above the mark, the continuation is upward. Sharing one sign rule with the floor case is what
	// keeps a decal shot at the top of a wall from folding back on itself.
	// A ceiling's normal points down, but only which AXIS it lies on matters -- the carry uses its
	// magnitude -- so the mark reads the same whether the surface faces up or down.
	const DecalFrame f = WallFrame(16.f, 8.f, 24.f);
	const float underCeiling[3] = { 0.f, 2.f, 4.f };   // above centre, 2 out from the wall
	float local[3];
	ComputeDecalLocal(f, underCeiling, local);
	float u, v;
	ASSERT_TRUE(ComputeDecalUnwrapUV(f, local, kUp, u, v));
	EXPECT_GT(v, local[1] * 0.5f + 0.5f);
}

TEST(DecalUnwrap, TurnsAboutTheOtherAxisForAVerticalCorner)
{
	// A wall met round a vertical corner has a normal along U, so the carry belongs on the ACROSS
	// axis and the height must be left alone. Putting it on the wrong axis is the BFG smear.
	const DecalFrame f = WallFrame(16.f, 8.f, 24.f);
	const float sideWall[3] = { 10.f, 5.f, 2.f };
	const float alongU[3] = { 1.f, 0.f, 0.f };
	float local[3];
	ComputeDecalLocal(f, sideWall, local);
	float u, v;
	ASSERT_TRUE(ComputeDecalUnwrapUV(f, local, alongU, u, v));
	EXPECT_FLOAT_EQ(local[1] * 0.5f + 0.5f, v);            // height untouched
	EXPECT_GT(u, local[0] * 0.5f + 0.5f);                  // carried further across
}

TEST(DecalUnwrap, LeavesTheMidlineAlone)
{
	// A point exactly on the decal's midline has no direction to continue in. Picking one anyway --
	// std::copysign returns +1 at zero -- makes the coordinate jump as the last bit changes, which
	// is a shimmering line down the middle of every wrapped decal.
	const DecalFrame f = WallFrame(16.f, 8.f, 24.f);
	const float onMidline[3] = { 0.f, 6.f, 0.f };
	float local[3];
	ComputeDecalLocal(f, onMidline, local);
	float u, v;
	ASSERT_TRUE(ComputeDecalUnwrapUV(f, local, kUp, u, v));
	EXPECT_FLOAT_EQ(0.5f, v);
}

TEST(DecalUnwrap, ReportsPastTheEndRatherThanClamping)
{
	// The caller must discard. Clamping repeats the edge texel for ever, which is the dragged column
	// again by another route -- the artifact this whole scheme exists to remove.
	const DecalFrame f = WallFrame(16.f, 8.f, 24.f);
	const float farOut[3] = { 40.f, 0.f, 0.f };
	float local[3];
	ComputeDecalLocal(f, farOut, local);
	float u, v;
	EXPECT_FALSE(ComputeDecalUnwrapUV(f, local, kOutOfWall, u, v));
	EXPECT_GT(u, 1.f);
}

TEST(DecalUnwrap, BlendsAtAnAmbiguousCornerRatherThanSnapping)
{
	// The normal comes from depth derivatives, which are noisy at a grazing angle. When it lands
	// midway between the two axes the carry has to SPLIT, not pick a side: picking made the choice
	// flip pixel to pixel and frame to frame, and a BFG mark on a column corner reshaped itself as
	// the camera moved. Half the carry each is the answer that does not jump.
	const DecalFrame f = WallFrame(16.f, 8.f, 24.f);
	const float rel[3] = { 4.f, 6.f, 4.f };
	const float diagonal[3] = { 0.70710678f, 0.f, 0.70710678f };   // 45 degrees between U and V
	float local[3];
	ComputeDecalLocal(f, rel, local);
	float u, v;
	ASSERT_TRUE(ComputeDecalUnwrapUV(f, local, diagonal, u, v));

	const float carry = 6.f;   // world units through the wall
	EXPECT_NEAR(local[0] * 0.5f + 0.5f + 0.5f * carry / 16.f * 0.5f, u, 1e-5f);
	EXPECT_NEAR(local[1] * 0.5f + 0.5f + 0.5f * carry / 8.f * 0.5f, v, 1e-5f);
}

TEST(DecalUnwrap, IsStableUnderASmallWobbleInTheNormal)
{
	// The real failure was not a wrong answer, it was a jumpy one. A normal that moves by a degree
	// must move the coordinate by about a degree's worth -- not across a branch to a different axis.
	const DecalFrame f = WallFrame(16.f, 8.f, 24.f);
	const float rel[3] = { 4.f, 5.f, 3.f };
	float local[3];
	ComputeDecalLocal(f, rel, local);

	const float justUnder[3] = { 0.7132f, 0.f, 0.7009f };
	const float justOver[3]  = { 0.7009f, 0.f, 0.7132f };
	float u1, v1, u2, v2;
	ASSERT_TRUE(ComputeDecalUnwrapUV(f, local, justUnder, u1, v1));
	ASSERT_TRUE(ComputeDecalUnwrapUV(f, local, justOver, u2, v2));
	EXPECT_NEAR(u1, u2, 0.01f);
	EXPECT_NEAR(v1, v2, 0.01f);
}

TEST(DecalUnwrap, RefusesADegenerateFrame)
{
	DecalFrame f{};   // all axes zero
	const float local[3] = { 0.f, 0.f, 0.f };
	float u, v;
	EXPECT_FALSE(ComputeDecalUnwrapUV(f, local, kUp, u, v));
}

TEST(DecalUnwrap, DeeperBoxDoesNotChangeTheScaleOfTheCarry)
{
	// The box's depth decides how FAR the wrap reaches, never how fast the picture moves. Mixing the
	// two up made the same corner look different for a big decal and a small one.
	const float rel[3] = { 0.f, 4.f, -8.f };
	float u1, v1, u2, v2, l1[3], l2[3];

	const DecalFrame shallow = WallFrame(16.f, 8.f, 24.f);
	const DecalFrame deep    = WallFrame(16.f, 8.f, 96.f);
	ComputeDecalLocal(shallow, rel, l1);
	ComputeDecalLocal(deep, rel, l2);
	ComputeDecalUnwrapUV(shallow, l1, kUp, u1, v1);
	ComputeDecalUnwrapUV(deep, l2, kUp, u2, v2);

	EXPECT_NEAR(v1, v2, 1e-5f);
	EXPECT_NEAR(u1, u2, 1e-5f);
}
