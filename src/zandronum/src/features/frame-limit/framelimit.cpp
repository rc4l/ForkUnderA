// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame-limit/framelimit.h"
#include "features/frame-limit/computation/framelimit_compute.h"
#include "features/mainloop/mainloop.h"
#include "features/telemetry/telemetry.h"

#include "c_cvars.h"
#include "doomdef.h"   // [rc4l] TICRATE

#include <chrono>
#include <thread>

// [rc4l] The one vid_maxfps, 0 for none, held to 35..1000 as upstream does.
CUSTOM_CVAR(Int, vid_maxfps, 500, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (self < TICRATE && self != 0) self = TICRATE;
	else if (self > 1000) self = 1000;
}

EXTERN_CVAR(Bool, cl_capfps)

namespace zx { namespace framelimit {

namespace {

// [rc4l] How early a browser's frame may come and still be drawn for its deadline.
const long long kSteppedToleranceUs = 2000;

long long g_deadlineUs = 0;

long long NowUs()
{
	return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}

int LimitFps()
{
	return ComputeFrameLimitFps(vid_maxfps, cl_capfps, TICRATE);
}

void WaitBeforePresent()
{
	const int fps = LimitFps();
	if (fps <= 0 || zx::mainloop::Stepped())
	{
		g_deadlineUs = 0;
		return;
	}
	ZX_ZONE("frame/limit_wait");
	g_deadlineUs = ComputeFrameDeadline(g_deadlineUs, NowUs(), fps);
	for (long long sleep = ComputeFrameLimitSleepUs(g_deadlineUs, NowUs()); sleep >= 0; sleep = ComputeFrameLimitSleepUs(g_deadlineUs, NowUs()))
		std::this_thread::sleep_for(std::chrono::microseconds(sleep));
}

bool SteppedFrameDue()
{
	const int fps = LimitFps();
	if (fps <= 0)
	{
		g_deadlineUs = 0;
		return true;
	}
	const long long now = NowUs();
	const long long deadline = ComputeFrameDeadline(g_deadlineUs, now, fps);
	if (!ComputeSteppedFrameDue(deadline, now, kSteppedToleranceUs)) return false;
	g_deadlineUs = deadline;
	return true;
}

}} // namespace zx::framelimit
