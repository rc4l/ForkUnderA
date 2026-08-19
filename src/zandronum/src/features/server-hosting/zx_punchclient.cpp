// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See zx_punchclient.h. Main thread only, like the rest of the browser.

// [rc4l] networkheaders.h FIRST, exactly as zx_reachprobe.cpp does it. It settles the winsock and
// windows.h ordering, and anything that pulls a system header ahead of it drags the vendored dxsdk
// winnt.h in first instead, which then fails to compile on its own intrinsics.
#include "networkheaders.h"

#include "features/server-hosting/zx_punchclient.h"
#include "features/server-browser/browser.h"
#include "features/server-hosting/computation/punchbroker_compute.h"
#include "features/server-browser/computation/registrymemory_compute.h"

#include "doomtype.h"
#include "c_console.h"
#include "c_dispatch.h"		// [rc4l] CCMD, for fua_punchtest at the foot of this file
#include "network.h"
#include "networkshared.h"

namespace zx
{

namespace
{

// [rc4l] The servers whose first legs are out, oldest first. A queue rather than the single
// target it used to be: the browser's punch-on-query path can find several unanswered servers in
// one refresh, and with one global the second cookie overwrote the first target -- every punch but
// the last went to the wrong server. The registry answers each first leg with exactly one cookie,
// so matching cookies to targets oldest-first is exact up to UDP reordering, which over one path
// costs at most a punch aimed at the wrong queued server -- a wasted packet, not a wrong action.
TArray<FString> g_AwaitingCookie;

// Second legs out, verdicts not yet back. Only so a verdict with nothing in flight is ignored
// rather than reported.
int g_AwaitingResults;

// One refresh can queue a handful; anything past that is a sign something is looping, not a
// bigger browser. Drop, don't grow.
const unsigned int kMaxAwaitingCookie = 8;

// [rc4l] Whether the address is on this machine or this network.
//
// A machine we reach without crossing a router has no hole to open, and asking a server on the
// internet to introduce two computers in the same house would be slower as well as absurd. The
// ranges are the private ones from RFC 1918 plus loopback and link-local.
bool IsLocalAddress( const NETADDRESS_s &address )
{
	// [rc4l] The v6 equivalents of the private ranges below: loopback, link-local (fe80::/10) and
	// unique-local (fc00::/7). Read from abIP6 -- abIP is meaningless while bIsIPv6 is set, and
	// judging a v6 address by four bytes of another field would punch for machines in this house.
	if ( address.bIsIPv6 )
	{
		static const BYTE abLoopback6[16] = { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1 };
		if ( memcmp( address.abIP6, abLoopback6, 16 ) == 0 )
			return true;
		if (( address.abIP6[0] == 0xfe ) && (( address.abIP6[1] & 0xc0 ) == 0x80 ))
			return true;
		if (( address.abIP6[0] & 0xfe ) == 0xfc )
			return true;
		return false;
	}

	const BYTE a = address.abIP[0];
	const BYTE b = address.abIP[1];

	if ( a == 127 )						// loopback
		return true;
	if ( a == 10 )						// 10.0.0.0/8
		return true;
	if (( a == 192 ) && ( b == 168 ))	// 192.168.0.0/16
		return true;
	if (( a == 172 ) && ( b >= 16 ) && ( b <= 31 ))	// 172.16.0.0/12
		return true;
	if (( a == 169 ) && ( b == 254 ))	// link-local, an address that failed to get a DHCP lease
		return true;

	return false;
}

// [rc4l] The registry to ask about `target`, which is the one that listed it wherever we know.
//
// Asking the registry that answered a refresh most recently is right the moment a refresh finishes
// and wrong afterwards, and on the reconnect path there has been no refresh at all. A registry
// holding no entry for a server can only answer NotListed, so asking the wrong one is the same as
// not asking. See features/server-browser/computation/registrymemory_compute.h.
bool RegistryFor( const FString &target, NETADDRESS_s &out )
{
	NETADDRESS_s server;
	const bool bHaveRemembered = server.LoadFromString( target.GetChars( ))
		&& BROWSER_GetRegistryForServer( server, out );

	NETADDRESS_s answering;
	const bool bHaveAnswering = BROWSER_GetServerRegistryAddress( answering );

	switch ( ChooseRegistry( bHaveRemembered, bHaveAnswering ))
	{
	case RegistryChoice::Remembered:
		return true;

	case RegistryChoice::Answering:
		out = answering;
		return true;

	default:
		return false;
	}
}

void Send( const FString &cookie, const FString &target )
{
	NETADDRESS_s registry;
	if ( RegistryFor( target, registry ) == false )
		return;

	NETBUFFER_s buffer;
	buffer.Init( MAX_UDP_PACKET, BUFFERTYPE_WRITE );
	buffer.Clear( );
	buffer.ByteStream.WriteLong( CLIENT_SERVERREGISTRY_PUNCH );
	buffer.ByteStream.WriteString( cookie.GetChars( ));
	buffer.ByteStream.WriteString( target.GetChars( ));

	NETWORK_LaunchPacket( &buffer, registry );
	buffer.Free( );
}

} // namespace

bool PunchRequestFor( const NETADDRESS_s &server, bool bFromList )
{
	NETADDRESS_s registry;
	const bool bRegistryKnown = BROWSER_GetServerRegistryAddress( registry );

	if ( DecidePunchIntent( bFromList, IsLocalAddress( server ), bRegistryKnown ) != PunchIntent::Ask )
		return false;

	if ( g_AwaitingCookie.Size( ) >= kMaxAwaitingCookie )
		return false;

	g_AwaitingCookie.Push( server.ToString( ));

	// First leg carries no cookie, which is what asks for one.
	Send( FString( ), g_AwaitingCookie.Last( ));
	return true;
}

void PunchCookieArrived( const char *pszCookie )
{
	if (( pszCookie == NULL ) || ( pszCookie[0] == 0 ) || ( g_AwaitingCookie.Size( ) == 0 ))
		return;

	// Oldest first: the registry answers each first leg with one cookie, in order.
	const FString target = g_AwaitingCookie[0];
	g_AwaitingCookie.Delete( 0 );
	++g_AwaitingResults;

	// Second leg. Echoing the cookie proves we are really at the address the registry saw, which is
	// the only thing that will make it instruct anybody.
	Send( FString( pszCookie ), target );
}

void PunchResultArrived( int verdict )
{
	if ( g_AwaitingResults <= 0 )
		return;
	--g_AwaitingResults;

	// [rc4l] Console only, and quiet about it. The connection attempt went out alongside the
	// request and has either succeeded or failed on its own by now, so there is nothing here to act
	// on. What this is for is making a failure diagnosable from a player's console log, which
	// matters more than usual because NAT traversal cannot be reproduced on the machine that wrote
	// it: the only reports worth having come from other people's networks.
	const char *pszWhy = NULL;

	switch ( static_cast<PunchVerdict>( verdict ))
	{
	case PunchVerdict::Broker:		pszWhy = "the registry asked the server to open for us"; break;
	case PunchVerdict::NoSupport:	pszWhy = "that server is too old to open a hole"; break;
	case PunchVerdict::NotListed:	pszWhy = "the registry does not know that server"; break;
	case PunchVerdict::RateLimited:	pszWhy = "the registry is rate limiting us"; break;
	case PunchVerdict::BadCookie:	pszWhy = "our address could not be confirmed"; break;
	}

	if ( pszWhy != NULL )
		DPrintf( "Hole punch: %s.\n", pszWhy );

	// [rc4l] A brokered punch is the starting gun for any challenge the browser is holding back. The
	// server is being told to open at this instant, so this is the one moment when our packet and its
	// packet are both in flight and neither has landed on the other's router to take the tuple the
	// other needs. See BROWSER_PunchBrokered.
	if ( static_cast<PunchVerdict>( verdict ) == PunchVerdict::Broker )
		BROWSER_PunchBrokered( );
}

void PunchRequestForced( const NETADDRESS_s &server )
{
	if ( g_AwaitingCookie.Size( ) >= kMaxAwaitingCookie )
		return;

	g_AwaitingCookie.Push( server.ToString( ));
	Send( FString( ), g_AwaitingCookie.Last( ));
}

} // namespace zx

