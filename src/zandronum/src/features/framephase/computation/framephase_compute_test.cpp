// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/framephase/computation/framephase_compute.h"

#include <gtest/gtest.h>
#include <string.h>

using namespace zx::framephase;

TEST(FramePhase, EachPhaseHasItsNameAndAnythingElseIsUnknown)
{
	EXPECT_STREQ("sim", ComputePhaseName(kPhaseSim));
	EXPECT_STREQ("present", ComputePhaseName(kPhasePresent));
	EXPECT_STREQ("input", ComputePhaseName(kPhaseInput));
	EXPECT_STREQ("display", ComputePhaseName(kPhaseDisplay));
	EXPECT_STREQ("flip", ComputePhaseName(kPhaseFlip));
	EXPECT_STREQ("?", ComputePhaseName(kPhaseCount));
	EXPECT_STREQ("?", ComputePhaseName(-1));
}

// [rc4l] What a judder hunt asks: in the frames that ran past a refresh, which phase grew.
TEST(FramePhase, SlowFramesAreAveragedApartFromTheRest)
{
	float phases[3 * kPhaseCount] = {};
	phases[0 * kPhaseCount + kPhaseWalk] = 800.f;
	phases[1 * kPhaseCount + kPhaseWalk] = 820.f;
	phases[1 * kPhaseCount + kPhaseSim] = 1500.f;
	phases[2 * kPhaseCount + kPhaseWalk] = 780.f;
	const double frameUs[3] = { 1400.0, 2900.0, 1300.0 };
	double slow[kPhaseCount], rest[kPhaseCount];
	EXPECT_EQ(1, ComputeSlowFramePhases(phases, frameUs, 3, 2083.0, slow, rest));
	EXPECT_DOUBLE_EQ(1500.0, slow[kPhaseSim]);
	EXPECT_DOUBLE_EQ(820.0, slow[kPhaseWalk]);
	EXPECT_DOUBLE_EQ(0.0, rest[kPhaseSim]);
	EXPECT_DOUBLE_EQ(790.0, rest[kPhaseWalk]);
}

TEST(FramePhase, NoFramesOnEitherSideLeaveZeroes)
{
	float phases[kPhaseCount] = {};
	phases[kPhaseWalk] = 100.f;
	const double frameUs[1] = { 1000.0 };
	double slow[kPhaseCount], rest[kPhaseCount];
	EXPECT_EQ(0, ComputeSlowFramePhases(phases, frameUs, 1, 2083.0, slow, rest));
	EXPECT_DOUBLE_EQ(0.0, slow[kPhaseWalk]);
	EXPECT_DOUBLE_EQ(100.0, rest[kPhaseWalk]);
	EXPECT_EQ(0, ComputeSlowFramePhases(phases, frameUs, 0, 2083.0, slow, rest));
}
