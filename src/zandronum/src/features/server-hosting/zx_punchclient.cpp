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

#include "doomtype.h"
#include "c_console.h"
#include "network.h"
#include "networkshared.h"

namespace zx
{

namespace
{

// The server this run is about. Kept because the second leg has to name it again, and because a
// verdict arriving for a join we have already abandoned is worth ignoring rather than reporting.
FString g_Target;

// [rc4l] Whether the address is on this machine or this network.
//
// A machine we reach without crossing a router has no hole to open, and asking a server on the
// internet to introduce two computers in the same house would be slower as well as absurd. The
// ranges are the private ones from RFC 1918 plus loopback and link-local.
bool IsLocalAddress( const NETADDRESS_s &address )
{
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

void Send( const FString &cookie )
{
	NETADDRESS_s registry;
	if ( BROWSER_GetServerRegistryAddress( registry ) == false )
		return;

	NETBUFFER_s buffer;
	buffer.Init( MAX_UDP_PACKET, BUFFERTYPE_WRITE );
	buffer.Clear( );
	buffer.ByteStream.WriteLong( CLIENT_SERVERREGISTRY_PUNCH );
	buffer.ByteStream.WriteString( cookie.GetChars( ));
	buffer.ByteStream.WriteString( g_Target.GetChars( ));

	NETWORK_LaunchPacket( &buffer, registry );
	buffer.Free( );
}

} // namespace

void PunchRequestFor( const NETADDRESS_s &server, bool bFromList )
{
	NETADDRESS_s registry;
	const bool bRegistryKnown = BROWSER_GetServerRegistryAddress( registry );

	if ( DecidePunchIntent( bFromList, IsLocalAddress( server ), bRegistryKnown ) != PunchIntent::Ask )
		return;

	g_Target = server.ToString( );

	// First leg carries no cookie, which is what asks for one.
	Send( FString( ));
}

void PunchCookieArrived( const char *pszCookie )
{
	if (( pszCookie == NULL ) || ( pszCookie[0] == 0 ) || g_Target.IsEmpty( ))
		return;

	// Second leg. Echoing the cookie proves we are really at the address the registry saw, which is
	// the only thing that will make it instruct anybody.
	Send( FString( pszCookie ));
}

void PunchResultArrived( int verdict )
{
	if ( g_Target.IsEmpty( ))
		return;

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

	g_Target = "";
}

} // namespace zx
