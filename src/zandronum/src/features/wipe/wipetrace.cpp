// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/wipe/wipetrace.h"
#include "features/wipe/computation/wipetrace_compute.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "d_main.h"   // [rc4l] D_WipeRunning

namespace {

zx::wipe::WipeTraceLog g_log;

}

// [rc4l] Records what the backend presents around each wipe for fua_wipe_report, afresh each time it is set; each frame is read back, so it is for tests only.
CUSTOM_CVAR(Bool, fua_wipe_trace, false, 0)
{
	g_log = zx::wipe::WipeTraceLog();
}

namespace zx { namespace wipe {

bool TraceWanted() { return fua_wipe_trace; }

void NoteWipeStarted()
{
	if (fua_wipe_trace) g_log.NoteStarted();
}

void TracePresented(const unsigned char *rgb, int w, int h)
{
	Thumb thumb;
	ComputeThumb(rgb, w, h, thumb);
	g_log.Frame(thumb, D_WipeRunning());
}

}} // namespace zx::wipe

// [rc4l] The wipes fua_wipe_trace saw: how many began and how many the backend presented, and each presented frame's distance from the picture before the wipe and the one after.
CCMD(fua_wipe_report)
{
	const std::vector<zx::wipe::WipeRecord> &wipes = g_log.Wipes();
	Printf("wipes started %d, presented %d\n", g_log.Started(), (int)wipes.size());
	for (size_t i = 0; i < wipes.size(); i++)
	{
		std::vector<double> toBefore, toAfter;
		const double span = zx::wipe::ComputeWipeDistances(wipes[i], toBefore, toAfter);
		// [rc4l] And how far its before is from the last wipe's after, which holds the picture a level change should still be showing (an intermission's, not the old map's).
		const double sinceLast = i > 0 ? zx::wipe::ComputeThumbDistance(wipes[i].before, wipes[i - 1].after) : -1.0;
		Printf("wipe %d: %d frames, before to after %.2f, before to the last wipe's after %.2f\n", (int)i + 1, (int)wipes[i].frames.size(), span, sinceLast);
		for (size_t f = 0; f < toBefore.size(); f++) Printf("  frame %d: %.2f from before, %.2f from after\n", (int)f + 1, toBefore[f], toAfter[f]);
	}
}
