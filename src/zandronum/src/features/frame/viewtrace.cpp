// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/viewtrace.h"
#include "features/frame/computation/viewtrace_compute.h"
#include "features/framephase/framephase.h"   // [rc4l] each frame's phases

#include "c_console.h"  // [rc4l] ConsoleState
#include "c_dispatch.h"
#include "doomstat.h"   // [rc4l] gametic, paused, menuactive
#include "r_utility.h"  // [rc4l] viewangle, LocalViewAngle, r_frameSerial

#include <chrono>
#include <stdlib.h>
#include <vector>

namespace zx { namespace frame {

namespace {

std::vector<ViewTraceSample> g_trace;
int g_want = 0;
unsigned g_lastSerial = 0;
FString g_csv;   // [rc4l] where every frame is written when the trace is done, empty for none
MouseTally g_mouse = MouseTally();
bool g_grabbed = false;
bool g_active = true;

long long NowUs()
{
	return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Report()
{
	const ViewTraceSummary r = ComputeViewTraceSummary(g_trace.data(), (int)g_trace.size());
	Printf("fua_viewtrace over %d frames (%.0f ms): %d ran a tic; the view turned on %d frames, %.1f deg in all, %.1f deg of it on tic frames\n",
		r.frames, r.spanMs, r.ticFrames, r.turnedFrames, r.turnDeg, r.ticTurnDeg);
	Printf("  largest step %.2f deg, longest gap between turning frames %.1f ms, frames the mouse moved but the view did not: %d\n",
		r.maxStepDeg, r.maxStillMs, r.mouseOnlyFrames);
	Printf("  %d gaps of %.0f ms or more between turning frames, each ended by a turn of %.2f deg against %.2f deg on a turning frame\n",
		r.longGaps, kLongGapMs, r.stepAfterGapDeg, r.meanStepDeg);
	if (g_csv.Len() > 0)
	{
		FILE *f = fopen(g_csv.GetChars(), "w");
		if (f != NULL)
		{
			fprintf(f, "us,deg,tic,local,view_at,present_at");
			for (int p = 0; p < zx::framephase::kPhaseCount; p++) fprintf(f, ",%s", zx::framephase::ComputePhaseName(p));
			fprintf(f, ",mouse_msgs,mouse_ignored,mouse_dx,mouse_dy,mouse_to_view,mouse_kept,grabbed,active,paused,menu,console\n");
			for (size_t i = 1; i < g_trace.size(); i++)
			{
				fprintf(f, "%lld,%.4f,%d,%u,%lld", g_trace[i].us - g_trace[0].us, ComputeTurnDegrees(g_trace[i - 1].angle, g_trace[i].angle),
					g_trace[i].gametic != g_trace[i - 1].gametic ? 1 : 0, g_trace[i].localAngle, g_trace[i].us);
				// [rc4l] Sample i+1 carries frame i's phases, recorded at the boundary that closed it; its present is on the absolute clock, for lining up with a screen capture.
				const float *ph = i + 1 < g_trace.size() ? g_trace[i + 1].phases : NULL;
				fprintf(f, ",%lld", i + 1 < g_trace.size() ? g_trace[i + 1].presentAt : 0LL);
				for (int p = 0; p < zx::framephase::kPhaseCount; p++) fprintf(f, ",%.0f", ph != NULL ? ph[p] : 0.f);
				const ViewTraceSample &v = g_trace[i];
				fprintf(f, ",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", v.mouse.messages, v.mouse.ignored, v.mouse.dx, v.mouse.dy, v.mouse.toView,
					v.mouse.keptFromView, v.grabbed, v.active, v.paused, v.menu, v.console);
			}
			fclose(f);
			Printf("  every frame written to %s\n", g_csv.GetChars());
		}
	}
	Printf("  per frame, ms since the last / deg turned, T where a tic ran:\n");
	FString line;
	const size_t rows = g_trace.size() < 97 ? g_trace.size() : 97;
	for (size_t i = 1; i < rows; i++)
	{
		line.AppendFormat(" %.1f/%.2f%s", (g_trace[i].us - g_trace[i - 1].us) / 1000.0,
			ComputeTurnDegrees(g_trace[i - 1].angle, g_trace[i].angle), g_trace[i].gametic != g_trace[i - 1].gametic ? "T" : "");
		if (i % 12 == 0) { Printf("%s\n", line.GetChars()); line = ""; }
	}
	if (line.Len() > 0) Printf("%s\n", line.GetChars());
}

} // namespace

void NoteMainView()
{
	if (g_want <= 0 || r_frameSerial == g_lastSerial) return;
	g_lastSerial = r_frameSerial;
	ViewTraceSample s;
	s.us = NowUs();
	s.angle = (unsigned)viewangle;
	s.localAngle = (unsigned)LocalViewAngle;
	s.gametic = gametic;
	const float *phases = zx::framephase::LastFramePhases();
	s.presentAt = zx::framephase::LastFramePhaseStarts()[zx::framephase::kPhasePresent];
	for (int p = 0; p < zx::framephase::kPhaseCount; p++) s.phases[p] = phases[p];
	s.mouse = ComputeMouseTallyTake(g_mouse);
	s.grabbed = g_grabbed;
	s.active = g_active;
	s.paused = paused != 0;
	s.menu = menuactive != MENU_Off;
	s.console = ConsoleState == c_down || ConsoleState == c_falling;
	g_trace.push_back(s);
	if ((int)g_trace.size() < g_want) return;
	Report();
	g_want = 0;
}

void EndViewTrace()
{
	if (g_want <= 0) return;
	g_want = 0;
	if (g_trace.size() >= 2) Report();
}

void NoteRawMouse(int dx, int dy, bool taken)
{
	if (g_want > 0) ComputeMouseTallyRaw(g_mouse, dx, dy, taken);
}

void NoteMouseEvent(bool toView)
{
	if (g_want > 0) ComputeMouseTallyEvent(g_mouse, toView);
}

void NoteMouseGrabbed(bool grabbed)
{
	g_grabbed = grabbed;
}

void NoteAppActive(bool active)
{
	g_active = active;
}

void ArmViewTrace(int frames, const char *csv)
{
	g_csv = csv != NULL ? csv : "";
	g_trace.clear();
	g_trace.reserve(frames);
	g_mouse = MouseTally();
	g_want = frames;
	Printf("fua_viewtrace: recording %d frames\n", frames);
}

}} // namespace zx::frame

// [rc4l] fua_viewtrace [frames] [file.csv]: record the next frames' main view (default 1000, 16 to 300000, minutes at any frame rate), say whether it turned between tics or only at them, and write every frame to the file when one is named; `fua_viewtrace end` reports early.
CCMD( fua_viewtrace )
{
	if (argv.argc() > 1 && stricmp(argv[1], "end") == 0) { zx::frame::EndViewTrace(); return; }
	const int n = argv.argc() > 1 ? atoi(argv[1]) : 1000;
	zx::frame::ArmViewTrace(n < 16 ? 16 : n > 300000 ? 300000 : n, argv.argc() > 2 ? argv[2] : NULL);
}
