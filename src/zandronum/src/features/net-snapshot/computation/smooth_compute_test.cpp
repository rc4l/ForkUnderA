//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: smooth_compute_test.cpp
//
// Description: Tests for smooth_compute: which ghosts are smoothed, a step
// folded over its tic, an extrapolated correction retained and settled, the
// census each path counts, each kind's own threshold, and the renderer's
// slide.
//
//-----------------------------------------------------------------------------

#include "smooth_compute.h"
#include <gtest/gtest.h>

#include <math.h>
#include <string.h>

#include "doomdef.h"		// TICRATE

using namespace zx::netsnapshot;

// [Kaori-Chani] A unit as a double: the arithmetic here is deliberately floating point, and on
// 48.16 FRACUNIT is a Fixed, which does not multiply with a double.
static const double U = 65536.0;

static SnapSmoothCfg smoothtest_Cfg(int step, int extrap, int missile, bool bStepPop)
{
	SnapSmoothCfg cfg;
	cfg.stepLimit = step;
	cfg.extrapLimit = extrap;
	cfg.missileLimit = missile;
	cfg.bStepPop = bStepPop;
	cfg.bInReset = false;
	cfg.ticRate = TICRATE;
	return cfg;
}

// [Kaori-Chani] Smooths one frame and gives the drawn x in units.
static double smoothtest_DrawX(SnapSmooth& sm, SnapSmoothIn& p, const SnapSmoothCfg& cfg,
	unsigned int ms, SnapSmoothStats& st)
{
	fixed_t dx, dy, dz;
	ComputeSmooth(sm, p, cfg, ms, st, dx, dy, dz);
	return (double)dx / U;
}

// [Kaori-Chani] The state the unsmoothed-and-tracked walk leaves behind, which the fold and past
// counts are measured from: in the stepped population, at 90 units on tic 4, the reset over.
static void smoothtest_AfterTrack(SnapSmooth& sm, SnapSmoothIn& p, SnapSmoothCfg& cfg,
	SnapSmoothStats& st)
{
	fixed_t dx, dy, dz;
	memset(&sm, 0, sizeof(sm));
	memset(&p, 0, sizeof(p));
	p.posGapTics = 1; p.serverTic = 1;
	ComputeSmooth(sm, p, cfg, 1000, st, dx, dy, dz);
	p.x = 40 * FRACUNIT; p.serverTic = 2;
	ComputeSmooth(sm, p, cfg, 1030, st, dx, dy, dz);
	cfg.bStepPop = true; cfg.stepLimit = 0;
	p.x = 80 * FRACUNIT; p.serverTic = 3;
	ComputeSmooth(sm, p, cfg, 1060, st, dx, dy, dz);
	cfg.stepLimit = 32; cfg.bInReset = true;
	p.x = 90 * FRACUNIT; p.serverTic = 4;
	ComputeSmooth(sm, p, cfg, 1090, st, dx, dy, dz);
	cfg.bInReset = false;
}

// [Kaori-Chani] The population, with the local player id 1 of 16 slots.
TEST(SnapshotSmooth, WhoIsSteppedFollowsThePopulation)
{
	static const struct { int pop; unsigned int id; bool bWant; } cases[] =
	{
		{ -1, 2, false }, { 0, 2, false },
		{ 1, 2, true }, { 1, 16, true }, { 1, 1, false }, { 1, 17, false },
		{ 1, 0, false },
		{ 2, 1, true }, { 2, 17, true }, { 2, 0, true },
	};
	for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		EXPECT_EQ(cases[i].bWant, ComputeSmoothStepPop(cases[i].pop, cases[i].id, 0, 16))
			<< "population " << cases[i].pop << ", id " << cases[i].id;
	}
}

// [Kaori-Chani] A 12-unit step over its tic, a lost record's 2-tic step and the next at 2x, then
// a teleport, which snaps.
TEST(SnapshotSmooth, ASteppedGhostFoldsCatchesUpThenSnaps)
{
	SnapSmoothCfg cfg = smoothtest_Cfg(32, 1, 1, ComputeSmoothStepPop(1, 2, 0, 16));
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	memset(&sm, 0, sizeof(sm));
	memset(&p, 0, sizeof(p));
	p.posGapTics = 1; p.serverTic = 1;
	smoothtest_DrawX(sm, p, cfg, 1000, st);

	p.x = 12 * FRACUNIT; p.serverTic = 2;
	EXPECT_NEAR(0.0, smoothtest_DrawX(sm, p, cfg, 1020, st), 0.01) << "step held";
	EXPECT_NEAR(5.88, smoothtest_DrawX(sm, p, cfg, 1034, st), 0.05) << "step halfway";
	EXPECT_NEAR(12.0, smoothtest_DrawX(sm, p, cfg, 1050, st), 0.01) << "step reached";

	p.x = 36 * FRACUNIT; p.serverTic = 4; p.posGapTics = 2;
	EXPECT_NEAR(12.0, smoothtest_DrawX(sm, p, cfg, 1107, st), 0.01) << "lost record held";

	p.x = 48 * FRACUNIT; p.serverTic = 5; p.posGapTics = 1;
	EXPECT_NEAR(23.76, smoothtest_DrawX(sm, p, cfg, 1135, st), 0.05) << "catch-up";
	EXPECT_NEAR(48.0, smoothtest_DrawX(sm, p, cfg, 1164, st), 0.01) << "back on target";

	p.x = 248 * FRACUNIT; p.serverTic = 6;
	EXPECT_NEAR(248.0, smoothtest_DrawX(sm, p, cfg, 1192, st), 0.01) << "teleport snaps";
}

// [Kaori-Chani] Extrapolated: the target drawn as it is between records, and a 0.4-unit
// correction retained while moving, settled after.
TEST(SnapshotSmooth, AnExtrapolatedCorrectionIsRetainedWhileMovingAndSettlesAtAStop)
{
	SnapSmoothCfg cfg = smoothtest_Cfg(32, 1, 1, ComputeSmoothStepPop(1, 2, 0, 16));
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	memset(&sm, 0, sizeof(sm));
	memset(&p, 0, sizeof(p));
	p.extrapolated = true; p.posGapTics = 1; p.serverTic = 1;
	p.vx = 7 * FRACUNIT;
	smoothtest_DrawX(sm, p, cfg, 2000, st);

	p.x = (fixed_t)(2.45 * U);	smoothtest_DrawX(sm, p, cfg, 2010, st);
	p.x = (fixed_t)(4.9 * U);	smoothtest_DrawX(sm, p, cfg, 2020, st);
	EXPECT_NEAR(4.9, smoothtest_DrawX(sm, p, cfg, 2030, st), 0.01) << "cap";
	EXPECT_NEAR(4.9, smoothtest_DrawX(sm, p, cfg, 2040, st), 0.01) << "cap held";

	p.x = (fixed_t)(5.3 * U); p.serverTic = 2;
	EXPECT_NEAR(4.9, smoothtest_DrawX(sm, p, cfg, 2050, st), 0.01) << "correction absorbed";
	EXPECT_NEAR(4.9, smoothtest_DrawX(sm, p, cfg, 2100, st), 0.01) << "retained while moving";

	p.extrapolated = false; p.vx = 0; p.serverTic = 3;
	EXPECT_NEAR(5.0, smoothtest_DrawX(sm, p, cfg, 2150, st), 0.02) << "settling at a stop";
	EXPECT_NEAR(5.3, smoothtest_DrawX(sm, p, cfg, 2400, st), 0.01) << "settled";
}

