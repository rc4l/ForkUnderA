// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] WHEN a return may actually be performed, as opposed to whether one is owed.
//
// This is a unit because getting it wrong looked exactly like getting it right. Being kicked queues
// a return; performing it immediately queues a load as a gameaction; and the kick's own teardown
// then replaces that gameaction with ga_fullconsole. The load never runs, nothing is printed, and
// the feature appears to do nothing at all. Loading the very same file by hand from the console
// worked perfectly, which is what proved the file was never the problem.
//
// So a return waits for three things at once: that one is owed, that we are actually out of the
// session, and that the engine has no action of its own still pending. Written down here so the
// order can be asserted rather than remembered.
//
// Header-pure by the features/ rules: no engine types.

#ifndef ZX_CONTINUERETURN_COMPUTE_H
#define ZX_CONTINUERETURN_COMPUTE_H

namespace zx
{

struct ContinueReturnInputs
{
	bool pending;		// a departure asked for a return
	bool inSession;		// still connected, so the teardown has not finished
	bool engineIdle;	// no gameaction of the engine's own is waiting to run

	ContinueReturnInputs() : pending(false), inSession(false), engineIdle(false) {}
};

enum class ContinueReturnStep
{
	Wait,
	Perform,
};

ContinueReturnStep DecideContinueReturn(const ContinueReturnInputs &in);

} // namespace zx

#endif // ZX_CONTINUERETURN_COMPUTE_H
