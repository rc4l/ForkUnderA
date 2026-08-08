// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Whether the outside world can reach a server we are hosting, as a CODE rather than a
// paragraph.
//
// This started as prose on the panel, and the prose grew: the verdict, then what the router said,
// then which port to forward and on which protocols. Five wrapped lines of it, in a region that also
// has to carry what the server is and how to administer it, for a question whose answer is one of
// four things. Long enough that the useful part -- "nobody outside has reached you" -- was the
// smallest thing on screen.
//
// So the panel shows the code and the tooltip carries the explanation. The wording is not lost, it
// is one hover away, and it is out of the way of the two lines that are true every time.
//
// Same shape as registrystatus_compute deliberately. Two places in this browser report the state of
// something we asked the network about, and they should not invent two different ways to say so.
//
// Header-pure by the features/ rules: no engine types, so the menu maps a tone onto its own colours.

#ifndef ZX_HOSTSTATUS_COMPUTE_H
#define ZX_HOSTSTATUS_COMPUTE_H

#include <string>

namespace zx
{

enum class HostStatus
{
	// Not advertised, so nobody outside was ever going to reach it and there is nothing to check.
	// Not a failure: it is what the player chose on the visibility row.
	LanOnly,

	// Advertised, and we are waiting to hear. Not a verdict.
	Checking,

	// A stranger reached us. The only way this is ever said -- it is never predicted from a router
	// agreeing, or from a port looking open from in here.
	Open,

	// [rc4l] Nothing arrived before we gave up. Named for what we OBSERVED, not for what we guess
	// caused it: an unforwarded port, a router that ignored us, carrier-grade NAT and a registry
	// that happened to be down all look identical from inside this process, and three of those four
	// are not the player's router.
	NoReply,
};

enum class HostTone
{
	Waiting,
	Good,
	Info,		// true, and nothing is wrong; the player asked for this
	Bad,
};

HostTone HostToneFor(HostStatus status);

// The stable short code. This is the part that goes on the panel, and the part someone quotes back.
const char *HostStatusCode(HostStatus status);

// One line of plain words, for the tooltip's second line.
const char *HostStatusText(HostStatus status);

// [rc4l] The hover text. The code, what it means, and -- only where there is something to do about
// it -- how to fix it. `port` is the port the server actually bound, which is not always the one that
// was asked for; `router` is whatever the port mapping had to say, or empty when it said nothing.
//
// The port is named in full with both protocols, because the game is UDP and direct downloads are
// TCP on the same number. Forwarding only UDP gives a server that works and downloads that do not,
// which reads as an unrelated bug.
std::string HostStatusTooltip(HostStatus status, int port, const std::string &router);

} // namespace zx

#endif // ZX_HOSTSTATUS_COMPUTE_H
