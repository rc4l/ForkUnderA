//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: interest_compute_test.cpp
//
// Description: Tests for interest_compute: the tier a record is ranked into,
// the observations reported beside it, and the refresh bands, event delivery
// and own-missile buckets.
//
//-----------------------------------------------------------------------------

#include "interest_compute.h"
#include <gtest/gtest.h>

#include <math.h>

using namespace zx::netsnapshot;

// [Kaori-Chani] A record at (x, y) owing position, the client at the origin facing +x with the
// default settings: cull at 4096 units, the 200-degree cone, missiles inside 4096, enters graded
// during the fill.
static SnapTierIn interesttest_In(SDWORD x, SDWORD y)
{
	SnapTierIn in;
	in.qx = x;
	in.qy = y;
	in.pri = 0;
	in.mask = SNAPGRP_POS;
	in.bView = true;
	in.viewX = 0;
	in.viewY = 0;
	in.viewCos = 1.0;
	in.viewSin = 0.0;
	in.age = 0;
	in.refreshAge = 0;
	in.bFillMode = false;
	in.bTransition = false;
	in.enterGrade = 2;
	in.missileDist = 4096;
	in.cullDist = 4096;
	in.cullHalfCos = cos(100.0 * 3.14159265358979 / 180.0);
	in.terminalThisFrame = 0;
	return in;
}

struct InterestCase { const char* name; SnapTierIn in; unsigned int want; };

static void interesttest_Check(const InterestCase* c, unsigned int n)
{
	for (unsigned int i = 0; i < n; ++i)
	{
		SnapTierOut out;
		EXPECT_EQ(c[i].want, ComputeTier(c[i].in, out)) << c[i].name;
	}
}

// [Kaori-Chani] Players and deaths outrank everything but the overdue.
TEST(SnapshotInterest, PlayersAndDeathsRankAtTheTop)
{
	InterestCase c[2];
	unsigned int n = 0;
	SnapTierIn in = interesttest_In(100, 0);
	in.pri = SNAPPRI_PLAYER;
	c[n++] = { "player", in, SNAPCL_TIER_TOP };
	in = interesttest_In(100, 0);
	in.bTransition = true;
	c[n++] = { "death", in, SNAPCL_TIER_ENTER };
	interesttest_Check(c, n);
}

// [Kaori-Chani] Enters: ENTER outside the fill; during it graded by ring.
TEST(SnapshotInterest, EntersAreGradedByRingDuringTheFill)
{
	InterestCase c[8];
	unsigned int n = 0;
	SnapTierIn in = interesttest_In(5000, 0);
	in.mask = SNAPGRP_ENTER | SNAPGRP_POS;
	c[n++] = { "enter", in, SNAPCL_TIER_ENTER };
	in.bFillMode = true;
	c[n++] = { "enter far, fill", in, SNAPCL_TIER_DIST + 1 };
	in.qx = 3000;
	c[n++] = { "enter mid, fill", in, SNAPCL_TIER_DIST };
	in.qx = 1500;
	c[n++] = { "enter in the third ring, fill", in, SNAPCL_TIER_DIST };
	in.qx = 600;
	c[n++] = { "enter near, fill", in, SNAPCL_TIER_ENTER };
	in.qx = 5000; in.bFillMode = false; in.enterGrade = 1;
	c[n++] = { "enter, always graded", in, SNAPCL_TIER_DIST + 1 };
	in.enterGrade = 0; in.bFillMode = true;
	c[n++] = { "enter, never graded", in, SNAPCL_TIER_ENTER };
	in.enterGrade = 1; in.bView = false;
	c[n++] = { "enter, no view", in, SNAPCL_TIER_ENTER };
	interesttest_Check(c, n);
}

// [Kaori-Chani] No viewpoint: a neutral band until the guard forces it.
TEST(SnapshotInterest, WithNoViewpointARecordSitsInANeutralBand)
{
	InterestCase c[2];
	unsigned int n = 0;
	SnapTierIn in = interesttest_In(100, 0);
	in.bView = false;
	c[n++] = { "no view", in, SNAPCL_TIER_DIST + 1 };
	in.age = SNAPCL_AGE_FORCE;
	c[n++] = { "no view, forced", in, SNAPCL_TIER_TOP };
	interesttest_Check(c, n);
}

