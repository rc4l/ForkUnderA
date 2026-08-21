// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The cases a Doom map actually contains, stated before any of this replaces GLWall::Process.
//
// The point of deriving geometry here rather than copying what GL produced is that the answer becomes
// checkable without a level loaded, a camera, or a screenshot. So the cases go in first: a one-sided
// wall, a step up, a step down, a window, a shut door, and the degenerate pair that keeps being
// confused -- a part that is ABSENT versus one that is present and zero-height.

#include <gtest/gtest.h>

#include "features/surfaces/computation/wallgeom_compute.h"

using namespace zx::surfaces;

namespace {

WallHeights OneSided(float floor, float ceiling)
{
	WallHeights h;
	h.frontFloor = floor; h.frontCeiling = ceiling;
	h.backFloor = floor;  h.backCeiling = ceiling;
	h.twoSided = false;
	return h;
}

WallHeights TwoSided(float ff, float fc, float bf, float bc)
{
	WallHeights h;
	h.frontFloor = ff; h.frontCeiling = fc;
	h.backFloor = bf;  h.backCeiling = bc;
	h.twoSided = true;
	return h;
}

} // namespace

// A solid wall is one part, floor to ceiling, and nothing else.
TEST(WallGeom, OneSidedWallIsAllMiddle)
{
	const WallHeights h = OneSided(0.f, 128.f);
	EXPECT_FALSE(ComputeUpperPart(h).present);
	EXPECT_FALSE(ComputeLowerPart(h).present);
	const WallPart mid = ComputeMiddlePart(h);
	ASSERT_TRUE(mid.present);
	EXPECT_FLOAT_EQ(0.f, mid.bottom);
	EXPECT_FLOAT_EQ(128.f, mid.top);
}

// A step up: the room behind has a higher floor, so this side shows a lower texture.
TEST(WallGeom, StepUpShowsALowerPart)
{
	const WallHeights h = TwoSided(0.f, 128.f, 32.f, 128.f);
	const WallPart low = ComputeLowerPart(h);
	ASSERT_TRUE(low.present);
	EXPECT_FLOAT_EQ(0.f, low.bottom);
	EXPECT_FLOAT_EQ(32.f, low.top);
	EXPECT_FALSE(ComputeUpperPart(h).present);
	// ...and the opening starts at the higher floor.
	const WallPart mid = ComputeMiddlePart(h);
	ASSERT_TRUE(mid.present);
	EXPECT_FLOAT_EQ(32.f, mid.bottom);
	EXPECT_FLOAT_EQ(128.f, mid.top);
}

// A step DOWN from this side shows nothing: that lower texture belongs to the other sidedef.
TEST(WallGeom, StepDownShowsNothingFromThisSide)
{
	const WallHeights h = TwoSided(32.f, 128.f, 0.f, 128.f);
	EXPECT_FALSE(ComputeLowerPart(h).present);
	EXPECT_FALSE(ComputeUpperPart(h).present);
	EXPECT_TRUE(ComputeMiddlePart(h).present);
}

// A window: the ceiling steps down behind and the floor steps up, so both parts show.
TEST(WallGeom, WindowShowsUpperAndLower)
{
	const WallHeights h = TwoSided(0.f, 128.f, 48.f, 96.f);
	const WallPart up = ComputeUpperPart(h);
	const WallPart low = ComputeLowerPart(h);
	ASSERT_TRUE(up.present);
	ASSERT_TRUE(low.present);
	EXPECT_FLOAT_EQ(96.f, up.bottom);
	EXPECT_FLOAT_EQ(128.f, up.top);
	EXPECT_FLOAT_EQ(0.f, low.bottom);
	EXPECT_FLOAT_EQ(48.f, low.top);
	const WallPart mid = ComputeMiddlePart(h);
	EXPECT_FLOAT_EQ(48.f, mid.bottom);
	EXPECT_FLOAT_EQ(96.f, mid.top);
}

// A shut door: the sector behind is squeezed flat, so there is no opening and the upper part covers
// the whole doorway. Getting this backwards is a door you can see through when it is closed.
TEST(WallGeom, ClosedDoorHasNoOpening)
{
	const WallHeights h = TwoSided(0.f, 128.f, 0.f, 0.f);
	EXPECT_FALSE(ComputeMiddlePart(h).present);
	const WallPart up = ComputeUpperPart(h);
	ASSERT_TRUE(up.present);
	EXPECT_FLOAT_EQ(0.f, up.bottom);
	EXPECT_FLOAT_EQ(128.f, up.top);
}