// [Kaori-Chani] A 1.2-unit correction at a threshold of 1: half a unit is inside the cell, so
// only 0.7 is drawn back, and it does not snap.
TEST(SnapshotSmooth, AnExcessUnderTheThresholdIsNotASnap)
{
	SnapSmoothCfg cfg = smoothtest_Cfg(32, 1, 1, ComputeSmoothStepPop(1, 2, 0, 16));
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	memset(&sm, 0, sizeof(sm));
	memset(&p, 0, sizeof(p));
	p.extrapolated = true; p.posGapTics = 1; p.serverTic = 1;
	p.vx = FRACUNIT;
	smoothtest_DrawX(sm, p, cfg, 3000, st);
	p.x = (fixed_t)(0.35 * U);	smoothtest_DrawX(sm, p, cfg, 3010, st);

	const unsigned int snapsBefore = st.snaps[1];
	p.x = (fixed_t)(-0.5 * U); p.serverTic = 2;
	EXPECT_NEAR(0.7, smoothtest_DrawX(sm, p, cfg, 3020, st), 0.02);
	EXPECT_EQ(snapsBefore, st.snaps[1]);
}

// [Kaori-Chani] Outside the population, or at a step threshold of 0, or inside a reset: drawn on
// the target, which the state tracks.
TEST(SnapshotSmooth, AnUnsmoothedGhostIsDrawnOnItsTargetAndTracked)
{
	SnapSmoothCfg cfg = smoothtest_Cfg(32, 3, 3, false);
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	fixed_t dx, dy, dz;
	memset(&sm, 0, sizeof(sm));
	memset(&p, 0, sizeof(p));
	p.posGapTics = 1; p.serverTic = 1;
	ComputeSmooth(sm, p, cfg, 1000, st, dx, dy, dz);

	p.x = 40 * FRACUNIT; p.serverTic = 2;
	ComputeSmooth(sm, p, cfg, 1030, st, dx, dy, dz);
	EXPECT_TRUE(sm.valid);
	EXPECT_EQ(40 * FRACUNIT, dx);
	EXPECT_EQ(40 * FRACUNIT, sm.tx);
	EXPECT_EQ(1030u, sm.ms);
	EXPECT_EQ(2, sm.serverTic);

	cfg.bStepPop = true; cfg.stepLimit = 0;
	p.x = 80 * FRACUNIT; p.serverTic = 3;
	ComputeSmooth(sm, p, cfg, 1060, st, dx, dy, dz);
	EXPECT_EQ(80 * FRACUNIT, dx) << "a step threshold of 0";

	cfg.stepLimit = 32; cfg.bInReset = true;
	p.x = 90 * FRACUNIT; p.serverTic = 4;
	ComputeSmooth(sm, p, cfg, 1090, st, dx, dy, dz);
	EXPECT_EQ(90 * FRACUNIT, dx) << "inside a reset";
}

// [Kaori-Chani] A stepped ghost folds a step down in y and z, then a later fold finds 1.5 units
// of it owed, then 5, then 10; an event alone folds too.
TEST(SnapshotSmooth, EachFoldIsCountedInItsResidualBucket)
{
	SnapSmoothCfg cfg = smoothtest_Cfg(32, 3, 3, false);
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	fixed_t dx, dy, dz;
	smoothtest_AfterTrack(sm, p, cfg, st);

	p.y = -6 * FRACUNIT; p.z = -6 * FRACUNIT; p.serverTic = 5;
	ComputeSmooth(sm, p, cfg, 1120, st, dx, dy, dz);
	EXPECT_EQ(0, dy);
	EXPECT_EQ(0, dz);
	EXPECT_GT(st.folds[0], 0u);

	const unsigned int r1 = st.resid[0][1], r2 = st.resid[0][2], r3 = st.resid[0][3];
	sm.ox = (float)(1.5 * U); sm.oy = sm.oz = 0.0f; sm.speed = 0.0f;
	p.eventSeq = 1;
	ComputeSmooth(sm, p, cfg, 1120, st, dx, dy, dz);
	sm.oy = (float)(5.0 * U); sm.ox = sm.oz = 0.0f; sm.speed = 0.0f;
	p.serverTic = 6;
	ComputeSmooth(sm, p, cfg, 1120, st, dx, dy, dz);
	sm.oz = (float)(10.0 * U); sm.ox = sm.oy = 0.0f; sm.speed = 0.0f;
	p.serverTic = 7;
	ComputeSmooth(sm, p, cfg, 1120, st, dx, dy, dz);
	EXPECT_EQ(r1 + 1, st.resid[0][1]);
	EXPECT_EQ(r2 + 1, st.resid[0][2]);
	EXPECT_EQ(r3 + 1, st.resid[0][3]);
}

// [Kaori-Chani] Drawn more than a unit outside its segment's box, which the fold never makes:
// counted as past.
TEST(SnapshotSmooth, DrawnOutsideItsSegmentBoxIsCountedAsPast)
{
	SnapSmoothCfg cfg = smoothtest_Cfg(32, 3, 3, false);
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	fixed_t dx, dy, dz;
	smoothtest_AfterTrack(sm, p, cfg, st);
	p.y = -6 * FRACUNIT; p.z = -6 * FRACUNIT; p.serverTic = 5;
	ComputeSmooth(sm, p, cfg, 1120, st, dx, dy, dz);

	const unsigned int pastBefore = st.past;
	sm.ox = (float)(4.0 * U); sm.oy = sm.oz = 0.0f; sm.speed = 0.0f;
	sm.bx0 = sm.bx1 = (float)p.x; sm.by0 = sm.by1 = (float)p.y;
	ComputeSmooth(sm, p, cfg, 1120, st, dx, dy, dz);
	EXPECT_EQ(pastBefore + 1, st.past);

	sm.ox = 0.0f; sm.oy = (float)(4.0 * U);
	sm.bx0 = sm.bx1 = (float)p.x; sm.by0 = sm.by1 = (float)p.y;
	ComputeSmooth(sm, p, cfg, 1120, st, dx, dy, dz);
	sm.oy = (float)(-4.0 * U);
	ComputeSmooth(sm, p, cfg, 1120, st, dx, dy, dz);
	EXPECT_EQ(pastBefore + 3, st.past);
}

// [Kaori-Chani] Extrapolated, a correction past half a unit in each axis either way keeps half a
// unit; the rest decays.
TEST(SnapshotSmooth, AnExtrapolatedCorrectionKeepsHalfAUnitEachWay)
{
	const SnapSmoothCfg cfg = smoothtest_Cfg(32, 3, 3, false);
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	fixed_t dx, dy, dz;
	memset(&sm, 0, sizeof(sm));
	memset(&p, 0, sizeof(p));
	p.extrapolated = true; p.posGapTics = 1; p.serverTic = 1;
	p.vy = FRACUNIT;
	ComputeSmooth(sm, p, cfg, 3000, st, dx, dy, dz);

	p.x = -2 * FRACUNIT; p.y = 2 * FRACUNIT; p.z = -2 * FRACUNIT; p.serverTic = 2;
	ComputeSmooth(sm, p, cfg, 3000, st, dx, dy, dz);
	EXPECT_NEAR(0.5 * U, sm.kx, 1.0);
	EXPECT_NEAR(-0.5 * U, sm.ky, 1.0);
	EXPECT_NEAR(0.5 * U, sm.kz, 1.0);

	p.x = 2 * FRACUNIT; p.y = -2 * FRACUNIT; p.z = 2 * FRACUNIT; p.serverTic = 3;
	ComputeSmooth(sm, p, cfg, 3000, st, dx, dy, dz);
	EXPECT_NEAR(-0.5 * U, sm.kx, 1.0);
	EXPECT_NEAR(0.5 * U, sm.ky, 1.0);
	EXPECT_NEAR(-0.5 * U, sm.kz, 1.0);
}

