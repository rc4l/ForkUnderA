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
// surface and had to be patched. Measuring the WALK from the blast's centre has nothing to
// degenerate: being within reach along the geometry is the only condition.

// ---------------------------------------------------------------------------------------------
// The creep: where a fragment reads the picture, and how far the soot walked to reach it
//
// A wall mark on a wall running EAST, its face pointing north. `rel` is measured from where the
// blast landed: x along the wall, y out through it, z up. There is no camera in any of these, which
// is the point -- every version of this that involved the view made marks change shape as the
// camera moved, twice.

namespace {
const float kWallNormal[3]  = { 0.f, 1.f, 0.f };   // the surface that was hit
const float kFloorNormal[3] = { 0.f, 0.f, 1.f };   // meets the wall in a horizontal corner
const float kSideNormal[3]  = { 1.f, 0.f, 0.f };   // meets the wall in a VERTICAL corner

float CreepU(const DecalFrame &f, const float rel[3], const float nrm[3])
{
	float u = 0, v = 0, path = 0;
	ComputeDecalCreepUV(f, rel, nrm, u, v, path);
	return u;
}
float CreepV(const DecalFrame &f, const float rel[3], const float nrm[3])
{
	float u = 0, v = 0, path = 0;
	ComputeDecalCreepUV(f, rel, nrm, u, v, path);
	return v;
}
float CreepPath(const DecalFrame &f, const float rel[3], const float nrm[3])
{
	float u = 0, v = 0, path = 0;
	ComputeDecalCreepUV(f, rel, nrm, u, v, path);
	return path;
}
}

TEST(DecalCreep, PutsTheCentreOfThePictureWhereTheBlastLanded)
{
	const DecalFrame f = WallFrame(16.f, 8.f, 16.f);
	const float atCentre[3] = { 0.f, 0.f, 0.f };

	EXPECT_NEAR(0.5f, CreepU(f, atCentre, kWallNormal), 1e-5f);
	EXPECT_NEAR(0.5f, CreepV(f, atCentre, kWallNormal), 1e-5f);
	EXPECT_NEAR(0.f, CreepPath(f, atCentre, kWallNormal), 1e-5f);
}

TEST(DecalCreep, IsUnstretchedOnTheSurfaceThatWasHit)
{
	// Half the box across is half the picture across. A mark on the wall it was made on is the case
	// that must never acquire a correction, because every other case is measured against it.
	const DecalFrame f = WallFrame(16.f, 8.f, 16.f);
	const float across[3] = { 8.f, 0.f, 0.f };    // half of the 16-unit half-width

	EXPECT_NEAR(0.75f, CreepU(f, across, kWallNormal), 1e-5f);
	EXPECT_NEAR(0.5f, CreepV(f, across, kWallNormal), 1e-5f);
}

TEST(DecalCreep, MeasuresTheWalkAndNotTheStraightLine)
{
	// [rc4l] The whole model in one assertion.
	//
	// A fragment on the floor 12 units below the mark and 9 out from the wall is 15 away through the
	// air. The soot cannot fly: it goes 12 down to the corner and 9 out from it, and the picture is
	// read at that distance. Charging the straight line instead is what let a mark reach surfaces it
	// had no path to.
	const DecalFrame f = WallFrame(16.f, 8.f, 16.f);
	const float onFloor[3] = { 0.f, 9.f, -12.f };

	EXPECT_NEAR(21.f, CreepPath(f, onFloor, kFloorNormal), 1e-4f);
}

TEST(DecalCreep, CarriesTheAlongCoordinateStraightOverACorner)
{
	// Two planes meet in a LINE. The coordinate running along that line does not change as the creep
	// crosses it -- only the one crossing it accumulates. A fragment 8 along the wall and 8 along the
	// floor beneath it read the same place across the picture.
	const DecalFrame f = WallFrame(16.f, 8.f, 16.f);
	const float onWall[3]  = { 8.f, 0.f, -4.f };
	const float onFloor[3] = { 8.f, 4.f, -12.f };

	EXPECT_NEAR(CreepU(f, onWall, kWallNormal), CreepU(f, onFloor, kFloorNormal), 1e-5f);
}

