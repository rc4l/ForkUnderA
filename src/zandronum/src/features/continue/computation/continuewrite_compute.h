// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Whether this shutdown is one worth remembering.
//
// The record is only as useful as its worst update. Overwriting a good session with a bad one is the
// failure that matters, because it destroys the thing the player wanted AND offers them the thing
// they did not -- so every doubtful case skips.
//
// CRASHES ARE THE REASON THIS IS NOT A SHUTDOWN HOOK. i_main.cpp registers atexit(call_terms), and
// I_FatalError exits through exit(), so the atterm chain runs on a fatal error exactly as it does on
// a clean quit. A record written from there would faithfully save the crash. The caller must
// therefore be the deliberate quit, and this unit refuses anything else it is told about.
//
// Header-pure by the features/ rules: no engine types.

#ifndef ZX_CONTINUEWRITE_COMPUTE_H
#define ZX_CONTINUEWRITE_COMPUTE_H

namespace zx
{

struct ContinueWriteInputs
{
	bool inMap;			// standing in a level, rather than at a menu or an intermission
	bool connecting;	// a connection is being attempted and has not landed
	bool crashing;		// a fatal error is on its way out

	ContinueWriteInputs() : inMap(false), connecting(false), crashing(false) {}
};

enum class ContinueWriteVerdict
{
	Write,
	Skip,
};

ContinueWriteVerdict DecideContinueWrite(const ContinueWriteInputs &in);

// Whether a join that just succeeded should be recorded. Separate from the quit path because the
// facts are different: the player is demonstrably in, so there is nothing to doubt except a crash
// arriving at the same moment.
ContinueWriteVerdict DecideContinueWriteOnJoin(bool crashing);

} // namespace zx

#endif // ZX_CONTINUEWRITE_COMPUTE_H
