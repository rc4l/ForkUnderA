// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/computation/viewtrace_compute.h"

#include <gtest/gtest.h>

#include <string.h>
#include <string>

using namespace zx::frame;

// [rc4l] A frame's mouse input from the window's messages to the view, and a take that starts it again.
TEST(MouseTally, MessagesMovementAndEventsAreCountedAndTakenOnce)
{
	MouseTally t = MouseTally();
	ComputeMouseTallyRaw(t, 3, -1, true);
	ComputeMouseTallyRaw(t, 2, 4, true);
	ComputeMouseTallyRaw(t, 50, 50, false);
	ComputeMouseTallyEvent(t, true);
	ComputeMouseTallyEvent(t, false);
	ComputeMouseTallyEvent(t, false);
	const MouseTally a = ComputeMouseTallyTake(t);
	EXPECT_EQ(3, a.messages);
	EXPECT_EQ(1, a.ignored);
	EXPECT_EQ(5, a.dx);
	EXPECT_EQ(3, a.dy);
	EXPECT_EQ(1, a.toView);
	EXPECT_EQ(2, a.keptFromView);
	const MouseTally b = ComputeMouseTallyTake(t);
	EXPECT_EQ(0, b.messages);
	EXPECT_EQ(0, b.dx);
	EXPECT_EQ(0, b.keptFromView);
}

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
	ViewTraceSample s[4] = {};
	const long long us[4] = { 0, 1000, 31000, 32000 };
	const unsigned angle[4] = { 0u, kDeg, kDeg, 9 * kDeg };
	for (int i = 0; i < 4; i++) { s[i].us = us[i]; s[i].angle = angle[i]; s[i].gametic = 1; }
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
	ViewTraceSample s[1] = {};
	const ViewTraceSummary r = ComputeViewTraceSummary(s, 1);
	EXPECT_EQ(1, r.frames);
	EXPECT_EQ(0, r.turnedFrames);
	EXPECT_DOUBLE_EQ(0.0, r.spanMs);
}

namespace {

// [rc4l] Three samples: a tic and a 2 degree turn between the first two, the third closing the second frame with its phases and present.
void ThreeSamples(ViewTraceSample *s)
{
	memset(s, 0, sizeof(ViewTraceSample) * 3);
	s[0].us = 1000; s[0].gametic = 5;
	s[1].us = 3000; s[1].gametic = 6; s[1].angle = 2 * kDeg; s[1].localAngle = 7;
	s[1].mouse.messages = 2; s[1].mouse.dx = 3; s[1].mouse.dy = -1; s[1].mouse.toView = 2; s[1].grabbed = true; s[1].active = true;
	s[1].x = 12.5f; s[1].y = -4.f; s[1].frac = 0.25f; s[1].clock = 9; s[1].ticNext = 120;
	s[2].us = 4500; s[2].gametic = 6; s[2].angle = 2 * kDeg; s[2].presentAt = 3900; s[2].frameUs = 1600;
	s[2].phases[zx::framephase::kPhaseSim] = 120.f; s[2].phases[zx::framephase::kPhaseWalk] = 800.f;
}

int Commas(const char *s)
{
	int n = 0;
	for (; *s != 0; s++) n += *s == ',';
	return n;
}

} // namespace

// [rc4l] fuactl trace reads the file by these names, one column per phase among them.
TEST(ViewTraceCsv, TheHeaderNamesEveryColumn)
{
	char h[1024];
	const int len = ComputeViewTraceCsvHeader(h, sizeof(h));
	EXPECT_EQ((int)strlen(h), len);
	EXPECT_EQ(0, strncmp(h, "us,deg,tic,local,view_at,present_at,frame_us,sim,walk,", 54));
	EXPECT_NE(nullptr, strstr(h, ",flip,mouse_msgs,"));
	EXPECT_EQ(std::string(",menu,console,x,y,frac,clock,tic_next"), std::string(h + len - 37));
	EXPECT_EQ(6 + zx::framephase::kPhaseCount + 16, Commas(h));
}