// [Kaori-Chani] A frame without a record measures the model's rate for the next fold, and one
// drawn back counts as reversed.
TEST(SnapshotSmooth, AFrameWithoutARecordMeasuresTheRate)
{
	const SnapSmoothCfg cfg = smoothtest_Cfg(32, 3, 3, false);
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	fixed_t dx, dy, dz;
	memset(&sm, 0, sizeof(sm));
	memset(&p, 0, sizeof(p));
	p.extrapolated = true; p.posGapTics = 1; p.serverTic = 1;
	p.vz = FRACUNIT;
	ComputeSmooth(sm, p, cfg, 4000, st, dx, dy, dz);

	p.z = FRACUNIT;
	ComputeSmooth(sm, p, cfg, 4010, st, dx, dy, dz);
	EXPECT_TRUE(sm.haveRate);
	EXPECT_NEAR(U / 10.0, sm.rz, 1.0);

	p.z = 2 * FRACUNIT; p.serverTic = 2;
	ComputeSmooth(sm, p, cfg, 4020, st, dx, dy, dz);
	EXPECT_GT(st.zero, 0u);
	EXPECT_EQ(2 * FRACUNIT, dz);

	const unsigned int revBefore = st.reversed;
	sm.oz = (float)(2.0 * U); sm.speed = (float)(U / 1.0);
	ComputeSmooth(sm, p, cfg, 4021, st, dx, dy, dz);
	EXPECT_EQ(revBefore + 1, st.reversed);
}

// [Kaori-Chani] A z offset alone keeps the extrapolated rules, a reset drops what is owed, and
// once settled it leaves those rules; one in y alone settles the same.
TEST(SnapshotSmooth, AnOffsetAloneKeepsTheExtrapolatedRulesUntilItSettles)
{
	SnapSmoothCfg cfg = smoothtest_Cfg(32, 3, 3, false);
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	fixed_t dx, dy, dz;
	memset(&sm, 0, sizeof(sm));
	memset(&p, 0, sizeof(p));
	sm.valid = true; sm.extrap = true; sm.extrapFam = true;
	sm.kz = (float)(0.25 * U);
	p.posGapTics = 1;

	const unsigned int offBefore = st.offFrames;
	ComputeSmooth(sm, p, cfg, 100, st, dx, dy, dz);
	EXPECT_TRUE(sm.extrapFam);
	EXPECT_EQ(offBefore + 1, st.offFrames);
	EXPECT_NEAR(0.125 * U, sm.kz, 1.0);

	cfg.bInReset = true;
	ComputeSmooth(sm, p, cfg, 110, st, dx, dy, dz);
	EXPECT_EQ(0.0f, sm.kz) << "a reset drops what is owed";
	EXPECT_TRUE(sm.valid);
	cfg.bInReset = false;

	sm.extrapFam = true;
	ComputeSmooth(sm, p, cfg, 120, st, dx, dy, dz);
	EXPECT_FALSE(sm.extrapFam) << "settled, so out of the extrapolated family";

	sm.extrap = true; sm.extrapFam = true;
	sm.ky = (float)(0.25 * U);
	ComputeSmooth(sm, p, cfg, 220, st, dx, dy, dz);
	EXPECT_NEAR(0.125 * U, sm.ky, 1.0);
}

TEST(SnapshotSmooth, TheRateNeedsTwoFramesApart)
{
	const SnapSmoothCfg cfg = smoothtest_Cfg(32, 3, 3, false);
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	fixed_t dx, dy, dz;
	memset(&sm, 0, sizeof(sm));
	memset(&p, 0, sizeof(p));
	p.posGapTics = 1;
	p.extrapolated = true;
	ComputeSmooth(sm, p, cfg, 5000, st, dx, dy, dz);
	ComputeSmooth(sm, p, cfg, 5000, st, dx, dy, dz);
	EXPECT_FALSE(sm.haveRate) << "two frames at the same moment";
}

TEST(SnapshotSmooth, AnOffsetBelowItsBoxIsPastToo)
{
	// [Kaori-Chani] In the stepped population, as the walk this follows leaves it.
	const SnapSmoothCfg cfg = smoothtest_Cfg(32, 3, 3, true);
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	fixed_t dx, dy, dz;
	memset(&sm, 0, sizeof(sm));
	memset(&p, 0, sizeof(p));
	p.posGapTics = 1;
	ComputeSmooth(sm, p, cfg, 6000, st, dx, dy, dz);

	const unsigned int lowBefore = st.past;
	sm.ox = (float)(-4.0 * U);
	ComputeSmooth(sm, p, cfg, 6000, st, dx, dy, dz);
	EXPECT_EQ(lowBefore + 1, st.past);
}

TEST(SnapshotSmooth, ASnapIsNotAMovingFrame)
{
	const SnapSmoothCfg cfg = smoothtest_Cfg(32, 3, 3, false);
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	fixed_t dx, dy, dz;
	memset(&sm, 0, sizeof(sm));
	memset(&p, 0, sizeof(p));
	p.posGapTics = 1;
	p.extrapolated = true; p.vx = FRACUNIT; p.serverTic = 1;
	ComputeSmooth(sm, p, cfg, 7000, st, dx, dy, dz);

	const unsigned int movingBefore = st.movingFrames, snapsBefore = st.snaps[1];
	p.x = 100 * FRACUNIT; p.serverTic = 2;
	ComputeSmooth(sm, p, cfg, 7010, st, dx, dy, dz);
	EXPECT_EQ(snapsBefore + 1, st.snaps[1]);
	EXPECT_EQ(movingBefore, st.movingFrames);
	EXPECT_EQ(100 * FRACUNIT, dx);
}

