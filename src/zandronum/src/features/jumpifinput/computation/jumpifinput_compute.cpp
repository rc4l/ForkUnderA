// [MGOOOOOO] Implementation of the pure A_JumpIfInput decision logic. No engine dependencies, so
// both the engine and the standalone test build compile this TU.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#include "features/jumpifinput/computation/jumpifinput_compute.h"

bool ComputeInputMatch(unsigned int buttons, unsigned int oldbuttons, int keys, int flags)
{
	const unsigned int mask = (unsigned int)keys;

	// [MGOOOOOO] An empty mask can never match; guard here so JIF_ALL doesn't treat 0 == 0 as a hit.
	if (mask == 0)
		return false;

	const unsigned int cur = buttons & mask;

	// [MGOOOOOO] Base test: all listed buttons held, or (default) any single one.
	bool match = (flags & JIF_ALL) ? (cur == mask) : (cur != 0);

	// [MGOOOOOO] Edge mode: only fire on the tic the input becomes newly satisfied.
	if (flags & JIF_EDGE)
	{
		if (flags & JIF_ALL)
			match = match && ((oldbuttons & mask) != mask); // the combo wasn't complete last tic
		else
			match = match && ((buttons & ~oldbuttons & mask) != 0); // some listed button just pressed
	}

	if (flags & JIF_NOT)
		match = !match;

	return match;
}

bool ComputeShouldSendFullButtons(unsigned int buttons, bool forceFull)
{
	// [MGOOOOOO] Bits 0-7 are the gameplay buttons always sent in the low byte; anything above
	// (BT_SPEED, movement dirs, BT_USER1-4, ...) needs the full 32-bit transmission.
	return forceFull || (buttons & 0xFFFFFF00u) != 0;
}
