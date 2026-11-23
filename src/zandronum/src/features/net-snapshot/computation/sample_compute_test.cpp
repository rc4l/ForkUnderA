// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: sample_compute_test.cpp
//
// Description: Tests for sample_compute: the fraction and curve between two
// dated records, the ages every sample stamps, and each exit a ghost takes --
// held, run on its wire velocity, ballistic under a gravity law, or drawn
// between two records dated ahead of now.
//
//-----------------------------------------------------------------------------

#include "sample_compute.h"
#include <gtest/gtest.h>

#include <math.h>
#include <string.h>

using namespace zx::netsnapshot;

// [Kaori-Chani] FRACUNIT as a double: on 48.16 it is a Fixed, and double * Fixed is deleted,
// so the arithmetic below states that it is deliberately floating point.
static const double SNAPTEST_FU = (double)FRACUNIT;

// [Kaori-Chani] A fixed_t reading is compared to within a unit, as the engine's own arithmetic is.
#define EXPECT_FIXED_NEAR(want, got)	EXPECT_NEAR((double)(want), (double)(got), 1.0)

// [Kaori-Chani] A record at `tic`, arriving at `ms`, at (x, y, z) units moving (vx, vy, vz)
// quarter-units per tic.
static SnapGhostSample sampletest_Record(int tic, unsigned int ms, SDWORD x, SDWORD y, SDWORD z,
	SWORD vx, SWORD vy, SWORD vz, WORD misc)
{
	SnapGhostSample s;
	memset(&s, 0, sizeof(s));
	s.state.classId = 7;
	s.state.qx = x;
	s.state.qy = y;
	s.state.qz = z;
	s.state.qvx = vx;
	s.state.qvy = vy;
	s.state.qvz = vz;
	s.state.qtics = 3;
	s.state.qstate = 12;
	s.state.misc = misc;
	s.wallMS = ms;
	s.serverTic = tic;
	s.gameTic = tic;
	s.posMS = ms;
	s.posTic = tic;
	s.appearTic = tic;
	return s;
}

// [Kaori-Chani] A ghost over `hist`, its newest at index `head`, undated and drawn from its
// arrival times.
static SnapSampleGhost sampletest_Ghost(const SnapGhostSample* hist, unsigned int head,
	unsigned int count)
{
	SnapSampleGhost g;
	g.hist = hist;
	g.head = head;
	g.count = count;
	g.enterMS = 0;
	g.eventSeq = 5;
	g.accelZ = 0.0;
	g.velZTic = hist[head].posTic;
	g.bOther = true;
	g.newestAt = hist[head].wallMS;
	g.posAt = hist[head].posMS;
	g.bDated = false;
	return g;
}

static SnapSampleContext sampletest_Context()
{
	SnapSampleContext c;
	c.clock = NULL;
	c.bHaveServerTic = true;
	c.newestServerTic = 110;
	c.newestGameTic = 112;
	c.missileCapMS = 500;
	c.lawGravity = NULL;
	return c;
}

static bool sampletest_LawYes(unsigned int classId, fixed_t, fixed_t, double& g, bool& bOldZ)
{
	g = 2.0 * SNAPTEST_FU;
	bOldZ = (classId == 8);
	return true;
}

static bool sampletest_LawNo(unsigned int, fixed_t, fixed_t, double&, bool&)
{
	return false;
}

// [Kaori-Chani] The fraction between the two records either side of now. Written so a NaN fails.
TEST(SnapshotSample, TheAheadFractionSitsBetweenTheTwoDates)
{
	static const struct { double older, newer, now, want; } cases[] =
	{
		{ 1000.0, 1028.5, 1000.0, 0.0 },	// at the older
		{ 1000.0, 1028.5, 1014.25, 0.5 },	// halfway
		{ 1000.0, 1028.5, 1028.5, 1.0 },	// at the newer
		{ 1000.0, 1028.5, 990.0, 0.0 },		// before the older: held to it
		{ 1000.0, 1028.5, 1040.0, 1.0 },	// past the newer: held to it
		{ 1000.0, 1000.0, 1000.0, 1.0 },	// one date: the newer
	};
	for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		EXPECT_TRUE(fabs(ComputeAheadFraction(cases[i].older, cases[i].newer, cases[i].now)
			- cases[i].want) <= 1e-9) << "case " << i;
	}
}

