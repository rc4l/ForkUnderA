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
// The mapping
//
// A wall mark 32 wide and 16 tall on a wall running east, its face pointing north. `rel` is measured
// from where the blast landed: x along the wall, y out through it, z up.
//
// There is no camera in any of these, and that is the point. Every earlier version of this needed a
// projection axis chosen before the surface was known, and every one of them degenerated on some
// surface and had to be patched. Measuring each surface in its own plane from the blast's centre has
// nothing to degenerate.

namespace {
const float kWallNormal[3]  = { 0.f, 1.f, 0.f };   // the surface that was hit
const float kFloorNormal[3] = { 0.f, 0.f, 1.f };
// Chord == radius is a surface the blast landed ON: it cuts the sphere through its middle, so it
// takes the picture at full size.
const float kR = 32.f;
}

TEST(DecalSurfaceUV, PutsTheCentreOfThePictureWhereTheBlastLanded)
{
	const DecalFrame f = WallFrame(16.f, 8.f, 16.f);
	const float atCentre[3] = { 0.f, 0.f, 0.f };
	float u = 0.f, v = 0.f;
	ASSERT_TRUE(ComputeDecalSurfaceUV(f, atCentre, kWallNormal, kR, kR, u, v));
	EXPECT_FLOAT_EQ(0.5f, u);
	EXPECT_FLOAT_EQ(0.5f, v);
}

TEST(DecalSurfaceUV, IsUnstretchedOnTheSurfaceThatWasHit)
{
	// Half the mark's width across is half the picture across. Anything else is the mark being drawn
	// at the wrong size on the very surface it was shot at.
	const DecalFrame f = WallFrame(16.f, 8.f, 16.f);
	const float across[3] = { 8.f, 0.f, 0.f };
	float u, v;
	ASSERT_TRUE(ComputeDecalSurfaceUV(f, across, kWallNormal, kR, kR, u, v));
	EXPECT_FLOAT_EQ(0.75f, u);
	EXPECT_FLOAT_EQ(0.5f, v);
}

TEST(DecalSurfaceUV, IsUnstretchedOnAFloorToo)
{
	// [rc4l] The case every plane projection got wrong.
	//
	// A floor met at a right angle has no movement along a wall's projection axis at all, so a
	// projection from that axis drags one row of texels across it -- and every attempt to patch that
	// broke somewhere else. Measured in the floor's OWN plane there is nothing to drag: eight units
	// across the floor is a quarter of a thirty-two-unit picture, exactly as it is on the wall.
	const DecalFrame f = WallFrame(16.f, 8.f, 16.f);
	const float acrossFloor[3] = { 8.f, 0.f, -4.f };
	float u, v;
	ASSERT_TRUE(ComputeDecalSurfaceUV(f, acrossFloor, kFloorNormal, kR, kR, u, v));
	EXPECT_FLOAT_EQ(0.75f, u);
}

TEST(DecalSurfaceUV, CoversEveryDirectionOnAFloor)
{
	// A wedge of floor was left unpainted where a strip aligned to one wall could not reach round a
	// corner. Nothing aligned to a wall decides this any more: the four compass directions on a floor
	// all land inside the picture and none of them is a special case.
	const DecalFrame f = WallFrame(16.f, 16.f, 16.f);
	const float dirs[4][3] = {
		{  8.f,  0.f, -4.f }, { -8.f,  0.f, -4.f },
		{  0.f,  8.f, -4.f }, {  0.f, -8.f, -4.f },
	};
	for (int i = 0; i < 4; i++)
	{
		float u, v;
		EXPECT_TRUE(ComputeDecalSurfaceUV(f, dirs[i], kFloorNormal, kR, kR, u, v))
			<< "direction " << i;
	}
}

TEST(DecalSurfaceUV, DoesNotDependOnAnythingButThePointAndTheSurface)
{
	// The signature carries no camera and no screen, which is what makes "the mark follows you when
	// you wiggle the mouse" impossible to reintroduce by accident. The same fragment on the same
	// surface must give the same answer every time it is asked.
	const DecalFrame f = WallFrame(16.f, 8.f, 16.f);
	const float rel[3] = { 5.f, 2.f, -3.f };
	float u1, v1, u2, v2;
	ComputeDecalSurfaceUV(f, rel, kFloorNormal, kR, kR, u1, v1);
	ComputeDecalSurfaceUV(f, rel, kFloorNormal, kR, kR, u2, v2);
	EXPECT_FLOAT_EQ(u1, u2);
	EXPECT_FLOAT_EQ(v1, v2);
}

TEST(DecalSurfaceUV, ReportsPastTheEdgeRatherThanClamping)
{
	// The caller must discard. Clamping repeats the edge texel for ever, which is a dragged row of
	// texels by another route -- the artifact the whole design exists to avoid.
	const DecalFrame f = WallFrame(16.f, 8.f, 16.f);
	const float farOut[3] = { 40.f, 0.f, 0.f };
	float u, v;
	EXPECT_FALSE(ComputeDecalSurfaceUV(f, farOut, kWallNormal, kR, kR, u, v));
	EXPECT_GT(u, 1.f);
}

