// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "joinintent_compute.h"

namespace zx
{

JoinIntent DecideJoinIntent(bool bHoldsServer, bool bConnected, bool bTargetIsCurrentServer)
{
	// Checked FIRST, and it has to be. A host pressing JOIN on their own row is the case that caused
	// this: taken as an ordinary join it stops the server, reloads the engine, and reconnects them to
	// something that no longer exists. Asking to be where you already are should cost nothing.
	if (bConnected && bTargetIsCurrentServer)
		return JoinIntent::AlreadyThere;

	// Anything else, while a server of ours exists, destroys it. Starting and Stopping count as much
	// as Running: a join during either still leaves the player without the server they were running.
	if (bHoldsServer)
		return JoinIntent::ConfirmStopHosting;

	return JoinIntent::Join;
}

} // namespace zx
