// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <functional>

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

TEST(ComputeMedianIntervalMs, NeitherOneEarlyFrameNorJitterPullsItShort)
{
	double ms[20];
	for (int i = 0; i < 20; i++) ms[i] = 16.7;
	ms[3] = 2.0;
	EXPECT_DOUBLE_EQ(16.7, ComputeMedianIntervalMs(ms, 20));
	// [rc4l] A 480 Hz display's intervals jitter either side of 2.08 ms, whose low tenth read 540 Hz in Firefox.
	for (int i = 0; i < 20; i++) ms[i] = i < 4 ? 1.85 : (i < 16 ? 2.08 : 2.31);
	EXPECT_DOUBLE_EQ(2.08, ComputeMedianIntervalMs(ms, 20));
	EXPECT_DOUBLE_EQ(480.0, ComputeRefreshFromIntervalMs(ComputeMedianIntervalMs(ms, 20)));
	EXPECT_DOUBLE_EQ(0.0, ComputeMedianIntervalMs(ms, 0));
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

TEST(ComputeFrameJudged, AnUntimedFrameOnlyWhileTheGameRunsFocused)
{
	EXPECT_TRUE(ComputeFrameJudged(true, false, false));
	EXPECT_TRUE(ComputeFrameJudged(false, true, true));
	EXPECT_FALSE(ComputeFrameJudged(false, false, true));
	EXPECT_FALSE(ComputeFrameJudged(false, true, false));
}

namespace {

// [rc4l] A load: the frame it makes at a scale, by the frame's number.
using Load = std::function<DynresSample(float scale, int frame)>;

// [rc4l] Plays ms of a load's frames through the controller, each at the scale it stands at, and returns the state and the share of that time spent below full scale.
DynresState Play(DynresState s, const Load &load, double ms, double budget, const float *steps, int count, double *belowFull = nullptr)
{
	double played = 0.0, below = 0.0;
	for (int i = 0; played < ms; i++)
	{
		const DynresSample x = load(steps[s.step], i);
		if (s.step > 0) below += x.frameMs;
		played += x.frameMs;
		s = ComputeDynresStep(s, x, budget, steps, count, DynresTuning());
	}
	if (belowFull) *belowFull = below / played;
	return s;
}

// [rc4l] Plays a load's frames until done holds or ms pass, returning the state and in *at how long it took, ms when done never held.
DynresState PlayUntil(DynresState s, const Load &load, const std::function<bool(const DynresState &)> &done, double ms, double budget, const float *steps, int count, double *at)
{
	double played = 0.0;
	for (int i = 0; played < ms && !done(s); i++)
	{
		const DynresSample x = load(steps[s.step], i);
		played += x.frameMs;
		s = ComputeDynresStep(s, x, budget, steps, count, DynresTuning());
	}
	*at = played;
	return s;
}

// [rc4l] Frames of a fixed CPU cost and a GPU cost that shrinks with the pixels.
Load Costs(double cpu, double gpu)
{
	return [=](float scale, int) { return Sample(-1, cpu, cpu + gpu * scale * scale); };
}

// [rc4l] Firefox under GPU load in the Sunder fight: four frames of the CPU's work let through, then one held at the animation frame for as long as the GPU takes to catch up, which shrinks with the pixels.
DynresSample FirefoxStalls(float scale, int frame)
{
	return frame % 5 < 4 ? Sample(-1, 16, 18) : Sample(-1, 2, 2 + 150 * scale * scale);
}

// [rc4l] A browser that waits on the GPU inside its own calls, which count as the frame's CPU time.
DynresSample WaitsInCalls(float scale, int)
{
	const double frame = 8 + 30 * scale * scale;
	return Sample(-1, frame - 0.5, frame);
}

const double k60 = 1000.0 / 60;

}

// [rc4l] The user's Firefox in the Sunder fight ran at 14 fps and stepped down never, since no three frames in a row were late with the CPU idle; half scale ran it at 35.
TEST(ComputeDynresStep, UntimedFirefoxsStallsStepDown)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, true, steps);
	double below = 0.0;
	DynresState s = Play(DynresState(), FirefoxStalls, 4000, k60, steps, n);
	EXPECT_EQ(n - 1, s.step);
	// [rc4l] The tries back up while the load lasts never reach full scale, since the frames there are slow.
	s = Play(s, FirefoxStalls, 30000, k60, steps, n, &below);
	EXPECT_DOUBLE_EQ(1.0, below);
}