TEST(DecalCreep, CrossesTheOtherWayForAVerticalCorner)
{
	// [rc4l] Which picture axis the corner runs along decides which coordinate is which.
	//
	// A wall meeting a FLOOR is a horizontal corner, so the walk crosses the picture vertically. Two
	// walls meeting is a vertical one and it crosses horizontally. Feeding both to the same axis
	// turns the mark on its side as it wraps -- the corner looked plausible and the wrap did not.
	const DecalFrame f = WallFrame(16.f, 8.f, 16.f);
	const float onFloor[3] = { 0.f, 6.f, -10.f };    // horizontal corner: V should move, U should not
	const float onSide[3]  = { 10.f, 6.f, 0.f };     // vertical corner: U should move, V should not

	EXPECT_NEAR(0.5f, CreepU(f, onFloor, kFloorNormal), 1e-5f);
	EXPECT_GT(std::fabs(CreepV(f, onFloor, kFloorNormal) - 0.5f), 0.1f);

	EXPECT_NEAR(0.5f, CreepV(f, onSide, kSideNormal), 1e-5f);
	EXPECT_GT(std::fabs(CreepU(f, onSide, kSideNormal) - 0.5f), 0.1f);
}

TEST(DecalCreep, ReachesBothSidesOfACornerBecauseTheShadowLandsOnIt)
{
	// [rc4l] The distance from the corner is ABSOLUTE, and that is not a convenience.
	//
	// Dropping a perpendicular from the impact onto the neighbouring surface cannot move along the
	// hit plane's normal, so its foot is always a point of the corner line -- the walk is therefore
	// symmetric about that line. Signing it away from the hit plane instead sent the creep the wrong
	// way round a pillar: the side faces run BACKWARDS from the corner and got nothing at all.
	const DecalFrame f = WallFrame(16.f, 8.f, 16.f);
	const float inFront[3] = { 10.f,  6.f, 0.f };
	const float behind[3]  = { 10.f, -6.f, 0.f };

	EXPECT_NEAR(CreepU(f, inFront, kSideNormal), CreepU(f, behind, kSideNormal), 1e-5f);
}

TEST(DecalCreep, ChargesTheWalkRoundTheBackOfACorner)
{
	// [rc4l] A corner line is infinite here and is not in the map: it runs until the wall ends.
	//
	// Charging only the crossing let soot arrive on floor round the FAR side of a convex corner as
	// cheaply as on floor directly in front of the wall, so a mark by a pillar's edge printed a
	// second, mirrored copy of itself on open floor beside it at full strength. Going round the back
	// costs the distance along the corner too, so that copy fades with how far past it sits.
	const DecalFrame f = WallFrame(16.f, 8.f, 16.f);
	// Offset ALONG the corner as well, because that is what the extra charge is made of.
	const float inFront[3] = { 20.f,  6.f, -10.f };
	const float behind[3]  = { 20.f, -6.f, -10.f };

	EXPECT_GT(CreepPath(f, behind, kSideNormal), CreepPath(f, inFront, kSideNormal));
}

TEST(DecalCreep, ChargesNothingExtraDirectlyAcrossTheCorner)
{
	// The other half of the same rule, and the reason the charge is safe to add: with no distance
	// along the corner there is no end to walk round, so the two sides cost the same. That is the
	// case which already looked right -- a mark running down a wall onto the floor beneath it, or
	// wrapping a pillar at its own height -- and it must not move.
	const DecalFrame f = WallFrame(16.f, 8.f, 16.f);
	const float inFront[3] = { 20.f,  6.f, 0.f };
	const float behind[3]  = { 20.f, -6.f, 0.f };

	EXPECT_NEAR(CreepPath(f, inFront, kSideNormal), CreepPath(f, behind, kSideNormal), 1e-4f);
}

TEST(DecalCreep, DoesNotDependOnAnythingButThePointAndTheSurface)
{
	// Asked twice with the same inputs, answered the same. A mark that changes as the camera moves
	// was reported three times, and each time the cause was a term that had no business being here.
	const DecalFrame f = WallFrame(16.f, 8.f, 16.f);
	const float rel[3] = { 4.f, 3.f, -7.f };
	float u1 = 0, v1 = 0, p1 = 0, u2 = 0, v2 = 0, p2 = 0;

	ComputeDecalCreepUV(f, rel, kFloorNormal, u1, v1, p1);
	ComputeDecalCreepUV(f, rel, kFloorNormal, u2, v2, p2);

	EXPECT_FLOAT_EQ(u1, u2);
	EXPECT_FLOAT_EQ(v1, v2);
	EXPECT_FLOAT_EQ(p1, p2);
}

TEST(DecalCreep, ReportsPastTheEdgeRatherThanClamping)
{
	// The caller must DISCARD outside 0..1. Clamping repeats the edge texel for ever, which is the
	// dragged row of pixels this model exists to remove, arrived at by another route.
	const DecalFrame f = WallFrame(16.f, 8.f, 16.f);
	const float farOut[3] = { 400.f, 0.f, 0.f };

	EXPECT_GT(CreepU(f, farOut, kWallNormal), 1.f);
}