// A sector with no height -- a crusher fully down, a door mid-move -- is not a surface. It is also
// not an error: the sidedef still exists and its geometry comes back when the sector moves.
TEST(WallGeom, ZeroHeightSectorHasNoParts)
{
	const WallHeights h = OneSided(64.f, 64.f);
	EXPECT_FALSE(ComputeMiddlePart(h).present);
	EXPECT_FALSE(ComputeSideHasGeometry(h));
}

// Absent and zero-height are different states and the caller does different things with them.
TEST(WallGeom, AbsentIsNotTheSameAsZeroHeight)
{
	// Ceilings level: no upper part exists at all.
	const WallHeights level = TwoSided(0.f, 128.f, 0.f, 128.f);
	const WallPart none = ComputeUpperPart(level);
	EXPECT_FALSE(none.present);
	EXPECT_FLOAT_EQ(none.bottom, none.top);   // a degenerate span, not garbage

	// A part that exists and is a hair tall is still present.
	const WallHeights sliver = TwoSided(0.f, 128.f, 0.f, 127.99f);
	EXPECT_TRUE(ComputeUpperPart(sliver).present);
}

// An inverted sector -- floor above ceiling, which maps do produce mid-move -- must not come back as
// a part with negative height. A quad wound backwards vanishes under back-face culling, which is a
// hole in the world rather than a visible error.
TEST(WallGeom, InvertedSectorProducesNothing)
{
	const WallHeights h = OneSided(128.f, 0.f);
	EXPECT_FALSE(ComputeMiddlePart(h).present);
	EXPECT_FALSE(ComputeSideHasGeometry(h));

	const WallHeights inverted = TwoSided(0.f, 128.f, 200.f, 64.f);
	const WallPart low = ComputeLowerPart(inverted);
	const WallPart up = ComputeUpperPart(inverted);
	if (low.present) EXPECT_GT(low.top, low.bottom);
	if (up.present) EXPECT_GT(up.top, up.bottom);
	EXPECT_FALSE(ComputeMiddlePart(inverted).present);   // floor above ceiling: no opening
}

// [rc4l] A sector squeezed past itself, which is what fua_surface_verify found on two real maps.
//
// A door mid-move, a crusher down, a lift whose ceiling has dropped below its own floor: the sector
// reports a ceiling BELOW its floor. Taken literally the upper texture hangs down through the
// doorway into space the lower texture already covers -- 28 pieces on dbab01 and 13 on dbab04
// disagreed with the capture exactly that way, every one of them by the gap between the two.
//
// The plane that stops it is the FRONT FLOOR. GL's comment says "the back sector's floor" and GL's
// code says ffh, and the two are the same number on every door whose sectors share a floor -- which
// is every door in these two maps, and why clamping to the back floor passed for as long as it did.
TEST(WallGeom, UpperStopsAtTheFrontFloorWhenTheBackSectorIsInverted)
{
	// A doorway: both sectors stand on the same floor, and the back ceiling has dropped past it.
	const WallHeights h = TwoSided(336.f, 464.f, 336.f, 256.f);
	const WallPart up = ComputeUpperPart(h);
	ASSERT_TRUE(up.present);
	EXPECT_FLOAT_EQ(336.f, up.bottom);   // the floor the player is standing on
	EXPECT_FLOAT_EQ(464.f, up.top);
}

// ...and the two are NOT the same number when the floors differ, which is where the old rule was
// wrong. GL clamps to 16 here, not to 336, and the upper reaches down to the front floor.
TEST(WallGeom, UpperClampsToTheFrontFloorAndNotTheBackOne)
{
	const WallHeights h = TwoSided(16.f, 464.f, 336.f, 256.f);
	const WallPart up = ComputeUpperPart(h);
	ASSERT_TRUE(up.present);
	EXPECT_FLOAT_EQ(256.f, up.bottom);   // no clamp at all: the front floor is BELOW the back ceiling
	EXPECT_FLOAT_EQ(464.f, up.top);
}