// [Kaori-Chani] Between two records through both velocities; z on a gravity arc is
// 10 + 5t - t*t/2 over 14 tics, which a straight line misses by 18 u at 0.75.
TEST(SnapshotSample, TheCurveRunsThroughBothVelocities)
{
	static const struct { double p0, v0, p1, v1, span, f, want; } cases[] =
	{
		{ 100.0, 7.0, 198.0, 7.0, 14.0, 0.5, 149.0 },	// straight
		{ 10.0, 5.0, -18.0, -9.0, 14.0, 0.75, 7.375 },	// the arc
		{ 10.0, 5.0, -18.0, -9.0, 14.0, 0.0, 10.0 },	// meets the older
		{ 0.0, 10.0, 0.0, -10.0, 2.0, 1.0, 0.0 },		// a bounce meets the newer
	};
	for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		EXPECT_TRUE(fabs(ComputeHermite(cases[i].p0, cases[i].v0, cases[i].p1, cases[i].v1,
			cases[i].span, cases[i].f) - cases[i].want) <= 1e-9) << "case " << i;
	}
}

TEST(SnapshotSample, FlightJoinsWithinReachOfItsSpeed)
{
	static const struct { double dist, va, vb, span; bool want; } cases[] =
	{
		{ 400.0, 20.0, 20.0, 15.0, true },		// a rocket over 15 tics
		{ 632.0, 20.0, 0.0, 15.0, true },		// the limit
		{ 632.0, 0.0, 20.0, 15.0, true },		// the faster speed is either
		{ 633.0, 0.0, 20.0, 15.0, false },		// past it
		{ 30.0, 0.0, 0.0, 1.0, true },			// at rest, within 32 units
		{ 2000.0, 25.0, 25.0, 2.0, false },		// a teleport
	};
	for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		EXPECT_EQ(cases[i].want,
			ComputeFlightJoins(cases[i].dist, cases[i].va, cases[i].vb, cases[i].span))
			<< "case " << i;
	}
}

TEST(SnapshotSample, TicBucketTakesItsEdges)
{
	static const struct { int tics, want; } cases[] =
	{
		{ 0, 0 }, { 3, 0 }, { 4, 1 }, { 8, 1 }, { 9, 2 }, { 17, 2 }, { 18, 3 }, { 35, 3 },
		{ 36, 4 }, { 4000, 4 },
	};
	for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
		EXPECT_EQ(cases[i].want, ComputeTicBucket(cases[i].tics)) << cases[i].tics << " tics";
}

// [Kaori-Chani] Two records at rest, the newer entering at 1200.
static void sampletest_Held(SnapGhostSample* h, SnapSampleGhost& g)
{
	memset(h, 0, sizeof(SnapGhostSample) * CLENT_GHOST_HISTORY);
	h[0] = sampletest_Record(100, 1000, 10, 20, 30, 0, 0, 0, 0);
	h[1] = sampletest_Record(104, 1100, 10, 20, 30, 0, 0, 0, 0);
	h[1].appearTic = 101;
	g = sampletest_Ghost(h, 1, 2);
	g.enterMS = 1200;
}

// [Kaori-Chani] Before its enter: absent, with its ages and tics already stamped.
TEST(SnapshotSample, ABsentBeforeItsEnterButItsAgesAreStamped)
{
	SnapGhostSample h[CLENT_GHOST_HISTORY];
	SnapSampleGhost g;
	sampletest_Held(h, g);
	const SnapSampleContext c = sampletest_Context();
	SnapGhostPose p;
	memset(&p, 0, sizeof(p));

	const SnapSampleOut o = ComputeSample(g, c, 1150, p);
	EXPECT_EQ(SNAP_SAMPLE_ABSENT, o.exit);
	EXPECT_EQ(50u, p.ageMS);
	EXPECT_EQ(50u, p.arrivalAgeMS);
	EXPECT_EQ(6, p.ageTics);
	EXPECT_EQ(11, p.appearAgeTics);
	EXPECT_EQ(104, p.serverTic);
	EXPECT_EQ(4, p.posGapTics);
	EXPECT_EQ(5u, p.eventSeq);
	EXPECT_EQ(3, p.tics);
	EXPECT_EQ(12, p.stateIdx);
	EXPECT_FALSE(p.dated);

	g.bDated = true;
	ComputeSample(g, c, 1150, p);
	EXPECT_TRUE(p.dated);
}

