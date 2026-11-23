//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: law_compute_test.cpp
//
// Description: Tests for law_compute: the gravity law against the engine's
// own values, and the run step's headings, buckets, gravity runs and caps a
// tic at a time.
//
//-----------------------------------------------------------------------------

#include "law_compute.h"
#include <gtest/gtest.h>

using namespace zx::netsnapshot;

// [Kaori-Chani] A track as StraightReset leaves an id: no heading, runs at 0.
static SnapRunTrack lawtest_Fresh()
{
	SnapRunTrack t;
	t.lastDir = 0xFFFF;
	t.lastVelZ = 0;
	t.lastDVelZ = 0;
	t.straightRun = 0;
	t.gravRun = 0;
	return t;
}

static const fixed_t LAWTEST_V = 10 * FRACUNIT;

// [Kaori-Chani] AActor::GetGravity's values, including its truncation.
TEST(SnapshotLaw, GravityFollowsTheEngineLaw)
{
	static const struct { float level, sector; fixed_t cls; double want; } cases[] =
	{
		{ 800.0f, 1.0f, FRACUNIT, 65536.0 },		// stock: 1 u/t/t
		{ 800.0f, 1.0f, FRACUNIT / 4, 16384.0 },	// a class at 0.25
		{ 800.0f, 1.0f, FRACUNIT * 2, 131072.0 },
		{ 800.0f, 0.5f, FRACUNIT, 32768.0 },		// a sector at 0.5
		{ 0.0f, 1.0f, FRACUNIT, 0.0 },
		{ 801.0f, 1.0f, FRACUNIT, 65617.0 },		// 65617.92, truncated
		{ 800.0f, 0.3f, FRACUNIT, 19660.0 },		// 19660.8, truncated
	};
	for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		EXPECT_EQ(cases[i].want, ComputeGravityAccel(cases[i].level, cases[i].sector, cases[i].cls))
			<< "case " << i << ": level " << cases[i].level << ", sector " << cases[i].sector;
	}
}

// [Kaori-Chani] Still in plan: everything clears, velz kept, nothing compared.
TEST(SnapshotLaw, StandingStillClearsTheTrack)
{
	SnapRunTrack t = lawtest_Fresh();
	t.lastDir = 100; t.straightRun = 9; t.gravRun = 9; t.lastDVelZ = 5;

	const SnapRunStep s = ComputeRunStep(t, 0, 0, -3 * FRACUNIT);

	EXPECT_FALSE(s.bMoving);
	EXPECT_FALSE(s.bCompared);
	EXPECT_EQ(-1, s.endedBucket);
	EXPECT_EQ(0xFFFF, t.lastDir);
	EXPECT_EQ(0, t.straightRun);
	EXPECT_EQ(0, t.gravRun);
	EXPECT_EQ(0, t.lastDVelZ);
	EXPECT_EQ(-3 * FRACUNIT, t.lastVelZ);
}

// [Kaori-Chani] The first moving tic, east (half the BAM circle): a run of one, the gravity run
// and velz change left as they were. Held east for three more tics gives runs of 4 and of 1 + 3,
// and a turn of about 2 degrees (atan2(0.4, 10)) then ends the run of 4 in the 4-7 bucket.
TEST(SnapshotLaw, ARunStartsLengthensAndIsEndedByATurn)
{
	SnapRunTrack t = lawtest_Fresh();
	t.gravRun = 7; t.lastDVelZ = 9;

	SnapRunStep s = ComputeRunStep(t, LAWTEST_V, 0, 0);
	EXPECT_TRUE(s.bMoving);
	EXPECT_FALSE(s.bCompared);
	EXPECT_EQ(1, t.straightRun);
	EXPECT_EQ(7, t.gravRun);
	EXPECT_EQ(9, t.lastDVelZ);
	EXPECT_EQ(32767, t.lastDir);

	for (int i = 0; i < 3; ++i)
		s = ComputeRunStep(t, LAWTEST_V, 0, 0);
	EXPECT_TRUE(s.bCompared);
	EXPECT_TRUE(s.bHeld);
	EXPECT_EQ(4, t.straightRun);
	EXPECT_EQ(10, t.gravRun);
	EXPECT_EQ(-1, s.endedBucket);

	s = ComputeRunStep(t, LAWTEST_V, 4 * FRACUNIT / 10, 0);
	EXPECT_TRUE(s.bCompared);
	EXPECT_FALSE(s.bHeld);
	EXPECT_EQ(2, s.endedBucket);
	EXPECT_EQ(1, t.straightRun);
	EXPECT_EQ(1, t.gravRun);

	// [Kaori-Chani] A turn the other way, east to south: the difference is taken whole, whatever
	// its sign.
	s = ComputeRunStep(t, LAWTEST_V, 0, 0);
	s = ComputeRunStep(t, 0, -LAWTEST_V, 0);
	EXPECT_FALSE(s.bHeld);
	EXPECT_EQ(1, t.gravRun);
}

