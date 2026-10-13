// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/viewtrace.h"
#include "features/frame/computation/viewtrace_compute.h"
#include "features/framephase/framephase.h"   // [rc4l] each frame's phases

#include "c_console.h"  // [rc4l] ConsoleState
#include "c_dispatch.h"
#include "doomstat.h"   // [rc4l] gametic, paused, menuactive
#include "r_utility.h"  // [rc4l] viewangle, LocalViewAngle, r_frameSerial

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
			char line[1024];
			ComputeViewTraceCsvHeader(line, sizeof(line));
			fprintf(f, "%s\n", line);
			for (int i = 1; i < (int)g_trace.size(); i++)
			{
				ComputeViewTraceCsvRow(line, sizeof(line), g_trace.data(), (int)g_trace.size(), i);
				fprintf(f, "%s\n", line);
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
	s.us = zx::framephase::NowUs();
	s.angle = (unsigned)viewangle;
	s.localAngle = (unsigned)LocalViewAngle;
	s.gametic = gametic;
	const float *phases = zx::framephase::LastFramePhases();
	s.presentAt = zx::framephase::LastFramePhaseStarts()[zx::framephase::kPhasePresent];
	s.frameUs = zx::framephase::LastFrameUs();
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