//*****************************************************************************
//
// [rc4l] Ask for a punch at an address of your choosing, skipping the "is it worth it" question.
//
// NAT traversal cannot be reproduced on one machine, which is the whole difficulty of working on it:
// two engines on the same computer are on the same network, so PunchRequestFor correctly declines
// and there is nothing to watch. That is right in play and useless for finding out whether the rest
// of the chain works at all.
//
// This forces only the ASKING. Every gate that matters lives on the registry -- the server has to be
// listed, it has to have said it can punch, the cookie has to come back from the address the
// registry itself saw, and the rate limit still applies -- so this cannot reach anything an ordinary
// join could not. It buys a way to exercise registry, server and schedule from one desk, and nothing
// else.
CCMD( fua_punchtest )
{
	if ( argv.argc( ) < 2 )
	{
		Printf( "usage: fua_punchtest <address[:port]>\n" );
		return;
	}

	NETADDRESS_s Target;
	if ( Target.LoadFromString( argv[1] ) == false )
	{
		Printf( "fua_punchtest: %s is not an address.\n", argv[1] );
		return;
	}

	if ( Target.usPort == 0 )
		Target.SetPort( DEFAULT_SERVER_PORT );

	Printf( "fua_punchtest: asking the registry to introduce us to %s.\n", Target.ToString( ));
	zx::PunchRequestForced( Target );
}