TEST(DecalSurfaceUV, RefusesADegenerateFrameOrNormal)
{
	DecalFrame f{};
	const float rel[3] = { 1.f, 1.f, 1.f };
	float u, v;
	EXPECT_FALSE(ComputeDecalSurfaceUV(f, rel, kWallNormal, kR, kR, u, v));

	const DecalFrame good = WallFrame(16.f, 8.f, 16.f);
	const float noNormal[3] = { 0.f, 0.f, 0.f };
	EXPECT_FALSE(ComputeDecalSurfaceUV(good, rel, noNormal, kR, kR, u, v));
}

TEST(DecalReach, IsNeverCloseEnoughToCutThePicture)
{
	// [rc4l] The hard circular rim.
	//
	// The sphere bounds how far through space a mark carries; the graphic's own alpha is what shapes
	// it. Sized at the picture's half-width the sphere cut the corners off instead -- a rectangle's
	// corner is further from its centre than its edge is -- and a rocket scorch came out as a disc
	// with a rim stamped through it. Whatever else this returns, it must clear the diagonal.
	const float cases[4][2] = { { 16.f, 8.f }, { 8.f, 16.f }, { 31.f, 31.f }, { 2.f, 2.5f } };
	for (int i = 0; i < 4; i++)
	{
		const float hw = cases[i][0], hh = cases[i][1];
		const float diagonal = std::sqrt(hw * hw + hh * hh);
		EXPECT_GT(ComputeDecalReach(hw, hh), diagonal)
			<< "half-extents " << hw << " x " << hh;
	}
}

TEST(DecalReach, LeavesRoomForASurfaceOffToTheSide)
{
	// A mark reaches a floor or a wall that stands off to one side of the impact, and the sphere has
	// to clear that too or it clips the far half of it. Half the picture again is the margin.
	EXPECT_FLOAT_EQ(std::sqrt(16.f * 16.f + 8.f * 8.f) * 1.5f, ComputeDecalReach(16.f, 8.f));
}


// ---------------------------------------------------------------------------------------------
// How much of the blast a surface cuts through

TEST(DecalChord, IsTheWholeRadiusOnTheSurfaceThatWasHit)
{
	// The impact sits on that surface, so the sphere is cut through its middle. Anything less would
	// shrink every mark in the game on the very surface it was made on.
	EXPECT_FLOAT_EQ(60.f, ComputeDecalChord(0.f, 60.f));
}

TEST(DecalChord, ShrinksAsASurfaceRecedes)
{
	// A sphere's cross-section, which is the whole rule. Nothing here was picked.
	EXPECT_FLOAT_EQ(48.f, ComputeDecalChord(36.f, 60.f));   // 3-4-5
	EXPECT_GT(ComputeDecalChord(10.f, 60.f), ComputeDecalChord(30.f, 60.f));
}

TEST(DecalChord, IsNothingBeyondTheBlast)
{
	// [rc4l] The ghost scorch on the floor beneath a mark on a wall.
	//
	// Every surface in range used to take the picture at FULL size, centred where the impact projects
	// onto it, faded only in alpha -- so a floor well below a wall mark got a complete second scorch
	// out in the open. At d >= R there is no disc at all, and a full-size copy is no longer reachable.
	EXPECT_FLOAT_EQ(0.f, ComputeDecalChord(60.f, 60.f));
	EXPECT_FLOAT_EQ(0.f, ComputeDecalChord(90.f, 60.f));
}

TEST(DecalSurfaceUV, ShrinksThePictureWithTheChord)
{
	// The mark gets smaller as its surface recedes, rather than arriving whole and faint. Half the
	// chord across is half the picture across, whatever the chord happens to be.
	const DecalFrame f = WallFrame(16.f, 16.f, 16.f);
	const float across[3] = { 8.f, 0.f, 0.f };
	float uFull, vFull, uHalf, vHalf;
	ASSERT_TRUE(ComputeDecalSurfaceUV(f, across, kWallNormal, 32.f, 32.f, uFull, vFull));
	ASSERT_TRUE(ComputeDecalSurfaceUV(f, across, kWallNormal, 16.f, 32.f, uHalf, vHalf));
	// Half the chord, so the same world offset is twice as far into the picture.
	EXPECT_FLOAT_EQ(0.75f, uFull);
	EXPECT_FLOAT_EQ(1.0f, uHalf);
}

TEST(DecalSurfaceUV, PaintsNothingWhereTheBlastDoesNotReach)
{
	const DecalFrame f = WallFrame(16.f, 16.f, 16.f);
	const float rel[3] = { 1.f, 0.f, 0.f };
	float u, v;
	EXPECT_FALSE(ComputeDecalSurfaceUV(f, rel, kWallNormal, 0.f, 32.f, u, v));
}
