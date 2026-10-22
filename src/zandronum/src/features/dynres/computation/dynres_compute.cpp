// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/dynres/computation/dynres_compute.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace zx { namespace dynres {

namespace {

const float kLinearSteps[] = { 1.f, 0.875f, 0.75f, 0.625f, 0.5f, 0.375f, 0.25f };
const float kPointSteps[] = { 1.f, 0.75f, 0.5f, 0.25f };
const double kCommonRates[] = { 30, 48, 50, 60, 72, 75, 85, 90, 100, 120, 144, 165, 170, 175, 180, 200, 240, 280, 300, 360, 480, 500, 540 };

DynresState Stepped(DynresState n, int step, const DynresTuning &t)
{
	n.step = step;
	n.settle = t.settleFrames;
	n.roomFrames = 0;
	n.haveAvg = false;
	n.quietMs = t.settleMs;
	n.winMs = 0.0;
	n.winFrames = 0;
	n.roomRun = 0;
	n.trial = false;
	n.changes++;
	return n;
}

// [rc4l] An untimed wait made twice as long, at least twice base and at most sixteen times it.
double Doubled(double wait, double base)
{
	return std::min(std::max(wait, base) * 2, base * 16);
}

// [rc4l] A step whose next whole window is judged against the window before it.
DynresState Trial(DynresState n, int step, bool up, double beforeMs, const DynresTuning &t)
{
	n = Stepped(n, step, t);
	n.beforeMs = beforeMs;
	n.trial = true;
	n.trialUp = up;
	if (up) n.sinceUpMs = n.roomMs = 0.0;
	return n;
}

// [rc4l] The window after a step: a step down is kept only if it shortened the frames and a step up undone if it made slow frames longer, each undoing doubling its direction's wait, and a step kept is followed at once by the next while the frames stay slow or keep room.
DynresState Judged(DynresState n, double mean, double budgetMs, int count, const DynresTuning &t)
{
	n.trial = false;
	const bool slow = mean > budgetMs * t.slowAt;
	if (n.trialUp)
	{
		if (slow && mean * t.trialGain > n.beforeMs)
		{
			n.upWaitMs = Doubled(n.upWaitMs, t.upWaitMs);
			n.upFailMs = n.beforeMs;
			n.roomMs = 0.0;
			return Stepped(n, n.step + 1, t);
		}
		return mean < budgetMs * t.roomAt && n.step > 0 ? Trial(n, n.step - 1, true, mean, t) : n;
	}
	// [rc4l] Frames no shorter at the lower scale were not the GPU's, however near on time they came.
	if (mean > n.beforeMs * t.keepAt)
	{
		n.downWaitMs = Doubled(n.downWaitMs, t.refuseMs);
		n.refuseMs = n.downWaitMs;
		n.refusedAtMs = n.beforeMs;
		return Stepped(n, n.step - 1, t);
	}
	// [rc4l] The wait to step up starts at the scale kept, while a step down undone leaves the wait where it was.
	n.roomMs = 0.0;
	return slow && n.step + 1 < count ? Trial(n, n.step + 1, false, mean, t) : n;
}

// [rc4l] A window with no step to judge: slow frames step down unless one was just undone at this load, and below full scale, room or the wait steps up.
DynresState Windowed(DynresState n, double mean, double budgetMs, int count, const DynresTuning &t)
{
	if (n.refuseMs > 0.0 && mean > n.refusedAtMs * t.rearmAt) n.refuseMs = 0.0;
	if (mean > budgetMs * t.slowAt && n.step + 1 < count && n.refuseMs <= 0.0)
	{
		// [rc4l] A step up soon followed by slow frames was a guess too early, so the next waits twice as long.
		if (n.sinceUpMs < t.upWaitMs) n.upWaitMs = Doubled(n.upWaitMs, t.upWaitMs);
		return Trial(n, n.step + 1, false, mean, t);
	}
	// [rc4l] A wait earned under a heavier load is forgotten once the frames come a tenth under what they were then.
	if (n.upFailMs > 0.0 && mean < n.upFailMs * t.trialGain)
	{
		n.upWaitMs = 0.0;
		n.upFailMs = 0.0;
	}
	if (n.step == 0) return n;
	n.roomRun = mean < budgetMs * t.roomAt ? n.roomRun + 1 : 0;
	if (n.roomRun >= t.roomWindows || n.roomMs >= std::max(n.upWaitMs, t.upWaitMs)) return Trial(n, n.step - 1, true, mean, t);
	return n;
}

// [rc4l] Untimed, from the frames alone, judged by their mean over whole windows since Firefox lets several through and then holds one for the GPU, and a browser may wait on the GPU inside its own calls; each step is a trial the next window keeps or undoes.
DynresState UntimedStep(DynresState n, const DynresSample &x, double budgetMs, int count, const DynresTuning &t)
{
	if (!(x.frameMs > 0.0)) return n;
	n.sinceUpMs += x.frameMs;
	if (n.refuseMs > 0.0) n.refuseMs -= x.frameMs;
	if (n.step > 0) n.roomMs += x.frameMs;
	if (n.quietMs > 0.0)
	{
		n.quietMs -= x.frameMs;
		return n;
	}
	n.winMs += x.frameMs;
	n.winFrames++;
	if (n.winMs < t.windowMs) return n;
	const double mean = n.winMs / n.winFrames;
	n.lastWindowMs = mean;
	n.winMs = 0.0;
	n.winFrames = 0;
	return n.trial ? Judged(n, mean, budgetMs, count, t) : Windowed(n, mean, budgetMs, count, t);
}
}

