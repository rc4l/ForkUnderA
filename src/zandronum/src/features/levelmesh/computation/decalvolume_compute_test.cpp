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
// A wall decal 32 wide and 16 tall on a wall running east, its face pointing north. `rel` is measured
// from the mark's centre: x along the wall, y out through it, z up.
//
// Note there is no surface normal here any more. Two earlier versions needed one, to decide which
// axis a surface had turned about, and it had to be recovered from depth derivatives -- noisy at
// grazing angles, so the answer flickered and the mark reshaped as the camera moved. Carrying
// outward from the centre instead needs only where the fragment already is.

TEST(DecalUnwrap, ChangesNothingOnTheSurfaceItWasShotAt)
{
	// local.z is zero there, so there is nothing to carry. If this ever stops holding, every decal in
	// the game shifts, which is worth one assertion.
	const DecalFrame f = WallFrame(16.f, 8.f, 24.f);
	const float rel[3] = { 8.f, 0.f, -4.f };
	float local[3];
	ComputeDecalLocal(f, rel, local);
	float u = 0.f, v = 0.f;
	ASSERT_TRUE(ComputeDecalUnwrapUV(f, local, u, v));
	EXPECT_FLOAT_EQ(local[0] * 0.5f + 0.5f, u);
	EXPECT_FLOAT_EQ(local[1] * 0.5f + 0.5f, v);
}

TEST(DecalUnwrap, DoesNotPaintTheCentreTexelThroughTheWholeBox)
{
	// [rc4l] The black slab, which is what a BFG mark on a staircase looked like.
	//
	// A fragment at the mark's centre line but far THROUGH the plane has no direction to continue in.
	// Answering anyway paints it with the middle texel of the graphic -- and the middle of a scorch is
	// solid black -- so the decal's box was drawn as a hard-edged black quad standing in the world,
	// complete with visible faces. There is no right answer at the centre; there is only the right
	// thing to do, which is draw nothing.
	const DecalFrame f = WallFrame(16.f, 8.f, 24.f);
	float u = 0.f, v = 0.f;
	for (float through = 1.f; through <= 20.f; through += 1.f)
	{
		const float rel[3] = { 0.f, through, 0.f };
		float local[3];
		ComputeDecalLocal(f, rel, local);
		EXPECT_FALSE(ComputeDecalUnwrapUV(f, local, u, v))
			<< "painted at " << through << " units through the plane, dead centre";
	}
}

TEST(DecalUnwrap, CarriesOutwardByTheDistanceTravelled)
{
	// Four units further through the plane is four units further into the picture -- the same scale as
	// the surface it was shot at, which is what "unstretched" means. A plain projection would leave the
	// coordinate unchanged here, and that unchanging coordinate IS the dragged column of texels.
	const DecalFrame f = WallFrame(16.f, 8.f, 24.f);
	const float onSurface[3] = { 0.f, 0.f, -4.f };   // t = (0, -0.5)
	const float carried[3]   = { 0.f, 4.f, -4.f };   // same place, 4 units through
	float ls[3], lc[3];
	ComputeDecalLocal(f, onSurface, ls);
	ComputeDecalLocal(f, carried, lc);

	float us, vs, uc, vc;
	ASSERT_TRUE(ComputeDecalUnwrapUV(f, ls, us, vs));
	ASSERT_TRUE(ComputeDecalUnwrapUV(f, lc, uc, vc));
	EXPECT_FLOAT_EQ(us, uc);                          // straight out is not sideways
	EXPECT_NEAR(vs - (4.f / 8.f) * 0.5f, vc, 1e-5f);  // 4 world units of an 8-unit half-height
}

TEST(DecalUnwrap, CarriesTheOtherWayAboveTheCentre)
{
	// Above the mark the continuation is upward, below it downward, and neither needs to be told which
	// -- the direction is where the fragment already is. Sharing one rule is what keeps a decal shot at
	// the top of a wall from folding back on itself.
	const DecalFrame f = WallFrame(16.f, 8.f, 24.f);
	const float above[3] = { 0.f, 2.f, 4.f };
	float local[3];
	ComputeDecalLocal(f, above, local);
	float u, v;
	ASSERT_TRUE(ComputeDecalUnwrapUV(f, local, u, v));
	EXPECT_GT(v, local[1] * 0.5f + 0.5f);
}