// [Kaori-Chani] A dated missile at 1 u a tic, then records 0.5 u back from the model's 0.7 and
// 0.45 u ahead: at a threshold of 0 each correction is a snap in its cell.
TEST(SnapshotSmooth, AtZeroEachCorrectionIsASnapInItsCell)
{
	const SnapSmoothCfg cfg = smoothtest_Cfg(32, 0, 0, false);
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	fixed_t dx, dy, dz;
	memset(&sm, 0, sizeof(sm));
	memset(&p, 0, sizeof(p));
	p.extrapolated = true; p.kind = SNAP_SMOOTH_BALLISTIC; p.dated = true;
	p.posGapTics = 1; p.serverTic = 1; p.vx = FRACUNIT;
	ComputeSmooth(sm, p, cfg, 1000, st, dx, dy, dz);

	p.x = (fixed_t)(0.35 * U);
	ComputeSmooth(sm, p, cfg, 1010, st, dx, dy, dz);
	const SnapSmoothSplit& m = st.ex[SNAP_SMOOTH_BALLISTIC][1];
	EXPECT_EQ(p.x, dx);
	EXPECT_TRUE(sm.haveRate);
	EXPECT_EQ(1u, m.moving);
	EXPECT_EQ(0u, m.reversed);

	p.x = (fixed_t)(0.2 * U); p.serverTic = 2;
	ComputeSmooth(sm, p, cfg, 1020, st, dx, dy, dz);
	EXPECT_EQ(p.x, dx);
	EXPECT_EQ(1u, m.snaps);
	EXPECT_EQ(1u, m.snapsBack);
	EXPECT_EQ(0u, m.folds);
	EXPECT_EQ(1u, st.snaps[1]);
	EXPECT_EQ(0u, st.folds[1]);
	EXPECT_EQ(0.0f, sm.kx);
	EXPECT_EQ(1u, m.moving);

	p.x = FRACUNIT; p.serverTic = 3;
	ComputeSmooth(sm, p, cfg, 1030, st, dx, dy, dz);
	EXPECT_EQ(p.x, dx);
	EXPECT_EQ(2u, m.snaps);
	EXPECT_EQ(1u, m.snapsBack);

	// [Kaori-Chani] A jump of 0.03 u ahead is no jump back.
	p.x += (fixed_t)(0.03 * U); p.serverTic = 4;
	ComputeSmooth(sm, p, cfg, 1030, st, dx, dy, dz);
	EXPECT_EQ(3u, m.snaps);
	EXPECT_EQ(1u, m.snapsBack);

	// [Kaori-Chani] The target drawn moving back without a record is reversed.
	p.x = FRACUNIT / 2;
	ComputeSmooth(sm, p, cfg, 1040, st, dx, dy, dz);
	EXPECT_EQ(p.x, dx);
	EXPECT_EQ(1u, m.reversed);
	EXPECT_EQ(2u, m.moving);
}

// [Kaori-Chani] The same at 1: the 0.5 u correction is retained, drawn off the target, and
// counted as a fold.
TEST(SnapshotSmooth, AtOneTheSameInputsFold)
{
	const SnapSmoothCfg cfg = smoothtest_Cfg(32, 0, 1, false);
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	fixed_t dx, dy, dz;
	memset(&sm, 0, sizeof(sm));
	memset(&p, 0, sizeof(p));
	p.extrapolated = true; p.kind = SNAP_SMOOTH_BALLISTIC; p.dated = true;
	p.posGapTics = 1; p.serverTic = 1; p.vx = FRACUNIT;
	ComputeSmooth(sm, p, cfg, 1000, st, dx, dy, dz);
	p.x = (fixed_t)(0.35 * U);
	ComputeSmooth(sm, p, cfg, 1010, st, dx, dy, dz);
	p.x = (fixed_t)(0.2 * U); p.serverTic = 2;
	ComputeSmooth(sm, p, cfg, 1020, st, dx, dy, dz);

	const SnapSmoothSplit& m = st.ex[SNAP_SMOOTH_BALLISTIC][1];
	EXPECT_NEAR(0.7, (double)dx / U, 0.01);
	EXPECT_EQ(1u, m.folds);
	EXPECT_NEAR(0.5, m.foldSum, 0.01);
}

// [Kaori-Chani] A body dated by arrival, standing still: its correction snaps into its own cell
// and, without a velocity, is never counted as back.
TEST(SnapshotSmooth, ABodyDatedByArrivalSnapsIntoItsOwnCell)
{
	const SnapSmoothCfg cfg = smoothtest_Cfg(32, 0, 0, false);
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	fixed_t dx, dy, dz;
	memset(&sm, 0, sizeof(sm));
	memset(&p, 0, sizeof(p));
	p.extrapolated = true; p.posGapTics = 1; p.serverTic = 1;
	ComputeSmooth(sm, p, cfg, 2000, st, dx, dy, dz);

	p.x = 2 * FRACUNIT; p.serverTic = 2;
	ComputeSmooth(sm, p, cfg, 2010, st, dx, dy, dz);
	EXPECT_EQ(p.x, dx);
	EXPECT_EQ(1u, st.ex[0][0].snaps);
	EXPECT_EQ(0u, st.ex[0][0].snapsBack);
	EXPECT_EQ(0u, st.ex[0][1].snaps);
	EXPECT_EQ(0u, st.ex[1][0].snaps);
}

// [Kaori-Chani] Not counted at 0: a stepped ghost outside the population, one owing an offset,
// and anything in a reset.
TEST(SnapshotSmooth, AtZeroAnUnsmoothedGhostCountsNothing)
{
	SnapSmoothCfg cfg = smoothtest_Cfg(32, 0, 0, false);
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	fixed_t dx, dy, dz;
	memset(&sm, 0, sizeof(sm));
	memset(&p, 0, sizeof(p));
	p.posGapTics = 1; p.serverTic = 1;
	ComputeSmooth(sm, p, cfg, 2015, st, dx, dy, dz);
	p.x = 5 * FRACUNIT; p.serverTic = 2;
	ComputeSmooth(sm, p, cfg, 2016, st, dx, dy, dz);
	EXPECT_EQ(p.x, dx);
	EXPECT_EQ(0u, st.snaps[0]);
	EXPECT_EQ(0u, st.folds[0]);

	// [Kaori-Chani] The owed offset is cleared as one snap of its 0.25 u, still, so neither back
	// nor off course.
	p.extrapolated = true;
	const unsigned int snapsBefore = st.snaps[1];
	const SnapSmoothSplit bodyBefore = st.ex[SNAP_SMOOTH_BODY][0];
	memset(&sm, 0, sizeof(sm));
	sm.valid = true; sm.extrap = true; sm.extrapFam = true;
	sm.kx = (float)(0.25 * U);
	p.x = 3 * FRACUNIT; p.serverTic = 3;
	ComputeSmooth(sm, p, cfg, 2020, st, dx, dy, dz);
	const SnapSmoothSplit& bodyNow = st.ex[SNAP_SMOOTH_BODY][0];
	EXPECT_EQ(p.x, dx);
	EXPECT_EQ(0.0f, sm.kx);
	EXPECT_EQ(snapsBefore + 1, st.snaps[1]);
	EXPECT_EQ(bodyBefore.snaps + 1, bodyNow.snaps);
	EXPECT_EQ(bodyBefore.snapSize[0] + 1, bodyNow.snapSize[0]);
	EXPECT_EQ(bodyBefore.snapsBack, bodyNow.snapsBack);
	EXPECT_EQ(bodyBefore.off15, bodyNow.off15);

	// [Kaori-Chani] Inside a reset nothing counts.
	sm.kx = (float)(0.25 * U);
	cfg.bInReset = true;
	p.x = 9 * FRACUNIT; p.serverTic = 4;
	ComputeSmooth(sm, p, cfg, 2030, st, dx, dy, dz);
	EXPECT_EQ(p.x, dx);
	EXPECT_EQ(0.0f, sm.kx);
	EXPECT_EQ(snapsBefore + 1, st.snaps[1]);
}