// The lower's mirror: the FRONT CEILING cuts the top off, which is what a back floor standing above
// the ceiling the player is looking through needs -- the sky case GL's own comment names.
TEST(WallGeom, LowerStopsAtTheFrontCeiling)
{
	const WallHeights h = TwoSided(16.f, 200.f, 336.f, 400.f);
	const WallPart low = ComputeLowerPart(h);
	ASSERT_TRUE(low.present);
	EXPECT_FLOAT_EQ(16.f, low.bottom);
	EXPECT_FLOAT_EQ(200.f, low.top);     // the front ceiling, not the back floor's 336
}

// [rc4l] Both clamps are decided by the PAIR of ends, not by each end on its own.
//
// A wall that pinches out at one end is still one quad, and GL moves the whole quad or neither end
// of it: `if (fch1<bfh1 && fch2<bfh2)`, with an AND. Asking each end separately bends a sloped wall
// where GL leaves it straight, and dbab04 -- 337 sloped pieces -- is where that shows.
TEST(WallGeom, TheLowerClampNeedsBOTHEndsToWantIt)
{
	// The front ceiling is below the back floor at the LEFT end only.
	const WallHeights left  = TwoSided(0.f, 100.f, 200.f, 400.f);
	const WallHeights right = TwoSided(0.f, 300.f, 200.f, 400.f);
	WallPart pl, pr;
	ComputeLowerSpan(left, right, pl, pr);
	EXPECT_FLOAT_EQ(200.f, pl.top);   // unclamped, because the right end did not ask
	EXPECT_FLOAT_EQ(200.f, pr.top);
}

TEST(WallGeom, TheLowerClampAppliesWhenBothEndsWantIt)
{
	const WallHeights left  = TwoSided(0.f, 100.f, 200.f, 400.f);
	const WallHeights right = TwoSided(0.f, 150.f, 200.f, 400.f);
	WallPart pl, pr;
	ComputeLowerSpan(left, right, pl, pr);
	EXPECT_FLOAT_EQ(100.f, pl.top);   // each end to its OWN ceiling, so the slope stays a slope
	EXPECT_FLOAT_EQ(150.f, pr.top);
}

// [rc4l] A lower that exists at one end and not the other, which is the dbab04 case.
//
// Back floor -64 at the left end (below the front floor: nothing to draw) and +32 at the right
// (above it: a step). GL draws the quad -- `if (bfh1>ffh1 || bfh2>ffh2)`, with an OR -- and it comes
// out as a triangle. Refusing it because one end is empty left four lowers on dbab04 that GL drew
// and the map did not account for.
TEST(WallGeom, ALowerThatPinchesOutAtOneEndStillHasTheOther)
{
	const WallHeights left  = TwoSided(-8.f, 200.f, -64.f, 400.f);
	const WallHeights right = TwoSided(-8.f, 200.f,  32.f, 400.f);
	WallPart pl, pr;
	ComputeLowerSpan(left, right, pl, pr);
	EXPECT_FALSE(pl.present);         // nothing at this end...
	ASSERT_TRUE(pr.present);          // ...and a 40-unit step at the other
	EXPECT_FLOAT_EQ(-8.f, pr.bottom);
	EXPECT_FLOAT_EQ(32.f, pr.top);
}

// The ordinary case must not move: a normal window still reads its own two planes.
TEST(WallGeom, TheClampDoesNothingToAnOrdinarySector)
{
	const WallHeights h = TwoSided(0.f, 128.f, 48.f, 96.f);
	EXPECT_FLOAT_EQ(96.f, ComputeUpperPart(h).bottom);
	EXPECT_FLOAT_EQ(48.f, ComputeLowerPart(h).top);
}

// [rc4l] What a two-sided middle is clipped to, which is not the opening.
//
// Every case below is a branch of DoMidTexture, and the first two are what the old "clip to the
// opening" rule got wrong on dbab02: a 128-unit grate came out 96 and a 235-unit one came out 96,
// both because the line has no upper texture and is therefore clipped to the ceiling ABOVE the
// opening rather than the one below it.
namespace {

zx::surfaces::MidTextureClip Hanging(float texTop, float texBottom)
{
	zx::surfaces::MidTextureClip c;
	c.texTop = texTop; c.texBottom = texBottom;
	c.hasUpper = true; c.hasLower = true;
	c.frontCeilingSky = false; c.backCeilingSky = false;
	c.wrap = false; c.clipToPlanes = true;
	return c;
}

} // namespace

