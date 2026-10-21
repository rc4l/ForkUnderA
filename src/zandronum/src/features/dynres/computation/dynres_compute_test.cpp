// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "features/dynres/computation/dynres_compute.h"

using namespace zx::dynres;

namespace {

DynresCaps Caps(double refresh, bool vsync, int limit, int target)
{
	DynresCaps c;
	c.refreshHz = refresh; c.vsync = vsync; c.limitFps = limit; c.targetFps = target;
	return c;
}

DynresSample Sample(double gpu, double cpu, double frame)
{
	DynresSample x;
	x.gpuMs = gpu; x.cpuMs = cpu; x.frameMs = frame;
	return x;
}

// [rc4l] Feeds one sample n times, as a run of identical frames.
DynresState Feed(DynresState s, const DynresSample &x, double budget, const float *steps, int count, int n, const DynresTuning &t = DynresTuning())
{
	for (int i = 0; i < n; i++) s = ComputeDynresStep(s, x, budget, steps, count, t);
	return s;
}

}

TEST(ComputeFrameBudgetMs, TheTargetOrTheRefresh)
{
	EXPECT_NEAR(1000.0 / 144, ComputeFrameBudgetMs(Caps(144, false, 0, 0)), 1e-9);
	EXPECT_NEAR(1000.0 / 60, ComputeFrameBudgetMs(Caps(0, false, 0, 0)), 1e-9);
	EXPECT_NEAR(1000.0 / 240, ComputeFrameBudgetMs(Caps(144, false, 0, 240)), 1e-9);
}

TEST(ComputeFrameBudgetMs, NeverAboveACapTheFrameCannotBeat)
{
	// [rc4l] vid_maxfps under the target: aim at what the limit allows.
	EXPECT_NEAR(1000.0 / 60, ComputeFrameBudgetMs(Caps(144, false, 60, 240)), 1e-9);
	// [rc4l] cl_capfps comes in as a limit of the tic rate.
	EXPECT_NEAR(1000.0 / 35, ComputeFrameBudgetMs(Caps(144, false, 35, 0)), 1e-9);
	// [rc4l] Vsync holds the frame to the refresh whatever the target says.
	EXPECT_NEAR(1000.0 / 144, ComputeFrameBudgetMs(Caps(144, true, 0, 480)), 1e-9);
	EXPECT_NEAR(1000.0 / 60, ComputeFrameBudgetMs(Caps(144, true, 60, 480)), 1e-9);
	// [rc4l] A limit over the target changes nothing, nor does vsync with no known refresh.
	EXPECT_NEAR(1000.0 / 120, ComputeFrameBudgetMs(Caps(0, true, 500, 120)), 1e-9);
}

TEST(ComputeDynresSteps, EighthsForLinearAndCoarserForPoint)
{
	float s[kMaxDynresSteps];
	ASSERT_EQ(5, ComputeDynresSteps(0.5f, false, s));
	EXPECT_FLOAT_EQ(1.f, s[0]); EXPECT_FLOAT_EQ(0.875f, s[1]); EXPECT_FLOAT_EQ(0.5f, s[4]);
	ASSERT_EQ(3, ComputeDynresSteps(0.5f, true, s));
	EXPECT_FLOAT_EQ(1.f, s[0]); EXPECT_FLOAT_EQ(0.75f, s[1]); EXPECT_FLOAT_EQ(0.5f, s[2]);
	ASSERT_EQ(7, ComputeDynresSteps(0.25f, false, s));
	EXPECT_FLOAT_EQ(0.25f, s[6]);
	// [rc4l] A floor between steps stops at the step above it, and full scale is always there.
	EXPECT_EQ(4, ComputeDynresSteps(0.6f, false, s));
	EXPECT_EQ(1, ComputeDynresSteps(1.f, true, s));
	EXPECT_EQ(1, ComputeDynresSteps(2.f, false, s));
}

