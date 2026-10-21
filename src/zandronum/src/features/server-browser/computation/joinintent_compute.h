// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] What pressing JOIN should actually do, which is not always "join".
//
// Three situations look identical at the button and end very differently. A player hosting a server
// pressed JOIN on THEIR OWN row -- the one they were already playing on -- and the engine tore
// itself down for a WAD reload, taking the hosted server with it, and then reconnected them to
// nothing. They asked to go somewhere they already were, and it cost them the server.
//
// So the decision is made here, where it can be tested, rather than inferred at the call site.

#ifndef ZX_JOININTENT_COMPUTE_H
#define ZX_JOININTENT_COMPUTE_H

#include <string>

namespace zx
{

enum class JoinIntent
{
	Join,				// ordinary: go there
	AlreadyThere,		// this is the server we are on; there is nothing to do but close the menu
	ConfirmStopHosting,	// going there ends a server of ours, so ask first
	RejoinOwnServer,	// our own server, and we are not on it: connect, and stop nothing
};

// [rc4l] Whether a browser row is a server WE are running, whatever address it happens to wear.
//
// It wears several. We connect to our own server on 127.0.0.1, LAN discovery lists it on this
// machine's local address, and the registry lists it on our public one -- three spellings of one
// server, and the browser shows two of them as separate rows. Comparing the row against the address
// we happen to be connected to therefore said "different server" about our own, which is what let
// JOIN offer to stop it. `localIp` and `publicIp` may be empty when unknown; empty never matches.
//
// The port has to agree as well, because the address alone does not distinguish our server from
// anything else on this machine or behind this router.
bool RowIsOwnServer(const std::string &rowIp, int rowPort, int hostPort,
                    const std::string &localIp, const std::string &publicIp);

// `bHoldsServer` is true while a server of ours exists in any form -- starting, running, or on its
// way down -- because all three are things a join would destroy.
//
// `bTargetIsCurrentServer` means the selected row IS the server this client is connected to, by the
// address we are actually connected on. `bTargetIsOwnServer` is the wider question RowIsOwnServer
// answers, and it exists because the narrow one kept saying no about our own server; `bOnOwnServer`
// says whether the connection we hold is to that server, which decides between "you are already
// here" and "connect to it".
JoinIntent DecideJoinIntent(bool bHoldsServer, bool bConnected, bool bTargetIsCurrentServer,
                            bool bTargetIsOwnServer, bool bOnOwnServer);

} // namespace zx

#endif // ZX_JOININTENT_COMPUTE_H