// [Kaori-Chani] Distance bands, behind two back, and the cull.
TEST(SnapshotInterest, DistanceBandsFacingAndTheCull)
{
	InterestCase c[12];
	unsigned int n = 0;
	SnapTierIn in = interesttest_In(300, 0);
	c[n++] = { "near", in, SNAPCL_TIER_DIST };
	in = interesttest_In(1500, 0);
	c[n++] = { "mid", in, SNAPCL_TIER_DIST + 2 };
	in = interesttest_In(512, 0);
	c[n++] = { "on a ring's edge, the next ring", in, SNAPCL_TIER_DIST + 1 };
	in = interesttest_In(0, 300);
	c[n++] = { "square to the view, in front", in, SNAPCL_TIER_DIST };
	in = interesttest_In(-1024, 0);
	c[n++] = { "behind, on the cone's near radius", in, SNAPCL_TIER_DIST + 4 };
	in = interesttest_In(4096, 0);
	c[n++] = { "on the cull distance", in, SNAPCL_TIER_DIST + 4 };
	in = interesttest_In(-300, 0); in.mask = SNAPGRP_POS;
	c[n++] = { "behind, inside the cone's near radius", in, SNAPCL_TIER_DIST + 2 };
	in = interesttest_In(-2000, 0);
	c[n++] = { "out of view", in, SNAPCL_TIER_COSMETIC };
	in.refreshAge = SNAPCL_AGE_COSMETIC;
	c[n++] = { "out of view, refresh owed", in, SNAPCL_TIER_REFRESH };
	in.mask = SNAPGRP_APPEAR;
	c[n++] = { "out of view, appearance owed", in, SNAPCL_TIER_ANIMREFRESH };
	in = interesttest_In(6000, 0);
	c[n++] = { "far in view", in, SNAPCL_TIER_DISTLAST };
	in.refreshAge = SNAPCL_AGE_COSMETIC;
	c[n++] = { "far in view, refresh owed", in, SNAPCL_TIER_REFRESH };
	interesttest_Check(c, n);
}

TEST(SnapshotInterest, BeyondTheRingsBehindFallsToTheLastBand)
{
	SnapTierIn in = interesttest_In(-10000, 0);
	in.cullDist = 100000;
	in.cullHalfCos = -1.0;
	const InterestCase c[1] = { { "beyond the rings, behind", in, SNAPCL_TIER_DISTLAST } };
	interesttest_Check(c, 1);
}

// [Kaori-Chani] Missiles: their band inside the radius, a floor outside it, no cull.
TEST(SnapshotInterest, MissilesKeepTheirBandAndTheirFloor)
{
	InterestCase c[7];
	unsigned int n = 0;
	SnapTierIn in = interesttest_In(3000, 0);
	in.pri = SNAPPRI_MISSILE;
	c[n++] = { "missile, threatening", in, SNAPCL_TIER_MISSILE };
	in.missileDist = 3000;
	c[n++] = { "missile, on its radius", in, SNAPCL_TIER_MISSILE };
	in.missileDist = 1000;
	c[n++] = { "missile, ballistic floor", in, SNAPCL_TIER_DIST + SNAPCL_BALLISTIC_FLOOR };
	in.pri = SNAPPRI_MISSILE | SNAPPRI_HOMING;
	c[n++] = { "missile, homing floor", in, SNAPCL_TIER_DIST + SNAPCL_HOMING_FLOOR };
	in = interesttest_In(-8000, 0); in.pri = SNAPPRI_MISSILE;
	c[n++] = { "missile far behind", in, SNAPCL_TIER_DIST + SNAPCL_BALLISTIC_FLOOR };
	in = interesttest_In(300, 0); in.pri = SNAPPRI_MISSILE; in.missileDist = 100;
	c[n++] = { "missile near, past its radius", in, SNAPCL_TIER_DIST };
	in = interesttest_In(800, 0); in.pri = SNAPPRI_MISSILE; in.missileDist = 600;
	c[n++] = { "missile in the second ring, past its radius", in, SNAPCL_TIER_DIST };
	interesttest_Check(c, n);
}