TEST(ComputeFrameWaitedOnGpu, AFrameOverBudgetWithIdleCpuWaitedOnTheGpu)
{
	const DynresTuning t;
	EXPECT_TRUE(ComputeFrameWaitedOnGpu(Sample(-1, 5, 30), 16.6, t));
	// [rc4l] On budget, a little over it as a display's timing jitters, or with the CPU busy, nothing says the GPU held it.
	EXPECT_FALSE(ComputeFrameWaitedOnGpu(Sample(-1, 5, 16.6), 16.6, t));
	EXPECT_FALSE(ComputeFrameWaitedOnGpu(Sample(-1, 5, 20), 16.6, t));
	EXPECT_FALSE(ComputeFrameWaitedOnGpu(Sample(-1, 25, 30), 16.6, t));
}

TEST(ComputeDynresStep, AGpuOverBudgetStepsDownThenSettles)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, false, steps);
	const DynresTuning t;
	DynresState s = ComputeDynresStep(DynresState(), Sample(20, 4, 20), 16.6, steps, n, t);
	EXPECT_EQ(1, s.step);
	EXPECT_EQ(1, s.changes);
	EXPECT_EQ(t.settleFrames, s.settle);
	// [rc4l] While it settles the timings still describe the old scale, so nothing moves.
	s = Feed(s, Sample(20, 4, 20), 16.6, steps, n, t.settleFrames);
	EXPECT_EQ(1, s.step);
	EXPECT_EQ(0, s.settle);
	s = ComputeDynresStep(s, Sample(20, 4, 20), 16.6, steps, n, t);
	EXPECT_EQ(2, s.step);
}

TEST(ComputeDynresStep, ItStopsAtTheLowestStep)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, true, steps);
	const DynresState s = Feed(DynresState(), Sample(50, 4, 50), 16.6, steps, n, 200);
	EXPECT_EQ(n - 1, s.step);
	EXPECT_EQ(n - 1, s.changes);
}

// [rc4l] A frame waiting on a cap (vsync, vid_maxfps) is long but cheap on the GPU, and blurring it would buy nothing.
TEST(ComputeDynresStep, ACappedFrameNeverStepsDown)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, false, steps);
	const DynresState s = Feed(DynresState(), Sample(2, 1, 16.6), 16.6, steps, n, 300);
	EXPECT_EQ(0, s.step);
	EXPECT_EQ(0, s.changes);
}

TEST(ComputeDynresStep, ACpuBoundFrameNeverStepsDown)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, false, steps);
	const DynresState s = Feed(DynresState(), Sample(15, 25, 25), 16.6, steps, n, 300);
	EXPECT_EQ(0, s.step);
}

TEST(ComputeDynresStep, HeadroomClimbsBackToFullScaleAndStaysThere)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, false, steps);
	const DynresTuning t;
	DynresState s;
	s.step = n - 1;
	s = Feed(s, Sample(2, 1, 2), 16.6, steps, n, 1000, t);
	EXPECT_EQ(0, s.step);
	EXPECT_EQ(n - 1, s.changes);
	s = Feed(s, Sample(2, 1, 2), 16.6, steps, n, 1000, t);
	EXPECT_EQ(0, s.step);
	EXPECT_EQ(n - 1, s.changes);
}

TEST(ComputeDynresStep, AStepUpWaitsForItsHeadroomToHold)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, false, steps);
	const DynresTuning t;
	DynresState s;
	s.step = 1;
	s = Feed(s, Sample(2, 1, 2), 16.6, steps, n, t.upFrames - 1, t);
	EXPECT_EQ(1, s.step);
	EXPECT_EQ(t.upFrames - 1, s.roomFrames);
	// [rc4l] One frame heavy enough to leave no room starts the count again.
	s = ComputeDynresStep(s, Sample(40, 1, 40), 16.6, steps, n, t);
	EXPECT_EQ(1, s.step);
	EXPECT_EQ(0, s.roomFrames);
	s = Feed(s, Sample(2, 1, 2), 16.6, steps, n, t.upFrames + 10, t);
	EXPECT_EQ(0, s.step);
}

// [rc4l] Not stepping up into a cost the next step would blow: 10 ms at 7/8 is about 13 ms at full scale, over 70% of 16.6.
TEST(ComputeDynresStep, ItDoesNotStepUpIntoAMissedFrame)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, false, steps);
	DynresState s;
	s.step = 1;
	s = Feed(s, Sample(10, 1, 10), 16.6, steps, n, 500);
	EXPECT_EQ(1, s.step);
}