// [Kaori-Chani] The input read from a pose, as the live mirror and the replay both take it.
TEST(SnapshotSmooth, TheInputIsReadFromAPose)
{
	SnapGhostPose pose;
	memset(&pose, 0, sizeof(pose));
	pose.x = 1; pose.y = 2; pose.z = 3; pose.vx = 4; pose.vy = 5; pose.vz = 6;
	pose.posGapTics = 2; pose.serverTic = 77; pose.eventSeq = 9;
	pose.extrapolated = true; pose.dated = true;
	pose.misc = SNAP_MISC_BALLISTIC | SNAP_MISC_STEERING;

	const SnapSmoothIn in = ComputeSmoothIn(pose);
	EXPECT_EQ(1, in.x);
	EXPECT_EQ(2, in.y);
	EXPECT_EQ(3, in.z);
	EXPECT_EQ(4, in.vx);
	EXPECT_EQ(5, in.vy);
	EXPECT_EQ(6, in.vz);
	EXPECT_EQ(2, in.posGapTics);
	EXPECT_EQ(77, in.serverTic);
	EXPECT_EQ(9u, in.eventSeq);
	EXPECT_TRUE(in.extrapolated);
	EXPECT_EQ(SNAP_SMOOTH_BALLISTIC, in.kind);
	EXPECT_TRUE(in.dated);
}

// [Kaori-Chani] The kind: the ballistic bit, alone or with steering, then steering alone, then neither.
TEST(SnapshotSmooth, TheKindFollowsTheMiscBits)
{
	SnapGhostPose pose;
	memset(&pose, 0, sizeof(pose));
	pose.misc = SNAP_MISC_BALLISTIC;
	EXPECT_EQ(SNAP_SMOOTH_BALLISTIC, ComputeSmoothIn(pose).kind);
	pose.misc = SNAP_MISC_STEERING;
	EXPECT_EQ(SNAP_SMOOTH_TURNING, ComputeSmoothIn(pose).kind);

	pose.misc = SNAPMISC_CORPSE; pose.dated = false; pose.extrapolated = false;
	const SnapSmoothIn in = ComputeSmoothIn(pose);
	EXPECT_EQ(SNAP_SMOOTH_BODY, in.kind);
	EXPECT_FALSE(in.dated);
	EXPECT_FALSE(in.extrapolated);
}

TEST(SnapshotSmooth, ACellsReportTextReadsAsItIsPrinted)
{
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	char text[320];

	text[0] = 'x';
	EXPECT_FALSE(ComputeSmoothCellText(st, SNAP_SMOOTH_TURNING, 1, "tics", text, sizeof(text)))
		<< "an empty cell prints nothing";
	EXPECT_EQ(0, text[0]);

	SnapSmoothSplit& c = st.ex[SNAP_SMOOTH_TURNING][1];
	c.folds = 2; c.foldSum = 1.0; c.snaps = 3; c.snapsBack = 1; c.reversed = 4;
	c.moving = 50; c.snapSize[0] = 1; c.snapSize[1] = 1; c.snapSize[2] = 1;
	c.off15 = 6; c.off45 = 5;
	static const char want[] = "turning/model 2 folds (mean 0.50 u), 3 snaps (1 back; by size"
		" 1 <0.5 u, 1 <2 u, 1 2+ u), REVERSED 4 of 50 moving tics, off course 6 past 15 deg"
		" and 5 past 45 deg of 53 moving tics and snaps";
	ASSERT_TRUE(ComputeSmoothCellText(st, SNAP_SMOOTH_TURNING, 1, "tics", text, sizeof(text)));
	EXPECT_STREQ(want, text);

	// [Kaori-Chani] Moving frames alone are enough to print, dated by arrival named so.
	st.ex[SNAP_SMOOTH_BODY][0].moving = 1;
	ASSERT_TRUE(ComputeSmoothCellText(st, SNAP_SMOOTH_BODY, 0, "frames", text, sizeof(text)));
	EXPECT_EQ(0, strncmp(text, "body/arrival 0 folds", 20)) << text;
	EXPECT_FALSE(ComputeSmoothCellText(st, SNAP_SMOOTH_BALLISTIC, 0, "frames", text, sizeof(text)));
}

// [Kaori-Chani] A short buffer cuts the text; a kind or dating out of range, or no room at all,
// writes nothing.
TEST(SnapshotSmooth, TheReportTextRefusesWhatItCannotWrite)
{
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmoothSplit& c = st.ex[SNAP_SMOOTH_TURNING][1];
	c.folds = 2; c.foldSum = 1.0; c.snaps = 3; c.snapsBack = 1; c.reversed = 4;
	c.moving = 50; c.snapSize[0] = 1; c.snapSize[1] = 1; c.snapSize[2] = 1;
	c.off15 = 6; c.off45 = 5;
	char text[320];

	EXPECT_TRUE(ComputeSmoothCellText(st, SNAP_SMOOTH_TURNING, 1, "tics", text, 16));
	EXPECT_EQ(15u, strlen(text));

	text[0] = 'x';
	EXPECT_FALSE(ComputeSmoothCellText(st, SNAP_SMOOTH_KINDS, 1, "tics", text, sizeof(text)));
	EXPECT_EQ(0, text[0]);

	EXPECT_FALSE(ComputeSmoothCellText(st, SNAP_SMOOTH_TURNING, 2, "tics", text, sizeof(text)));

	text[0] = 'x';
	EXPECT_FALSE(ComputeSmoothCellText(st, SNAP_SMOOTH_TURNING, 1, "tics", text, 0));
	EXPECT_EQ('x', text[0]) << "no room at all leaves the buffer alone";
}

// [Kaori-Chani] A record 0.5 u back from the model's 0.7: a body or turning missile folds it at
// 0.7, a ballistic one snaps to 0.2.
TEST(SnapshotSmooth, EachKindTakesItsOwnThreshold)
{
	const SnapSmoothCfg cfg = smoothtest_Cfg(32, 1, 0, false);
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	fixed_t dx, dy, dz;
	static const BYTE kinds[3] = { SNAP_SMOOTH_BODY, SNAP_SMOOTH_BALLISTIC, SNAP_SMOOTH_TURNING };
	for (unsigned int k = 0; k < 3; ++k)
	{
		memset(&sm, 0, sizeof(sm));
		memset(&p, 0, sizeof(p));
		p.extrapolated = true; p.kind = kinds[k]; p.dated = true;
		p.posGapTics = 1; p.serverTic = 1; p.vx = FRACUNIT;
		ComputeSmooth(sm, p, cfg, 1000, st, dx, dy, dz);
		p.x = (fixed_t)(0.35 * U);
		ComputeSmooth(sm, p, cfg, 1010, st, dx, dy, dz);
		p.x = (fixed_t)(0.2 * U); p.serverTic = 2;
		ComputeSmooth(sm, p, cfg, 1020, st, dx, dy, dz);
		if (kinds[k] == SNAP_SMOOTH_BALLISTIC)
		{
			EXPECT_EQ(p.x, dx) << "kind " << (int)kinds[k] << " snaps";
			EXPECT_EQ(0.0f, sm.kx) << "kind " << (int)kinds[k];
		}
		else
		{
			EXPECT_NEAR(0.7, (double)dx / U, 0.01) << "kind " << (int)kinds[k] << " folds";
			EXPECT_NE(0.0f, sm.kx) << "kind " << (int)kinds[k];
		}
	}
	EXPECT_EQ(1u, st.ex[SNAP_SMOOTH_BODY][1].folds);
	EXPECT_EQ(1u, st.ex[SNAP_SMOOTH_BALLISTIC][1].snaps);
	EXPECT_EQ(1u, st.ex[SNAP_SMOOTH_TURNING][1].folds);
}

