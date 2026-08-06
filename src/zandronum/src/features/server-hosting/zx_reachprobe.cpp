// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See zx_reachprobe.h for the two-socket argument, which is the part that makes this test
// mean anything.

#include "networkheaders.h"

#include "features/server-hosting/zx_reachprobe.h"
#include "features/server-browser/browser.h"

#include "doomtype.h"
#include "c_dispatch.h"
#include "huffman.h"
#include "i_system.h"
#include "m_random.h"
#include "network.h"
#include "networkshared.h"
#include "v_text.h"

#include <cstring>
#include <string>

namespace zx
{

namespace
{

// The engine's own client socket carries the request and receives the cookie, so only the listener
// is hand-rolled here. That is not a shortcut, it is the design: the engine's socket is bound to a
// DIFFERENT port from the one under test, so the conversation cannot open a NAT mapping for the port
// we are asking about.
#ifdef _WIN32
typedef SOCKET probe_socket_t;
#define PROBE_INVALID_SOCKET INVALID_SOCKET
#define probe_close_socket   closesocket
#else
typedef int probe_socket_t;
#define PROBE_INVALID_SOCKET (-1)
#define probe_close_socket   close
#endif

ProbePhase		g_Phase = ProbePhase::Idle;
int				g_Port = 0;
std::string		g_Nonce;
std::string		g_Cookie;
int				g_PhaseStartMs = 0;

probe_socket_t	g_Listen = PROBE_INVALID_SOCKET;

// The last finished verdict, and what it was about.
bool			g_HaveCached = false;
ProbePhase		g_CachedPhase = ProbePhase::Idle;
ProbeCacheKey	g_CachedKey;
int				g_CachedAtMs = 0;

// [rc4l] What the registry said our public address looked like. We cannot see it from here and it
// is only used as part of the cache key: an answer recorded on one connection says nothing after
// the ISP has moved us.
std::string		g_PublicIp;

std::string LocalSubnetKey( void )
{
	// [rc4l] Coarse on purpose: the /24 of whatever address the engine bound. A fingerprint for "the
	// same network", not an identifier. Moving between home and a phone hotspot changes it, which is
	// exactly when a cached forwarding answer stops being true.
	NETADDRESS_s local = NETWORK_GetLocalAddress( );
	std::string out = local.ToString( );

	const size_t colon = out.find( ':' );
	if ( colon != std::string::npos )
		out = out.substr( 0, colon );

	const size_t lastDot = out.rfind( '.' );
	if ( lastDot != std::string::npos )
		out = out.substr( 0, lastDot );

	return out;
}

ProbeCacheKey CurrentKey( int port )
{
	ProbeCacheKey key;
	key.publicIp = g_PublicIp;
	key.localSubnet = LocalSubnetKey( );
	key.port = port;
	return key;
}

void CloseListener( void )
{
	if ( g_Listen != PROBE_INVALID_SOCKET )
	{
		probe_close_socket( g_Listen );
		g_Listen = PROBE_INVALID_SOCKET;
	}
}

// Bind the port under test. Failing here is its own answer: something on this machine already has
// the port, so a server would not get it either.
bool OpenListener( int port )
{
	CloseListener( );

	g_Listen = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( g_Listen == PROBE_INVALID_SOCKET )
		return false;

	sockaddr_in address;
	memset( &address, 0, sizeof address );
	address.sin_family = AF_INET;
	address.sin_port = htons( static_cast<unsigned short>( port ));
	address.sin_addr.s_addr = htonl( INADDR_ANY );

	if ( bind( g_Listen, reinterpret_cast<sockaddr *>( &address ), sizeof address ) < 0 )
	{
		CloseListener( );
		return false;
	}

	// Non-blocking: this is polled from the menu ticker and must never stall a frame.
#ifdef _WIN32
	u_long nonBlocking = 1;
	ioctlsocket( g_Listen, FIONBIO, &nonBlocking );
#else
	fcntl( g_Listen, F_SETFL, O_NONBLOCK | fcntl( g_Listen, F_GETFL, 0 ));
#endif

	return true;
}

void SendRequest( const std::string &cookie )
{
	NETADDRESS_s registry;
	if ( BROWSER_GetServerRegistryAddress( registry ) == false )
	{
		g_Phase = ProbePhase::Failed;
		return;
	}

	NETBUFFER_s buffer;
	buffer.Init( MAX_UDP_PACKET, BUFFERTYPE_WRITE );
	buffer.Clear( );
	buffer.ByteStream.WriteLong( CLIENT_SERVERREGISTRY_REACHTEST );
	buffer.ByteStream.WriteString( cookie.c_str( ));
	buffer.ByteStream.WriteShort( static_cast<short>( g_Port ));
	buffer.ByteStream.WriteString( g_Nonce.c_str( ));

	NETWORK_LaunchPacket( &buffer, registry );
	buffer.Free( );
}

void Finish( ProbePhase phase )
{
	g_Phase = phase;
	CloseListener( );

	if ( ProbeIsFinished( phase ))
	{
		// Failed is cached too, briefly. Re-asking a registry that is down, once per frame, would be
		// a small flood of our own making, and the TTL is short enough that a recovery is noticed.
		g_HaveCached = true;
		g_CachedPhase = phase;
		g_CachedKey = CurrentKey( g_Port );
		g_CachedAtMs = I_MSTime( );
	}
}

// Drain the listening socket. Only a packet carrying OUR nonce counts, see reachprobe_compute.h.
void PollListener( void )
{
	if ( g_Listen == PROBE_INVALID_SOCKET )
		return;

	for ( ;; )
	{
		unsigned char encoded[MAX_UDP_PACKET];
		sockaddr_in from;
		// [rc4l] Winsock spells this int and POSIX spells it socklen_t; the engine's own socket code
		// makes the same split, so it is spelled out here rather than assumed.
#ifdef _WIN32
		int fromLen = sizeof from;
#else
		socklen_t fromLen = sizeof from;
#endif

		const int received = recvfrom( g_Listen, reinterpret_cast<char *>( encoded ), sizeof encoded,
			0, reinterpret_cast<sockaddr *>( &from ), &fromLen );

		if ( received <= 0 )
			return;

		unsigned char decoded[MAX_UDP_PACKET];
		int decodedSize = sizeof decoded;
		HUFFMAN_Decode( encoded, decoded, received, &decodedSize );

		if ( decodedSize <= 0 )
			continue;

		NETBUFFER_s message;
		message.Init( MAX_UDP_PACKET, BUFFERTYPE_READ );
		message.Clear( );
		memcpy( message.pbData, decoded, decodedSize );
		message.ulCurrentSize = decodedSize;
		message.ByteStream.pbStream = message.pbData;
		message.ByteStream.pbStreamEnd = message.pbData + decodedSize;

		const long command = message.ByteStream.ReadByte( );
		// Copied before anything else is read: ReadString hands back a pointer into one static
		// buffer that every call reuses.
		const std::string nonce = message.ByteStream.ReadString( );
		message.Free( );

		if ( command != SERVERREGISTRY_REACHPROBE )
			continue;

		if ( ProbeNonceMatches( g_Nonce, nonce ) == false )
			continue;			// somebody else's packet, or a replay of nothing

		Finish( ProbePhase::Reachable );
		return;
	}
}

} // namespace

void ReachProbeCookieArrived( const char *pszCookie )
{
	if ( g_Phase != ProbePhase::AwaitingCookie )
		return;			// unasked for, or too late; either way it changes nothing

	g_Cookie = ( pszCookie != NULL ) ? pszCookie : "";
	if ( g_Cookie.empty( ))
		return;

	g_Phase = ProbePhase::AwaitingProbe;
	g_PhaseStartMs = I_MSTime( );

	// Second leg: the echo. Only now will the registry send anything unsolicited.
	SendRequest( g_Cookie );
}

void ReachProbeSetPublicIp( const char *pszIp )
{
	g_PublicIp = ( pszIp != NULL ) ? pszIp : "";
}

void ReachProbeRequest( int port )
{
	if (( port <= 0 ) || ( port > 65535 ))
		return;

	// Already running for this port.
	if (( g_Port == port ) && ( ProbeIsFinished( g_Phase ) == false ) && ( g_Phase != ProbePhase::Idle ))
		return;

	// A usable cached answer for this exact question.
	if ( g_HaveCached && ProbeCacheUsable( g_CachedKey, CurrentKey( port ), I_MSTime( ) - g_CachedAtMs ))
	{
		g_Port = port;
		g_Phase = g_CachedPhase;
		return;
	}

	g_Port = port;

	// [rc4l] A nonce nobody else can guess is the whole false-positive defence, so it is built from
	// several draws rather than one.
	char buffer[64];
	sprintf( buffer, "%08x%08x%08x", static_cast<unsigned int>( M_Random( ) * 1103515245 + M_Random( )),
		static_cast<unsigned int>( M_Random( ) * 22695477 + M_Random( )),
		static_cast<unsigned int>( I_MSTime( )));
	g_Nonce = buffer;

	if ( OpenListener( port ) == false )
	{
		// Something local already holds it, so a server would not get it either. That is a real
		// answer about this port, not a failure of the test.
		Finish( ProbePhase::Unreachable );
		return;
	}

	g_Cookie = "";
	g_Phase = ProbePhase::AwaitingCookie;
	g_PhaseStartMs = I_MSTime( );

	SendRequest( "" );
}

void ReachProbeTick( void )
{
	if ( ProbeIsFinished( g_Phase ) || ( g_Phase == ProbePhase::Idle ))
		return;

	PollListener( );

	if ( ProbeIsFinished( g_Phase ))
		return;

	const int elapsed = I_MSTime( ) - g_PhaseStartMs;
	const ProbePhase next = StepProbe( g_Phase, false, false, elapsed );

	if ( next != g_Phase )
		Finish( next );
}

ProbePhase ReachProbeStatus( int port )
{
	if (( g_Port == port ) && ( g_Phase != ProbePhase::Idle ))
		return g_Phase;

	if ( g_HaveCached && ProbeCacheUsable( g_CachedKey, CurrentKey( port ), I_MSTime( ) - g_CachedAtMs ))
		return g_CachedPhase;

	return ProbePhase::Idle;
}

void ReachProbeForget( void )
{
	g_HaveCached = false;
	g_CachedPhase = ProbePhase::Idle;
	g_Phase = ProbePhase::Idle;
	g_Port = 0;
	CloseListener( );
}

void ReachProbeShutdown( void )
{
	ReachProbeForget( );
}

} // namespace zx

//*****************************************************************************
//
// [rc4l] Re-check by hand, for when a player has just changed something on their router and does not
// want to wait out the cache.
CCMD( fua_recheckport )
{
	zx::ReachProbeForget( );
	Printf( "Port reachability will be checked again next time the HOST tab asks.\n" );
}