TEST(SnapshotSample, DrawnFromTheMomentItEntered)
{
	SnapGhostSample h[CLENT_GHOST_HISTORY];
	SnapSampleGhost g;
	sampletest_Held(h, g);
	const SnapSampleContext c = sampletest_Context();
	SnapGhostPose p;
	memset(&p, 0, sizeof(p));
	EXPECT_EQ(SNAP_SAMPLE_HELD, ComputeSample(g, c, 1200, p).exit);
}

// [Kaori-Chani] Held at rest, then moving; dated ages come from the caller, and render times
// before them read 0.
TEST(SnapshotSample, HeldAtRestThenMoving)
{
	SnapGhostSample h[CLENT_GHOST_HISTORY];
	SnapSampleGhost g;
	sampletest_Held(h, g);
	const SnapSampleContext c = sampletest_Context();
	SnapGhostPose p;
	memset(&p, 0, sizeof(p));

	g.enterMS = 0;
	g.newestAt = 1300;
	SnapSampleOut o = ComputeSample(g, c, 1200, p);
	EXPECT_EQ(SNAP_SAMPLE_HELD, o.exit);
	EXPECT_EQ(0u, p.ageMS);
	EXPECT_EQ(100u, p.arrivalAgeMS);
	EXPECT_EQ(10 * FRACUNIT, p.x);
	EXPECT_FALSE(p.extrapolated);

	h[1].state.qx = 14;
	g.newestAt = h[1].wallMS;
	o = ComputeSample(g, c, 1200, p);
	EXPECT_EQ(SNAP_SAMPLE_HELDMOVE, o.exit);
	EXPECT_EQ(14 * FRACUNIT, p.x);
}

// [Kaori-Chani] Moving in y alone, then in z alone, is moving too; and two records on one
// arrival give no velocity to call movement.
TEST(SnapshotSample, MovingInOneAxisAloneStillCountsAsMoving)
{
	SnapGhostSample h[CLENT_GHOST_HISTORY];
	SnapSampleGhost g;
	sampletest_Held(h, g);
	const SnapSampleContext c = sampletest_Context();
	SnapGhostPose p;
	memset(&p, 0, sizeof(p));
	g.enterMS = 0;

	h[1].state.qy = 21;
	EXPECT_EQ(SNAP_SAMPLE_HELDMOVE, ComputeSample(g, c, 1200, p).exit) << "y alone";
	h[1].state.qy = 20;
	h[1].state.qz = 29;
	EXPECT_EQ(SNAP_SAMPLE_HELDMOVE, ComputeSample(g, c, 1200, p).exit) << "z alone";
	h[1].state.qz = 30;
	h[1].state.qx = 14;

	h[1].wallMS = h[0].wallMS;
	EXPECT_EQ(SNAP_SAMPLE_HELD, ComputeSample(g, c, 1200, p).exit) << "both on one arrival";
}

// [Kaori-Chani] One sample holds whatever it is, and its position gap stays 1.
TEST(SnapshotSample, OneSampleHoldsWhateverItIs)
{
	SnapGhostSample h[CLENT_GHOST_HISTORY];
	SnapSampleGhost g;
	sampletest_Held(h, g);
	const SnapSampleContext c = sampletest_Context();
	SnapGhostPose p;
	memset(&p, 0, sizeof(p));
	g.enterMS = 0;
	g.count = 1;

	const SnapSampleOut o = ComputeSample(g, c, 1200, p);
	EXPECT_EQ(SNAP_SAMPLE_HELDNEW, o.exit);
	EXPECT_EQ(1, p.posGapTics);
}

// [Kaori-Chani] No server tic yet, or tics not past the record's: no age in tics; a gap past
// 255 tics reads 255.
TEST(SnapshotSample, AgesInTicsNeedAServerTicAndCapAt255)
{
	SnapGhostSample h[CLENT_GHOST_HISTORY];
	SnapSampleGhost g;
	sampletest_Held(h, g);
	SnapGhostPose p;
	memset(&p, 0, sizeof(p));
	g.enterMS = 0;

	SnapSampleContext n = sampletest_Context();
	n.bHaveServerTic = false;
	h[0].posTic = -400;
	ComputeSample(g, n, 1200, p);
	EXPECT_EQ(0, p.ageTics);
	EXPECT_EQ(0, p.appearAgeTics);
	EXPECT_EQ(255, p.posGapTics);

	n.bHaveServerTic = true;
	n.newestServerTic = 104;
	n.newestGameTic = 101;
	h[0].posTic = 103;
	ComputeSample(g, n, 1200, p);
	EXPECT_EQ(0, p.ageTics);
	EXPECT_EQ(0, p.appearAgeTics);
	EXPECT_EQ(1, p.posGapTics);

	h[0].posTic = 102;
	ComputeSample(g, n, 1200, p);
	EXPECT_EQ(2, p.posGapTics);
}

