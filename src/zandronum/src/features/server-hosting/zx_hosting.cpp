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

#include "doomtype.h"
#include "c_dispatch.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_random.h"
#include "templates.h"
#include "v_text.h"

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

namespace
{

HostLifecycle	g_Life;
HostConfig		g_Config;
FString			g_Secret;
FString			g_Address;
std::string		g_Pending;			// child output not yet scanned for the marker
FString			g_Recent;			// the tail, for the panel
int				g_LastTickMs	= 0;

// Enough to explain a failure, little enough that a chatty server cannot grow it without bound.
const size_t kMaxRecent = 4096;

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

	g_Secret = "";
	g_Address = "";
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

		if ( g_Life.state == HostState::Starting )
		{
			g_Pending += chunk;

			if ( g_Pending.find( kReadyMarker ) != std::string::npos )
			{
				g_Life = StepHostLifecycle( g_Life, HostEvent::ReadyObserved, "" );
				g_Pending.clear( );
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

	g_Life = TickHostLifecycle( g_Life, delta );
}

//*****************************************************************************
//
void HostShutdown( void )
{
	HostStop( );
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

void HostChildAnnounceReady( void )
{
	static bool bAnnounced = false;

	if ( bAnnounced || ( StartedByAGame( ) == false ))
		return;

	bAnnounced = true;

	FString line;
	line.Format( "%s\n", kReadyMarker );
	WriteUpThePipe( line.GetChars( ));
}

} // namespace zx

//*****************************************************************************
//
void SERVER_FUA_AnnounceReadyOnce( void )
{
	zx::HostChildAnnounceReady( );
}
