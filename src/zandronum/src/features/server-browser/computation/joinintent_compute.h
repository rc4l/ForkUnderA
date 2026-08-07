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

namespace zx
{

enum class JoinIntent
{
	Join,				// ordinary: go there
	AlreadyThere,		// this is the server we are on; there is nothing to do but close the menu
	ConfirmStopHosting,	// going there ends a server of ours, so ask first
};

// `bHoldsServer` is true while a server of ours exists in any form -- starting, running, or on its
// way down -- because all three are things a join would destroy.
//
// `bTargetIsCurrentServer` means the selected row IS the server this client is connected to. That is
// the test, rather than "is it our hosted one": a player already on a server gains nothing by
// rejoining it, and the reload would cost them their place either way.
JoinIntent DecideJoinIntent(bool bHoldsServer, bool bConnected, bool bTargetIsCurrentServer);

} // namespace zx

#endif // ZX_JOININTENT_COMPUTE_H
