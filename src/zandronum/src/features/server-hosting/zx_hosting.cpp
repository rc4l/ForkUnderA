// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] networkheaders.h rather than a raw <windows.h>. The build reshapes the modern Windows
// SDK into the legacy DirectX layout under dxsdk/, and that directory is on the include path -- so
// an unqualified windows.h finds the reshaped winnt.h and fails with two hundred C2733s naming
// intrinsics. This header is how every other engine file gets there, and it works.
//
// It must come FIRST, before any engine header: doomtype.h and friends pull in parts of the SDK
// themselves, and once they have, the reshaped copy is already the one in play.
#include "networkheaders.h"

#include "features/server-hosting/zx_hosting.h"
#include "features/server-hosting/zx_hostprocess.h"
#include "features/port-mapping/zx_portmap.h"

#include "doomtype.h"
#include "c_dispatch.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_random.h"
// [rc4l] NETWORK_GetLocalPort: the port the server ACTUALLY bound, which is what the ready line
// carries. networkheaders.h is already first above, so this is safe to add here.
#include "network.h"
#include "templates.h"
#include "v_text.h"

#include <cstring>		// strlen
#include <string>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace zx
{

namespace
{
// Our own pid, for the child to watch. No engine helper exists for this and it is two lines.
int OurProcessId( void )
{
#ifdef _WIN32
	return static_cast<int>( GetCurrentProcessId( ));
#else
	return static_cast<int>( getpid( ));
#endif
}
} // namespace

//*****************************************************************************
//
// [rc4l] The line a server we started prints once it is genuinely up.
//
// A MARKER WE OWN, rather than a grep for one of the engine's own startup messages. Matching prose
// like "UDP Initialized" would mean the readiness of every hosted server depends on nobody ever
// rewording a Printf -- and the failure, if someone did, is a host that spins forever with no
// explanation. This string is a contract between two pieces of our own code, so it can only break
// deliberately.
//
const char *const kReadyMarker = "[fua-host] ready";

// [rc4l] And the second one: the registry reached us from outside. See the header for why this is
// the only reachability test worth trusting.
const char *const kReachableMarker = "[fua-host] reachable";

namespace
{

HostLifecycle	g_Life;
bool			g_bReadyEdge	= false;
HostReach		g_Reach			= HostReach::NotPublic;
unsigned		g_Generation	= 0;
int				g_PublicMs		= 0;
HostConfig		g_Config;
FString			g_Secret;
FString			g_Address;
std::string		g_Pending;			// child output not yet scanned for the marker
FString			g_Recent;			// the tail, for the panel
int				g_LastTickMs	= 0;

// Enough to explain a failure, little enough that a chatty server cannot grow it without bound.
const size_t kMaxRecent = 4096;

// A minute. The registry verifies within seconds of an announcement; the rest is slack for a slow
// round trip and a registry that is briefly busy.
const int kReachTimeoutMs = 60000;

// [rc4l] Long enough that guessing it is not a strategy, and it only has to survive the lifetime of
// one process. Drawn from the engine's RNG rather than anything the player can influence, and never
// written to disk or shown -- a host who can read it gains nothing they do not already have.
FString MakeSecret( void )
{
	static const char *const kAlphabet = "abcdefghijklmnopqrstuvwxyz0123456789";
	FString out;

	for ( int i = 0; i < 24; ++i )
		out += kAlphabet[M_Random( ) % 36];

	return out;
}

void RememberOutput( const std::string &text )
{
	if ( text.empty( ))
		return;

	g_Recent += text.c_str( );

	if ( g_Recent.Len( ) > kMaxRecent )
		g_Recent = g_Recent.Right( kMaxRecent );
}

} // namespace

//*****************************************************************************
//
bool HostStart( const HostConfig &config )
{
	HostStop( );

	g_Config = config;
	g_Secret = MakeSecret( );
	g_Recent = "";
	g_Pending.clear( );
	g_LastTickMs = static_cast<int>( I_MSTime( ));

	g_Config.rconSecret = g_Secret.GetChars( );
	g_Config.parentPid = OurProcessId( );

	// Argv[0] is whatever started US. A server is the same binary, so there is nothing to locate and
	// nothing to get wrong -- and it can never be a different build from the one the player is
	// running, which is a whole class of version-mismatch failure that simply cannot arise.
	const std::vector<std::string> args = BuildHostArgs( Args->GetArg( 0 ), g_Config );

	std::string error;
	if ( HostProcessStart( args, error ) == false )
	{
		g_Life = StepHostLifecycle( HostLifecycle( ), HostEvent::SpawnFailed, error );
		return false;
	}

	g_Life = StepHostLifecycle( HostLifecycle( ), HostEvent::Spawned, "" );

	// Never reused, so a stale reference to an earlier host can always be told apart from this one.
	++g_Generation;

	// A local server was never meant to be reachable from outside, so there is nothing to wait for
	// and nothing to report -- saying "unreachable" about it would be answering a question nobody
	// asked, in a way that reads as a fault.
	g_Reach = config.advertise ? HostReach::Waiting : HostReach::NotPublic;
	g_PublicMs = 0;

	// [rc4l] The router is NOT asked yet, deliberately. Nothing here knows which port the server will
	// end up on -- NETWORK_Construct falls back to the next free one and only the child finds out --
	// so mapping the requested port now would forward a port the server may never listen on: a hole
	// in somebody's network leading nowhere, and a public server unreachable for a reason the
	// reachability check cannot explain. The mapping is opened on the ready line, where the real port
	// is known.

	g_Address = "127.0.0.1:";
	g_Address.AppendFormat( "%d", ResolveHostPort( g_Config.port, 10666 ));

	return true;
}

//*****************************************************************************
//
void HostStop( void )
{
	if ( HostHoldsProcess( g_Life.state ))
		g_Life = StepHostLifecycle( g_Life, HostEvent::StopRequested, "" );

	HostProcessStop( );

	// Straight to a terminal state: the process is gone by the time HostProcessStop returns, so
	// waiting for an exit notification that has already happened would hang the UI in Stopping.
	if ( g_Life.state == HostState::Stopping )
		g_Life = StepHostLifecycle( g_Life, HostEvent::ChildExited, "" );

	// Give the port back. A mapping left behind is a hole in somebody's network that outlives the
	// game that asked for it, and the lease is a backstop for the crash case, not a substitute.
	PortMapClose( );

	g_Secret = "";
	g_Address = "";
	g_bReadyEdge = false;
	g_Reach = HostReach::NotPublic;
	g_PublicMs = 0;
}

//*****************************************************************************
//
void HostTick( void )
{
	if ( IsHostFinished( g_Life.state ))
		return;

	const int now = static_cast<int>( I_MSTime( ));
	const int elapsed = now - g_LastTickMs;
	const int delta = ( elapsed > 0 ) ? elapsed : 0;
	g_LastTickMs = now;

	// Drain first. A child that died with something to say must have said it before we notice the
	// death, or the failure arrives with no reason attached.
	const std::string chunk = HostProcessReadOutput( );
	if ( chunk.empty( ) == false )
	{
		RememberOutput( chunk );

		// Watched for the whole life of the server, not only while it is starting: the registry
		// verifies on its own schedule, and it is usually well after the server is up.
		if (( g_Reach == HostReach::Waiting )
			&& ( chunk.find( kReachableMarker ) != std::string::npos ))
		{
			g_Reach = HostReach::Reachable;
		}

		if ( g_Life.state == HostState::Starting )
		{
			g_Pending += chunk;

			int boundPort = 0;
			if ( ParseHostReadyLine( g_Pending, &boundPort ))
			{
				// [rc4l] Believe the child over our own request. Asking for a port that is already
				// taken does not fail -- NETWORK_Construct binds the next one free -- so the address
				// built at launch from g_Config.port can point at somebody ELSE'S server. When it did,
				// the panel advertised their address and the auto-join walked the player into their
				// game, where the file check failed and read as "it hosted the wrong thing".
				if ( boundPort > 0 )
				{
					g_Address.Format( "127.0.0.1:%d", boundPort );

					if ( boundPort != ResolveHostPort( g_Config.port, 10666 ))
					{
						Printf( TEXTCOLOR_GOLD "Port %d was already in use, so the server is on %d "
							"instead.\n" TEXTCOLOR_NORMAL,
							ResolveHostPort( g_Config.port, 10666 ), boundPort );
					}
				}

				// [rc4l] Now the router can be asked, and asked for the RIGHT port -- ONLY for a
				// public server, which is already an explicit choice on an explicit tab. A game that
				// quietly opened ports on somebody's network would be doing the thing routers switch
				// UPnP off to prevent.
				//
				// It still runs before the registry check and still never replaces it: a router
				// agreeing proves a router agreed, and behind carrier-grade NAT that happens while the
				// server stays invisible.
				if ( g_Config.advertise )
				{
					PortMapOpen( ( boundPort > 0 ) ? boundPort
						: ResolveHostPort( g_Config.port, 10666 ), g_Config.hostName.c_str( ));
				}

				g_Life = StepHostLifecycle( g_Life, HostEvent::ReadyObserved, "" );
				g_Pending.clear( );

				// Raised only on the transition, and only if the transition actually happened.
				// Whoever is watching gets exactly one chance to act on it.
				g_bReadyEdge = HostAcceptsClients( g_Life.state );
			}
			else if ( g_Pending.size( ) > kMaxRecent )
			{
				// Keep a tail large enough that the marker cannot be split across the boundary.
				g_Pending = g_Pending.substr( g_Pending.size( ) - 256 );
			}
		}
	}

	if (( HostProcessRunning( ) == false ) && HostHoldsProcess( g_Life.state ))
	{
		const std::string tail = g_Recent.GetChars( );
		g_Life = StepHostLifecycle( g_Life, HostEvent::ChildExited,
			ExplainHostFailure( tail, HostProcessExitCode( )));
		return;
	}

	if (( g_Reach == HostReach::Waiting ) && HostAcceptsClients( g_Life.state ))
	{
		g_PublicMs += delta;

		// [rc4l] Giving up is a REPORT, not a verdict on the network. The registry verifies within
		// seconds of an announcement, so a minute of silence means the packet is not arriving -- but
		// what the panel says is "we did not hear back", because that is the only thing we know.
		if ( g_PublicMs >= kReachTimeoutMs )
			g_Reach = HostReach::Unreachable;
	}

	g_Life = TickHostLifecycle( g_Life, delta );
}

//*****************************************************************************
//
void HostForget( void )
{
	// Refuses while anything is still running: "forget" would then mean losing the handle to a
	// process we are still responsible for, which is how orphans are made.
	if ( HostHoldsProcess( g_Life.state ))
		return;

	g_Life = HostLifecycle( );
	g_Recent = "";
	g_Pending.clear( );
}

//*****************************************************************************
//
void HostShutdown( void )
{
	HostStop( );
}

unsigned HostGeneration( void )
{
	return g_Generation;
}

HostReach HostReachability( void )
{
	return g_Reach;
}

HostState HostCurrentState( void )
{
	return g_Life.state;
}

const char *HostReason( void )
{
	return g_Life.reason.c_str( );
}

bool HostIsActive( void )
{
	return HostHoldsProcess( g_Life.state );
}

bool HostIsReady( void )
{
	return HostAcceptsClients( g_Life.state );
}

bool HostTakeReadyEdge( void )
{
	const bool bWas = g_bReadyEdge;
	g_bReadyEdge = false;
	return bWas;
}

FString HostConnectAddress( void )
{
	return g_Address;
}

const char *HostRconSecret( void )
{
	return g_Secret.GetChars( );
}

//*****************************************************************************
//
bool HostOwnsAddress( const FString &address )
{
	// [rc4l] Bound to the address of the server WE STARTED, and only while we still hold it. Keying
	// the auto-login on "is this loopback" instead would hand administrator rights on any local
	// server to anyone who merely pointed a client at it -- including one they did not start.
	if ( HostIsActive( ) == false )
		return false;
	if ( g_Address.IsEmpty( ) || g_Secret.IsEmpty( ))
		return false;

	return ( address.CompareNoCase( g_Address ) == 0 );
}

const char *HostRecentOutput( void )
{
	return g_Recent.GetChars( );
}

const HostConfig &HostCurrentConfig( void )
{
	return g_Config;
}

} // namespace zx