// [Kaori-Chani] A body or turning missile owing its fold becomes ballistic: cleared, a ballistic
// snap under 0.5 u drawn back from 0.7 to 0.5, so back and past both angles.
TEST(SnapshotSmooth, AKindTurningBallisticClearsWhatItOwed)
{
	const SnapSmoothCfg cfg = smoothtest_Cfg(32, 1, 0, false);
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	fixed_t dx, dy, dz;
	static const BYTE owing[2] = { SNAP_SMOOTH_BODY, SNAP_SMOOTH_TURNING };
	for (unsigned int k = 0; k < 2; ++k)
	{
		memset(&sm, 0, sizeof(sm));
		memset(&p, 0, sizeof(p));
		p.extrapolated = true; p.kind = owing[k]; p.dated = true;
		p.posGapTics = 1; p.serverTic = 1; p.vx = FRACUNIT;
		ComputeSmooth(sm, p, cfg, 1000, st, dx, dy, dz);
		p.x = (fixed_t)(0.35 * U);
		ComputeSmooth(sm, p, cfg, 1010, st, dx, dy, dz);
		p.x = (fixed_t)(0.2 * U); p.serverTic = 2;
		ComputeSmooth(sm, p, cfg, 1020, st, dx, dy, dz);
		ASSERT_NE(0.0f, sm.kx) << "kind " << (int)owing[k] << " owes a fold";

		const unsigned int snaps = st.snaps[1], folds = st.folds[1];
		const SnapSmoothSplit before = st.ex[SNAP_SMOOTH_BALLISTIC][1];
		p.kind = SNAP_SMOOTH_BALLISTIC; p.x = (fixed_t)(0.5 * U);
		ComputeSmooth(sm, p, cfg, 1030, st, dx, dy, dz);
		const SnapSmoothSplit& now = st.ex[SNAP_SMOOTH_BALLISTIC][1];
		EXPECT_EQ(p.x, dx) << "kind " << (int)owing[k];
		EXPECT_EQ(0.0f, sm.kx) << "kind " << (int)owing[k];
		EXPECT_EQ(0.0f, sm.ox) << "kind " << (int)owing[k];
		EXPECT_EQ(snaps + 1, st.snaps[1]) << "kind " << (int)owing[k];
		EXPECT_EQ(folds, st.folds[1]) << "kind " << (int)owing[k];
		EXPECT_EQ(before.snaps + 1, now.snaps) << "kind " << (int)owing[k];
		EXPECT_EQ(before.snapSize[2], now.snapSize[2]) << "kind " << (int)owing[k];
		EXPECT_EQ(before.snapSize[0] + before.snapSize[1] + 1,
			now.snapSize[0] + now.snapSize[1]) << "kind " << (int)owing[k];
		EXPECT_EQ(before.snapsBack + 1, now.snapsBack) << "kind " << (int)owing[k];
		EXPECT_EQ(before.off15 + 1, now.off15) << "kind " << (int)owing[k];
		EXPECT_EQ(before.off45 + 1, now.off45) << "kind " << (int)owing[k];
		EXPECT_EQ(before.moving, now.moving) << "kind " << (int)owing[k];
	}
}

// [Kaori-Chani] A clear's size goes in its bucket, and forward is not back.
TEST(SnapshotSmooth, AClearsSizeGoesInItsBucket)
{
	const SnapSmoothCfg cfg = smoothtest_Cfg(32, 1, 0, false);
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	fixed_t dx, dy, dz;
	static const float owed[2] = { 1.0f, 3.0f };
	static const double ahead[2] = { 0.02, 1.0 };	// a step forward, the first under 0.05 u
	for (unsigned int k = 0; k < 2; ++k)
	{
		const SnapSmoothSplit before = st.ex[SNAP_SMOOTH_BALLISTIC][1];
		memset(&sm, 0, sizeof(sm));
		memset(&p, 0, sizeof(p));
		sm.valid = true; sm.extrap = true; sm.extrapFam = true; sm.serverTic = 1;
		sm.ox = (float)(owed[k] * U);
		p.extrapolated = true; p.kind = SNAP_SMOOTH_BALLISTIC; p.dated = true;
		p.posGapTics = 1; p.serverTic = 1; p.vx = FRACUNIT;
		p.x = (fixed_t)((owed[k] + ahead[k]) * U);
		ComputeSmooth(sm, p, cfg, 1000, st, dx, dy, dz);
		const SnapSmoothSplit& now = st.ex[SNAP_SMOOTH_BALLISTIC][1];
		EXPECT_EQ(p.x, dx) << "owed " << owed[k];
		EXPECT_EQ(before.snaps + 1, now.snaps) << "owed " << owed[k];
		EXPECT_EQ(before.snapSize[k + 1] + 1, now.snapSize[k + 1]) << "owed " << owed[k];
		EXPECT_EQ(before.snapsBack, now.snapsBack) << "owed " << owed[k];
		EXPECT_EQ(before.off15, now.off15) << "owed " << owed[k];
	}
}

// [Kaori-Chani] A stepped ghost's clear counts nothing.
TEST(SnapshotSmooth, ASteppedGhostsClearCountsNothing)
{
	const SnapSmoothCfg cfg = smoothtest_Cfg(32, 1, 0, false);
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	fixed_t dx, dy, dz;
	const unsigned int snapsStepped = st.snaps[1] + st.snaps[0];
	memset(&sm, 0, sizeof(sm));
	memset(&p, 0, sizeof(p));
	sm.valid = true; sm.serverTic = 1; sm.ox = (float)U;
	p.posGapTics = 1; p.serverTic = 1; p.x = 4 * FRACUNIT;
	ComputeSmooth(sm, p, cfg, 1000, st, dx, dy, dz);
	EXPECT_EQ(p.x, dx);
	EXPECT_EQ(0.0f, sm.ox);
	EXPECT_EQ(snapsStepped, st.snaps[1] + st.snaps[0]);
}

// [Kaori-Chani] A ballistic missile at 0 becomes a body: its next correction inside the cell
// folds under 1, in the body's cell.
TEST(SnapshotSmooth, ABallisticMissileTurningBodyFoldsInTheBodysCell)
{
	const SnapSmoothCfg cfg = smoothtest_Cfg(32, 1, 0, false);
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	fixed_t dx, dy, dz;
	memset(&sm, 0, sizeof(sm));
	memset(&p, 0, sizeof(p));
	p.extrapolated = true; p.kind = SNAP_SMOOTH_BALLISTIC; p.dated = false;
	p.posGapTics = 1; p.serverTic = 1; p.vx = FRACUNIT;
	ComputeSmooth(sm, p, cfg, 1000, st, dx, dy, dz);
	p.x = (fixed_t)(0.35 * U);
	ComputeSmooth(sm, p, cfg, 1010, st, dx, dy, dz);
	EXPECT_EQ(p.x, dx);

	p.kind = SNAP_SMOOTH_BODY; p.x = (fixed_t)(0.2 * U); p.serverTic = 2;
	ComputeSmooth(sm, p, cfg, 1020, st, dx, dy, dz);
	EXPECT_NEAR(0.7, (double)dx / U, 0.01);
	EXPECT_EQ(1u, st.ex[SNAP_SMOOTH_BODY][0].folds);
}