TEST(ComputeDynresStep, UntimedGpuWaitsCountedAsCpuStepDown)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, true, steps);
	double below = 0.0, at = 0.0;
	DynresState s = PlayUntil(DynresState(), WaitsInCalls, [&](const DynresState &d) { return d.step == n - 1 && !d.trial; }, 4500, k60, steps, n, &at);
	EXPECT_LT(at, 4500);
	s = Play(s, WaitsInCalls, 30000, k60, steps, n, &below);
	EXPECT_DOUBLE_EQ(1.0, below);
}

// [rc4l] It steps once a window's frames are slow, each step judged over the window after it.
TEST(ComputeDynresStep, UntimedASlowWindowStepsDownAtOnce)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, false, steps);
	const DynresTuning t;
	DynresState s = Play(DynresState(), Costs(4, 40), t.windowMs - 50, k60, steps, n);
	EXPECT_EQ(0, s.step);
	s = Play(s, Costs(4, 40), 100, k60, steps, n);
	EXPECT_EQ(1, s.step);
	EXPECT_TRUE(s.trial);
	EXPECT_DOUBLE_EQ(44.0, s.beforeMs);
	EXPECT_DOUBLE_EQ(44.0, s.lastWindowMs);
}

TEST(ComputeDynresStep, UntimedLightFramesNeverStepDown)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, false, steps);
	EXPECT_EQ(0, Play(DynresState(), Costs(4, 0), 60000, k60, steps, n).changes);
	// [rc4l] Frames on time at a display's 60 Hz, and a little over it as its timing jitters.
	EXPECT_EQ(0, Play(DynresState(), Costs(18.5, 0), 60000, k60, steps, n).changes);
}

// [rc4l] Frames the CPU holds are no shorter at a lower scale, so each step down is undone and the next waits longer, up to sixteen seconds while the load stays as it was.
TEST(ComputeDynresStep, UntimedACpuBoundFightTriesLessAndLess)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, true, steps);
	const DynresTuning t;
	double below = 0.0;
	DynresState s = Play(DynresState(), Costs(30, 0), 3000, k60, steps, n);
	ASSERT_EQ(0, s.step);
	EXPECT_EQ(2, s.changes);
	EXPECT_DOUBLE_EQ(t.refuseMs * 2, s.downWaitMs);
	EXPECT_DOUBLE_EQ(30.0, s.refusedAtMs);
	s = Play(s, Costs(30, 0), 120000, k60, steps, n);
	EXPECT_DOUBLE_EQ(t.refuseMs * 16, s.downWaitMs);
	s = Play(s, Costs(30, 0), 60000, k60, steps, n, &below);
	EXPECT_LT(below, 0.1);
}

// [rc4l] A step down refused at one load is tried again at once when the frames grow a quarter heavier, since the load is then another.
TEST(ComputeDynresStep, UntimedAHeavierLoadEndsARefusal)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, true, steps);
	double at = 0.0;
	const DynresState s = PlayUntil(DynresState(), Costs(30, 0), [](const DynresState &d) { return d.refuseMs > 0.0; }, 5000, k60, steps, n, &at);
	ASSERT_GT(s.refuseMs, 0.0);
	const DynresState same = Play(s, Costs(30, 0), 1000, k60, steps, n);
	EXPECT_EQ(0, same.step);
	EXPECT_GT(same.refuseMs, 0.0);
	const DynresState heavier = PlayUntil(s, Costs(4, 40), [](const DynresState &d) { return d.step == 1; }, 1500, k60, steps, n, &at);
	EXPECT_LT(at, 1500);
	EXPECT_TRUE(heavier.trial);
}

// [rc4l] The check's load ended at half scale with a long wait, which held it there for seconds; frames with room step straight back up.
TEST(ComputeDynresStep, UntimedTheEndOfALoadBringsFullScaleBack)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.25f, false, steps);
	DynresState s = Play(DynresState(), Costs(2, 40), 20000, k60, steps, n);
	ASSERT_GT(s.step, 1);
	s = Play(s, Costs(4, 2), 8000, k60, steps, n);
	EXPECT_EQ(0, s.step);
}

