//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: i_tictime.h
//
// Description: The monotonic high-resolution time base tics are derived
// from on every platform (design doc section 8). Tics come from dividing the
// whole elapsed span of the counter, so rounding error stays below one tic and
// the long-run rate is exactly TICRATE, whatever the timer granularity,
// scheduler load or wall-clock adjustment.
//
//-----------------------------------------------------------------------------

#ifndef __I_TICTIME_H__
#define __I_TICTIME_H__

typedef unsigned long long	tictime_raw_t;

// Fails here, not as MSVC's C2556 in the definitions, if tictime_raw_t narrows.
typedef char tictime_assert_raw_is_64bit[(sizeof(tictime_raw_t) == 8) ? 1 : -1];

// Makes the frequency's static initialization explicit; safe to call again.
void TIC_Init();

// The raw monotonic counter (arbitrary epoch), and its counts per second,
// which are never below TICRATE.
tictime_raw_t TIC_GetRawCounter();
tictime_raw_t TIC_GetRawFrequency();

// floor( raw * mul / frequency ), overflow-safe for any realistic uptime.
// TIC_ScaleRaw(delta, 1000)    -> milliseconds
// TIC_ScaleRaw(delta, TICRATE) -> whole tics
tictime_raw_t TIC_ScaleRaw(tictime_raw_t raw, tictime_raw_t mul);

// ceil( tic * frequency / TICRATE ): the first raw count at which the tic
// counter reads `tic`, so TIC_ScaleRaw( TIC_TicsToRaw( t ), TICRATE ) == t.
tictime_raw_t TIC_TicsToRaw(tictime_raw_t tic);

// The two conversions with the frequency as a parameter, for the self-test.
tictime_raw_t TIC_ScaleRawWith(tictime_raw_t raw, tictime_raw_t mul, tictime_raw_t freq);
tictime_raw_t TIC_TicsToRawWith(tictime_raw_t tic, tictime_raw_t freq);

#endif // __I_TICTIME_H__