// [rc4l] A row is its own frame's view and mouse with the next sample's length, phases and present, which closed it.
TEST(ViewTraceCsv, ARowTakesItsPhasesFromTheSampleThatClosedIt)
{
	ViewTraceSample s[3];
	ThreeSamples(s);
	char r[1024], h[1024];
	ComputeViewTraceCsvHeader(h, sizeof(h));
	ComputeViewTraceCsvRow(r, sizeof(r), s, 3, 1);
	EXPECT_EQ(Commas(h), Commas(r));
	EXPECT_EQ(0, strncmp(r, "2000,2.0000,1,7,3000,3900,1600,120,800,0,", 41)) << r;
	EXPECT_EQ(std::string(",2,0,3,-1,2,0,1,1,0,0,0,12.500,-4.000,0.250,9,120"), std::string(r + strlen(r) - 49)) << r;
}

// [rc4l] The last sample has nothing after it, so its frame's length, phases and present are zero.
TEST(ViewTraceCsv, TheLastRowHasNoPhases)
{
	ViewTraceSample s[3];
	ThreeSamples(s);
	char r[1024];
	ComputeViewTraceCsvRow(r, sizeof(r), s, 3, 2);
	EXPECT_EQ(0, strncmp(r, "3500,0.0000,0,0,4500,0,0,0,0,", 29)) << r;
}

// [rc4l] Walking forward never steps back; one frame drawn behind the frame before it is one back step, and standing still or turning around slowly are not.
TEST(ViewTrace, AFrameDrawnBehindTheOneBeforeIsABackStep)
{
	ViewTraceSample s[6] = {};
	const float xs[6] = { 0.f, 2.f, 4.f, 3.f, 5.f, 7.f };
	for (int i = 0; i < 6; i++) s[i].x = xs[i];
	EXPECT_EQ(1, ComputeViewBackSteps(s, 6, 0.25f));
	const float walk[6] = { 0.f, 2.f, 4.f, 6.f, 6.f, 8.f };
	for (int i = 0; i < 6; i++) s[i].x = walk[i];
	EXPECT_EQ(0, ComputeViewBackSteps(s, 6, 0.25f));
	const float jitter[6] = { 0.f, 0.1f, 0.f, 0.1f, 0.f, 0.1f };
	for (int i = 0; i < 6; i++) s[i].x = jitter[i];
	EXPECT_EQ(0, ComputeViewBackSteps(s, 6, 0.25f));
	EXPECT_EQ(0, ComputeViewBackSteps(s, 2, 0.25f));
}

// [rc4l] A view walking 35 units a tic, drawn every 4 ms, steps 4.9 units; one that holds 24 ms and then catches up lurches once, at any frame length.
TEST(ViewTrace, AViewThatHoldsAndCatchesUpLurches)
{
	ViewTraceSample s[40] = {};
	for (int i = 0; i < 40; i++) { s[i].us = i * 4000; s[i].y = i * 4000 * 35.f * 35.f / 1000000.f; }
	EXPECT_EQ(0, ComputeViewLurches(s, 40));
	for (int i = 10; i < 16; i++) s[i].y = s[9].y;
	EXPECT_EQ(1, ComputeViewLurches(s, 40));
	EXPECT_EQ(0, ComputeViewLurches(s, 2));
	ViewTraceSample still[4] = {};
	for (int i = 0; i < 4; i++) still[i].us = i * 1000;
	EXPECT_EQ(0, ComputeViewLurches(still, 4));
	EXPECT_EQ(0, ComputeViewLurches(still, 1 + 0 * 3));
	ViewTraceSample instant[3] = {};
	instant[2].y = 5.f;
	EXPECT_EQ(0, ComputeViewLurches(instant, 3));
}

TEST(ViewTraceCsv, ASmallBufferIsCutAndTerminated)
{
	char full[1024], small[16];
	ComputeViewTraceCsvHeader(full, sizeof(full));
	EXPECT_EQ(15, ComputeViewTraceCsvHeader(small, sizeof(small)));
	EXPECT_EQ(std::string(full, 15), std::string(small));
}
