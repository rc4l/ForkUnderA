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
	if (step < n.step) n.sinceUp = 0;
	n.step = step;
	n.settle = t.settleFrames;
	n.roomFrames = 0;
	n.missStreak = 0;
	n.haveAvg = false;
	n.frameAvgMs = 0.0;
	n.trial = 0;
	n.changes++;
	return n;
}

// [rc4l] An untimed wait made twice as long, at least twice base and at most sixteen times it.
template <typename T> T Doubled(T wait, T base)
{
	return std::min(std::max(wait, base) * 2, base * 16);
}

// [rc4l] A step whose frames are judged against the frames before it.
DynresState Trial(DynresState n, int step, bool up, const DynresTuning &t)
{
	const double before = n.frameAvgMs;
	n = Stepped(n, step, t);
	n.beforeMs = before;
	n.trial = t.trialFrames;
	n.trialUp = up;
	return n;
}

// [rc4l] Untimed, from the frame alone: a run of frames that waited on the GPU steps down and a wait steps up, each judged once settled and undone unless the frames bear it out, so a scale that changes nothing goes back to full; each direction's wait doubles when its step is undone.
DynresState UntimedStep(DynresState n, const DynresSample &x, double budgetMs, int count, const DynresTuning &t)
{
	n.frameAvgMs = n.frameAvgMs > 0.0 ? n.frameAvgMs + t.weight * (x.frameMs - n.frameAvgMs) : x.frameMs;
	if (n.trial > 0)
	{
		if (--n.trial > 0) return n;
		const bool late = n.frameAvgMs > budgetMs * t.lateAt;
		// [rc4l] A step up that made late frames longer cost what the GPU could not give.
		if (n.trialUp && late && n.frameAvgMs * t.trialGain > n.beforeMs)
		{
			n.upWait = Doubled(n.upWait, t.upFramesUntimed);
			n.upFailMs = n.beforeMs;
			return Stepped(n, n.step + 1, t);
		}
		// [rc4l] A step down that left the frames no shorter was not the GPU's to make, however near on time they came.
		if (!n.trialUp && n.frameAvgMs > n.beforeMs * t.trialGain)
		{
			n.downWaitMs = Doubled(n.downWaitMs, t.refuseMs);
			n.refuseMs = n.downWaitMs;
			return Stepped(n, n.step - 1, t);
		}
		return n;
	}
	if (n.refuseMs > 0.0) n.refuseMs -= x.frameMs;
	n.missStreak = ComputeFrameWaitedOnGpu(x, budgetMs, t) ? n.missStreak + 1 : 0;
	if (n.missStreak >= t.untimedMisses && n.step + 1 < count && n.refuseMs <= 0.0)
	{
		if (n.sinceUp < t.upFramesUntimed) n.upWait = Doubled(n.upWait, t.upFramesUntimed);
		return Trial(n, n.step + 1, false, t);
	}
	// [rc4l] A wait earned under a heavier load is forgotten once the frames, settled, come a tenth under what they were then.
	if (n.upFailMs > 0.0 && n.roomFrames >= t.settleFrames && n.frameAvgMs < n.upFailMs * t.trialGain)
	{
		n.upWait = 0;
		n.upFailMs = 0.0;
	}
	if (n.step > 0 && ++n.roomFrames >= std::max(n.upWait, t.upFramesUntimed)) return Trial(n, n.step - 1, true, t);
	return n;
}

}

double ComputeFrameBudgetMs(const DynresCaps &caps)
{
	double hz = caps.targetFps > 0 ? caps.targetFps : (caps.refreshHz > 0.0 ? caps.refreshHz : 60.0);
	if (caps.vsync && caps.refreshHz > 0.0 && caps.refreshHz < hz) hz = caps.refreshHz;
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

bool ComputeFrameWaitedOnGpu(const DynresSample &x, double budgetMs, const DynresTuning &t)
{
	return x.frameMs > budgetMs * t.lateAt && x.cpuMs < x.frameMs * 0.6;
}

DynresState ComputeDynresStep(const DynresState &s, const DynresSample &x, double budgetMs, const float *steps, int count, const DynresTuning &t)
{
	DynresState n = s;
	if (n.step >= count) n.step = count > 0 ? count - 1 : 0;
	if (n.sinceUp < (1 << 20)) n.sinceUp++;
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

bool ComputeFrameJudged(bool timed, bool running, bool focused)
{
	return timed || (running && focused);
}

DynresState ComputeDynresUnjudged(const DynresState &s, int settleFrames)
{
	DynresState n = s;
	n.settle = std::max(n.settle, settleFrames);
	n.missStreak = 0;
	n.roomFrames = 0;
	n.frameAvgMs = 0.0;
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
	double hz = ComputeRefreshFromIntervalMs(ComputeMedianIntervalMs(r.intervals, r.count));
	if (r.sinceWindow >= kRefreshFrames)
	{
		r.sinceWindow = 0;
		r.windowHz[r.nextWindow] = hz;
		r.nextWindow = (r.nextWindow + 1) % kRefreshWindows;
		if (r.windows < kRefreshWindows) r.windows++;
	}
	for (int i = 0; i < r.windows; i++) hz = std::max(hz, r.windowHz[i]);
	return hz;
}

}} // namespace zx::dynres