// [Kaori-Chani] Off course and snap sizes on a ballistic missile at 0 moving +x, each
// correction's angle and size against the 15 and 45 degree marks.
TEST(SnapshotSmooth, OffCourseAndSnapSizesAreCountedAgainstTheirMarks)
{
	const SnapSmoothCfg cfg = smoothtest_Cfg(32, 1, 0, false);
	SnapSmoothStats st;
	memset(&st, 0, sizeof(st));
	SnapSmooth sm;
	SnapSmoothIn p;
	fixed_t dx, dy, dz;
	memset(&sm, 0, sizeof(sm));
	memset(&p, 0, sizeof(p));
	p.extrapolated = true; p.kind = SNAP_SMOOTH_BALLISTIC; p.dated = true;
	p.posGapTics = 1; p.serverTic = 1; p.vx = FRACUNIT;
	ComputeSmooth(sm, p, cfg, 3000, st, dx, dy, dz);

	static const double path[7][2] =
	{
		{ 0.35, 0.30 }, { 0.45, 0.80 }, { 0.90, 0.89 }, { 0.90, 0.93 }, { 1.10, 1.01 },
		{ 1.00, 1.01 }, { 3.50, 1.01 },
	};
	for (unsigned int i = 0; i < 7; ++i)
	{
		p.x = (fixed_t)(path[i][0] * U); p.y = (fixed_t)(path[i][1] * U); ++p.serverTic;
		ComputeSmooth(sm, p, cfg, 3000, st, dx, dy, dz);
		EXPECT_EQ(p.x, dx) << "step " << i;
		EXPECT_EQ(p.y, dy) << "step " << i;
	}

	const SnapSmoothSplit& c = st.ex[SNAP_SMOOTH_BALLISTIC][1];
	EXPECT_EQ(7u, c.snaps);
	EXPECT_EQ(5u, c.snapSize[0]);
	EXPECT_EQ(1u, c.snapSize[1]);
	EXPECT_EQ(1u, c.snapSize[2]);
	EXPECT_EQ(4u, c.off15);
	EXPECT_EQ(2u, c.off45);
	EXPECT_EQ(1u, c.snapsBack);
	EXPECT_EQ(0u, c.moving);

	p.x = (fixed_t)(3.1 * U);
	ComputeSmooth(sm, p, cfg, 3010, st, dx, dy, dz);
	EXPECT_EQ(5u, c.off15);
	EXPECT_EQ(3u, c.off45);
	EXPECT_EQ(1u, c.reversed);
	EXPECT_EQ(1u, c.moving);
}

// [Kaori-Chani] A held monster (id 300) slides at pop 0 and 1, a remote player (id 2) at 0 only;
// at 2 nothing slides unless the step threshold is 0.
TEST(SnapshotSmooth, WhoTakesTheRenderersSlide)
{
	SnapSmooth s;
	memset(&s, 0, sizeof(s));
	SnapSmoothIn in;
	memset(&in, 0, sizeof(in));
	SnapSmoothCfg cfg = smoothtest_Cfg(32, 1, 0, false);

	static const struct { int pop; unsigned int id; int step; bool bWant; } who[] =
	{
		{ 0, 300, 32, true }, { 1, 300, 32, true }, { 0, 2, 32, true },
		{ 1, 2, 32, false }, { 2, 300, 32, false }, { 2, 300, 0, true },
	};
	for (unsigned int i = 0; i < sizeof(who) / sizeof(who[0]); ++i)
	{
		cfg.bStepPop = ComputeSmoothStepPop(who[i].pop, who[i].id, 0, 16);
		cfg.stepLimit = who[i].step;
		EXPECT_EQ(who[i].bWant, ComputeSmoothSlides(s, in, cfg, false))
			<< "population " << who[i].pop << ", id " << who[i].id << ", step " << who[i].step;
	}
}

// [Kaori-Chani] No slide for a raw walking step, an extrapolated ghost or one settling, but a
// lunge under the switch slides.
TEST(SnapshotSmooth, WhatRefusesTheSlide)
{
	SnapSmooth s;
	memset(&s, 0, sizeof(s));
	SnapSmoothIn in;
	memset(&in, 0, sizeof(in));
	const SnapSmoothCfg cfg = smoothtest_Cfg(32, 1, 0, false);

	EXPECT_FALSE(ComputeSmoothSlides(s, in, cfg, true)) << "a raw walking step";

	static const fixed_t moving[3][3] =
	{
		{ FRACUNIT, 0, 0 }, { 0, -FRACUNIT, 0 }, { 0, 0, FRACUNIT },
	};
	for (unsigned int k = 0; k < 3; ++k)
	{
		in.vx = moving[k][0]; in.vy = moving[k][1]; in.vz = moving[k][2];
		EXPECT_TRUE(ComputeSmoothSlides(s, in, cfg, true)) << "a lunge on axis " << k;
	}
	in.vx = in.vy = in.vz = 0;

	in.extrapolated = true;
	EXPECT_FALSE(ComputeSmoothSlides(s, in, cfg, false)) << "an extrapolated ghost";
	in.extrapolated = false;

	s.extrapFam = true;
	EXPECT_FALSE(ComputeSmoothSlides(s, in, cfg, false)) << "one still settling";
}

// [Kaori-Chani] A frame sliding and not, and a step past 32 u a tic of the gap.
TEST(SnapshotSmooth, ASlidingFrameSetsItsPositionAndPrev)
{
	const fixed_t pose[3] = { (fixed_t)(10 * U), (fixed_t)(20 * U), (fixed_t)(30 * U) };
	fixed_t pos[3] = { 0, 0, 0 }, prev[3] = { 1, 1, 1 };

	ComputeSlideFrame(false, false, pose, 1, pos, prev);
	EXPECT_EQ(pose[0], pos[0]);
	EXPECT_EQ(pose[2], pos[2]);
	EXPECT_EQ(pose[1], prev[1]);

	pos[0] = pos[1] = pos[2] = 0; prev[0] = prev[1] = prev[2] = 1;
	ComputeSlideFrame(true, false, pose, 1, pos, prev);
	EXPECT_EQ(0, pos[0]);
	EXPECT_EQ(0, pos[2]);
	EXPECT_EQ(1, prev[0]);
	EXPECT_EQ(1, prev[2]);

	pos[0] = (fixed_t)(4 * U); pos[1] = (fixed_t)(20 * U); pos[2] = (fixed_t)(30 * U);
	ComputeSlideFrame(true, true, pose, 1, pos, prev);
	EXPECT_EQ(pose[0], pos[0]);
	EXPECT_EQ((fixed_t)(4 * U), prev[0]);
	EXPECT_EQ(pose[1], prev[1]);
	EXPECT_EQ(pose[2], prev[2]);
}

