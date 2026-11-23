// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: formattest.cpp
//
// Description: The one format self-test that only a game binary can answer.
//
// The format's codecs are tested in format_test.cpp, whose GoogleTest cases
// are these blocks as step 1 ported them, and which runs on every platform
// CI builds -- including wasm32, the only 32-bit one. Twenty-three of the
// source tree's twenty-four blocks are those, and carrying them here as well
// would be the second copy that retiring the source tree's standalone runner
// was meant to end: two sets of assertions, built differently, free to drift.
//
// What a test binary cannot answer is whether THIS build decodes correctly.
// It is compiled from the portable target's own flags, without the engine's
// defines, and the time base is the engine's: TICRATE, and the platform's
// own clock frequency through i_tictime.h. So that block stays here.
//
// The other check that needs the game is the decode check, which hashes the
// poses a payload fixed in the source leaves after the game's own ApplyDelta.
// It sits in entities.cpp beside that decode rather than here, and its
// constant was pinned once the three native builds agreed on it.
//
//-----------------------------------------------------------------------------

#include <stdlib.h>

#include "doomdef.h"		// TICRATE
#include "i_tictime.h"
#include "features/net-snapshot/selftest.h"

using namespace zx::netsnapshot;

// The blocks run together behind fua_snap_selftest; each names itself, so one
// that never ran shows in the summary.
//
// [Kaori-Chani] Qualified, as every other file in the module writes its definitions:
// selftest.h declares this inside zx::netsnapshot, and a using-directive brings names
// in rather than putting a definition there. Written unqualified, this defined a second
// function in the global namespace and left the declared one with no body -- which only
// the linker can see, and nothing links the module yet.
unsigned int zx::netsnapshot::RunFormatSelfTests()
{
	int failures = 0;

	SelfTestBlock("time base");
	// ---- time base: round-trip exactness at and above the freq >= TICRATE
	// boundary, plus the platform's own frequency (i_tictime.h) ----
	{
		const tictime_raw_t freqs[] = {
			(tictime_raw_t)TICRATE,			// the exact boundary
			(tictime_raw_t)TICRATE + 1,
			1000ull,
			3579545ull,						// legacy PC timer rate
			10000000ull,					// typical QueryPerformanceFrequency
			1000000000ull,					// CLOCK_MONOTONIC
			TIC_GetRawFrequency() };		// this machine
		const tictime_raw_t tics[] = {
			0ull, 1ull, 34ull, 35ull, 36ull, 1000ull, 12345ull,
			(tictime_raw_t)TICRATE * 3600ull * 24ull,			// one day
			(tictime_raw_t)TICRATE * 3600ull * 24ull * 365ull };	// one year

		for (unsigned int i = 0; i < sizeof(freqs) / sizeof(freqs[0]); ++i)
		{
			for (unsigned int j = 0; j < sizeof(tics) / sizeof(tics[0]); ++j)
			{
				const tictime_raw_t raw = TIC_TicsToRawWith(tics[j], freqs[i]);
				if (TIC_ScaleRawWith(raw, TICRATE, freqs[i]) != tics[j])
				{
					SelfTestPrint("fua_snap_selftest: tic round trip failed (freq=%.0f tic=%.0f)\n",
						(double)freqs[i], (double)tics[j]);
					++failures;
				}
				// The boundary must be exact on BOTH sides: one count earlier
				// must still read as the previous tic.
				if ((tics[j] > 0) &&
					(TIC_ScaleRawWith(raw - 1, TICRATE, freqs[i]) != tics[j] - 1))
				{
					SelfTestPrint("fua_snap_selftest: tic boundary not exact (freq=%.0f tic=%.0f)\n",
						(double)freqs[i], (double)tics[j]);
					++failures;
				}
			}
		}

		// The precondition itself: everything above is only guaranteed while
		// the platform frequency clears TICRATE.
		if (TIC_GetRawFrequency() < (tictime_raw_t)TICRATE)
		{
			SelfTestPrint("fua_snap_selftest: platform frequency %.0f < TICRATE - tic math "
				"guarantees do not hold\n", (double)TIC_GetRawFrequency());
			++failures;
		}
	}

	return (unsigned int)failures;
}