// [rc4l] Frames paced to a 60 Hz display never show room, so below full scale a step up is tried after the wait and kept when the frames stay on time.
TEST(ComputeDynresStep, UntimedAStepUpIsTriedAfterTheWait)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, true, steps);
	const DynresTuning t;
	DynresState s;
	s.step = 1;
	double at = 0.0;
	s = PlayUntil(s, Costs(k60, 0), [](const DynresState &d) { return d.step == 0; }, 10000, k60, steps, n, &at);
	EXPECT_GE(at, t.upWaitMs);
	EXPECT_LT(at, t.upWaitMs + 2 * t.windowMs);
	EXPECT_TRUE(s.trialUp);
	s = PlayUntil(s, Costs(k60, 0), [](const DynresState &d) { return !d.trial; }, 2000, k60, steps, n, &at);
	EXPECT_LT(at, 2000);
	EXPECT_EQ(0, s.step);
}

// [rc4l] A step up back into the load is undone and makes the next try wait twice as long, up to sixteen times, until frames a tenth lighter than before it forget the wait.
TEST(ComputeDynresStep, UntimedAStepUpIntoTheLoadIsUndoneAndWaitsLonger)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, true, steps);
	const DynresTuning t;
	DynresState s = Play(DynresState(), Costs(4, 40), 10000, k60, steps, n);
	ASSERT_EQ(n - 1, s.step);
	EXPECT_DOUBLE_EQ(t.upWaitMs * 2, s.upWaitMs);
	EXPECT_DOUBLE_EQ(14.0, s.upFailMs);
	s = Play(s, Costs(4, 40), 300000, k60, steps, n);
	EXPECT_EQ(n - 1, s.step);
	EXPECT_DOUBLE_EQ(t.upWaitMs * 16, s.upWaitMs);
	s = Play(s, Costs(4, 10), 2500, k60, steps, n);
	EXPECT_DOUBLE_EQ(0.0, s.upWaitMs);
	EXPECT_DOUBLE_EQ(0.0, s.upFailMs);
}

// [rc4l] A step up kept and soon followed by slow frames was a guess too early, so the next one waits twice as long; one long before keeps the wait it had.
TEST(ComputeDynresStep, UntimedAStepDownSoonAfterAStepUpWaitsLongerNextTime)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, true, steps);
	const DynresTuning t;
	const auto stepped = [](const DynresState &d) { return d.step == 1; };
	double at = 0.0;
	DynresState s;
	s.sinceUpMs = 500.0;
	s = PlayUntil(s, Costs(4, 40), stepped, 2000, k60, steps, n, &at);
	EXPECT_EQ(1, s.step);
	EXPECT_DOUBLE_EQ(t.upWaitMs * 2, s.upWaitMs);
	DynresState late;
	late.upWaitMs = 12000.0;
	late = PlayUntil(late, Costs(4, 40), stepped, 2000, k60, steps, n, &at);
	EXPECT_EQ(1, late.step);
	EXPECT_DOUBLE_EQ(12000.0, late.upWaitMs);
}

// [rc4l] An open console drew once a tic and a page out of focus was slowed, which stepped a browser down to its lowest scale.
TEST(ComputeDynresUnjudged, TheScaleHoldsAndItsWindowStartsOver)
{
	DynresState s;
	s.step = 1;
	s.roomFrames = 90;
	s.roomRun = 1;
	s.winMs = 700.0;
	s.winFrames = 20;
	s.settle = 3;
	DynresState held = ComputeDynresUnjudged(s, 30);
	EXPECT_EQ(1, held.step);
	EXPECT_EQ(30, held.settle);
	EXPECT_EQ(0, held.roomFrames);
	EXPECT_EQ(0, held.roomRun);
	EXPECT_DOUBLE_EQ(0.0, held.winMs);
	EXPECT_EQ(0, held.winFrames);
	s.settle = 40;
	EXPECT_EQ(40, ComputeDynresUnjudged(s, 30).settle);
}

TEST(ComputeDynresStep, UntimedAFrameOfNoLengthCountsForNothing)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, true, steps);
	DynresState s;
	s.winMs = 500.0;
	s.winFrames = 10;
	const DynresState after = ComputeDynresStep(s, Sample(-1, 0, 0), k60, steps, n, DynresTuning());
	EXPECT_DOUBLE_EQ(500.0, after.winMs);
	EXPECT_EQ(10, after.winFrames);
}

