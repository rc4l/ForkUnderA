// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/computation/viewtrace_compute.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>

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

namespace {

// [rc4l] Appends to `out` while it fits, always terminated, counting what was asked for.
struct Line
{
	char *out;
	int size, len;
	void Add(const char *fmt, ...)
	{
		va_list ap;
		va_start(ap, fmt);
		const int room = len < size ? size - len : 0;
		const int n = vsnprintf(room > 0 ? out + len : NULL, room, fmt, ap);
		va_end(ap);
		len += n;
	}
	int Done() const { return len < size ? len : size - 1; }
};

} // namespace

int ComputeViewTraceCsvHeader(char *out, int size)
{
	Line l = { out, size, 0 };
	l.Add("us,deg,tic,local,view_at,present_at,frame_us");
	for (int p = 0; p < zx::framephase::kPhaseCount; p++) l.Add(",%s", zx::framephase::ComputePhaseName(p));
	l.Add(",mouse_msgs,mouse_ignored,mouse_dx,mouse_dy,mouse_to_view,mouse_kept,grabbed,active,paused,menu,console,x,y,frac,clock,tic_next");
	return l.Done();
}

int ComputeViewTraceCsvRow(char *out, int size, const ViewTraceSample *s, int n, int i)
{
	Line l = { out, size, 0 };
	const ViewTraceSample &v = s[i];
	const ViewTraceSample *next = i + 1 < n ? &s[i + 1] : NULL;
	l.Add("%lld,%.4f,%d,%u,%lld,%lld,%lld", v.us - s[0].us, ComputeTurnDegrees(s[i - 1].angle, v.angle), v.gametic != s[i - 1].gametic ? 1 : 0,
		v.localAngle, v.us, next != NULL ? next->presentAt : 0LL, next != NULL ? next->frameUs : 0LL);
	for (int p = 0; p < zx::framephase::kPhaseCount; p++) l.Add(",%.0f", next != NULL ? next->phases[p] : 0.f);
	l.Add(",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", v.mouse.messages, v.mouse.ignored, v.mouse.dx, v.mouse.dy, v.mouse.toView,
		v.mouse.keptFromView, v.grabbed, v.active, v.paused, v.menu, v.console);
	l.Add(",%.3f,%.3f,%.3f,%d,%u", v.x, v.y, v.frac, v.clock, v.ticNext);
	return l.Done();
}

int ComputeViewBackSteps(const ViewTraceSample *s, int n, float minMove)
{
	int back = 0;
	bool lastBack = false;   // [rc4l] the step forward again after a back step is the same rubber band
	for (int i = 2; i < n; i++)
	{
		const float ax = s[i - 1].x - s[i - 2].x, ay = s[i - 1].y - s[i - 2].y;
		const float bx = s[i].x - s[i - 1].x, by = s[i].y - s[i - 1].y;
		const float len = sqrtf(ax * ax + ay * ay);
		const bool against = len > minMove && ax * bx + ay * by < -minMove * len;
		if (against && !lastBack) back++;
		lastBack = against && !lastBack;
	}
	return back;
}

int ComputeViewLurches(const ViewTraceSample *s, int n)
{
	if (n < 3) return 0;
	const double spanUs = (double)(s[n - 1].us - s[0].us);
	if (spanUs <= 0.0) return 0;
	const double dx = s[n - 1].x - s[0].x, dy = s[n - 1].y - s[0].y;
	const double speed = sqrt(dx * dx + dy * dy) / spanUs;   // [rc4l] map units a microsecond
	const double halfTic = speed * 1000000.0 / 35.0 * 0.5;
	if (halfTic <= 0.0) return 0;
	int lurches = 0;
	for (int i = 1; i < n; i++)
	{
		const double sx = s[i].x - s[i - 1].x, sy = s[i].y - s[i - 1].y;
		const double step = sqrt(sx * sx + sy * sy);
		if (step - speed * (double)(s[i].us - s[i - 1].us) > halfTic) lurches++;
	}
	return lurches;
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
