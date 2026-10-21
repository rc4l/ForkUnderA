// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/mainloop/mainloop.h"
#include "features/mainloop/computation/mainloop_compute.h"
#include "i_system.h"   // [rc4l] I_MSTime
#include "i_video.h"   // [rc4l] I_WaitVBL
#include "st_start.h"   // [rc4l] ST_Endoom

namespace zx { namespace mainloop {

static FrameDriver g_driver = nullptr;
static bool g_quitPending = false;
static unsigned g_quitAt = 0;

bool Stepped() { return g_driver != nullptr; }
void SetDriver(FrameDriver driver) { g_driver = driver; }

void StartStepped(void (*frame)())
{
	g_driver(frame);
	throw SteppedLoopStarted();
}

void Quit(int vbls)
{
	if (!Stepped())
	{
		if (vbls > 0) I_WaitVBL(vbls);
		ST_Endoom();
		return;
	}
	g_quitPending = true;
	g_quitAt = I_MSTime() + ComputeVblMilliseconds(vbls);
}

bool QuitPending()
{
	return g_quitPending;
}

void FinishPendingQuit()
{
	if (!g_quitPending || !ComputeDeadlinePassed(g_quitAt, I_MSTime())) return;
	g_quitPending = false;
	ST_Endoom();
}

}} // namespace zx::mainloop
