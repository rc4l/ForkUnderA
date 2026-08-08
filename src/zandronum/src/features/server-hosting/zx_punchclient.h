// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Ask the registry to get a server's router to let us in, while we are already knocking.
//
// NOTHING HERE BLOCKS A JOIN, and that is the whole design rather than a caution. The connection
// attempt goes out exactly as it always did, at the same moment; this runs alongside it. If the punch
// lands, the connection that was already in flight starts working. If it does not, the join fails the
// way it would have anyway.
//
// That is ICE's answer (RFC 8445) reduced to the smallest thing that helps: try every path at once
// and use whichever answers, rather than testing them in turn and paying for the ones that fail. It
// is also what makes this safe to put in front of every join in the game. There is no timeout to sit
// through, no state a join waits on, and no way for a registry that has gone down to stop anybody
// connecting to a server that was reachable all along.
//
// The two legs are the reach probe's cookie handshake, for the same reason it has one: this ends with
// a stranger being sent a packet, so we have to prove we are where we say we are before the registry
// will act. See features/server-hosting/computation/punchbroker_compute.h for the rule that governs
// both ends of it.

#ifndef ZX_PUNCHCLIENT_H
#define ZX_PUNCHCLIENT_H

// Forward-declared rather than included. This header is pulled into translation units that settle
// their own winsock ordering, and dragging networkshared.h ahead of networkheaders.h is what makes
// the vendored dxsdk winnt.h fail on its own intrinsics.
struct NETADDRESS_s;

namespace zx
{

// [rc4l] Ask for an introduction to `server`, if asking is worth it at all.
//
// `bFromList` is false for an address the player typed or pasted: the registry has no entry for it,
// so the only answer available is NotListed and the round trip would buy nothing.
//
// Cheap and silent when it decides not to ask, so callers can call it on every join without
// checking anything first.
void PunchRequestFor( const NETADDRESS_s &server, bool bFromList );

// The registry answering the first leg. Sends the second, which is the one that earns a punch.
void PunchCookieArrived( const char *pszCookie );

// The registry's verdict. Recorded for the console rather than acted on: by the time it arrives the
// connection attempt is already in flight, and a refusal changes nothing about what we do next.
void PunchResultArrived( int verdict );

} // namespace zx

#endif // ZX_PUNCHCLIENT_H
