//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: i_tictime_wait.cpp
//
// Description: Waiting for a tic's deadline on a desktop platform. This is the
// time base's platform half, in its own file that CMake chooses, so the browser
// build's version is i_tictime_web.cpp rather than a condition in here
// (tools/platform-ifdef-tripwire.sh). Each platform sleeps until its deadline
// rather than sleeping near it and spinning, which costs a few percent of a
// core for precision the tic count never needed: tics come from an absolute
// counter, so a late wakeup runs its tic later in the same slot and loses none.
//
//-----------------------------------------------------------------------------


#include "i_tictime.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

// [Kaori-Chani] Hundred-nanosecond units, which is what a Windows waitable timer takes.
#define TICTIME_HUNDRED_NS_PER_SECOND	10000000ull
#define TICTIME_NS_PER_SECOND			1000000000ull

bool TIC_SleepWaits()
{
	return true;
}

#if defined(_WIN32)

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION	0x00000002
#endif

// [Kaori-Chani] A high-resolution waitable timer, as tools/mcp_sample.cpp already does; the
// plain fallback gives about a millisecond, i_main.cpp having called timeBeginPeriod.
static HANDLE tictime_CreateTimer()
{
	HANDLE timer = CreateWaitableTimerExW(NULL, NULL,
		CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	if (timer == NULL)
		timer = CreateWaitableTimerExW(NULL, NULL, 0, TIMER_ALL_ACCESS);
	return timer;
}

// [Kaori-Chani] One kernel object for the process, fallback included, rather than one opened
// and closed 35 times a second.
static HANDLE tictime_Timer()
{
	static HANDLE timer = tictime_CreateTimer();
	return timer;
}

void TIC_SleepUntilRaw(tictime_raw_t deadline)
{
	const tictime_raw_t now = TIC_GetRawCounter();
	if (deadline <= now)
		return;
	// A negative due time is relative, in hundred-nanosecond units.
	const tictime_raw_t units = TIC_ScaleRaw(deadline - now, TICTIME_HUNDRED_NS_PER_SECOND);
	HANDLE timer = tictime_Timer();
	LARGE_INTEGER due;
	due.QuadPart = -(LONGLONG)units;
	if (timer != NULL && units != 0 && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE))
	{
		WaitForSingleObject(timer, INFINITE);
		return;
	}
	// [Kaori-Chani] With no timer to wait on, sleep the span out rather than return, which would
	// leave the caller's loop spinning -- the one thing this replaced.
	const DWORD ms = (DWORD)TIC_ScaleRaw(deadline - now, 1000);
	Sleep(ms > 0 ? ms : 1);
}

#elif defined(__APPLE__)

// [Kaori-Chani] A relative sleep of the span left, since Darwin has no clock_nanosleep and
// mach_wait_until wants mach_absolute_time's timebase rather than this counter's.
void TIC_SleepUntilRaw(tictime_raw_t deadline)
{
	const tictime_raw_t now = TIC_GetRawCounter();
	if (deadline <= now)
		return;
	const tictime_raw_t ns = deadline - now;
	struct timespec ts;
	ts.tv_sec = (time_t)(ns / TICTIME_NS_PER_SECOND);
	ts.tv_nsec = (long)(ns % TICTIME_NS_PER_SECOND);
	nanosleep(&ts, NULL);
}

#else

// [Kaori-Chani] No conversion needed: this counter *is* CLOCK_MONOTONIC in nanoseconds, so
// an absolute sleep on the same clock cannot drift.
void TIC_SleepUntilRaw(tictime_raw_t deadline)
{
	if (deadline <= TIC_GetRawCounter())
		return;
	struct timespec ts;
	ts.tv_sec = (time_t)(deadline / TICTIME_NS_PER_SECOND);
	ts.tv_nsec = (long)(deadline % TICTIME_NS_PER_SECOND);
	clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
}

#endif