TEST(DecalCreep, SurvivesADegenerateFrameOrNormal)
{
	// Nothing here may divide by zero: a degenerate box or an absent normal has to leave the caller
	// with a coordinate it will simply discard, not a NaN that paints the whole screen.
	const DecalFrame bad = { { 0.f, 0.f, 0.f }, { 0.f, 0.f, 1.f }, { 0.f, 1.f, 0.f } };
	const float rel[3] = { 1.f, 1.f, 1.f };
	const float noNormal[3] = { 0.f, 0.f, 0.f };
	float u = 0, v = 0, path = -1.f;

	ComputeDecalCreepUV(bad, rel, kWallNormal, u, v, path);
	EXPECT_EQ(0.f, path);

	ComputeDecalCreepUV(WallFrame(16.f, 8.f, 16.f), rel, noNormal, u, v, path);
	EXPECT_EQ(0.f, path);
}

// ---------------------------------------------------------------------------------------------
// What is left of the blast after the walk

TEST(DecalCreepReach, IsWholeCloseInAndGoneAtTheLimit)
{
	EXPECT_FLOAT_EQ(1.f, ComputeDecalCreepReach(0.f, 60.f));
	EXPECT_FLOAT_EQ(1.f, ComputeDecalCreepReach(30.f, 60.f));   // the fade starts at half the radius
	EXPECT_FLOAT_EQ(0.f, ComputeDecalCreepReach(60.f, 60.f));
	EXPECT_FLOAT_EQ(0.f, ComputeDecalCreepReach(90.f, 60.f));
}

TEST(DecalCreepReach, FadesRatherThanStopping)
{
	// A mark with picture left when it runs out of reach must not end on a line. Monotonic, and
	// strictly between, across the whole fade.
	const float a = ComputeDecalCreepReach(36.f, 60.f);
	const float b = ComputeDecalCreepReach(48.f, 60.f);

	EXPECT_LT(a, 1.f);
	EXPECT_GT(a, 0.f);
	EXPECT_LT(b, a);
}

TEST(DecalCreepReach, IsNothingWithoutARadius)
{
	EXPECT_FLOAT_EQ(0.f, ComputeDecalCreepReach(1.f, 0.f));
}

// ---------------------------------------------------------------------------------------------
// Fading, and what a decal is shaded at

TEST(DecalFade, HoldsFullAlphaBeforeItStartsToDecay)
{
	// [rc4l] GoAway2 -- the BFG glow and the plasma flare -- holds for a second and then fades over
	// three. A single ramp from spawn stood in for every fader here, so a glow was already at two
	// thirds brightness the moment anyone saw it. Beside GL that reads as "much dimmer in Vulkan".
	EXPECT_FLOAT_EQ(1.f, ComputeDecalFade(100, 35, 105, 100));
	EXPECT_FLOAT_EQ(1.f, ComputeDecalFade(100, 35, 105, 134));
}

TEST(DecalFade, FallsLinearlyOnceItStarts)
{
	EXPECT_FLOAT_EQ(1.f, ComputeDecalFade(100, 35, 100, 135));
	EXPECT_FLOAT_EQ(0.5f, ComputeDecalFade(100, 35, 100, 185));
	EXPECT_FLOAT_EQ(0.f, ComputeDecalFade(100, 35, 100, 235));
	EXPECT_FLOAT_EQ(0.f, ComputeDecalFade(100, 35, 100, 999));
}

TEST(DecalFade, NeverFadesWithoutAFader)
{
	// [rc4l] Every animator that is not a fader -- stretchers, sliders, colour changers -- leaves the
	// alpha alone and never removes the mark. Ramping those out made permanent marks disappear.
	EXPECT_FLOAT_EQ(1.f, ComputeDecalFade(100, 0, 0, 100000));
}

TEST(DecalShadeLight, IsFullBrightnessForAGlow)
{
	// [rc4l] DECALDEF marks the glows fullbright and gl_decal.cpp shades them at 255. Shading at the
	// sector's light instead makes a glow vanish into a dark corridor while GL's stays bright. Only
	// the SHADING is affected -- the fog still comes from the sector, or a glow would burn through
	// smoke that dims everything around it.
	EXPECT_EQ(255, ComputeDecalShadeLight(true, 96));
	EXPECT_EQ(96, ComputeDecalShadeLight(false, 96));
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