// [Kaori-Chani] A velz change over a sixteenth breaks the straight run but not the gravity run
// while it repeats, which is constant acceleration; the acceleration stopping restarts it, and
// after a held run a second difference the other way does too.
TEST(SnapshotLaw, ConstantAccelerationHoldsTheGravityRun)
{
	SnapRunTrack t = lawtest_Fresh();
	const fixed_t g = FRACUNIT / 2;

	ComputeRunStep(t, LAWTEST_V, 0, 0);
	SnapRunStep s = ComputeRunStep(t, LAWTEST_V, 0, -g);
	EXPECT_FALSE(s.bHeld);
	EXPECT_EQ(0, s.endedBucket);
	EXPECT_EQ(-g, t.lastDVelZ);

	s = ComputeRunStep(t, LAWTEST_V, 0, -2 * g);
	EXPECT_FALSE(s.bHeld);
	EXPECT_EQ(2, t.gravRun);
	EXPECT_EQ(1, t.straightRun);

	s = ComputeRunStep(t, LAWTEST_V, 0, -2 * g);
	EXPECT_EQ(1, t.gravRun);

	ComputeRunStep(t, LAWTEST_V, 0, -2 * g);
	ComputeRunStep(t, LAWTEST_V, 0, -2 * g);
	s = ComputeRunStep(t, LAWTEST_V, 0, -3 * g);
	EXPECT_EQ(1, t.gravRun);
}

// [Kaori-Chani] A sixteenth exactly still holds; the second difference past a quarter restarts
// the gravity run.
TEST(SnapshotLaw, ASixteenthHoldsAndAQuarterRestartsTheGravityRun)
{
	SnapRunTrack t = lawtest_Fresh();

	ComputeRunStep(t, LAWTEST_V, 0, 0);
	SnapRunStep s = ComputeRunStep(t, LAWTEST_V, 0, SNAP_STRAIGHT_ZTOL);
	EXPECT_TRUE(s.bHeld);
	EXPECT_EQ(2, t.straightRun);

	s = ComputeRunStep(t, LAWTEST_V, 0,
		SNAP_STRAIGHT_ZTOL + SNAP_STRAIGHT_ZTOL + SNAP_GRAV_ZTOL + 1);
	EXPECT_FALSE(s.bHeld);
	EXPECT_EQ(1, t.gravRun);
}

// [Kaori-Chani] Each ended run reports the bucket its length falls in.
TEST(SnapshotLaw, EachEndedRunReportsItsBucket)
{
	static const struct { BYTE run; int bucket; } ends[] =
	{
		{ 1, 0 }, { 2, 1 }, { 3, 1 }, { 4, 2 }, { 7, 2 }, { 8, 3 }, { 15, 3 }, { 16, 4 }, { 255, 4 },
	};
	for (unsigned int i = 0; i < sizeof(ends) / sizeof(ends[0]); ++i)
	{
		SnapRunTrack t = lawtest_Fresh();
		t.lastDir = 32767;
		t.straightRun = ends[i].run;
		const SnapRunStep s = ComputeRunStep(t, 0, LAWTEST_V, 0);	// north: a quarter turn
		EXPECT_EQ(ends[i].bucket, s.endedBucket) << "a run of " << (int)ends[i].run;
	}
}

TEST(SnapshotLaw, BothRunsStopAt255)
{
	SnapRunTrack t = lawtest_Fresh();
	t.lastDir = 32767; t.straightRun = 254; t.gravRun = 254;

	SnapRunStep s = ComputeRunStep(t, LAWTEST_V, 0, 0);
	EXPECT_TRUE(s.bHeld);
	EXPECT_EQ(255, t.straightRun);
	EXPECT_EQ(255, t.gravRun);

	s = ComputeRunStep(t, LAWTEST_V, 0, 0);
	EXPECT_TRUE(s.bHeld);
	EXPECT_EQ(255, t.straightRun);
	EXPECT_EQ(255, t.gravRun);
}

// [Kaori-Chani] A heading held across the wrap just either side of west stays held.
TEST(SnapshotLaw, AHeadingHeldAcrossTheWrapStaysHeld)
{
	SnapRunTrack t = lawtest_Fresh();
	ComputeRunStep(t, -LAWTEST_V, 1, 0);			// just below west: near 65535
	const WORD high = t.lastDir;
	SnapRunStep s = ComputeRunStep(t, -LAWTEST_V, -1, 0);	// just above: near 0
	EXPECT_GT(high, 65000);
	EXPECT_LT(t.lastDir, 500);
	EXPECT_TRUE(s.bHeld);

	t = lawtest_Fresh();
	ComputeRunStep(t, -LAWTEST_V, -1, 0);
	s = ComputeRunStep(t, -LAWTEST_V, 1, 0);		// and back across the other way
	EXPECT_TRUE(s.bHeld);
}

// [Kaori-Chani] Due west lands at 65534, not the 0xFFFF that means not moving.
TEST(SnapshotLaw, DueWestIsNotTheNotMovingHeading)
{
	SnapRunTrack t = lawtest_Fresh();
	SnapRunStep s;
	for (int i = 0; i < 3; ++i)
		s = ComputeRunStep(t, -LAWTEST_V, 0, 0);
	EXPECT_EQ(65534, t.lastDir);
	EXPECT_EQ(3, t.straightRun);
	EXPECT_TRUE(s.bHeld);
}