// [Kaori-Chani] Appearance alone, far enough to be cosmetic, near enough not to be.
TEST(SnapshotInterest, AppearanceAloneIsCosmeticUntilItIsNear)
{
	InterestCase c[3];
	unsigned int n = 0;
	SnapTierIn in = interesttest_In(1500, 0);
	in.mask = SNAPGRP_APPEAR;
	c[n++] = { "appearance only", in, SNAPCL_TIER_COSMETIC };
	in.refreshAge = SNAPCL_AGE_COSMETIC;
	c[n++] = { "appearance only, refresh owed", in, SNAPCL_TIER_ANIMREFRESH };
	in = interesttest_In(300, 0); in.mask = SNAPGRP_APPEAR;
	c[n++] = { "appearance only, near", in, SNAPCL_TIER_DIST };
	interesttest_Check(c, n);
}

// [Kaori-Chani] The starvation guard.
TEST(SnapshotInterest, TheStarvationGuardPromotesAnAgeingRecord)
{
	InterestCase c[4];
	unsigned int n = 0;
	SnapTierIn in = interesttest_In(1500, 0);
	in.age = SNAPCL_AGE_FORCE * 2;
	c[n++] = { "overdue", in, SNAPCL_TIER_OVERDUE };
	in.age = SNAPCL_AGE_FORCE;
	c[n++] = { "forced", in, SNAPCL_TIER_TOP };
	in.age = SNAPCL_AGE_PROMOTE;
	c[n++] = { "promoted two bands", in, SNAPCL_TIER_DIST };
	in = interesttest_In(5000, 0); in.cullDist = 100000; in.age = SNAPCL_AGE_PROMOTE;
	c[n++] = { "promoted from far", in, SNAPCL_TIER_DIST + 2 };
	interesttest_Check(c, n);
}

// [Kaori-Chani] The observations beside the band: the ring, the facing, forced, cosmetic and
// culled.
TEST(SnapshotInterest, ObservationsReportTheRingAndTheFacing)
{
	SnapTierOut out;
	SnapTierIn in = interesttest_In(100, 0);
	in.pri = SNAPPRI_PLAYER;
	ComputeTier(in, out);
	EXPECT_EQ(0u, out.distBucket);
	EXPECT_TRUE(out.bInFront);
	EXPECT_FALSE(out.bForced);
	EXPECT_FALSE(out.bCosmetic);
	EXPECT_FALSE(out.bCulled);
	EXPECT_EQ(SNAP_TERMINAL_NONE, out.terminal);

	in = interesttest_In(-600, 0);
	ComputeTier(in, out);
	EXPECT_EQ(1u, out.distBucket);
	EXPECT_FALSE(out.bInFront);

	in = interesttest_In(0, 300);
	ComputeTier(in, out);
	EXPECT_TRUE(out.bInFront);
}

TEST(SnapshotInterest, ObservationsReportCulledForcedAndCosmetic)
{
	SnapTierOut out;
	SnapTierIn in = interesttest_In(20000, 0);
	ComputeTier(in, out);
	EXPECT_EQ((unsigned int)(SNAPCL_DIST_BUCKETS - 1), out.distBucket);
	EXPECT_TRUE(out.bCulled);

	in.bView = false;
	ComputeTier(in, out);
	EXPECT_EQ((unsigned int)(SNAPCL_DIST_BUCKETS - 1), out.distBucket);
	EXPECT_TRUE(out.bInFront) << "with no view everything counts as in front";

	in.age = SNAPCL_AGE_FORCE;
	ComputeTier(in, out);
	EXPECT_TRUE(out.bForced);

	in = interesttest_In(-2000, 0);
	ComputeTier(in, out);
	EXPECT_TRUE(out.bCosmetic);

	in = interesttest_In(1500, 0);
	in.age = SNAPCL_AGE_FORCE * 2;
	ComputeTier(in, out);
	EXPECT_TRUE(out.bForced);
}