TEST(ComputeDynresStep, UntimedARunOfFramesWaitingOnTheGpuStepsDown)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, false, steps);
	const DynresTuning t;
	DynresState s = Feed(DynresState(), Sample(-1, 3, 30), 16.6, steps, n, t.untimedMisses - 1, t);
	EXPECT_EQ(0, s.step);
	EXPECT_EQ(t.untimedMisses - 1, s.missStreak);
	// [rc4l] One frame that did not wait starts the run again.
	s = ComputeDynresStep(s, Sample(-1, 3, 16), 16.6, steps, n, t);
	EXPECT_EQ(0, s.missStreak);
	s = Feed(s, Sample(-1, 3, 30), 16.6, steps, n, t.untimedMisses, t);
	EXPECT_EQ(1, s.step);
	// [rc4l] Nothing steps past the lowest scale.
	DynresState low;
	low.step = n - 1;
	EXPECT_EQ(n - 1, Feed(low, Sample(-1, 3, 30), 16.6, steps, n, 50, t).step);
}

// [rc4l] A browser that does not pace its frames to a display runs them as fast as the CPU goes, which is no sign of the GPU at all.
TEST(ComputeDynresStep, UntimedACpuBoundFrameNeverStepsDown)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, false, steps);
	EXPECT_EQ(0, Feed(DynresState(), Sample(-1, 3.7, 4.0), 3.0, steps, n, 500).step);
	EXPECT_EQ(0, Feed(DynresState(), Sample(-1, 25, 30), 16.6, steps, n, 500).step);
}

TEST(ComputeDynresStep, UntimedFramesOnBudgetClimbSlowly)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, false, steps);
	const DynresTuning t;
	DynresState up;
	up.step = 1;
	up = Feed(up, Sample(-1, 2, 16.6), 16.6, steps, n, t.upFramesUntimed - 1, t);
	EXPECT_EQ(1, up.step);
	up = ComputeDynresStep(up, Sample(-1, 2, 16.6), 16.6, steps, n, t);
	EXPECT_EQ(0, up.step);
	// [rc4l] One late frame, as a display's timing jitters, does not start the count again, but a run of them does.
	DynresState held;
	held.step = 1;
	held = Feed(held, Sample(-1, 2, 16.6), 16.6, steps, n, 100, t);
	held = ComputeDynresStep(held, Sample(-1, 25, 30), 16.6, steps, n, t);
	EXPECT_EQ(101, held.roomFrames);
	held = Feed(held, Sample(-1, 25, 30), 16.6, steps, n, 4, t);
	EXPECT_EQ(0, held.roomFrames);
	EXPECT_EQ(1, held.step);
}

// [rc4l] Without timings a step up is a guess, and one undone soon after makes the next guess wait twice as long, up to sixteen times.
TEST(ComputeDynresStep, UntimedAStepUpSoonUndoneWaitsLongerNextTime)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, false, steps);
	const DynresTuning t;
	DynresState s;
	s.step = 1;
	for (int round = 1; round <= 6; round++)
	{
		// [rc4l] After the first, each step up also waits out the settle its step down began.
		const int wait = std::max(s.upWait, t.upFramesUntimed) + (round == 1 ? 0 : t.settleFrames + t.trialFrames);
		int frames = 0;
		for (; s.step != 0 && frames < 10000; frames++) s = ComputeDynresStep(s, Sample(-1, 2, 16.6), 16.6, steps, n, t);
		ASSERT_EQ(0, s.step) << "round " << round;
		EXPECT_EQ(wait, frames) << "round " << round;
		EXPECT_EQ(0, s.sinceUp);
		s = Feed(s, Sample(-1, 3, 30), 16.6, steps, n, t.settleFrames + t.untimedMisses, t);
		ASSERT_EQ(1, s.step) << "round " << round;
		EXPECT_EQ(std::min(t.upFramesUntimed << round, t.upFramesUntimed * 16), s.upWait);
	}
	// [rc4l] A step down long after the last step up keeps the wait it had.
	DynresState late;
	late.step = 0;
	late.upWait = 240;
	late = Feed(late, Sample(-1, 3, 30), 16.6, steps, n, t.untimedMisses, t);
	EXPECT_EQ(1, late.step);
	EXPECT_EQ(240, late.upWait);
}

