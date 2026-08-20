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

#include "features/levelmesh/computation/wallgeom_compute.h"

using namespace zx::levelmesh;

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