TEST(WallGeom, AMiddleWithNoUpperIsClippedToTheHIGHERCeiling)
{
	// front ceiling 416, back ceiling 384: the opening's top is 384 and GL uses 416.
	const WallHeights h = TwoSided(288.f, 416.f, 288.f, 384.f);
	zx::surfaces::MidTextureClip c = Hanging(416.f, 288.f);
	c.hasUpper = false;
	WallPart p0, p1;
	ComputeMiddleClip(h, h, c, p0, p1);
	EXPECT_FLOAT_EQ(416.f, p0.top);
	EXPECT_FLOAT_EQ(288.f, p0.bottom);
}

TEST(WallGeom, AMiddleWithAnUpperIsClippedToTheLOWERCeiling)
{
	const WallHeights h = TwoSided(288.f, 416.f, 288.f, 384.f);
	WallPart p0, p1;
	ComputeMiddleClip(h, h, Hanging(416.f, 288.f), p0, p1);
	EXPECT_FLOAT_EQ(384.f, p0.top);
}

TEST(WallGeom, AnIntraSkyLineWithNoUpperIsNotClippedAtAll)
{
	const WallHeights h = TwoSided(0.f, 200.f, 0.f, 100.f);
	zx::surfaces::MidTextureClip c = Hanging(1000.f, -1000.f);
	c.hasUpper = false;
	c.frontCeilingSky = c.backCeilingSky = true;
	c.wrap = true;   // so the texture's own extent does not clip it either
	WallPart p0, p1;
	ComputeMiddleClip(h, h, c, p0, p1);
	EXPECT_FLOAT_EQ(1000.f, p0.top);
}

TEST(WallGeom, AMiddleWithNoLowerIsClippedToTheLOWERFloor)
{
	const WallHeights h = TwoSided(64.f, 400.f, 16.f, 400.f);
	zx::surfaces::MidTextureClip c = Hanging(400.f, 0.f);
	c.hasLower = false;
	WallPart p0, p1;
	ComputeMiddleClip(h, h, c, p0, p1);
	EXPECT_FLOAT_EQ(16.f, p0.bottom);   // the back floor, below the front's 64
}

// With a lower texture there are two branches, and which one fires turns on whether the floors
// CROSS. They cross here -- the back floor is below the front's -- so GL draws down to the back
// sector's floor and lets the front sector's plane clip the polygon for it.
TEST(WallGeom, AMiddleWhoseFloorsCrossIsDrawnToTheBackFloor)
{
	const WallHeights h = TwoSided(64.f, 400.f, 16.f, 400.f);
	WallPart p0, p1;
	ComputeMiddleClip(h, h, Hanging(400.f, 0.f), p0, p1);
	EXPECT_FLOAT_EQ(16.f, p0.bottom);
}

// And when they do not cross, the ordinary case: the higher of the two, which IS the opening's floor.
TEST(WallGeom, AMiddleWithALowerIsOtherwiseClippedToTheHIGHERFloor)
{
	const WallHeights h = TwoSided(16.f, 400.f, 64.f, 400.f);
	WallPart p0, p1;
	ComputeMiddleClip(h, h, Hanging(400.f, 0.f), p0, p1);
	EXPECT_FLOAT_EQ(64.f, p0.bottom);
}

// The texture's own extent brings the polygon in -- but only when it does not repeat, and only when
// both ends agree, which is the same both-ends rule the upper and the lower clamps follow.
TEST(WallGeom, AHangingTextureThatEndsEarlyBringsTheWallWithIt)
{
	const WallHeights h = TwoSided(0.f, 400.f, 0.f, 400.f);
	WallPart p0, p1;
	ComputeMiddleClip(h, h, Hanging(200.f, 72.f), p0, p1);
	EXPECT_FLOAT_EQ(200.f, p0.top);
	EXPECT_FLOAT_EQ(72.f, p0.bottom);
}

