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
//
// [rc4l] SOCKET, INVALID_SOCKET, closesocket and ioctlsocket all come from networkheaders.h, which
// defines the POSIX spellings for us. Rolling a private set of those here duplicated something the
// engine already had and got it wrong on the platforms this machine cannot build.

ProbePhase		g_Phase = ProbePhase::Idle;

// [rc4l] WHICH FAMILY THIS ATTEMPT IS ASKING ABOUT.
//
// The registry probes back to whatever address it saw the request come from, so the family under
// test is chosen by the address we send to -- not by anything in the packet. One attempt therefore
// answers for one family, and "can anyone reach me" is only answered by asking both.
bool			g_TryingV6 = false;
bool			g_TriedV4 = false;
bool			g_TriedV6 = false;
int				g_Port = 0;
std::string		g_Nonce;
std::string		g_Cookie;
int				g_PhaseStartMs = 0;

SOCKET			g_Listen = INVALID_SOCKET;

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
	if ( g_Listen != INVALID_SOCKET )
	{
		closesocket( g_Listen );
		g_Listen = INVALID_SOCKET;
	}
}

// Bind the port under test. Failing here is its own answer: something on this machine already has
// the port, so a server would not get it either.
//
// [rc4l] ONE SOCKET THAT HEARS BOTH FAMILIES, the same trick and the same fallback as
// network_AllocateSocket in network.cpp -- turning IPV6_V6ONLY off makes a v6 socket accept v4
// peers as well, so there is one bind, one read loop, and no second question about which socket a
// packet arrived on.
//
// It was AF_INET outright, which asked the v4 question and drew the answer as though it were the
// whole one. A host reachable over v6 and not over v4 -- which is every player behind carrier-grade
// NAT with working v6, and the case this fork's v6 support exists for -- was told INTERNET in red
// while a server there is perfectly joinable. Painting somebody's working network as shut is the
// failure the colour rules in DrawHostVisibility are careful to avoid everywhere else.
//
// The fallback is not padding: IPV6_V6ONLY defaults differently per platform and a host can have the
// v6 stack switched off, and a listener that can only ever hear v6 would be a worse probe than the
// v4-only one it replaced.
bool OpenListener( int port )
{
	CloseListener( );

	bool bDualStack = false;

	g_Listen = socket( PF_INET6, SOCK_DGRAM, IPPROTO_UDP );
	if ( g_Listen != INVALID_SOCKET )
	{
		int off = 0;
		if ( setsockopt( g_Listen, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&off, sizeof( off )) == 0 )
			bDualStack = true;
		else
			CloseListener( );
	}

	if ( g_Listen == INVALID_SOCKET )
	{
		g_Listen = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
		if ( g_Listen == INVALID_SOCKET )
			return false;
	}

	// [rc4l] The bind has to match the socket that was actually opened, which is why the fallback is
	// tracked rather than assumed from whether the first call succeeded.
	int result;

	if ( bDualStack )
	{
		sockaddr_in6 address6;
		memset( &address6, 0, sizeof address6 );
		address6.sin6_family = AF_INET6;
		address6.sin6_port = htons( static_cast<unsigned short>( port ));
		address6.sin6_addr = in6addr_any;

		result = bind( g_Listen, reinterpret_cast<sockaddr *>( &address6 ), sizeof address6 );
	}
	else
	{
		sockaddr_in address;
		memset( &address, 0, sizeof address );
		address.sin_family = AF_INET;
		address.sin_port = htons( static_cast<unsigned short>( port ));
		address.sin_addr.s_addr = htonl( INADDR_ANY );

		result = bind( g_Listen, reinterpret_cast<sockaddr *>( &address ), sizeof address );
	}

	if ( result < 0 )
	{
		CloseListener( );
		return false;
	}

	// Non-blocking: this is polled from the menu ticker and must never stall a frame.
	//
	// [rc4l] ioctlsocket on both platforms, exactly as network.cpp does it. The fcntl version this
	// started as needed <fcntl.h>, which networkheaders.h does not pull in -- so it compiled on
	// Windows, where the branch is dead, and broke on the two platforms that actually take it.
	unsigned long nonBlocking = 1;
	ioctlsocket( g_Listen, FIONBIO, &nonBlocking );

	return true;
}

