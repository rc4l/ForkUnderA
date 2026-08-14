// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "joinintent_compute.h"

namespace zx
{

bool RowIsOwnServer(const std::string &rowIp, int rowPort, int hostPort,
                    const std::string &localIp, const std::string &publicIp)
{
	if (rowIp.empty() || (rowPort <= 0) || (rowPort != hostPort))
		return false;

	// Loopback is how we connect to it ourselves.
	if ((rowIp == "127.0.0.1") || (rowIp == "localhost"))
		return true;

	// Empty is "we do not know", not "matches anything". Without this an unset public IP would make
	// every row on an unknown address read as ours the moment the ports lined up.
	if (!localIp.empty() && (rowIp == localIp))
		return true;
	if (!publicIp.empty() && (rowIp == publicIp))
		return true;

	return false;
}

JoinIntent DecideJoinIntent(bool bHoldsServer, bool bConnected, bool bTargetIsCurrentServer,
                            bool bTargetIsOwnServer, bool bOnOwnServer)
{
	// Checked FIRST, and it has to be. A host pressing JOIN on their own row is the case that caused
	// this: taken as an ordinary join it stops the server, reloads the engine, and reconnects them to
	// something that no longer exists. Asking to be where you already are should cost nothing.
	if (bConnected && bTargetIsCurrentServer)
		return JoinIntent::AlreadyThere;

	// [rc4l] The same thing, reached the long way round. The test above compares the row against the
	// address we are connected ON, which for our own server is 127.0.0.1 while the row says our LAN
	// or public address. Those never matched, so a host on their own server fell through to the
	// warning below and was offered the chance to stop the server they were standing in.
	if (bTargetIsOwnServer && bHoldsServer)
		return bOnOwnServer ? JoinIntent::AlreadyThere : JoinIntent::RejoinOwnServer;

	// Anything else, while a server of ours exists, destroys it. Starting and Stopping count as much
	// as Running: a join during either still leaves the player without the server they were running.
	if (bHoldsServer)
		return JoinIntent::ConfirmStopHosting;

	return JoinIntent::Join;
}

} // namespace zx