TEST(ComputeShortIntervalMs, TheLowTenthWhichOneEarlyFrameCannotPullDown)
{
	double ms[20];
	for (int i = 0; i < 20; i++) ms[i] = 16.7;
	ms[3] = 2.0;
	EXPECT_DOUBLE_EQ(16.7, ComputeShortIntervalMs(ms, 20));
	for (int i = 0; i < 10; i++) ms[i] = 33.4;
	EXPECT_DOUBLE_EQ(16.7, ComputeShortIntervalMs(ms, 20));
	EXPECT_DOUBLE_EQ(0.0, ComputeShortIntervalMs(ms, 0));
}

TEST(ComputeDynresStep, AStepPastAShortenedListComesBackInsideIt)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.75f, true, steps);
	DynresState s;
	s.step = 5;
	s.settle = 3;
	EXPECT_EQ(n - 1, ComputeDynresStep(s, Sample(2, 1, 2), 16.6, steps, n, DynresTuning()).step);
	EXPECT_EQ(0, ComputeDynresStep(s, Sample(2, 1, 2), 16.6, steps, 0, DynresTuning()).step);
}

TEST(ComputeSceneRect, TheFrameAtFullScaleAndRoundedBelowIt)
{
	SceneRect r = ComputeSceneRect(1920, 1080, 1.f);
	EXPECT_EQ(1920, r.w); EXPECT_EQ(1080, r.h);
	r = ComputeSceneRect(1920, 1080, 2.f);
	EXPECT_EQ(1920, r.w); EXPECT_EQ(1080, r.h);
	r = ComputeSceneRect(1920, 1080, 0.5f);
	EXPECT_EQ(960, r.w); EXPECT_EQ(540, r.h);
	r = ComputeSceneRect(1265, 681, 0.75f);
	EXPECT_EQ(949, r.w); EXPECT_EQ(511, r.h);
	r = ComputeSceneRect(1, 1, 0.25f);
	EXPECT_EQ(1, r.w); EXPECT_EQ(1, r.h);
}

TEST(ComputeRefreshFromIntervalMs, SnapsToTheNearestCommonRate)
{
	EXPECT_DOUBLE_EQ(60.0, ComputeRefreshFromIntervalMs(16.8));
	EXPECT_DOUBLE_EQ(144.0, ComputeRefreshFromIntervalMs(6.9));
	EXPECT_DOUBLE_EQ(170.0, ComputeRefreshFromIntervalMs(1000.0 / 170));
	EXPECT_DOUBLE_EQ(165.0, ComputeRefreshFromIntervalMs(1000.0 / 165.5));
	EXPECT_DOUBLE_EQ(480.0, ComputeRefreshFromIntervalMs(2.08));
	EXPECT_NEAR(1000.0 / 2.6, ComputeRefreshFromIntervalMs(2.6), 1e-9);
	EXPECT_DOUBLE_EQ(0.0, ComputeRefreshFromIntervalMs(0.0));
	EXPECT_DOUBLE_EQ(0.0, ComputeRefreshFromIntervalMs(0.54));
	EXPECT_DOUBLE_EQ(1000.0, ComputeRefreshFromIntervalMs(1.0));
}

