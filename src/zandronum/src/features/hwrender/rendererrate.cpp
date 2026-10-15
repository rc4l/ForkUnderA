// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] vid_fps per renderer: GL's frame and the backend's, each split at LiveFrame and summed into its last whole second.

#include "features/hwrender/backendonly.h"

#include "stats.h"
#include "i_system.h"
#include "features/hwrender/computation/rendererrate_compute.h"

namespace zx { namespace hwrender {

static cycle_t       g_frameClock;
static RendererRate  g_glRate = ComputeRateStart(), g_backendRate = ComputeRateStart();

void FrameTimingBegin()
{
	g_frameClock.Reset();
	g_frameClock.Clock();
}

// [rc4l] The time since the last split, added to rate, and the clock started again for the next part of the frame.
static void SplitInto(RendererRate &rate)
{
	g_frameClock.Unclock();
	ComputeRateFrame(rate, I_MSTime(), g_frameClock.TimeMS());
	g_frameClock.Reset();
	g_frameClock.Clock();
}

void GLFrameDone() { SplitInto(g_glRate); }
void BackendFrameDone() { SplitInto(g_backendRate); }
bool BackendRateLive() { return ComputeRateLive(g_backendRate, I_MSTime()); }

void RendererRateShown(bool backend, int &ms, int &fps)
{
	const RendererRate &r = backend ? g_backendRate : g_glRate;
	ms = r.shownMs;
	fps = r.shownFps;
}

}} // namespace zx::hwrender
