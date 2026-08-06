// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] "Can anyone out there reach me on this port?", asked before hosting anything on it.
//
// The rules -- the state machine, the nonce, the cache key -- are in
// computation/reachprobe_compute.h, which is where the reasoning about reflection and spoofing
// lives. This is the socket work that carries them out.
//
// TWO SOCKETS, AND THAT IS THE WHOLE TRICK.
//
// A naive version sends the request from the very port it wants tested. That looks tidy and quietly
// answers the wrong question: the outbound packet makes the player's NAT open a mapping for that
// port, and on a port-preserving router the probe then arrives through the mapping rather than
// through any forwarding. The test would congratulate people whose port is shut.
//
// So the request goes out on an EPHEMERAL socket, and a second socket bound to the port under test
// does nothing but listen. The only mapping the conversation creates belongs to the ephemeral one,
// so a packet arriving on the port under test cannot have ridden in on it -- it got there because
// the port is genuinely open, which is the question that was asked.
//
// The listening socket is closed the moment there is a verdict, so the port is free again long
// before a server wants it.

#ifndef ZX_REACHPROBE_H
#define ZX_REACHPROBE_H

#include "features/server-hosting/computation/reachprobe_compute.h"

namespace zx
{

// Begin a check for `port`, or do nothing if a usable cached answer already covers it. Safe to call
// repeatedly -- the HOST tab calls it whenever the port changes.
void ReachProbeRequest( int port );

// Drains the sockets and advances the clock. Called from the browser's ticker.
void ReachProbeTick( void );

// Where the check for `port` stands. Idle means nothing is known and nothing is running -- which is
// the honest answer before anyone has asked.
ProbePhase ReachProbeStatus( int port );

// Throw away any cached verdict, so the next request re-checks. For the manual re-check.
void ReachProbeForget( void );

// Close sockets and forget everything. Registered with atterm.
void ReachProbeShutdown( void );

// [rc4l] The cookie leg, handed over by the engine's normal packet handling -- it arrives on the
// engine's own socket, because that is the socket the request went out on.
void ReachProbeCookieArrived( const char *pszCookie );

// What the registry observed our public address to be. Part of the cache key: a verdict recorded on
// one connection says nothing after the ISP has moved us.
void ReachProbeSetPublicIp( const char *pszIp );

} // namespace zx

#endif // ZX_REACHPROBE_H
