//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: i_tictime.cpp
//
// Description: The tic time base. See i_tictime.h.
//
//-----------------------------------------------------------------------------


#include "i_tictime.h"
#include "doomdef.h"	// TICRATE

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

// [Kaori-Chani] Hundred-nanosecond units, which is what a Windows waitable timer takes.
#define TICTIME_HUNDRED_NS_PER_SECOND	10000000ull
#define TICTIME_NS_PER_SECOND			1000000000ull

// Replaces a failed or below-TICRATE frequency, which would divide by zero.
#define TICTIME_FALLBACK_FREQUENCY	1000000ull

static tictime_raw_t tictime_QueryFrequency()
{
#ifdef _WIN32
	LARGE_INTEGER li;
	if (QueryPerformanceFrequency(&li) && ((tictime_raw_t)li.QuadPart >= TICRATE))
		return (tictime_raw_t)li.QuadPart;
	return TICTIME_FALLBACK_FREQUENCY;
#else
	// clock_gettime(CLOCK_MONOTONIC) reports in nanoseconds.
	return 1000000000ull;
#endif
}

// [Kaori-Chani] Read once on first use, not eagerly with a zero check at each read. C++11
// onward initialises a function-local static exactly once even under threads, which is the
// race a zero check defends against, so the defence becomes the mechanism.
static tictime_raw_t tictime_Frequency()
{
	static const tictime_raw_t freq = tictime_QueryFrequency();
	return freq;
}

void TIC_Init()
{
	// Kept for its callers: the frequency is read here or at the first use, either way once.
	(void)tictime_Frequency();
}

tictime_raw_t TIC_GetRawCounter()
{
#ifdef _WIN32
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return (tictime_raw_t)li.QuadPart;
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (tictime_raw_t)ts.tv_sec * 1000000000ull + (tictime_raw_t)ts.tv_nsec;
#endif
}

tictime_raw_t TIC_GetRawFrequency()
{
	return tictime_Frequency();
}

//*****************************************************************************
//	FREQUENCY-PARAMETERIZED CORES

tictime_raw_t TIC_ScaleRawWith(tictime_raw_t raw, tictime_raw_t mul, tictime_raw_t freq)
{
	// Split division: the remainder is below freq, so r*mul cannot overflow
	// 64 bits at any uptime.
	const tictime_raw_t q = raw / freq;
	const tictime_raw_t r = raw % freq;
	return q * mul + (r * mul) / freq;
}

tictime_raw_t TIC_TicsToRawWith(tictime_raw_t tic, tictime_raw_t freq)
{
	// Ceiling division; the round trip needs freq >= TICRATE (i_tictime.h).
	const tictime_raw_t q = tic / TICRATE;
	const tictime_raw_t r = tic % TICRATE;
	return q * freq + (r * freq + TICRATE - 1) / TICRATE;
}

tictime_raw_t TIC_ScaleRaw(tictime_raw_t raw, tictime_raw_t mul)
{
	return TIC_ScaleRawWith(raw, mul, TIC_GetRawFrequency());
}

tictime_raw_t TIC_TicsToRaw(tictime_raw_t tic)
{
	return TIC_TicsToRawWith(tic, TIC_GetRawFrequency());
}

//*****************************************************************************
//	WAITING FOR A DEADLINE
//
// [Kaori-Chani] Each platform sleeps until the deadline rather than sleeping near it and
// spinning the rest. Spinning would buy sub-millisecond wake precision at a few percent of
// a core on every server at all times, and the tic count does not depend on when the loop
// wakes: a late wakeup runs its tic later in the same slot, and none is lost or gained.

#if defined(__EMSCRIPTEN__)

// [Kaori-Chani] The browser drives the loop one frame at a time, and TryRunTics skips the
// wait whenever a main-loop driver is installed, so nothing reaches this there. Blocking
// inside an animation frame would freeze the tab, so it must stay a no-op rather than
// become a sleep that happens not to be called.
void TIC_SleepUntilRaw(tictime_raw_t)
{
}

#elif defined(_WIN32)

// [Kaori-Chani] A high-resolution waitable timer, as tools/mcp_sample.cpp already does here:
// Sleep's granularity is the system timer tick, and this asks for fine granularity for this
// one timer rather than changing anything else on the machine. Where the flag is
// unsupported a plain waitable timer gives about a millisecond, i_main.cpp having already
// called timeBeginPeriod.
static HANDLE tictime_Timer()
{
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION	0x00000002
#endif
	static HANDLE timer = CreateWaitableTimerExW(NULL, NULL,
		CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	if (timer == NULL)
		timer = CreateWaitableTimerExW(NULL, NULL, 0, TIMER_ALL_ACCESS);
	return timer;
}

void TIC_SleepUntilRaw(tictime_raw_t deadline)
{
	const tictime_raw_t now = TIC_GetRawCounter();
	if (deadline <= now)
		return;
	HANDLE timer = tictime_Timer();
	if (timer == NULL)
		return;
	// A negative due time is relative, in hundred-nanosecond units.
	const tictime_raw_t units = TIC_ScaleRaw(deadline - now, TICTIME_HUNDRED_NS_PER_SECOND);
	if (units == 0)
		return;
	LARGE_INTEGER due;
	due.QuadPart = -(LONGLONG)units;
	if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE))
		WaitForSingleObject(timer, INFINITE);
}

#elif defined(__APPLE__)

// [Kaori-Chani] Darwin has no clock_nanosleep, and mach_wait_until takes a deadline on
// mach_absolute_time's timebase rather than on the CLOCK_MONOTONIC nanoseconds this
// counter reports -- converting between them needs mach_timebase_info and is a hazard for
// no gain here. A relative sleep of the remaining span is exact instead, because the span
// is computed from an absolute deadline on the counter's own clock and the caller
// recomputes it, so nothing accumulates.
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

// [Kaori-Chani] The deadline needs no conversion: this counter *is* CLOCK_MONOTONIC in
// nanoseconds, so an absolute sleep on the same clock cannot drift and cannot be cut short
// into an early tic.
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

