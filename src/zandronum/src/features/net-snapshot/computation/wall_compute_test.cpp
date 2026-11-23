// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: wall_compute_test.cpp
//
// Description: Tests for wall_compute: the box sweep against a line, a path's
// reach and trace from a pose, where that path first touches a line, and
// PIT_CheckLine's flags with P_TryMove's opening.
//
//-----------------------------------------------------------------------------

#include "wall_compute.h"
#include <gtest/gtest.h>

#include <math.h>
#include <string.h>

#include "doomdef.h"		// TICRATE
#include "features/net-snapshot/format.h"

using namespace zx::netsnapshot;

// [Kaori-Chani] A rocket's box 10 degrees off a wall at x = 100.
static const double WALLTEST_RAD = 10.0 * 3.14159265358979 / 180.0;

TEST(SnapshotWall, SweepTouchesALongWall)
{
	const double sn = sin(WALLTEST_RAD), cs = cos(WALLTEST_RAD);
	double s = 0.0;
	EXPECT_TRUE(ComputeSweepLine(0.0, 0.0, sn, cs, 11.0, 100.0, -1000.0, 100.0, 1000.0, s));
	EXPECT_NEAR(89.0 / sn, s, 0.01);
}

TEST(SnapshotWall, SweepPassesAJamb)
{
	const double sn = sin(WALLTEST_RAD), cs = cos(WALLTEST_RAD);
	double s = 0.0;
	EXPECT_FALSE(ComputeSweepLine(0.0, 0.0, sn, cs, 11.0, 100.0, -1000.0, 100.0, 0.0, s));
}

TEST(SnapshotWall, SweepTakesASegmentsEdgeAndMissesPastIt)
{
	double s = 0.0;
	EXPECT_TRUE(ComputeSweepLine(0.0, 0.0, 1.0, 0.0, 11.0, 100.0, 5.0, 100.0, 50.0, s));
	EXPECT_NEAR(89.0, s, 0.01);
	EXPECT_FALSE(ComputeSweepLine(0.0, 0.0, 1.0, 0.0, 11.0, 100.0, 20.0, 100.0, 50.0, s));
}

// [Kaori-Chani] The same segment drawn the other way, and one of no length.
TEST(SnapshotWall, SweepIgnoresSegmentDirectionAndRejectsZeroLength)
{
	double s = 0.0;
	EXPECT_TRUE(ComputeSweepLine(0.0, 0.0, 1.0, 0.0, 11.0, 100.0, 50.0, 100.0, 5.0, s));
	EXPECT_NEAR(89.0, s, 0.01);
	EXPECT_FALSE(ComputeSweepLine(0.0, 0.0, 1.0, 0.0, 11.0, 100.0, 5.0, 100.0, 5.0, s));
}

TEST(SnapshotWall, SweepTouchesASlantedWallEitherWayRound)
{
	double s = 0.0;
	EXPECT_TRUE(ComputeSweepLine(0.0, 0.0, 1.0, 0.0, 11.0, 150.0, -50.0, 100.0, 50.0, s));
	EXPECT_TRUE(ComputeSweepLine(0.0, 0.0, 1.0, 0.0, 11.0, 100.0, 50.0, 150.0, -50.0, s));
}

TEST(SnapshotWall, AStandingBodyHasNoPath)
{
	SnapWallPath p;
	EXPECT_FALSE(ComputeWallPath(0, 0, 500, 16 * FRACUNIT, 0, 0, p));
}

// [Kaori-Chani] 3, 4 units a tic is 5 a tic; 500 ms is 17.5 tics.
TEST(SnapshotWall, APathMeasuresItsSpeedDirectionReachAndTrace)
{
	SnapWallPath p;
	ASSERT_TRUE(ComputeWallPath(3 * FRACUNIT, 4 * FRACUNIT, 500, 16 * FRACUNIT,
		10 * FRACUNIT, -10 * FRACUNIT, p));
	EXPECT_NEAR(5.0, p.speed, 1e-9);
	EXPECT_NEAR(0.6, p.dx, 1e-9);
	EXPECT_NEAR(0.8, p.dy, 1e-9);
	EXPECT_NEAR(87.5, p.reach, 1e-9);
	EXPECT_NEAR(87.5 + 16.0 * 1.41421356, p.traceLen, 1e-9);
	EXPECT_EQ(16.0, p.r);
	EXPECT_EQ(10.0, p.ox);
	EXPECT_EQ(-10.0, p.oy);
}

TEST(SnapshotWall, TheLinesTreatABodyAsAProjectileOnlyWhenItIsOne)
{
	EXPECT_TRUE(ComputeWallProjectile(true, false, true, 0));
	EXPECT_TRUE(ComputeWallProjectile(false, true, true, 0));
	EXPECT_TRUE(ComputeWallProjectile(false, false, false, SNAP_MISC_BALLISTIC));
	EXPECT_FALSE(ComputeWallProjectile(false, false, true, SNAP_MISC_BALLISTIC));
	EXPECT_FALSE(ComputeWallProjectile(false, false, false, 0));
}

