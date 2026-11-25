/*
 ** i_timer.cpp
 **
 **---------------------------------------------------------------------------
 ** Copyright 2012-2015 Alexey Lysiuk
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

#include <assert.h>

#include "basictypes.h"
// [rc4l] no basicinlines.h in our base; its contents live in basictypes.h here.
#include "doomdef.h"
#include "i_system.h"
#include "i_tictime.h"	// [Kaori-Chani] the shared monotonic tic time base
#include "templates.h"


namespace
{

// [Kaori-Chani] Tics come from the shared monotonic counter, and the spans are kept as raw
// counts so that no tic boundary is lost to millisecond rounding.
tictime_raw_t s_rawBase;
tictime_raw_t s_rawFreezeStart;
tictime_raw_t s_ticStartRaw;
tictime_raw_t s_ticNextRaw;
uint32 s_ticNextMS;
int s_ticFrozen;

bool s_timerInitialized;

// [Kaori-Chani] Seeded on its first read, since I_MSTime is read before I_InitTimer, so every
// reader takes the base first rather than read the counter before the seed.
tictime_raw_t RawBase()
{
	if (0 == s_rawBase)
	{
		s_rawBase = TIC_GetRawCounter();
	}

	return s_rawBase;
}

int GetTimePolled(bool saveMS)
{
	if (0 != s_ticFrozen)
	{
		return s_ticFrozen;
	}

	const tictime_raw_t base = RawBase();
	const tictime_raw_t now = TIC_GetRawCounter();
	// [Kaori-Chani] One exact division across the whole span, so the tic count cannot drift
	// however long the process has been up.
	const tictime_raw_t curtic = TIC_ScaleRaw(now - base, TICRATE);

	if (saveMS)
	{
		s_ticStartRaw = base + TIC_TicsToRaw(curtic);
		s_ticNextRaw = base + TIC_TicsToRaw(curtic + 1);
		s_ticNextMS = static_cast<uint32>(TIC_ScaleRaw(s_ticNextRaw - base, 1000));
	}

	return static_cast<int>(curtic);
}

int WaitForTicPolled(int prevTic)
{
	int time;

	assert(0 == s_ticFrozen);

	while ((time = GetTimePolled(false)) <= prevTic)
	{
		// [Kaori-Chani] Wait for the next tic's own boundary, where a second thread used to
		// signal a condition variable on each one.
		TIC_SleepUntilRaw(RawBase() + TIC_TicsToRaw(static_cast<tictime_raw_t>(prevTic) + 1));
	}

	return time;
}

void FreezeTimePolled(bool frozen)
{
	if (frozen)
	{
		assert(0 == s_ticFrozen);
		s_ticFrozen = GetTimePolled(false);
		s_rawFreezeStart = TIC_GetRawCounter();
	}
	else
	{
		assert(0 != s_ticFrozen);
		// [Kaori-Chani] Shift the base by the frozen span exactly, losing no part of a tic.
		s_rawBase = RawBase() + (TIC_GetRawCounter() - s_rawFreezeStart);
		s_ticFrozen = 0;
	}
}

} // unnamed namespace


unsigned int I_MSTime()
{
	// [Kaori-Chani] The counter the tics come from, where this read gettimeofday, which an NTP
	// correction steps mid-session.
	const tictime_raw_t base = RawBase();
	return static_cast<unsigned int>(TIC_ScaleRaw(TIC_GetRawCounter() - base, 1000));
}

unsigned int I_FPSTime()
{
	// [Kaori-Chani] Only differences are taken from this, so the counter's own epoch serves as
	// well as the boot time the KERN_BOOTTIME sysctl reported.
	return static_cast<unsigned int>(TIC_ScaleRaw(TIC_GetRawCounter(), 1000));
}


// [rc4l] ours returns fixed_t (48.16), not upstream's double -- see posix/i_system.h.
fixed_t I_GetTimeFrac(uint32* ms)
{
	const tictime_raw_t now = TIC_GetRawCounter();

	if (NULL != ms)
	{
		*ms = s_ticNextMS;
	}

	if (s_ticNextRaw <= s_ticStartRaw)
	{
		return FRACUNIT;
	}

	// [Kaori-Chani] FRACUNIT is a 48.16 value here, so the scale takes its raw 65536.
	const tictime_raw_t mul = static_cast<tictime_raw_t>(FRACUNIT.Raw()) * TICRATE;
	const tictime_raw_t frac = TIC_ScaleRaw(now - s_ticStartRaw, mul);

	return clamp<fixed_t>(fixed_t::FromRaw(static_cast<int64_t>(frac)), 0, FRACUNIT);
}


void I_InitTimer()
{
	assert(!s_timerInitialized);
	s_timerInitialized = true;

	// [Kaori-Chani] No thread to start: the tics are read from the shared counter, so there is
	// none to join at exit or to lose a tic to.
	TIC_Init();
	(void)RawBase();

	I_GetTime = GetTimePolled;
	I_WaitForTic = WaitForTicPolled;
	I_FreezeTime = FreezeTimePolled;
}

void I_ShutdownTimer()
{
	// [Kaori-Chani] Nothing to shut down now the timer thread is gone, and still called on the
	// path where the IWAD selector was cancelled.
	s_timerInitialized = false;
}
