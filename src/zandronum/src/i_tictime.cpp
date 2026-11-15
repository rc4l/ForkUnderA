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

// [Kaori-Chani] Read once on first use rather than eagerly with a zero check at each read,
// since C++11 initialises a function-local static exactly once even under threads.
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