//*****************************************************************************
//
// [rc4l] CHILD SIDE. Everything above this line runs in the game; everything below runs in the
// server the game started, and the only thing they share is the pipe.
//
namespace zx
{

namespace
{

// Whether this process was started by a game rather than by a person. Answered once: Args is stable
// and the question is asked on every console line.
bool StartedByAGame( void )
{
	static int cached = -1;

	if ( cached < 0 )
		cached = (( Args != NULL ) && ( Args->CheckParm( "-fua_hostparent" ) != 0 )) ? 1 : 0;

	return ( cached == 1 );
}

// Straight to the inherited handle. No engine console machinery in the way -- see the header for why
// -stdout is the wrong road, and note that this must not itself Printf or it would recurse.
void WriteUpThePipe( const char *text )
{
#ifdef _WIN32
	static HANDLE hOut = INVALID_HANDLE_VALUE;
	if ( hOut == INVALID_HANDLE_VALUE )
		hOut = GetStdHandle( STD_OUTPUT_HANDLE );

	if (( hOut == NULL ) || ( hOut == INVALID_HANDLE_VALUE ))
		return;

	DWORD written = 0;
	WriteFile( hOut, text, static_cast<DWORD>( strlen( text )), &written, NULL );
#else
	const size_t len = strlen( text );
	ssize_t done = 0;
	while ( done < static_cast<ssize_t>( len ))
	{
		const ssize_t n = write( STDOUT_FILENO, text + done, len - done );
		if ( n <= 0 )
			break;
		done += n;
	}
#endif
}

} // namespace

void HostChildEcho( const char *text )
{
	if (( text == NULL ) || ( StartedByAGame( ) == false ))
		return;

	WriteUpThePipe( text );
}

void HostChildAnnounceReachable( void )
{
	static bool bAnnounced = false;

	if ( bAnnounced || ( StartedByAGame( ) == false ))
		return;

	bAnnounced = true;

	FString line;
	line.Format( "%s\n", kReachableMarker );
	WriteUpThePipe( line.GetChars( ));
}

void HostChildAnnounceReady( void )
{
	static bool bAnnounced = false;

	if ( bAnnounced || ( StartedByAGame( ) == false ))
		return;

	bAnnounced = true;

	// [rc4l] The port we ACTUALLY bound, which is not necessarily the one we were told to use:
	// NETWORK_Construct falls back to the next free port rather than failing. The parent cannot know
	// this and has no way to ask, so it is said here, on the one channel that is definitely ours.
	FString line;
	line.Format( "%s %u\n", kReadyMarker, static_cast<unsigned int>( NETWORK_GetLocalPort( )));
	WriteUpThePipe( line.GetChars( ));
}

} // namespace zx

//*****************************************************************************
//
void SERVER_FUA_AnnounceReadyOnce( void )
{
	zx::HostChildAnnounceReady( );
}
