// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/computation/viewtrace_compute.h"

#include <gtest/gtest.h>

using namespace zx::frame;

namespace {

const unsigned kDeg = 11930465u;   // [rc4l] one degree in BAM, near enough

} // namespace

TEST(ViewTrace, ATurnIsMeasuredTheShortWayRound)
{
	EXPECT_NEAR(1.0, ComputeTurnDegrees(0u, kDeg), 1e-6);
	EXPECT_NEAR(-1.0, ComputeTurnDegrees(kDeg, 0u), 1e-6);
	EXPECT_NEAR(2.0, ComputeTurnDegrees(0u - kDeg, kDeg), 1e-6);
}

// [rc4l] Mouse turning reaches the view every frame, so it is spread over frames that ran no tic.
TEST(ViewTrace, AViewTurningEveryFrameSpreadsItsTurningBetweenTics)
{
	ViewTraceSample s[6];
	for (int i = 0; i < 6; i++) { s[i].us = i * 1000; s[i].angle = i * kDeg; s[i].localAngle = i * kDeg; s[i].gametic = i < 3 ? 10 : 11; }
	const ViewTraceSummary r = ComputeViewTraceSummary(s, 6);
	EXPECT_EQ(6, r.frames);
	EXPECT_DOUBLE_EQ(5.0, r.spanMs);
	EXPECT_EQ(1, r.ticFrames);
	EXPECT_EQ(5, r.turnedFrames);
	EXPECT_EQ(0, r.mouseOnlyFrames);
	EXPECT_NEAR(5.0, r.turnDeg, 1e-5);
	EXPECT_NEAR(1.0, r.ticTurnDeg, 1e-5);
	EXPECT_NEAR(1.0, r.maxStepDeg, 1e-5);
	EXPECT_DOUBLE_EQ(1.0, r.maxStillMs);
	EXPECT_EQ(0, r.longGaps);
	EXPECT_DOUBLE_EQ(0.0, r.stepAfterGapDeg);
	EXPECT_NEAR(1.0, r.meanStepDeg, 1e-5);
}

// [rc4l] A stall: the view stands still for 30 ms mid-turn and then catches up in one lump.
TEST(ViewTrace, AStallIsAGapFollowedByALump)
{
	ViewTraceSample s[4] = { { 0, 0u, 0u, 1 }, { 1000, kDeg, 0u, 1 }, { 31000, kDeg, 0u, 1 }, { 32000, 9 * kDeg, 0u, 1 } };
	const ViewTraceSummary r = ComputeViewTraceSummary(s, 4);
	EXPECT_EQ(2, r.turnedFrames);
	EXPECT_EQ(1, r.longGaps);
	EXPECT_DOUBLE_EQ(31.0, r.maxStillMs);
	EXPECT_NEAR(8.0, r.stepAfterGapDeg, 1e-5);
	EXPECT_NEAR(4.5, r.meanStepDeg, 1e-5);
}

// [rc4l] The fault this looks for: the mouse moves within a tic and the view waits for the tic to turn at all.
TEST(ViewTrace, AViewTurningOnlyAtTicsLeavesTheMouseUnseenBetweenThem)
{
	ViewTraceSample s[5];
	for (int i = 0; i < 5; i++) { s[i].us = i * 1000; s[i].angle = 0; s[i].localAngle = i * kDeg; s[i].gametic = 10; }
	s[4].angle = 4 * kDeg; s[4].localAngle = 0; s[4].gametic = 11;
	const ViewTraceSummary r = ComputeViewTraceSummary(s, 5);
	EXPECT_EQ(1, r.ticFrames);
	EXPECT_EQ(1, r.turnedFrames);
	EXPECT_EQ(3, r.mouseOnlyFrames);
	EXPECT_NEAR(4.0, r.turnDeg, 1e-5);
	EXPECT_NEAR(4.0, r.ticTurnDeg, 1e-5);
	EXPECT_DOUBLE_EQ(0.0, r.maxStillMs);   // [rc4l] one turning frame has no gap before it
}

TEST(ViewTrace, TooFewFramesSayNothing)
{
	ViewTraceSample s[1] = { { 0, 0u, 0u, 0 } };
	const ViewTraceSummary r = ComputeViewTraceSummary(s, 1);
	EXPECT_EQ(1, r.frames);
	EXPECT_EQ(0, r.turnedFrames);
	EXPECT_DOUBLE_EQ(0.0, r.spanMs);
}
