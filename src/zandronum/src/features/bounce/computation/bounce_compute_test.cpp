// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Tests for how a bounce ends, which line a blocked move hit, and what a client leaves to the server.
#include "gtest/gtest.h"
#include "features/bounce/computation/bounce_compute.h"

#include <cmath>

namespace
{
using namespace zx::bounce;

// [Doruk-Aksoy] BounceCount 2: the first bounce leaves one, the second is the last and leaves none.
TEST(ComputeBounceSpent, CountsDownToTheLast)
{
	int count = 2;
	EXPECT_FALSE(ComputeBounceSpent(count));
	EXPECT_EQ(count, 1);
	EXPECT_TRUE(ComputeBounceSpent(count));
	EXPECT_EQ(count, 0);
}

// [Doruk-Aksoy] A spent count of 0 reads as unlimited from then on, which is why the bounce that spends it must end the actor.
TEST(ComputeBounceSpent, ZeroIsUnlimited)
{
	int count = 0;
	for (int i = 0; i < 3; i++)
		EXPECT_FALSE(ComputeBounceSpent(count));
	EXPECT_EQ(count, 0);
}

// [Doruk-Aksoy] A negative count (A_HideInCeiling's 3D floor index) is never treated as a bounce budget.
TEST(ComputeBounceSpent, NegativeIsLeftAlone)
{
	int count = -1;
	EXPECT_FALSE(ComputeBounceSpent(count));
	EXPECT_EQ(count, -1);
}

// [Doruk-Aksoy] A floors-only or ceilings-only bouncer tries too, since a ramp stops an XY move with no line to bounce off.
TEST(ComputeTriesWallBounce, AnyOfWallsFloorsCeilings)
{
	EXPECT_TRUE(ComputeTriesWallBounce(ZX_BOUNCE_Walls));
	EXPECT_TRUE(ComputeTriesWallBounce(ZX_BOUNCE_Floors));
	EXPECT_TRUE(ComputeTriesWallBounce(ZX_BOUNCE_Ceilings));
	EXPECT_FALSE(ComputeTriesWallBounce(0));
	EXPECT_FALSE(ComputeTriesWallBounce(~(ZX_BOUNCE_Walls | ZX_BOUNCE_Floors | ZX_BOUNCE_Ceilings)));
}

// [Doruk-Aksoy] Each plane asks for its own flag, so a walls-only bouncer bounces off neither.
TEST(ComputeBouncesOffPlane, EachPlaneItsOwnFlag)
{
	EXPECT_TRUE(ComputeBouncesOffPlane(ZX_BOUNCE_Floors, false));
	EXPECT_FALSE(ComputeBouncesOffPlane(ZX_BOUNCE_Floors, true));
	EXPECT_TRUE(ComputeBouncesOffPlane(ZX_BOUNCE_Ceilings, true));
	EXPECT_FALSE(ComputeBouncesOffPlane(ZX_BOUNCE_Ceilings, false));
	EXPECT_FALSE(ComputeBouncesOffPlane(ZX_BOUNCE_Walls, false));
	EXPECT_FALSE(ComputeBouncesOffPlane(ZX_BOUNCE_Walls, true));
}

// [Doruk-Aksoy] Coming down on an actor it did not bounce off, a missile explodes and anything else lands, so neither hangs there.
TEST(ComputeOnTopResult, NeverHangs)
{
	EXPECT_EQ(ComputeOnTopResult(true, true), OnTop::Bounced);
	EXPECT_EQ(ComputeOnTopResult(true, false), OnTop::Bounced);
	EXPECT_EQ(ComputeOnTopResult(false, true), OnTop::Explode);
	EXPECT_EQ(ComputeOnTopResult(false, false), OnTop::Land);
}

// [Doruk-Aksoy] Only a server-owned bouncing missile; the client's own missiles and non-bouncing ones keep their rules.
TEST(ComputeClientLeavesActorsToServer, ServerOwnedBouncingMissilesOnly)
{
	EXPECT_TRUE(ComputeClientLeavesActorsToServer(true, true, ZX_BOUNCE_Walls));
	EXPECT_FALSE(ComputeClientLeavesActorsToServer(false, true, ZX_BOUNCE_Walls));
	EXPECT_FALSE(ComputeClientLeavesActorsToServer(true, false, ZX_BOUNCE_Walls));
	EXPECT_FALSE(ComputeClientLeavesActorsToServer(true, true, 0));
}

// [Doruk-Aksoy] Moving east at 10: a north-south face takes all of it, an east-west face it slides along none, whichever way either is drawn.
TEST(ComputeHeadOn, SpeedAcrossTheLine)
{
	EXPECT_DOUBLE_EQ(ComputeHeadOn(0, 64, 10, 0), 10);
	EXPECT_DOUBLE_EQ(ComputeHeadOn(0, -64, 10, 0), 10);
	EXPECT_DOUBLE_EQ(ComputeHeadOn(64, 0, 10, 0), 0);
	EXPECT_DOUBLE_EQ(ComputeHeadOn(-64, 0, 10, 0), 0);
	EXPECT_NEAR(ComputeHeadOn(64, 64, 10, 0), 10 / std::sqrt(2.0), 1e-12);
}

// [Doruk-Aksoy] A line of no length has no direction to run into.
TEST(ComputeHeadOn, ZeroLengthLine)
{
	EXPECT_DOUBLE_EQ(ComputeHeadOn(0, 0, 10, 5), 0);
}

// [Doruk-Aksoy] The first refusing line is kept, then only a more head-on one replaces it.
TEST(ComputeKeepsRefusedLine, MoreHeadOnOnly)
{
	EXPECT_TRUE(ComputeKeepsRefusedLine(false, 0, 0));
	EXPECT_TRUE(ComputeKeepsRefusedLine(true, 0, 10));
	EXPECT_FALSE(ComputeKeepsRefusedLine(true, 10, 10));
	EXPECT_FALSE(ComputeKeepsRefusedLine(true, 10, 0));
}

// [Doruk-Aksoy] Lines stand in as ints; the refused wall wins over the divider the walk named last.
TEST(ComputeBlockedLine, RefusedLineFirst)
{
	int wall = 1, floorLine = 2, ceilingLine = 3, divider = 4;
	EXPECT_EQ(ComputeBlockedLine(&wall, &floorLine, &ceilingLine, &divider, true, true), &wall);
}

// [Doruk-Aksoy] With nothing refused, the plane the mover is outside of names the line, the floor before the ceiling.
TEST(ComputeBlockedLine, ThePlaneItIsOutsideOf)
{
	int floorLine = 2, ceilingLine = 3, divider = 4;
	EXPECT_EQ(ComputeBlockedLine<int>(nullptr, &floorLine, &ceilingLine, &divider, true, true), &floorLine);
	EXPECT_EQ(ComputeBlockedLine<int>(nullptr, &floorLine, &ceilingLine, &divider, false, true), &ceilingLine);
	EXPECT_EQ(ComputeBlockedLine<int>(nullptr, nullptr, &ceilingLine, &divider, true, true), &ceilingLine);
	EXPECT_EQ(ComputeBlockedLine<int>(nullptr, &floorLine, nullptr, &divider, false, true), &divider);
}

// [Doruk-Aksoy] Inside both planes with nothing refused, the walk's own line stands, which may be none at all.
TEST(ComputeBlockedLine, ElseTheWalksLine)
{
	int floorLine = 2, ceilingLine = 3, divider = 4;
	EXPECT_EQ(ComputeBlockedLine<int>(nullptr, &floorLine, &ceilingLine, &divider, false, false), &divider);
	EXPECT_EQ(ComputeBlockedLine<int>(nullptr, nullptr, nullptr, nullptr, true, true), nullptr);
}
}