// [Kaori-Chani] A steering missile at (100, 200, 300) moving 2, -1, 1 units a tic.
static void sampletest_Vel(SnapGhostSample* h, SnapSampleGhost& g)
{
	memset(h, 0, sizeof(SnapGhostSample) * CLENT_GHOST_HISTORY);
	h[0] = sampletest_Record(100, 1000, 0, 0, 0, 0, 0, 0, SNAP_MISC_STEERING);
	h[1] = sampletest_Record(104, 1100, 100, 200, 300, 8, -4, 4, SNAP_MISC_STEERING);
	g = sampletest_Ghost(h, 1, 2);
	g.accelZ = -1.0 * SNAPTEST_FU;
}

// [Kaori-Chani] 2 tics on (70 ms): x by 2 * 2 units, y by 2 * -1, z with the measured
// acceleration while it moves in z.
TEST(SnapshotSample, AMissileRunsOnItsWireVelocity)
{
	SnapGhostSample h[CLENT_GHOST_HISTORY];
	SnapSampleGhost g;
	sampletest_Vel(h, g);
	const SnapSampleContext c = sampletest_Context();
	SnapGhostPose p;

	const SnapSampleOut o = ComputeSample(g, c, 1170, p);
	const double t = 70.0 * TICRATE / 1000.0;
	EXPECT_EQ(SNAP_SAMPLE_EXTRAPVEL, o.exit);
	EXPECT_FALSE(o.bVelCapped);
	EXPECT_FALSE(o.bVelCell);
	EXPECT_TRUE(p.extrapolated);
	EXPECT_EQ(CLENT_VELEXTRAP_MS, p.exCapMS);
	EXPECT_FIXED_NEAR((100 + 2.0 * t) * SNAPTEST_FU, p.x);
	EXPECT_FIXED_NEAR((200 - 1.0 * t) * SNAPTEST_FU, p.y);
	EXPECT_FIXED_NEAR((300 + 1.0 * t) * SNAPTEST_FU - 0.5 * SNAPTEST_FU * t * t, p.z);
	EXPECT_EQ(100 * FRACUNIT, p.ox);
	EXPECT_EQ(-1.0 * SNAPTEST_FU, p.exAccelZ);
}

// [Kaori-Chani] Past CLENT_VELEXTRAP_MS it stops there; level in z, no acceleration.
TEST(SnapshotSample, TheWireVelocityRunStopsAtItsCap)
{
	SnapGhostSample h[CLENT_GHOST_HISTORY];
	SnapSampleGhost g;
	sampletest_Vel(h, g);
	const SnapSampleContext c = sampletest_Context();
	SnapGhostPose p;

	h[1].state.qvz = 0;
	const SnapSampleOut o = ComputeSample(g, c, 1100 + 400, p);
	const double tc = CLENT_VELEXTRAP_MS * TICRATE / 1000.0;
	EXPECT_TRUE(o.bVelCapped);
	EXPECT_FIXED_NEAR((100 + 2.0 * tc) * SNAPTEST_FU, p.x);
	EXPECT_EQ(0.0, p.exAccelZ);
}

