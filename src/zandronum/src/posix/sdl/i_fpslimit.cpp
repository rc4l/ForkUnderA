/*
** i_fpslimit.cpp
** The frame limiter, from hardware.cpp
**
**---------------------------------------------------------------------------
** Copyright 1998-2006 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

// [rc4l] Moved out of hardware.cpp whole, so a browser build, whose page paces its frames, leaves it out (features/web/webhost.cpp).

#include <signal.h>
#include <time.h>

#include "hardware.h"
#include "c_console.h"
#include "c_cvars.h"
#include "doomtype.h"

EXTERN_CVAR(Int, vid_maxfps);

// [rc4l] A ThreadSanitizer build cannot follow the thread glibc starts for a SIGEV_THREAD timer, whose first malloc crashes, so it runs uncapped.
#include "features/sanitizer/sanitizer.h"

#if !defined(__APPLE__) && !defined(ZX_THREAD_SANITIZER)
Semaphore FPSLimitSemaphore;

static void FPSLimitNotify(sigval val)
{
	SEMAPHORE_SIGNAL(FPSLimitSemaphore)
}

void I_SetFPSLimit(int limit)
{
	static sigevent FPSLimitEvent;
	static timer_t FPSLimitTimer;
	static bool FPSLimitTimerEnabled = false;
	static bool EventSetup = false;
	if(!EventSetup)
	{
		EventSetup = true;
		FPSLimitEvent.sigev_notify = SIGEV_THREAD;
		FPSLimitEvent.sigev_signo = 0;
		FPSLimitEvent.sigev_value.sival_int = 0;
		FPSLimitEvent.sigev_notify_function = FPSLimitNotify;
		FPSLimitEvent.sigev_notify_attributes = NULL;

		SEMAPHORE_INIT(FPSLimitSemaphore, 0, 0)
	}

	if (limit < 0)
	{
		limit = vid_maxfps;
	}
	// Kill any leftover timer.
	if (FPSLimitTimerEnabled)
	{
		timer_delete(FPSLimitTimer);
		FPSLimitTimerEnabled = false;
	}
	if (limit == 0)
	{ // no limit
		DPrintf("FPS timer disabled\n");
	}
	else
	{
		FPSLimitTimerEnabled = true;
		if(timer_create(CLOCK_REALTIME, &FPSLimitEvent, &FPSLimitTimer) == -1)
			Printf("Failed to create FPS limitter event\n");
		itimerspec period = { {0, 0}, {0, 0} };
		period.it_value.tv_nsec = period.it_interval.tv_nsec = 1000000000 / limit;
		if(timer_settime(FPSLimitTimer, 0, &period, NULL) == -1)
			Printf("Failed to set FPS limitter timer\n");
		DPrintf("FPS timer set to %u ms\n", (unsigned int) period.it_interval.tv_nsec / 1000000);
	}
}
#else
// So Apple doesn't support POSIX timers and I can't find a good substitute short of
// having Objective-C Cocoa events or something like that.
void I_SetFPSLimit(int limit)
{
}
#endif