TEST(SnapshotSmooth, AStepPastTheLimitSnapsInsteadOfSliding)
{
	const fixed_t pose[3] = { (fixed_t)(10 * U), (fixed_t)(20 * U), (fixed_t)(30 * U) };
	fixed_t pos[3] = { 0, 0, 0 }, prev[3] = { 1, 1, 1 };
	static const struct { double step; unsigned int gap; bool bSlid; } snaps[] =
	{
		{ 32.0, 1, true }, { 32.5, 1, false }, { 16.0, 0, true }, { 32.5, 0, false },
		{ 64.0, 2, true }, { 128.0, 6, true }, { 129.0, 6, false },
	};
	for (unsigned int i = 0; i < sizeof(snaps) / sizeof(snaps[0]); ++i)
	{
		pos[0] = pose[0] - (fixed_t)(snaps[i].step * U); pos[1] = pose[1]; pos[2] = pose[2];
		const fixed_t from = pos[0];
		ComputeSlideFrame(true, true, pose, snaps[i].gap, pos, prev);
		EXPECT_EQ(pose[0], pos[0]) << "a step of " << snaps[i].step << " over " << snaps[i].gap;
		EXPECT_EQ(snaps[i].bSlid ? from : pose[0], prev[0])
			<< "a step of " << snaps[i].step << " over " << snaps[i].gap;
	}

	// [Kaori-Chani] The distance is taken in all three axes: 25 u in y and in z is 35 u.
	pos[0] = pose[0];
	pos[1] = pose[1] - (fixed_t)(25 * U);
	pos[2] = pose[2] - (fixed_t)(25 * U);
	ComputeSlideFrame(true, true, pose, 1, pos, prev);
	EXPECT_EQ(pose[1], prev[1]);
	EXPECT_EQ(pose[2], prev[2]);
}

// [Kaori-Chani] Four frames a tic, records mid-tic: 8 u every tic moves 2 u a frame, and 10 u
// every 2 tics slides one tic and holds the next.
TEST(SnapshotSmooth, AWalkerDrawsAnEvenSlideWhateverItsRecordRate)
{
	for (unsigned int every = 1; every <= 2; ++every)
	{
		const double step = (every == 1) ? 8.0 : 10.0;
		fixed_t wp[3] = { 0, 0, 0 }, wv[3] = { 0, 0, 0 }, target[3] = { 0, 0, 0 };
		double last = 0.0;
		unsigned int moved = 0, still = 0;
		for (unsigned int tic = 0; tic < 16; ++tic)
		{
			for (unsigned int fr = 0; fr < 4; ++fr)
			{
				if ((fr == 2) && ((tic % every) == 0))
					target[0] += (fixed_t)(step * U);
				ComputeSlideFrame(true, fr == 0, target, every, wp, wv);
				const double drawn = (double)wv[0]
					+ ((double)wp[0] - (double)wv[0]) * (fr / 4.0);
				const double d = (drawn - last) / U;
				last = drawn;
				if (tic < 2)
					continue;
				if (every == 1)
				{
					EXPECT_NEAR(2.0, d, 1e-9) << "tic " << tic << ", frame " << fr;
				}
				else if (fabs(d) < 1e-9)
					++still;
				else if (fabs(d - 2.5) < 1e-9)
					++moved;
				else
					ADD_FAILURE() << "tic " << tic << ", frame " << fr << " moved " << d;
			}
		}
		if (every == 2)
		{
			EXPECT_EQ(28u, moved);
			EXPECT_EQ(28u, still);
		}
	}
}

// [Kaori-Chani] Plan C's start pose, the one the replay and the live frame share: the renderer
// blends from start to end by frac, so a start set this way draws the fold.
TEST(SnapshotSmooth, AnAnchoredStartDrawsTheFoldThisFrame)
{
	const fixed_t end[3] = { (fixed_t)(100 * U), (fixed_t)(20 * U), (fixed_t)(-5 * U) };
	fixed_t start[3];

	// With the whole tic left the start is the fold itself, since nothing is blended off yet.
	{
		const double here[3] = { 100 * U + 1000.0, 20 * U, -5 * U };
		EXPECT_TRUE(ComputeAnchorStart(end, here, (fixed_t)0, start));
		EXPECT_EQ((fixed_t)(100 * U + 1000.0), start[0]);
		EXPECT_EQ(end[1], start[1]);
		EXPECT_EQ(end[2], start[2]);
	}

	// Half way through, half the offset is blended off, so the start carries twice it.
	{
		const double here[3] = { 100 * U + 1000.0, 20 * U - 300.0, -5 * U };
		EXPECT_TRUE(ComputeAnchorStart(end, here, (fixed_t)(U / 2), start));
		EXPECT_EQ((fixed_t)(100 * U + 2000.0), start[0]);
		EXPECT_EQ((fixed_t)(20 * U - 600.0), start[1]) << "a negative offset scales the same way";
		EXPECT_EQ(end[2], start[2]);
	}

	// A fold that landed on the end gives the end exactly, on every frac.
	{
		const double here[3] = { 100 * U, 20 * U, -5 * U };
		EXPECT_TRUE(ComputeAnchorStart(end, here, (fixed_t)(U / 3), start));
		for (unsigned int a = 0; a < 3; ++a)
			EXPECT_EQ(end[a], start[a]);
	}
}

// [Kaori-Chani] The guard: under a sixteenth of a tic there is too little left to divide an
// offset into, and a frac forced to 1 by cl_capfps or r_NoInterpolate lands there too.
TEST(SnapshotSmooth, TheLastSixteenthOfATicDrawsTheEndOutright)
{
	const fixed_t end[3] = { (fixed_t)(100 * U), (fixed_t)0, (fixed_t)0 };
	const double here[3] = { 100 * U + 1000.0, 0.0, 0.0 };
	fixed_t start[3];

	// Exactly a sixteenth left still anchors, which is the edge the rule names.
	EXPECT_TRUE(ComputeAnchorStart(end, here, (fixed_t)(U * 15 / 16), start));
	EXPECT_EQ((fixed_t)(100 * U + 16000.0), start[0]);

	// A unit past it does not, and the start is left on the end.
	EXPECT_FALSE(ComputeAnchorStart(end, here, (fixed_t)(U * 15 / 16 + 1), start));
	EXPECT_EQ(end[0], start[0]);

	// As for a frac of 1, where the end is what the frame draws.
	EXPECT_FALSE(ComputeAnchorStart(end, here, (fixed_t)U, start));
	EXPECT_EQ(end[0], start[0]);
}

// [Kaori-Chani] A fold that did not move comes back on the end for every frac, which the
// truncated form missed by a unit.
TEST(SnapshotSmooth, ARestingMirrorIsNeverDrawnAUnitLow)
{
	static const double xs[] = { 1.0, 1234.5, 100 * U, 12345678.0, -98765432.0 };
	for (unsigned int i = 0; i < sizeof(xs) / sizeof(xs[0]); ++i)
	{
		const fixed_t end[3] = { (fixed_t)xs[i], (fixed_t)(-xs[i]), (fixed_t)0 };
		const double here[3] = { (double)end[0], (double)end[1], (double)end[2] };
		for (unsigned int n = 0; n <= 16; ++n)
		{
			fixed_t start[3];
			ComputeAnchorStart(end, here, (fixed_t)(U * n / 17), start);
			for (unsigned int a = 0; a < 3; ++a)
				EXPECT_EQ(end[a], start[a]) << "x " << xs[i] << ", frac " << n << "/17";
		}
	}
}