double ComputeFrameBudgetMs(const DynresCaps &caps)
{
	// [rc4l] Untimed, a refresh read under 60 Hz may be the GPU's own lag, so it is taken for no slower display than that.
	const double refresh = caps.untimed && caps.refreshHz > 0.0 ? std::max(caps.refreshHz, kUntimedHz) : caps.refreshHz;
	double hz = caps.targetFps > 0 ? caps.targetFps : (refresh > 0.0 && !caps.untimed ? refresh : kUntimedHz);
	if (caps.vsync && refresh > 0.0 && refresh < hz) hz = refresh;
	if (caps.limitFps > 0 && caps.limitFps < hz) hz = caps.limitFps;
	return 1000.0 / hz;
}

int ComputeDynresSteps(float minScale, bool pointFilter, float out[kMaxDynresSteps])
{
	const float *all = pointFilter ? kPointSteps : kLinearSteps;
	const int total = pointFilter ? (int)(sizeof(kPointSteps) / sizeof(kPointSteps[0])) : (int)(sizeof(kLinearSteps) / sizeof(kLinearSteps[0]));
	int n = 0;
	for (int i = 0; i < total; i++)
		if (i == 0 || all[i] >= minScale - 1e-6f) out[n++] = all[i];
	return n;
}

DynresState ComputeDynresStep(const DynresState &s, const DynresSample &x, double budgetMs, const float *steps, int count, const DynresTuning &t)
{
	DynresState n = s;
	if (n.step >= count) n.step = count > 0 ? count - 1 : 0;
	if (n.settle > 0)
	{
		n.settle--;
		return n;
	}
	if (x.gpuMs < 0.0) return UntimedStep(n, x, budgetMs, count, t);
	n.gpuAvgMs = n.haveAvg ? n.gpuAvgMs + t.weight * (x.gpuMs - n.gpuAvgMs) : x.gpuMs;
	n.haveAvg = true;
	if (n.gpuAvgMs > budgetMs * t.downAt && n.gpuAvgMs >= x.cpuMs && n.step + 1 < count)
		return Stepped(n, n.step + 1, t);
	if (n.step > 0)
	{
		const double ratio = steps[n.step - 1] / steps[n.step];
		n.roomFrames = n.gpuAvgMs * ratio * ratio <= budgetMs * t.upAt ? n.roomFrames + 1 : 0;
		if (n.roomFrames >= t.upFrames) return Stepped(n, n.step - 1, t);
	}
	return n;
}

const char *ComputeDynresWhy(const DynresState &was, const DynresState &now, bool timed)
{
	if (now.changes == was.changes) return "";
	if (timed) return now.step > was.step ? "the GPU is over budget" : "the GPU has room";
	if (now.trial) return now.trialUp ? "trying a step up" : "trying a step down, the frames were slow";
	return now.step > was.step ? "a step up that made slow frames longer, undone" : "a step down that left the frames no shorter, undone";
}

bool ComputeFrameJudged(bool timed, bool running, bool focused)
{
	return timed || (running && focused);
}

DynresState ComputeDynresUnjudged(const DynresState &s, int settleFrames)
{
	DynresState n = s;
	n.settle = std::max(n.settle, settleFrames);
	n.roomFrames = 0;
	n.roomRun = 0;
	n.winMs = 0.0;
	n.winFrames = 0;
	return n;
}

double ComputeMedianIntervalMs(const double *ms, int n)
{
	if (n <= 0) return 0.0;
	std::vector<double> sorted(ms, ms + n);
	std::sort(sorted.begin(), sorted.end());
	return sorted[n / 2];
}

SceneRect ComputeSceneRect(int frameW, int frameH, float scale)
{
	SceneRect r;
	if (scale >= 1.f)
	{
		r.w = frameW;
		r.h = frameH;
		return r;
	}
	r.w = (int)std::lround(frameW * scale);
	r.h = (int)std::lround(frameH * scale);
	if (r.w < 1) r.w = 1;
	if (r.h < 1) r.h = 1;
	return r;
}

double ComputeRefreshFromIntervalMs(double shortestMs)
{
	if (!(shortestMs > 0.0)) return 0.0;
	const double hz = 1000.0 / shortestMs;
	if (hz > 1000.0) return 0.0;
	double nearest = kCommonRates[0];
	for (double rate : kCommonRates)
		if (std::fabs(hz - rate) < std::fabs(hz - nearest)) nearest = rate;
	return std::fabs(hz - nearest) <= nearest * 0.04 ? nearest : hz;
}

double ComputeFrameRefreshHz(FrameRefresh &r, double frameMs)
{
	if (frameMs > 0.0)
	{
		r.intervals[r.next] = frameMs;
		r.next = (r.next + 1) % kRefreshFrames;
		if (r.count < kRefreshFrames) r.count++;
		r.sinceWindow++;
	}
	const double hz = ComputeRefreshFromIntervalMs(ComputeMedianIntervalMs(r.intervals, r.count));
	if (r.sinceWindow >= kRefreshFrames)
	{
		r.sinceWindow = 0;
		r.fastestHz = std::max(r.fastestHz, hz);
	}
	return std::max(hz, r.fastestHz);
}

}} // namespace zx::dynres