// [Kaori-Chani] x/y older than the record: run on from them over their own clock, within half a
// unit of their cell either way.
TEST(SnapshotSample, AnOlderPositionIsKeptInsideItsRoundingCell)
{
	SnapGhostSample h[CLENT_GHOST_HISTORY];
	SnapSampleGhost g;
	sampletest_Vel(h, g);
	const SnapSampleContext c = sampletest_Context();
	SnapGhostPose p;

	h[1].state.qvz = 0;
	h[1].posMS = 1050;
	g.posAt = 1050;
	SnapSampleOut o = ComputeSample(g, c, 1100 + 400, p);
	EXPECT_TRUE(o.bVelCell);
	EXPECT_TRUE(o.bVelCellEdge);
	EXPECT_EQ(100 * FRACUNIT + FRACUNIT / 2, p.x);
	EXPECT_EQ(200 * FRACUNIT - FRACUNIT / 2, p.y);
	EXPECT_EQ(0u, p.exCapMS);

	h[1].state.qvx = -8;
	h[1].state.qvy = 4;
	o = ComputeSample(g, c, 1055, p);
	EXPECT_TRUE(o.bVelCell);
	EXPECT_FALSE(o.bVelCellEdge);
	EXPECT_FIXED_NEAR(100.0 * SNAPTEST_FU - 2.0 * SNAPTEST_FU * (5.0 * TICRATE / 1000.0), p.x);
	EXPECT_FIXED_NEAR(200.0 * SNAPTEST_FU + 1.0 * SNAPTEST_FU * (5.0 * TICRATE / 1000.0), p.y);

	o = ComputeSample(g, c, 1100 + 400, p);
	EXPECT_TRUE(o.bVelCellEdge);
	EXPECT_EQ(100 * FRACUNIT - FRACUNIT / 2, p.x);
	EXPECT_EQ(200 * FRACUNIT + FRACUNIT / 2, p.y);
}

// [Kaori-Chani] A player, a body (no missile bit), or a ghost at rest, does not take this exit:
// the body is held at its newest record.
TEST(SnapshotSample, APlayerBodyOrRestingGhostIsHeldInstead)
{
	SnapGhostSample h[CLENT_GHOST_HISTORY];
	SnapSampleGhost g;
	sampletest_Vel(h, g);
	const SnapSampleContext c = sampletest_Context();
	SnapGhostPose p;

	g.bOther = false;
	EXPECT_EQ(SNAP_SAMPLE_HELDMOVE, ComputeSample(g, c, 1200, p).exit) << "a player";
	g.bOther = true;

	h[1].state.misc = 0;
	const SnapSampleOut o = ComputeSample(g, c, 1200, p);
	EXPECT_EQ(SNAP_SAMPLE_HELDMOVE, o.exit) << "a body";
	EXPECT_FALSE(p.extrapolated);
	EXPECT_EQ(100 * FRACUNIT, p.x);
	EXPECT_EQ(200 * FRACUNIT, p.y);
}

TEST(SnapshotSample, MovingInZAloneStillRunsOnItsVelocity)
{
	SnapGhostSample h[CLENT_GHOST_HISTORY];
	SnapSampleGhost g;
	sampletest_Vel(h, g);
	const SnapSampleContext c = sampletest_Context();
	SnapGhostPose p;

	h[1].state.qvx = h[1].state.qvy = 0;
	h[1].state.qvz = 4;
	EXPECT_EQ(SNAP_SAMPLE_EXTRAPVEL, ComputeSample(g, c, 1200, p).exit);

	h[1].state.qvz = 0;
	EXPECT_EQ(SNAP_SAMPLE_HELDMOVE, ComputeSample(g, c, 1200, p).exit) << "at rest";
}

// [Kaori-Chani] A ballistic missile at 10 units a tic in x.
static void sampletest_Ballistic(SnapGhostSample* h, SnapSampleGhost& g)
{
	memset(h, 0, sizeof(SnapGhostSample) * CLENT_GHOST_HISTORY);
	h[0] = sampletest_Record(100, 1000, 0, 0, 1000, 40, 0, 0, SNAP_MISC_BALLISTIC);
	h[1] = sampletest_Record(104, 1100, 40, 0, 1000, 40, 0, 0, SNAP_MISC_BALLISTIC);
	g = sampletest_Ghost(h, 1, 2);
	g.bOther = false;
}

