// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/continue/computation/continuerehost_compute.h"

namespace zx
{

ContinueRehostStep DecideContinueRehost(const ContinueRehostInputs &in)
{
	// Missing beats different: a restart cannot conjure a file, so it would cost the player their
	// map to arrive at the same refusal.
	if (in.filesFound == false)
		return ContinueRehostStep::RefuseMissing;

	if (in.filesMatchOurs)
		return ContinueRehostStep::Host;

	return ContinueRehostStep::ReloadThenHost;
}

} // namespace zx