// [Kaori-Chani] A terminal sequence carrying appearance is promoted while the frame's quota
// lasts, then ranked as a missile; without appearance it is only seen.
TEST(SnapshotInterest, ATerminalSequenceIsPromotedUntilTheQuotaRunsOut)
{
	SnapTierOut out;
	SnapTierIn in = interesttest_In(300, 0);
	in.pri = SNAPPRI_TERMINAL;
	in.mask = SNAPGRP_APPEAR;
	EXPECT_EQ(SNAPCL_TIER_ENTER, ComputeTier(in, out));
	EXPECT_EQ(SNAP_TERMINAL_PROMOTED, out.terminal);

	in.terminalThisFrame = SNAPCL_TERMINAL_MAX;
	EXPECT_EQ(SNAPCL_TIER_MISSILE, ComputeTier(in, out));
	EXPECT_EQ(SNAP_TERMINAL_CAPPED, out.terminal);
}

TEST(SnapshotInterest, ACappedTerminalSequenceKeepsItsFloorAndIsNotCosmetic)
{
	SnapTierOut out;
	SnapTierIn in = interesttest_In(1500, 0);
	in.pri = SNAPPRI_TERMINAL;
	in.mask = SNAPGRP_APPEAR;
	in.terminalThisFrame = SNAPCL_TERMINAL_MAX;
	in.missileDist = 100;
	EXPECT_EQ(SNAPCL_TIER_DIST + SNAPCL_BALLISTIC_FLOOR, ComputeTier(in, out));
	EXPECT_EQ(SNAP_TERMINAL_CAPPED, out.terminal);
	EXPECT_FALSE(out.bCosmetic);

	in.mask = SNAPGRP_POS;
	EXPECT_EQ(SNAPCL_TIER_DIST + SNAPCL_BALLISTIC_FLOOR, ComputeTier(in, out));
	EXPECT_EQ(SNAP_TERMINAL_SEEN, out.terminal) << "without appearance it is only seen";
}

TEST(SnapshotInterest, EachGroupHasItsRefreshBand)
{
	EXPECT_EQ(SNAPCL_TIER_REFRESH, ComputeRefreshBand(SNAPGRP_POS));
	EXPECT_EQ(SNAPCL_TIER_REFRESH, ComputeRefreshBand(SNAPGRP_POSZ));
	EXPECT_EQ(SNAPCL_TIER_ANIMREFRESH, ComputeRefreshBand(SNAPGRP_APPEAR));
}

TEST(SnapshotInterest, EventDeliveryTakesItsCopiesSpacingAndDeadline)
{
	static const struct { bool bTerminal; int setting; unsigned int c, s, d; } cases[] =
	{
		{ false, 1, 1, 1, SNAPCL_EVENT_DEADLINE },
		{ false, 0, 1, 1, SNAPCL_EVENT_DEADLINE },		// at least one copy
		{ false, 9, 4, 1, SNAPCL_EVENT_DEADLINE + 3 },	// at most four
		{ true, 1, SNAPCL_EVENT_TERMINAL_COPIES, SNAPCL_EVENT_TERMINAL_SPACING,
			SNAPCL_EVENT_DEADLINE
				+ (SNAPCL_EVENT_TERMINAL_COPIES - 1) * SNAPCL_EVENT_TERMINAL_SPACING },
	};
	for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		unsigned int c = 0, s = 0, d = 0;
		ComputeEventDelivery(cases[i].bTerminal, cases[i].setting, c, s, d);
		EXPECT_EQ(cases[i].c, c) << "case " << i << ": copies";
		EXPECT_EQ(cases[i].s, s) << "case " << i << ": spacing";
		EXPECT_EQ(cases[i].d, d) << "case " << i << ": deadline";
	}
}

TEST(SnapshotInterest, OwnEnterBucketsFollowTheWait)
{
	static const struct { int wait; unsigned int want; } cases[] =
	{
		{ -1, 0 }, { 0, 0 }, { 1, 1 }, { 2, 2 }, { 3, 3 }, { 4, 3 }, { 5, 4 }, { 8, 4 },
		{ 9, 5 },
	};
	for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		EXPECT_EQ(cases[i].want, ComputeOwnEnterBucket(cases[i].wait))
			<< "a wait of " << cases[i].wait;
	}
}
