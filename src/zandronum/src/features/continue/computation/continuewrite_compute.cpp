// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/continue/computation/continuewrite_compute.h"

namespace zx
{

ContinueWriteVerdict DecideContinueWrite(const ContinueWriteInputs &in)
{
	// A crash is not a session. Checked first because it is the one that can be true alongside
	// anything else, and the one whose cost is highest.
	if (in.crashing)
		return ContinueWriteVerdict::Skip;

	// Mid-connect there is nothing to go back to: the old session is gone and the new one never
	// arrived, so recording either would be a lie.
	if (in.connecting)
		return ContinueWriteVerdict::Skip;

	// Our own server is running, so the offline record already says what to come back to and this
	// snapshot would replace it with wherever we were standing when we started it.
	if (in.hosting)
		return ContinueWriteVerdict::Skip;

	// Quitting from a menu is quitting, not leaving off somewhere.
	if (in.inMap == false)
		return ContinueWriteVerdict::Skip;

	return ContinueWriteVerdict::Write;
}

ContinueWriteVerdict DecideContinueWriteOnJoin(bool crashing)
{
	return crashing ? ContinueWriteVerdict::Skip : ContinueWriteVerdict::Write;
}

} // namespace zx