TEST(ComputeFrameRefreshHz, HoldsTheFastestRecentWindowThroughSlowFrames)
{
	FrameRefresh r;
	EXPECT_DOUBLE_EQ(0.0, ComputeFrameRefreshHz(r, 0.0));
	for (int i = 0; i < kRefreshFrames; i++) ComputeFrameRefreshHz(r, 1000.0 / 144);
	EXPECT_EQ(1, r.windows);
	// [rc4l] A GPU-bound run reads 30 Hz on its own, but the last whole window's 144 holds.
	double hz = 0.0;
	for (int i = 0; i < kRefreshFrames * (kRefreshWindows - 1); i++) hz = ComputeFrameRefreshHz(r, 33.3);
	EXPECT_DOUBLE_EQ(144.0, hz);
	// [rc4l] Once every held window is slow, the slow rate is the display's.
	hz = ComputeFrameRefreshHz(r, 33.3);
	for (int i = 0; i < kRefreshFrames; i++) hz = ComputeFrameRefreshHz(r, 33.3);
	EXPECT_EQ(kRefreshWindows, r.windows);
	EXPECT_DOUBLE_EQ(30.0, hz);
	// [rc4l] A faster window shows at once, before it is whole.
	for (int i = 0; i < kRefreshFrames / 2; i++) hz = ComputeFrameRefreshHz(r, 1000.0 / 60);
	EXPECT_DOUBLE_EQ(60.0, hz);
}

// [rc4l] Frames that shorten at the lower scale were the GPU's, so the step down is kept.
TEST(ComputeDynresStep, UntimedAStepDownThatShortensTheFramesIsKept)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.25f, false, steps);
	const DynresTuning t;
	DynresState s = Feed(DynresState(), Sample(-1, 3, 30), 16.6, steps, n, t.untimedMisses, t);
	ASSERT_EQ(1, s.step);
	EXPECT_DOUBLE_EQ(30.0, s.beforeMs);
	EXPECT_EQ(t.trialFrames, s.trial);
	// [rc4l] Still late but a tenth shorter, it is judged over the trial and kept, with no further step while it is judged.
	s = Feed(s, Sample(-1, 3, 26), 16.6, steps, n, t.settleFrames + t.trialFrames - 1, t);
	EXPECT_EQ(1, s.step);
	EXPECT_EQ(1, s.trial);
	s = ComputeDynresStep(s, Sample(-1, 3, 26), 16.6, steps, n, t);
	EXPECT_EQ(1, s.step);
	EXPECT_EQ(0, s.trial);
	EXPECT_DOUBLE_EQ(0.0, s.refuseMs);
	// [rc4l] Frames still waiting on the GPU then step down again.
	s = Feed(s, Sample(-1, 3, 26), 16.6, steps, n, t.untimedMisses, t);
	EXPECT_EQ(2, s.step);
	// [rc4l] Frames not a tenth shorter but no longer late keep it too.
	DynresState onBudget = Feed(DynresState(), Sample(-1, 3, 26), 16.6, steps, n, t.untimedMisses, t);
	ASSERT_EQ(1, onBudget.step);
	onBudget = Feed(onBudget, Sample(-1, 3, 24), 16.6, steps, n, t.settleFrames + t.trialFrames, t);
	EXPECT_EQ(1, onBudget.step);
	EXPECT_DOUBLE_EQ(0.0, onBudget.refuseMs);
}

// [rc4l] Frames no shorter at the lower scale were not the GPU's, so the step down is undone and the next waits twice as long each time, up to sixteen times.
TEST(ComputeDynresStep, UntimedAStepDownThatDoesNotHelpIsUndone)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, false, steps);
	const DynresTuning t;
	DynresState s;
	double wait = 0.0;
	for (int round = 1; round <= 6; round++)
	{
		// [rc4l] Waiting frames step down once the undoing's settle and the refusal after it are out.
		int frames = 0;
		for (; s.step == 0 && frames < 10000; frames++) s = ComputeDynresStep(s, Sample(-1, 3, 30), 16.6, steps, n, t);
		ASSERT_EQ(1, s.step) << "round " << round;
		// [rc4l] The wait is in time, so frames of 30 ms wait it out in a thirtieth as many frames.
		EXPECT_EQ(round == 1 ? t.untimedMisses : t.settleFrames + (int)std::ceil(wait / 30.0), frames) << "round " << round;
		s = Feed(s, Sample(-1, 3, 30), 16.6, steps, n, t.settleFrames + t.trialFrames, t);
		ASSERT_EQ(0, s.step) << "round " << round;
		wait = std::min(t.refuseMs * (1 << round), t.refuseMs * 16);
		EXPECT_DOUBLE_EQ(wait, s.downWaitMs) << "round " << round;
		EXPECT_DOUBLE_EQ(wait, s.refuseMs) << "round " << round;
	}
}