// [Kaori-Chani] 140 ms is 4.9 tics at 10 units a tic.
TEST(SnapshotSample, ABallisticMissileRunsToItsCap)
{
	SnapGhostSample h[CLENT_GHOST_HISTORY];
	SnapSampleGhost g;
	sampletest_Ballistic(h, g);
	const SnapSampleContext c = sampletest_Context();
	SnapGhostPose p;

	SnapSampleOut o = ComputeSample(g, c, 1240, p);
	const double t = 140.0 * TICRATE / 1000.0;
	EXPECT_EQ(SNAP_SAMPLE_EXTRAP, o.exit);
	EXPECT_FALSE(o.bCapped);
	EXPECT_EQ(SNAP_AHEAD_NONE, o.ahead);
	EXPECT_EQ(SNAP_LAW_NONE, o.law);
	EXPECT_FIXED_NEAR((40 + 10.0 * t) * SNAPTEST_FU, p.x);
	EXPECT_EQ(40 * FRACUNIT, p.ox);
	EXPECT_EQ(500u, p.exCapMS);
	EXPECT_EQ(0.0, p.exAccelZ);
	EXPECT_TRUE(p.extrapolated);

	const double tc = 500.0 * TICRATE / 1000.0;
	o = ComputeSample(g, c, 1100 + 900, p);
	EXPECT_TRUE(o.bCapped);
	EXPECT_FIXED_NEAR((40 + 10.0 * tc) * SNAPTEST_FU, p.x);

	o = ComputeSample(g, c, 1100 + 500, p);
	EXPECT_FALSE(o.bCapped) << "exactly at the cap";
	EXPECT_FIXED_NEAR((40 + 10.0 * tc) * SNAPTEST_FU, p.x);
}

// [Kaori-Chani] No lookup, then one that does not answer, take the measured acceleration.
TEST(SnapshotSample, GravityFallsBackToTheMeasuredAcceleration)
{
	SnapGhostSample h[CLENT_GHOST_HISTORY];
	SnapSampleGhost g;
	sampletest_Ballistic(h, g);
	SnapSampleContext c = sampletest_Context();
	SnapGhostPose p;
	const double t = 140.0 * TICRATE / 1000.0;

	h[1].state.misc = (WORD)(SNAP_MISC_BALLISTIC | SNAP_MISC_STEERING);
	h[1].state.qvz = 8;
	g.accelZ = -1.0 * SNAPTEST_FU;

	SnapSampleOut o = ComputeSample(g, c, 1240, p);
	EXPECT_EQ(SNAP_LAW_NOFN, o.law);
	EXPECT_EQ(-1.0 * SNAPTEST_FU, p.exAccelZ);
	EXPECT_FIXED_NEAR(1000.0 * SNAPTEST_FU + 2.0 * SNAPTEST_FU * t - 0.5 * SNAPTEST_FU * t * t, p.z);

	c.lawGravity = sampletest_LawNo;
	o = ComputeSample(g, c, 1240, p);
	EXPECT_EQ(SNAP_LAW_NOFN, o.law) << "a lookup that does not answer";
}

// [Kaori-Chani] A lookup that answers takes its law from the velocity's tic, and the old-z class
// takes the other half tic.
TEST(SnapshotSample, GravityUsesTheLawWhenTheLookupAnswers)
{
	SnapGhostSample h[CLENT_GHOST_HISTORY];
	SnapSampleGhost g;
	sampletest_Ballistic(h, g);
	SnapSampleContext law = sampletest_Context();
	SnapGhostPose p;
	const double t = 140.0 * TICRATE / 1000.0;

	h[1].state.misc = (WORD)(SNAP_MISC_BALLISTIC | SNAP_MISC_STEERING);
	h[1].state.qvz = 8;
	g.accelZ = -1.0 * SNAPTEST_FU;
	law.lawGravity = sampletest_LawYes;
	g.velZTic = 103;

	SnapSampleOut o = ComputeSample(g, law, 1240, p);
	const double vz0 = 2.0 * SNAPTEST_FU - 2.0 * SNAPTEST_FU;
	EXPECT_EQ(SNAP_LAW_USED, o.law);
	EXPECT_EQ(2.0 * SNAPTEST_FU, o.lawG);
	EXPECT_EQ(-2.0 * SNAPTEST_FU, p.exAccelZ);
	EXPECT_FIXED_NEAR(1000.0 * SNAPTEST_FU + vz0 * t - SNAPTEST_FU * t * (t - 1.0), p.z);

	h[1].state.classId = 8;
	o = ComputeSample(g, law, 1240, p);
	EXPECT_FIXED_NEAR(1000.0 * SNAPTEST_FU + vz0 * t - SNAPTEST_FU * t * (t + 1.0), p.z);
}