// [Kaori-Chani] 7 units a tic for 400 ms reaches 98 units, a box of half-width 10.
static SnapWallPath walltest_Path()
{
	SnapWallPath p;
	ComputeWallPath(7 * FRACUNIT, 0, 400, 10 * FRACUNIT, 0, 0, p);
	return p;
}

// [Kaori-Chani] A wall at x = 50 is touched at 40, its opening point level with the centre, the
// height on its arc.
TEST(SnapshotWall, ContactTouchesAWallAndTakesTheHeightOnItsArc)
{
	const SnapWallPath p = walltest_Path();
	SnapWallContact c;
	ASSERT_TRUE(ComputeWallContact(p, 50.0, -100.0, 0.0, 200.0, 0.4, -1.0, 8.0, 2.0, -1.0, c));
	EXPECT_NEAR(40.0, c.s, 1e-6);
	EXPECT_NEAR(50.0, c.px, 1e-9);
	EXPECT_NEAR(0.0, c.py, 1e-9);
	EXPECT_NEAR(40.0, c.cx, 1e-6);
	EXPECT_NEAR(0.0, c.cy, 1e-9);
	const double t = 40.0 / 7.0;
	EXPECT_NEAR(8.0 + 2.0 * t - 0.5 * t * t, c.z, 1e-6);
}

// [Kaori-Chani] No nearer than a line already found, or past the reach, or no line at all.
TEST(SnapshotWall, ContactIgnoresWhatIsNoNearerOrOutOfReach)
{
	const SnapWallPath p = walltest_Path();
	SnapWallContact c;
	EXPECT_FALSE(ComputeWallContact(p, 50.0, -100.0, 0.0, 200.0, 0.4, 40.0, 0.0, 0.0, 0.0, c));
	EXPECT_TRUE(ComputeWallContact(p, 50.0, -100.0, 0.0, 200.0, 0.4, 41.0, 0.0, 0.0, 0.0, c));
	EXPECT_FALSE(ComputeWallContact(p, 200.0, -100.0, 0.0, 200.0, 0.4, -1.0, 0.0, 0.0, 0.0, c));
	EXPECT_FALSE(ComputeWallContact(p, 50.0, 0.0, 0.0, 0.0, 0.4, -1.0, 0.0, 0.0, 0.0, c));
}

// [Kaori-Chani] A segment the box passes takes the centre's crossing, its opening point clamped
// to the segment's end, whichever end it is seen from and whether it is reached or not.
TEST(SnapshotWall, ContactClampsTheOpeningPointToTheSegment)
{
	const SnapWallPath p = walltest_Path();
	SnapWallContact c;

	ASSERT_TRUE(ComputeWallContact(p, 50.0, 20.0, 0.0, 20.0, 0.3, -1.0, 0.0, 0.0, 0.0, c));
	EXPECT_NEAR(0.3 * p.traceLen, c.s, 1e-9);
	EXPECT_NEAR(20.0, c.py, 1e-9);

	// Seen from the far end, the clamp is to the other end.
	ASSERT_TRUE(ComputeWallContact(p, 50.0, -20.0, 0.0, -20.0, 0.3, -1.0, 0.0, 0.0, 0.0, c));
	EXPECT_NEAR(-20.0, c.py, 1e-9);

	// Short of the segment's start, the clamp is to its far end.
	ASSERT_TRUE(ComputeWallContact(p, 50.0, -40.0, 0.0, 20.0, 0.3, -1.0, 0.0, 0.0, 0.0, c));
	EXPECT_NEAR(-20.0, c.py, 1e-9);
}

// [Kaori-Chani] A line the box already overlaps is touched at once.
TEST(SnapshotWall, ContactWithAnOverlappedLineIsImmediate)
{
	const SnapWallPath p = walltest_Path();
	SnapWallContact c;
	ASSERT_TRUE(ComputeWallContact(p, 2.0, -100.0, 0.0, 200.0, 0.0, -1.0, 0.0, 0.0, 0.0, c));
	EXPECT_EQ(0.0, c.s);
	ASSERT_TRUE(ComputeWallContact(p, 9.5, -100.0, 0.0, 200.0, 0.0, -1.0, 0.0, 0.0, 0.0, c));
	EXPECT_EQ(0.0, c.s);
}

// [Kaori-Chani] One touched exactly at the reach still counts.
TEST(SnapshotWall, ContactExactlyAtTheReachCounts)
{
	const SnapWallPath p = walltest_Path();
	SnapWallContact c;
	ASSERT_TRUE(ComputeWallContact(p, 108.0, -100.0, 0.0, 200.0, 0.0, -1.0, 0.0, 0.0, 0.0, c));
	EXPECT_NEAR(98.0, c.s, 1e-6);
}