void SendRequest( const std::string &cookie )
{
	// [rc4l] The registry of the family under test, falling back to whichever one answered when this
	// machine has no address of that family at all -- on a v4-only host there is nothing v6 to ask.
	NETADDRESS_s registry;
	if ( BROWSER_GetServerRegistryAddressForFamily( g_TryingV6, registry ) == false )
	{
		if ( BROWSER_GetServerRegistryAddress( registry ) == false )
		{
			g_Phase = ProbePhase::Failed;
			return;
		}
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

// [rc4l] Start the handshake again against the family we have not spent yet. False when there is
// nothing left to try, which is when a verdict is finally the host's verdict.
bool RetryOtherFamily( ProbePhase verdict )
{
	const bool bOther = !g_TryingV6;

	NETADDRESS_s registry;
	const bool bAvailable = BROWSER_GetServerRegistryAddressForFamily( bOther, registry );

	// The rule itself is computation/reachprobe_compute's; this is only the socket work behind it.
	if ( ComputeShouldTryOtherFamily( verdict, g_TryingV6, g_TriedV4, g_TriedV6, bAvailable ) == false )
		return false;

	g_TryingV6 = bOther;

	// A cookie is issued to the source address, and the other family is a different source address,
	// so the handshake starts from the top rather than reusing the one we hold.
	g_Cookie = "";
	g_Phase = ProbePhase::AwaitingCookie;
	g_PhaseStartMs = I_MSTime( );

	SendRequest( "" );
	return true;
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
	if ( g_Listen == INVALID_SOCKET )
		return;

	for ( ;; )
	{
		unsigned char encoded[MAX_UDP_PACKET];

		// [rc4l] Wide enough for either family now that the listener hears both. A sockaddr_in here
		// would be too small for a v6 sender, and recvfrom truncates the address rather than saying
		// so -- the packet still arrives, but anything later reading `from` reads rubbish.
		sockaddr_storage from;
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

std::string ReachProbePublicIp( void )
{
	return g_PublicIp;
}

void ReachProbeRequest( int port )
{
	if (( port <= 0 ) || ( port > 65535 ))
		return;

	// Already running for this port.
	if (( g_Port == port ) && ( ProbeIsFinished( g_Phase ) == false ) && ( g_Phase != ProbePhase::Idle ))
		return;

	// A usable cached answer for this exact question.
	if ( g_HaveCached && ProbeCacheUsable( g_CachedKey, CurrentKey( port ), I_MSTime( ) - g_CachedAtMs, g_CachedPhase ))
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

	// [rc4l] Both families unspent, and v6 asked FIRST: it is the one that needs no forwarding to
	// work, so on a host that has it this is usually the attempt that succeeds and the v4 round trip
	// is never spent at all.
	g_TriedV4 = false;
	g_TriedV6 = false;
	g_TryingV6 = true;

	NETADDRESS_s probe;
	if ( BROWSER_GetServerRegistryAddressForFamily( true, probe ) == false )
		g_TryingV6 = false;

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

	if ( next == g_Phase )
		return;

	// [rc4l] Mark the family spent BEFORE deciding, so a retry cannot come back to it.
	if ( g_TryingV6 )
		g_TriedV6 = true;
	else
		g_TriedV4 = true;

	// [rc4l] Reachable on one family is reachable, full stop -- a player joins over whichever one
	// works. Anything else is only this family's answer, so the other one is asked before the pill
	// is allowed to say the port is shut.
	if ( RetryOtherFamily( next ))
		return;

	Finish( next );
}

ProbePhase ReachProbeStatus( int port )
{
	if (( g_Port == port ) && ( g_Phase != ProbePhase::Idle ))
		return g_Phase;

	if ( g_HaveCached && ProbeCacheUsable( g_CachedKey, CurrentKey( port ), I_MSTime( ) - g_CachedAtMs, g_CachedPhase ))
		return g_CachedPhase;

	return ProbePhase::Idle;
}

void ReachProbeRelease( void )
{
	CloseListener( );

	// An unfinished check is abandoned rather than recorded. It was interrupted, so it knows nothing,
	// and writing a verdict here would cache "unreachable" for a port that was never actually tested.
	if ( ProbeIsFinished( g_Phase ) == false )
	{
		g_Phase = ProbePhase::Idle;
		g_Port = 0;
	}
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

// [rc4l] Everything the check knows about itself, for fua_portstatus. A white INTERNET option covers
// four different failures and they want different fixes, so the state has to be readable.
std::string ReachProbeDebugText( void )
{
	static const char *const phases[] = {
		"idle", "waiting for the registry's cookie", "waiting for the probe", "reachable",
		"unreachable", "the registry never answered",
	};

	std::string out = "port " + std::to_string( g_Port ) + ": "
		+ phases[static_cast<int>( g_Phase )] + "\n";

	out += "cookie: " + ( g_Cookie.empty( ) ? std::string( "(none yet)" ) : g_Cookie ) + "\n";
	out += "public ip as the registry sees us: "
		+ ( g_PublicIp.empty( ) ? std::string( "(never told)" ) : g_PublicIp ) + "\n";
	out += std::string( "listener on the port under test: " )
		+ (( g_Listen != INVALID_SOCKET ) ? "open" : "closed" ) + "\n";

	// [rc4l] WHICH FAMILY, because the verdict is per family and the pill is not. "Unreachable" with
	// only v4 spent means something different from "unreachable" with both spent, and without this
	// the two read identically.
	out += std::string( "asking over: " ) + ( g_TryingV6 ? "IPv6" : "IPv4" ) + "\n";
	out += std::string( "families tried: " )
		+ ( g_TriedV4 ? "v4 " : "" ) + ( g_TriedV6 ? "v6" : "" )
		+ (( !g_TriedV4 && !g_TriedV6 ) ? "(none yet)" : "" ) + "\n";

	NETADDRESS_s registry;
	if ( BROWSER_GetServerRegistryAddress( registry ))
		out += std::string( "registry resolves to " ) + registry.ToString( ) + "\n";
	else
		out += "registry does NOT resolve, so no request can leave\n";

	NETADDRESS_s perFamily;
	if ( BROWSER_GetServerRegistryAddressForFamily( false, perFamily ))
		out += std::string( "registry over v4: " ) + perFamily.ToString( ) + "\n";
	else
		out += "registry over v4: none, so v4 cannot be tested\n";

	if ( BROWSER_GetServerRegistryAddressForFamily( true, perFamily ))
		out += std::string( "registry over v6: " ) + perFamily.ToString( ) + "\n";
	else
		out += "registry over v6: none, so v6 cannot be tested\n";

	return out;
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

//*****************************************************************************
//
// [rc4l] Which leg the check is on, because "the button is white" covers four different failures and
// they need different fixes. Reads state, changes none.
CCMD( fua_portstatus )
{
	Printf( "%s", zx::ReachProbeDebugText( ).c_str( ));
}