// [Kaori-Chani] Four records on a 28.5 ms/tic clock, dated 285, 313.5 and 342 (h[3] carries
// h[2]'s position).
static void sampletest_Ahead(SnapGhostSample* h, SnapSampleGhost& g, SnapClockModel& clock,
	SnapSampleContext& a)
{
	clock.baseMS = 0;
	clock.baseTic = 0;
	clock.intercept = 0.0;
	clock.slope = 28.5;
	a = sampletest_Context();
	a.clock = &clock;

	memset(h, 0, sizeof(SnapGhostSample) * CLENT_GHOST_HISTORY);
	h[0] = sampletest_Record(10, 1000, 0, 0, 0, 40, 0, 0, SNAP_MISC_BALLISTIC);
	h[1] = sampletest_Record(11, 1010, 10, 0, 0, 40, 0, 0, SNAP_MISC_BALLISTIC);
	h[2] = sampletest_Record(12, 1020, 20, 0, 0, 40, 0, 0, SNAP_MISC_BALLISTIC);
	h[3] = h[2];
	h[3].wallMS = 1030;
	g = sampletest_Ghost(h, 3, 4);
	g.bDated = true;
	g.posAt = 342;
	g.newestAt = 342;
}

// [Kaori-Chani] At 300 it is between tics 10 and 11, a little over half way.
TEST(SnapshotSample, APositionDatedAheadIsDrawnBetweenThePairEitherSide)
{
	SnapGhostSample h[CLENT_GHOST_HISTORY];
	SnapSampleGhost g;
	SnapClockModel clock;
	SnapSampleContext a;
	sampletest_Ahead(h, g, clock, a);
	SnapGhostPose p;

	const SnapSampleOut o = ComputeSample(g, a, 300, p);
	const double f = (300.0 - 285.0) / 28.5;
	EXPECT_EQ(SNAP_SAMPLE_AHEAD, o.exit);
	EXPECT_EQ(SNAP_AHEAD_INTERP, o.ahead);
	EXPECT_FIXED_NEAR(ComputeHermite(0.0, 10.0 * SNAPTEST_FU, 10.0 * SNAPTEST_FU, 10.0 * SNAPTEST_FU,
		1.0, f), p.x);
	EXPECT_EQ(p.x, p.ox);
	EXPECT_EQ(0u, p.exCapMS);
	EXPECT_TRUE(p.extrapolated);
}

// [Kaori-Chani] Tic 10 far from tic 11, as a teleport: held on the extrap exit.
TEST(SnapshotSample, AJumpBetweenTheDatedPairHoldsOnTheExtrapExit)
{
	SnapGhostSample h[CLENT_GHOST_HISTORY];
	SnapSampleGhost g;
	SnapClockModel clock;
	SnapSampleContext a;
	sampletest_Ahead(h, g, clock, a);
	SnapGhostPose p;

	h[0].state.qx = -5000;
	const SnapSampleOut o = ComputeSample(g, a, 300, p);
	EXPECT_EQ(SNAP_SAMPLE_EXTRAP, o.exit);
	EXPECT_EQ(SNAP_AHEAD_JUMP, o.ahead);
}

TEST(SnapshotSample, NothingDatedAtOrBeforeNowIsTheFirstCase)
{
	SnapGhostSample h[CLENT_GHOST_HISTORY];
	SnapSampleGhost g;
	SnapClockModel clock;
	SnapSampleContext a;
	sampletest_Ahead(h, g, clock, a);
	SnapGhostPose p;

	const SnapSampleOut o = ComputeSample(g, a, 200, p);
	EXPECT_EQ(SNAP_SAMPLE_EXTRAP, o.exit);
	EXPECT_EQ(SNAP_AHEAD_FIRST, o.ahead);
}

// [Kaori-Chani] Dated, but not after now; or undated: no attempt.
TEST(SnapshotSample, NoAttemptWhenNothingIsDatedAhead)
{
	SnapGhostSample h[CLENT_GHOST_HISTORY];
	SnapSampleGhost g;
	SnapClockModel clock;
	SnapSampleContext a;
	sampletest_Ahead(h, g, clock, a);
	SnapGhostPose p;

	EXPECT_EQ(SNAP_AHEAD_NONE, ComputeSample(g, a, 400, p).ahead) << "now past the newest date";
	EXPECT_EQ(SNAP_AHEAD_NONE, ComputeSample(g, a, 342, p).ahead) << "now at the newest date";
	g.bDated = false;
	EXPECT_EQ(SNAP_AHEAD_NONE, ComputeSample(g, a, 300, p).ahead) << "undated";
}
