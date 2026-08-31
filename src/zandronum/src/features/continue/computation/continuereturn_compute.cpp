// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/continue/computation/continuereturn_compute.h"

namespace zx
{

ContinueReturnStep DecideContinueReturn(const ContinueReturnInputs &in)
{
	if (in.pending == false)
		return ContinueReturnStep::Wait;

	// Still connected: the teardown that asked for this has not finished.
	if (in.inSession)
		return ContinueReturnStep::Wait;

	// The engine has an action of its own pending, and ours would be overwritten by it.
	if (in.engineIdle == false)
		return ContinueReturnStep::Wait;

	return ContinueReturnStep::Perform;
}

} // namespace zx
