// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/computation/viewtrace_compute.h"

#include <math.h>

namespace zx { namespace frame {

void ComputeMouseTallyRaw(MouseTally &t, int dx, int dy, bool taken)
{
	t.messages++;
	if (!taken) { t.ignored++; return; }
	t.dx += dx;
	t.dy += dy;
}

void ComputeMouseTallyEvent(MouseTally &t, bool toView)
{
	(toView ? t.toView : t.keptFromView)++;
}

MouseTally ComputeMouseTallyTake(MouseTally &t)
{
	const MouseTally r = t;
	t = MouseTally();
	return r;
}

double ComputeTurnDegrees(unsigned from, unsigned to)
{
	return (double)(int)(to - from) * 360.0 / 4294967296.0;
}

ViewTraceSummary ComputeViewTraceSummary(const ViewTraceSample *s, int n)
{
	ViewTraceSummary r = { n, 0.0, 0, 0, 0, 0.0, 0.0, 0.0, 0.0, 0, 0.0, 0.0 };
	if (n < 2) return r;
	r.spanMs = (s[n - 1].us - s[0].us) / 1000.0;
	long long lastTurnUs = -1;
	for (int i = 1; i < n; i++)
	{
		const bool tic = s[i].gametic != s[i - 1].gametic;
		const double step = fabs(ComputeTurnDegrees(s[i - 1].angle, s[i].angle));
		if (tic) r.ticFrames++;
		if (step > 0.0)
		{
			r.turnedFrames++;
			r.turnDeg += step;
			if (tic) r.ticTurnDeg += step;
			if (step > r.maxStepDeg) r.maxStepDeg = step;
			const double gapMs = lastTurnUs >= 0 ? (s[i].us - lastTurnUs) / 1000.0 : 0.0;
			if (gapMs > r.maxStillMs) r.maxStillMs = gapMs;
			if (gapMs >= kLongGapMs) { r.longGaps++; r.stepAfterGapDeg += step; }
			lastTurnUs = s[i].us;
		}
		else if (!tic && s[i].localAngle != s[i - 1].localAngle) r.mouseOnlyFrames++;
	}
	if (r.longGaps > 0) r.stepAfterGapDeg /= r.longGaps;
	if (r.turnedFrames > 0) r.meanStepDeg = r.turnDeg / r.turnedFrames;
	return r;
}

}} // namespace zx::frame