// [Kaori-Chani] A body: what blocks it, monsters-only and floaters' lines, and a railing raising
// the opening. The body's flags carry from one step to the next, as the engine's would.
TEST(SnapshotWall, LineFlagsBlockABody)
{
	SnapBodyFlags b;
	memset(&b, 0, sizeof(b));
	b.height = 56.0;
	b.maxStep = 24.0;
	SnapLineFlags l;
	bool bRail = true;

	memset(&l, 0, sizeof(l));
	EXPECT_FALSE(ComputeLineFlagsBlock(l, b, false, bRail)) << "a plain line";
	EXPECT_FALSE(bRail) << "a plain line raises no railing";

	l.bBlocking = true;
	EXPECT_TRUE(ComputeLineFlagsBlock(l, b, false, bRail)) << "a blocking line";

	memset(&l, 0, sizeof(l));
	l.bBlockEverything = true;
	EXPECT_TRUE(ComputeLineFlagsBlock(l, b, false, bRail)) << "block everything";

	memset(&l, 0, sizeof(l));
	l.bBlockMonsters = true;
	EXPECT_TRUE(ComputeLineFlagsBlock(l, b, false, bRail)) << "block monsters";
	b.bNoBlockMonst = true;
	EXPECT_FALSE(ComputeLineFlagsBlock(l, b, false, bRail)) << "block monsters, but not this one";

	memset(&l, 0, sizeof(l));
	l.bBlockFloaters = true;
	EXPECT_FALSE(ComputeLineFlagsBlock(l, b, false, bRail)) << "block floaters, not a floater";
	b.bFloat = true;
	EXPECT_TRUE(ComputeLineFlagsBlock(l, b, false, bRail)) << "block floaters, a floater";

	memset(&l, 0, sizeof(l));
	EXPECT_FALSE(ComputeLineFlagsBlock(l, b, false, bRail));
	l.bRailing = true;
	l.bBlocking = true;
	EXPECT_FALSE(ComputeLineFlagsBlock(l, b, false, bRail)) << "a railing does not block";
	EXPECT_TRUE(bRail) << "a railing raises the opening";
}

// [Kaori-Chani] A projectile passes what does not name projectiles or everything. The body is as
// the flag walk above leaves it at this point: a floater that monsters' lines do not stop.
TEST(SnapshotWall, LineFlagsBlockAProjectile)
{
	SnapBodyFlags b;
	memset(&b, 0, sizeof(b));
	b.height = 56.0;
	b.maxStep = 24.0;
	b.bFloat = true;
	b.bNoBlockMonst = false;
	SnapLineFlags l;
	bool bRail = true;

	memset(&l, 0, sizeof(l));
	l.bBlocking = true;
	l.bBlockMonsters = true;
	EXPECT_FALSE(ComputeLineFlagsBlock(l, b, true, bRail)) << "blocking and monsters, a projectile";
	l.bBlockProjectile = true;
	EXPECT_TRUE(ComputeLineFlagsBlock(l, b, true, bRail));

	memset(&l, 0, sizeof(l));
	l.bBlockProjectile = true;
	EXPECT_TRUE(ComputeLineFlagsBlock(l, b, true, bRail));

	memset(&l, 0, sizeof(l));
	l.bBlockEverything = true;
	EXPECT_TRUE(ComputeLineFlagsBlock(l, b, true, bRail));
}

// [Kaori-Chani] The opening: too short, a head above, and each kind's steps.
TEST(SnapshotWall, TheOpeningBlocksByHeightAndStep)
{
	SnapBodyFlags b;
	memset(&b, 0, sizeof(b));
	b.height = 56.0;
	b.maxStep = 24.0;

	EXPECT_TRUE(ComputeOpeningBlocks(b, 100.0, 50.0, 50.0)) << "too short an opening";
	EXPECT_FALSE(ComputeOpeningBlocks(b, 106.0, 50.0, 50.0));
	EXPECT_TRUE(ComputeOpeningBlocks(b, 100.0, 0.0, 50.0)) << "a head above the opening";
	EXPECT_FALSE(ComputeOpeningBlocks(b, 106.0, 0.0, 50.0));

	b.bCeilingHugger = true;
	EXPECT_FALSE(ComputeOpeningBlocks(b, 100.0, 0.0, 50.0)) << "a ceiling hugger";
	b.bCeilingHugger = false;

	EXPECT_FALSE(ComputeOpeningBlocks(b, 200.0, 24.0, 0.0)) << "a step it can take";
	EXPECT_TRUE(ComputeOpeningBlocks(b, 200.0, 25.0, 0.0)) << "a step too high";

	b.bMissile = true;
	EXPECT_TRUE(ComputeOpeningBlocks(b, 200.0, 1.0, 0.0)) << "a missile takes no step";
	EXPECT_FALSE(ComputeOpeningBlocks(b, 200.0, 0.0, 0.0));

	b.bStepMissile = true;
	EXPECT_FALSE(ComputeOpeningBlocks(b, 200.0, 24.0, 0.0)) << "a stepping missile";
	EXPECT_TRUE(ComputeOpeningBlocks(b, 200.0, 25.0, 0.0));

	b.bFloorHugger = true;
	EXPECT_FALSE(ComputeOpeningBlocks(b, 200.0, 100.0, 0.0)) << "a floor hugger takes any step";
}