TEST(DecalUnwrap, CarriesSidewaysForAVerticalCorner)
{
	// A wall met round a vertical corner is reached by travelling ACROSS, and the same rule covers it
	// with no case of its own. Putting the distance on the wrong axis is the smear this replaced.
	const DecalFrame f = WallFrame(16.f, 8.f, 24.f);
	const float sideways[3] = { 10.f, 5.f, 0.f };
	float local[3];
	ComputeDecalLocal(f, sideways, local);
	float u, v;
	ASSERT_TRUE(ComputeDecalUnwrapUV(f, local, u, v));
	EXPECT_FLOAT_EQ(0.5f, v);                        // dead level: stays level
	EXPECT_GT(u, local[0] * 0.5f + 0.5f);            // carried further across
}

TEST(DecalUnwrap, IsContinuousEverywhereItPaints)
{
	// [rc4l] The seam, and the pale streak that came with it.
	//
	// The direction used to come from sign(), which jumps: two fragments a hair either side of the
	// centre line got opposite answers and the coordinate tore in two down the middle. That tear is a
	// visible line through a mark on a step -- and it also wrecks the mip level, because a shader reads
	// level of detail from how fast this coordinate changes, so the tear went pale and streaky as well.
	// Sweeping across the centre must move the coordinate smoothly, or not paint at all.
	const DecalFrame f = WallFrame(16.f, 8.f, 24.f);
	float prevU = 0.f, prevV = 0.f;
	bool have = false;
	for (float x = -6.f; x <= 6.f; x += 0.25f)
	{
		const float rel[3] = { x, 6.f, -3.f };   // 6 units through the wall: a real carry
		float local[3];
		ComputeDecalLocal(f, rel, local);
		float u, v;
		if (!ComputeDecalUnwrapUV(f, local, u, v)) { have = false; continue; }
		if (have)
		{
			EXPECT_LT(std::fabs(u - prevU), 0.06f) << "at x=" << x;
			EXPECT_LT(std::fabs(v - prevV), 0.06f) << "at x=" << x;
		}
		prevU = u; prevV = v; have = true;
	}
}

TEST(DecalUnwrap, ReportsPastTheEndRatherThanClamping)
{
	// The caller must discard. Clamping repeats the edge texel for ever, which is a dragged column of
	// texels by another route -- the artifact this whole scheme exists to remove.
	const DecalFrame f = WallFrame(16.f, 8.f, 24.f);
	const float farOut[3] = { 40.f, 0.f, 0.f };
	float local[3];
	ComputeDecalLocal(f, farOut, local);
	float u, v;
	EXPECT_FALSE(ComputeDecalUnwrapUV(f, local, u, v));
	EXPECT_GT(u, 1.f);
}

TEST(DecalUnwrap, RefusesADegenerateFrame)
{
	DecalFrame f{};   // all axes zero
	const float local[3] = { 0.f, 0.f, 0.f };
	float u, v;
	EXPECT_FALSE(ComputeDecalUnwrapUV(f, local, u, v));
}

TEST(DecalUnwrap, DeeperBoxDoesNotChangeTheScaleOfTheCarry)
{
	// The box's depth decides how FAR the wrap reaches, never how fast the picture moves. Mixing the
	// two up made the same corner look different for a big decal and a small one.
	const float rel[3] = { 0.f, 4.f, -4.f };
	float u1, v1, u2, v2, l1[3], l2[3];

	const DecalFrame shallow = WallFrame(16.f, 8.f, 24.f);
	const DecalFrame deep    = WallFrame(16.f, 8.f, 96.f);
	ComputeDecalLocal(shallow, rel, l1);
	ComputeDecalLocal(deep, rel, l2);
	ASSERT_TRUE(ComputeDecalUnwrapUV(shallow, l1, u1, v1));
	ASSERT_TRUE(ComputeDecalUnwrapUV(deep, l2, u2, v2));

	EXPECT_NEAR(v1, v2, 1e-5f);
	EXPECT_NEAR(u1, u2, 1e-5f);
}