// [rc4l] A fight longer than the frames the refresh was held over read as a 30 Hz display, so its frames were on time and nothing stepped down.
TEST(ComputeFrameRefreshHz, AFightOfAnyLengthIsNoSlowerDisplay)
{
	FrameRefresh r;
	EXPECT_DOUBLE_EQ(0.0, ComputeFrameRefreshHz(r, 0.0));
	for (int i = 0; i < kRefreshFrames; i++) ComputeFrameRefreshHz(r, 1000.0 / 144);
	double hz = 0.0;
	for (int i = 0; i < 2000; i++) hz = ComputeFrameRefreshHz(r, 33.3);
	EXPECT_DOUBLE_EQ(144.0, hz);
	// [rc4l] A faster rate shows once it holds half the window, before the window is whole.
	for (int i = 0; i < kRefreshFrames / 2; i++) hz = ComputeFrameRefreshHz(r, 1000.0 / 240);
	EXPECT_DOUBLE_EQ(144.0, hz);
	hz = ComputeFrameRefreshHz(r, 1000.0 / 240);
	EXPECT_DOUBLE_EQ(240.0, hz);
}

// [rc4l] Untimed, the display's refresh is aimed at only up to 60 Hz, and a slower one read from the frames is no slower display than that, since slow frames read as one.
TEST(ComputeFrameBudgetMs, UntimedAimsAtSixtyUnlessATargetIsPicked)
{
	DynresCaps c = Caps(480, true, 500, 0);
	c.untimed = true;
	EXPECT_NEAR(k60, ComputeFrameBudgetMs(c), 1e-9);
	c.refreshHz = 35;
	EXPECT_NEAR(k60, ComputeFrameBudgetMs(c), 1e-9);
	c.refreshHz = 0;
	EXPECT_NEAR(k60, ComputeFrameBudgetMs(c), 1e-9);
	// [rc4l] A target picked is aimed at, under a display read at 60 Hz or over and under the frame limit.
	c.targetFps = 144;
	EXPECT_NEAR(1000.0 / 144, ComputeFrameBudgetMs(c), 1e-9);
	c.refreshHz = 96;
	EXPECT_NEAR(1000.0 / 96, ComputeFrameBudgetMs(c), 1e-9);
	c.refreshHz = 35;
	EXPECT_NEAR(k60, ComputeFrameBudgetMs(c), 1e-9);
	c.limitFps = 30;
	EXPECT_NEAR(1000.0 / 30, ComputeFrameBudgetMs(c), 1e-9);
}

TEST(ComputeDynresWhy, NamesTheStepAndWhatMadeIt)
{
	DynresState was, now;
	EXPECT_STREQ("", ComputeDynresWhy(was, now, false));
	now.changes = 1;
	now.step = 1;
	EXPECT_STREQ("the GPU is over budget", ComputeDynresWhy(was, now, true));
	EXPECT_STREQ("a step up that made slow frames longer, undone", ComputeDynresWhy(was, now, false));
	now.trial = true;
	EXPECT_STREQ("trying a step down, the frames were slow", ComputeDynresWhy(was, now, false));
	now.trialUp = true;
	EXPECT_STREQ("trying a step up", ComputeDynresWhy(was, now, false));
	was.step = 2;
	now.trial = false;
	EXPECT_STREQ("the GPU has room", ComputeDynresWhy(was, now, true));
	EXPECT_STREQ("a step down that left the frames no shorter, undone", ComputeDynresWhy(was, now, false));
}

// [rc4l] Frames the CPU holds just over the line at a lower scale tried a step down every few seconds, and each undoing restarted the wait to step up, so the probe's Firefox stayed at 3/4 after the load ended.
TEST(ComputeDynresStep, UntimedStepsDownUndoneLeaveTheWaitToStepUp)
{
	float steps[kMaxDynresSteps];
	const int n = ComputeDynresSteps(0.5f, true, steps);
	const DynresTuning t;
	DynresState s;
	s.step = 1;
	double at = 0.0;
	s = PlayUntil(s, Costs(21, 0), [](const DynresState &d) { return d.step == 0; }, 10000, k60, steps, n, &at);
	EXPECT_GE(s.changes, 2);
	EXPECT_LT(at, t.upWaitMs + 2000);
}
