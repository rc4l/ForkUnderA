// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Hosting, as the rest of the engine sees it.
//
// One server at a time, owned by this game, started from the browser's HOST tab. This layer is the
// join between three things that each know nothing about the others: the process (zx_hostprocess),
// the state machine (hostlifecycle_compute), and the command line (hostargs_compute).
//
// WHAT IT ADDS ON TOP OF THEM is readiness and secrecy.
//
// Readiness, because a spawned process is not a listening server. We watch the child's own output
// for the line where it says it is up, and until that arrives the client is not told to connect.
// Connecting early does not fail politely -- it fails as a timeout, several seconds later, blamed on
// the network.
//
// Secrecy, because the host is the administrator of a server they started, and should not have to
// prove it with a password they were never given. A secret is generated per spawn, handed to the
// child on its command line, and presented on connect. It is worth nothing after the process it
// belongs to has gone, which is the only property that makes generating it on the fly acceptable.

#ifndef ZX_HOSTING_H
#define ZX_HOSTING_H

#include "features/server-hosting/computation/hostargs_compute.h"
#include "features/server-hosting/computation/hostlifecycle_compute.h"

#include "zstring.h"

namespace zx
{

// Start a server from `config`. False if it could not even be spawned; the reason is in HostReason.
bool HostStart( const HostConfig &config );

// Stop whatever we are hosting. Safe when nothing is.
void HostStop( void );

// Discard a finished host's record, so the UI can go back to offering a new one. Only meaningful
// once it has stopped or failed -- a running server is not something to forget about.
void HostForget( void );

// Called every frame. Drains the child's output, advances the clock, and moves the state machine.
void HostTick( void );

// Registered with atterm.
void HostShutdown( void );

HostState HostCurrentState( void );

// Why the last host failed, or "" when it did not. Player-facing.
const char *HostReason( void );

// True while we hold a server process, in any state. This is what teardown must consult.
bool HostIsActive( void );

// True once the server is listening and the client may connect.
bool HostIsReady( void );

// [rc4l] True exactly ONCE, on the frame the server first became ready. An edge rather than a level,
// because the caller's job is to join -- and a level would have it trying to join again on every
// frame after that, on a connection it already has.
bool HostTakeReadyEdge( void );

// The address to connect to, once ready.
FString HostConnectAddress( void );

// The secret the host authenticates with. Empty when we are not hosting.
const char *HostRconSecret( void );

// [rc4l] True if `address` is the server we started. The RCON auto-login is bound to THIS, not to
// "is it loopback" -- otherwise anyone running any local server would be handed admin on it.
bool HostOwnsAddress( const FString &address );

// The last few lines the child said, for the panel to show. Bounded.
const char *HostRecentOutput( void );

// Config of the running (or last) host, for the UI to show what it started.
const HostConfig &HostCurrentConfig( void );

} // namespace zx

namespace zx
{

// [rc4l] CHILD SIDE. Everything above runs in the game; these two run in the server it started.
//
// The channel is deliberately ours rather than the engine's -stdout. That path decides where to
// write by probing the handle with GetFileInformationByHandle, which is documented to fail on
// anonymous pipes -- and when it fails it calls AllocConsole, which would pop a console window onto
// the desktop of a player who asked for a headless server. Writing to the inherited handle directly
// asks no questions and cannot conjure a window.

// Mirror one console line up the pipe. Does nothing in a server a person started themselves.
void HostChildEcho( const char *text );

// Say once, up the same pipe, that this server is listening.
void HostChildAnnounceReady( void );

} // namespace zx

// Same thing under the engine's own naming, for the server tick to call.
void SERVER_FUA_AnnounceReadyOnce( void );

#endif // ZX_HOSTING_H
