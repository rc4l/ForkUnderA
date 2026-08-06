// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Asking the router to open the port, so the player does not have to.
//
// WHAT THIS IS NOT: an answer. A successful mapping proves a router agreed to something, not that
// anyone can reach us -- behind carrier-grade NAT the request succeeds and the server is still
// invisible, because the block is at the ISP on hardware the player does not own. So this runs
// BEFORE the registry check and never replaces it. It improves the odds; verification still decides.
//
// IT ONLY EVER RUNS WHEN ASKED. Nothing here happens unless the player chose to host publicly, which
// is already an explicit choice on an explicit tab. A game that quietly opened ports on somebody's
// router would be doing the thing routers disable UPnP to prevent, and being a game is not an excuse.
//
// BOTH PROTOCOLS, ALWAYS. The game is UDP and the direct-download listener is TCP on the same number.
// Mapping one and not the other produces a server that works while downloads mysteriously fail --
// which reads as a bug in the downloader and is really half a port forward.
//
// EVERYTHING IS OFF THE GAME THREAD. Discovery waits seconds for routers to answer, and the frame
// does not stop for that.

#ifndef ZX_PORTMAP_H
#define ZX_PORTMAP_H

namespace zx
{

// How the attempt is going, for the panel to say.
enum class PortMapState
{
	Idle,			// never asked
	Trying,			// discovery or the requests are in flight
	Mapped,			// a router agreed -- which is not the same as reachable
	Unsupported,	// nothing answered, or the router has this switched off
	Conflict,		// something else already holds that port
	Failed,			// asked and could not
};

// Ask for `port` on both protocols. Returns immediately; the work happens on its own thread.
// `description` shows in the router's UI and carries the server's name.
void PortMapOpen( int port, const char *description );

// Give the port back. Safe when nothing was ever mapped.
//
// [rc4l] Called on every route out of hosting, because a mapping we leave behind is a hole in
// somebody's network that outlives the game that asked for it. The lease is a backstop for the
// crash case, not a substitute for this.
void PortMapClose( void );

PortMapState PortMapCurrentState( void );

// A player-facing sentence for the current state. Never NULL.
const char *PortMapStatusText( void );

// Registered with atterm.
void PortMapShutdown( void );

} // namespace zx

#endif // ZX_PORTMAP_H
