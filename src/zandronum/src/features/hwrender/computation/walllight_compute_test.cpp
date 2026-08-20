// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The shipped fault these exist for: firing a plasma rifle at a wall and stopping left a blue
// pool on that wall permanently, in GL only, with the scorch sitting unlit in the middle of it.
// Every instrument said there was nothing there -- 0 active lights, 0 linked light nodes, and with
// every decal on the map destroyed the pool was still there -- because the light really was gone.
// What survived was a wall still holding the buffer index it had while the light was alive.
//
// Measured on dbab04 as +1.55 blue on wall the marks do not cover, unchanged 400 tics later, and
// zero the moment any other weapon fired a shot.

#include <gtest/gtest.h>

#include "features/hwrender/computation/walllight_compute.h"

using namespace zx::hwrender;

// The bug itself. A wall replayed from the cache is holding index 40 from the frame the bolt was in
// flight; this frame there are no lights at all, so the pass that fills the index does not run.
TEST(WallLight, ACarriedIndexIsNeverApplied)
{
	EXPECT_EQ(kNoWallLightIndex, ComputeWallLightIndex(false, 40, 40));
}

// And it stays wrong for as long as nobody fires: every later frame asks the same question.
TEST(WallLight, StillNotAppliedManyFramesLater)
{
	int carried = 40;
	for (int frame = 0; frame < 500; frame++)
	{
		const int use = ComputeWallLightIndex(false, kNoWallLightIndex, carried);
		EXPECT_EQ(kNoWallLightIndex, use);
		carried = use;   // whatever the wall keeps, it is not allowed to resurrect the light
	}
}

// The pass ran and found nothing: an empty upload is -1, and that is what the wall applies.
TEST(WallLight, RanAndFoundNothing)
{
	EXPECT_EQ(kNoWallLightIndex, ComputeWallLightIndex(true, -1, 40));
}

// The pass ran and found lights: that index, this frame, is the one thing that may be used.
TEST(WallLight, RanAndFoundLights)
{
	EXPECT_EQ(12, ComputeWallLightIndex(true, 12, -1));
	EXPECT_EQ(0, ComputeWallLightIndex(true, 0, 999));
}

// Index zero is a real index, not an absence. A rule written with `if (index)` would drop the first
// upload of every frame, which is the light nearest the camera more often than not.
TEST(WallLight, ZeroIsARealIndex)
{
	EXPECT_EQ(0, ComputeWallLightIndex(true, 0, -1));
	EXPECT_NE(kNoWallLightIndex, ComputeWallLightIndex(true, 0, -1));
}

// What the fix has to be worth: firing again refreshes it, which is exactly how a player stumbled
// into clearing the stuck pool. That must be the ONLY thing that changes a wall's lights -- not the
// wall being redrawn, not it being replayed from the cache.
TEST(WallLight, TheNextShotRefreshesIt)
{
	const int stuck = ComputeWallLightIndex(false, kNoWallLightIndex, 40);
	EXPECT_EQ(kNoWallLightIndex, stuck);
	EXPECT_EQ(7, ComputeWallLightIndex(true, 7, stuck));
}