TEST(WallGeom, AWrappingMiddleIsNotBroughtInByItsTexture)
{
	const WallHeights h = TwoSided(0.f, 400.f, 0.f, 400.f);
	zx::surfaces::MidTextureClip c = Hanging(200.f, 72.f);
	c.wrap = true;
	WallPart p0, p1;
	ComputeMiddleClip(h, h, c, p0, p1);
	EXPECT_FLOAT_EQ(400.f, p0.top);     // the planes, and nothing else
	EXPECT_FLOAT_EQ(0.f, p0.bottom);
}

// Both sides the same sector with nothing forcing a clip: GL does not clip to planes at all, because
// clipping to a plane against itself can only produce artefacts.
TEST(WallGeom, AMiddleInOneSectorIsTheTextureAndNothingElse)
{
	const WallHeights h = TwoSided(0.f, 128.f, 0.f, 128.f);
	zx::surfaces::MidTextureClip c = Hanging(300.f, 172.f);
	c.clipToPlanes = false;
	WallPart p0, p1;
	ComputeMiddleClip(h, h, c, p0, p1);
	EXPECT_FLOAT_EQ(300.f, p0.top);
	EXPECT_FLOAT_EQ(172.f, p0.bottom);
}

// [rc4l] A wall that pinches out is CUT SHORT, not turned into a triangle.
//
// This is the fault no ladder could see, because none of them ever compared a HORIZONTAL texture
// coordinate. GL finds where the top edge meets the bottom edge, moves that end of the wall to the
// crossing, and moves `u` with it -- so its quad is narrower than the linedef and ours was not.
// 33 pieces on dbab04, every one a wall that pinches.
TEST(WallGeom, AWallThatPinchesAtTheRightEndStopsThere)
{
	// Top runs 100 -> 0, bottom is flat at 50: they cross halfway.
	const float ztop[2]    = { 100.f, 0.f };
	const float zbottom[2] = {  50.f, 50.f };
	zx::surfaces::WallPinch p;
	ComputeWallPinch(ztop, zbottom, p);
	EXPECT_FLOAT_EQ(0.f, p.fracLeft);
	EXPECT_FLOAT_EQ(0.5f, p.fracRight);
	EXPECT_FLOAT_EQ(50.f, p.ztop[1]);     // collapsed to the crossing height...
	EXPECT_FLOAT_EQ(50.f, p.zbottom[1]);  // ...at both corners of that end
	EXPECT_FLOAT_EQ(100.f, p.ztop[0]);    // the other end is untouched
}

TEST(WallGeom, AWallThatPinchesAtTheLeftEndStartsThere)
{
	const float ztop[2]    = { 0.f, 100.f };
	const float zbottom[2] = { 50.f, 50.f };
	zx::surfaces::WallPinch p;
	ComputeWallPinch(ztop, zbottom, p);
	EXPECT_FLOAT_EQ(0.5f, p.fracLeft);
	EXPECT_FLOAT_EQ(1.f, p.fracRight);
	EXPECT_FLOAT_EQ(50.f, p.ztop[0]);
	EXPECT_FLOAT_EQ(50.f, p.zbottom[0]);
	EXPECT_FLOAT_EQ(100.f, p.ztop[1]);
}

TEST(WallGeom, AnOrdinaryWallIsNotCutAtAll)
{
	const float ztop[2]    = { 100.f, 120.f };
	const float zbottom[2] = {   0.f,  10.f };
	zx::surfaces::WallPinch p;
	ComputeWallPinch(ztop, zbottom, p);
	EXPECT_FLOAT_EQ(0.f, p.fracLeft);
	EXPECT_FLOAT_EQ(1.f, p.fracRight);
	EXPECT_FLOAT_EQ(100.f, p.ztop[0]);
	EXPECT_FLOAT_EQ(10.f, p.zbottom[1]);
}

// Parallel edges never cross, so a wall whose top and bottom slope together pinches nowhere -- and
// the division that would find the crossing must not be attempted.
TEST(WallGeom, ParallelEdgesPinchNowhere)
{
	const float ztop[2]    = { -10.f, -30.f };
	const float zbottom[2] = {   0.f, -20.f };   // same slope: always 10 below the top
	zx::surfaces::WallPinch p;
	ComputeWallPinch(ztop, zbottom, p);
	EXPECT_FLOAT_EQ(0.f, p.fracLeft);
	EXPECT_FLOAT_EQ(1.f, p.fracRight);
	EXPECT_FLOAT_EQ(-10.f, p.ztop[0]);
}
