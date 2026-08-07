// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] networkheaders.h first, before any engine header -- see zx_wadserve.cpp for the whole
// story. It also supplies SOCKET / INVALID_SOCKET / closesocket on POSIX.
#include "networkheaders.h"

#include "features/port-mapping/zx_portmap.h"
#include "features/port-mapping/computation/natpmp_compute.h"
#include "features/port-mapping/computation/ssdp_compute.h"
#include "features/port-mapping/computation/upnpsoap_compute.h"

#include "c_console.h"
#include "doomtype.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <algorithm>
#include <vector>

#ifdef _WIN32
// [rc4l] IP_MULTICAST_IF lives here, not in what networkheaders.h pulls in. Needed to choose the
// interface an SSDP search leaves by; see DiscoverGateways.
#include <ws2tcpip.h>
typedef SOCKET zx_socket_t;
#define ZX_INVALID_SOCKET INVALID_SOCKET
#define zx_close_socket closesocket
#else
#include <netinet/in.h>
#include <unistd.h>
typedef int zx_socket_t;
#define ZX_INVALID_SOCKET (-1)
#define zx_close_socket close
#endif

namespace zx
{

namespace
{

std::atomic<int>	g_State( static_cast<int>( PortMapState::Idle ));
std::atomic<bool>	g_Busy( false );

std::mutex			g_Lock;
std::string			g_ControlUrl;		// filled once a gateway has been found
std::string			g_ServiceType;
std::string			g_RouterHost;		// for NAT-PMP, which needs no discovery of its own
int					g_MappedPort	= 0;
bool				g_UsedNatPmp	= false;

// Long enough for a slow router, short enough that a player is not left waiting on one that is
// never going to answer.
const int kDiscoverMs = 2000;
const int kHttpTimeoutMs = 4000;
const int kNatPmpTimeoutMs = 500;

// An hour. Long enough to play, short enough that a mapping left behind by a crash is not permanent
// -- and many routers ignore this and make it permanent anyway, which is why PortMapClose exists.
const int kLeaseSeconds = 3600;

void SetTimeout( zx_socket_t sock, int optname, int ms )
{
#ifdef _WIN32
	DWORD value = static_cast<DWORD>( ms );
	setsockopt( sock, SOL_SOCKET, optname, reinterpret_cast<const char *>( &value ), sizeof( value ));
#else
	struct timeval value;
	value.tv_sec = ms / 1000;
	value.tv_usec = ( ms % 1000 ) * 1000;
	setsockopt( sock, SOL_SOCKET, optname, reinterpret_cast<const char *>( &value ), sizeof( value ));
#endif
}

//*****************************************************************************
//
// [rc4l] Which of our own addresses reaches the outside world.
//
// Connecting a UDP socket sends nothing -- it only fixes a route -- so this asks the operating
// system the question it is already best placed to answer, and asks it without touching the network.
// Enumerating interfaces instead means picking one, and on a machine with a VPN, a VM bridge and a
// hypervisor adapter there is no rule for choosing that is right more often than this is.
std::string LocalAddressFor( const std::string &peer )
{
	const zx_socket_t sock = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( sock == ZX_INVALID_SOCKET )
		return "";

	sockaddr_in target;
	memset( &target, 0, sizeof( target ));
	target.sin_family = AF_INET;
	target.sin_port = htons( 9 );			// discard; nothing is ever sent there
	target.sin_addr.s_addr = inet_addr( peer.c_str( ));

	std::string out;

	if ( connect( sock, reinterpret_cast<sockaddr *>( &target ), sizeof( target )) == 0 )
	{
		sockaddr_in mine;
		memset( &mine, 0, sizeof( mine ));

		// Winsock spells this int; POSIX spells it socklen_t. networkheaders.h does not reconcile
		// the two, so the type is named per platform rather than assumed.
#ifdef _WIN32
		int length = static_cast<int>( sizeof( mine ));
#else
		socklen_t length = static_cast<socklen_t>( sizeof( mine ));
#endif

		if ( getsockname( sock, reinterpret_cast<sockaddr *>( &mine ), &length ) == 0 )
			out = inet_ntoa( mine.sin_addr );
	}

	zx_close_socket( sock );
	return out;
}

//*****************************************************************************
//
// One HTTP exchange, opened and closed. Routers keep no connections and we want none.
std::string HttpExchange( const std::string &host, int port, const std::string &request )
{
	const zx_socket_t sock = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	if ( sock == ZX_INVALID_SOCKET )
		return "";

	SetTimeout( sock, SO_RCVTIMEO, kHttpTimeoutMs );
	SetTimeout( sock, SO_SNDTIMEO, kHttpTimeoutMs );

	sockaddr_in target;
	memset( &target, 0, sizeof( target ));
	target.sin_family = AF_INET;
	target.sin_port = htons( static_cast<unsigned short>( port ));
	target.sin_addr.s_addr = inet_addr( host.c_str( ));

	std::string out;

	if ( connect( sock, reinterpret_cast<sockaddr *>( &target ), sizeof( target )) == 0 )
	{
		size_t sent = 0;
		bool bOk = true;

		while (( sent < request.size( )) && bOk )
		{
			const int n = static_cast<int>( send( sock, request.c_str( ) + sent,
				static_cast<int>( request.size( ) - sent ), 0 ));
			if ( n <= 0 )
				bOk = false;
			else
				sent += static_cast<size_t>( n );
		}

		// Bounded: a device that keeps talking does not get to hold this thread open forever, and a
		// device description worth reading is a few kilobytes.
		while ( bOk && ( out.size( ) < 64 * 1024 ))
		{
			char buffer[2048];
			const int n = static_cast<int>( recv( sock, buffer, sizeof( buffer ), 0 ));
			if ( n <= 0 )
				break;
			out.append( buffer, static_cast<size_t>( n ));
		}
	}

	zx_close_socket( sock );
	return out;
}

//*****************************************************************************
//
// [rc4l] Shout for a gateway and take the first plausible answer.
//
// Every reply is put through IsAcceptableLocation before it is believed -- anything on the LAN can
// answer this, and the whole point of that check is that we are about to fetch what it says.
std::vector<std::string> DiscoverGateways( void )
{
	std::vector<std::string> found;

	const zx_socket_t sock = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( sock == ZX_INVALID_SOCKET )
		return found;

	SetTimeout( sock, SO_RCVTIMEO, kDiscoverMs );

	// [rc4l] CHOOSE THE INTERFACE. Without this the whole feature is a coin toss.
	//
	// A multicast send with no interface set goes out whichever one the routing table ranks first,
	// and on any real machine that is not the LAN: five phantom Wi-Fi adapters on 169.254 addresses,
	// a Bluetooth PAN, a Tailscale device. Measured on the machine this was written on, an unbound
	// search got ZERO replies while the identical search bound to the LAN address got dozens -- so
	// we were reporting "your router will not open ports automatically" without the question ever
	// having reached the router.
	//
	// The address that routes to the internet is the one the router is on, and LocalAddressFor
	// answers that without sending anything. Failing to set it is not fatal: it leaves the old
	// coin-toss behaviour rather than refusing to look at all.
	const std::string localAddress = LocalAddressFor( "8.8.8.8" );
	if ( localAddress.empty( ) == false )
	{
		in_addr iface;
		memset( &iface, 0, sizeof( iface ));
		iface.s_addr = inet_addr( localAddress.c_str( ));

		setsockopt( sock, IPPROTO_IP, IP_MULTICAST_IF,
			reinterpret_cast<const char *>( &iface ), sizeof( iface ));

		// Bind it too, so the replies come back on the interface we asked from.
		sockaddr_in local;
		memset( &local, 0, sizeof( local ));
		local.sin_family = AF_INET;
		local.sin_port = 0;
		local.sin_addr.s_addr = iface.s_addr;
		bind( sock, reinterpret_cast<sockaddr *>( &local ), sizeof( local ));
	}

	sockaddr_in target;
	memset( &target, 0, sizeof( target ));
	target.sin_family = AF_INET;
	target.sin_port = htons( static_cast<unsigned short>( kSsdpPort ));
	target.sin_addr.s_addr = inet_addr( kSsdpAddress );

	// Two search targets, because some firmware answers only the specific one and some only the
	// generic. Asking twice costs two datagrams.
	static const char *const kTargets[] = {
		"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
		"upnp:rootdevice",
	};

	for ( int t = 0; t < 2; ++t )
	{
		const std::string search = BuildSsdpSearch( kTargets[t], 2 );
		sendto( sock, search.c_str( ), static_cast<int>( search.size( )), 0,
			reinterpret_cast<sockaddr *>( &target ), sizeof( target ));

		// [rc4l] EVERY answer is kept, not just the first.
		//
		// The first reply used to win outright, and on a normal home network the first reply is not
		// the router: a smart TV answers upnp:rootdevice enthusiastically and repeatedly. We would
		// take its description URL, find no WAN connection service in it, and conclude the network
		// could not forward ports -- having never spoken to the router at all.
		//
		// So the caller gets the whole list and keeps looking until one of them actually offers the
		// service we need.
		for ( int i = 0; i < 16; ++i )
		{
			char buffer[2048];
			const int n = static_cast<int>( recv( sock, buffer, sizeof( buffer ) - 1, 0 ));
			if ( n <= 0 )
				break;

			buffer[n] = '\0';
			const std::string location = LocationFromSsdpReply(
				std::string( buffer, static_cast<size_t>( n )));

			if ( location.empty( ))
				continue;

			// Devices answer both searches, and repeat themselves within one.
			if ( std::find( found.begin( ), found.end( ), location ) == found.end( ))
				found.push_back( location );
		}
	}

	zx_close_socket( sock );
	return found;
}

//*****************************************************************************
//
// Try one mapping over SOAP.
MapResult AddMapping( const std::string &controlUrl, const std::string &serviceType,
	const std::string &localAddress, int port, bool tcp, const std::string &description )
{
	const HttpUrl url = ParseHttpUrl( controlUrl );
	if ( !url.valid )
		return MapResult::Failed;

	PortMapRequest request;
	request.serviceType = serviceType;
	request.internalHost = localAddress;
	request.description = description;
	request.externalPort = port;
	request.internalPort = port;
	request.tcp = tcp;
	request.leaseSeconds = kLeaseSeconds;

	const std::string body = BuildAddPortMappingBody( request );
	const std::string http = BuildSoapRequest( url.host, url.port, url.path, serviceType,
		"AddPortMapping", body );

	return ReadMapResponse( HttpExchange( url.host, url.port, http ));
}

void DeleteMapping( const std::string &controlUrl, const std::string &serviceType, int port,
	bool tcp )
{
	const HttpUrl url = ParseHttpUrl( controlUrl );
	if ( !url.valid )
		return;

	const std::string body = BuildDeletePortMappingBody( serviceType, port, tcp );
	const std::string http = BuildSoapRequest( url.host, url.port, url.path, serviceType,
		"DeletePortMapping", body );

	HttpExchange( url.host, url.port, http );
}

//*****************************************************************************
//
// [rc4l] NAT-PMP, aimed at the device SSDP already found.
//
// The protocol says to ask the default gateway, and finding that means reading the routing table --
// three platform implementations for a fallback. The device that answered an SSDP search IS the
// gateway, so discovery is reused rather than repeated.
bool TryNatPmp( const std::string &routerHost, int port, bool tcp )
{
	const zx_socket_t sock = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( sock == ZX_INVALID_SOCKET )
		return false;

	SetTimeout( sock, SO_RCVTIMEO, kNatPmpTimeoutMs );

	sockaddr_in target;
	memset( &target, 0, sizeof( target ));
	target.sin_family = AF_INET;
	target.sin_port = htons( static_cast<unsigned short>( kNatPmpPort ));
	target.sin_addr.s_addr = inet_addr( routerHost.c_str( ));

	const std::vector<unsigned char> request = BuildNatPmpMapRequest( port, port, tcp,
		kLeaseSeconds );

	bool bOk = false;

	// Two attempts. A single lost datagram on a busy wireless network is not a router refusing.
	for ( int attempt = 0; ( attempt < 2 ) && !bOk; ++attempt )
	{
		sendto( sock, reinterpret_cast<const char *>( &request[0] ),
			static_cast<int>( request.size( )), 0,
			reinterpret_cast<sockaddr *>( &target ), sizeof( target ));

		unsigned char buffer[64];
		const int n = static_cast<int>( recv( sock, reinterpret_cast<char *>( buffer ),
			sizeof( buffer ), 0 ));
		if ( n <= 0 )
			continue;

		const std::vector<unsigned char> bytes( buffer, buffer + n );
		const NatPmpReply reply = ReadNatPmpMapReply( bytes, tcp );

		if ( reply.valid && ( reply.resultCode == 0 ))
			bOk = true;
	}

	zx_close_socket( sock );
	return bOk;
}

//*****************************************************************************
//
void MapThread( int port, std::string description )
{
	g_State.store( static_cast<int>( PortMapState::Trying ));

	const std::vector<std::string> locations = DiscoverGateways( );
	if ( locations.empty( ))
	{
		g_State.store( static_cast<int>( PortMapState::Unsupported ));
		g_Busy.store( false );
		return;
	}

	// [rc4l] Walk the responders until one of them is actually a gateway. Anything on the network may
	// answer an SSDP search, and most of what answers cannot forward a port.
	std::string location;
	std::string controlUrlFound;
	std::string serviceTypeFound;

	const char *const kServicesProbe[] = { kServiceWanIp, kServiceWanPpp };

	for ( size_t d = 0; ( d < locations.size( )) && controlUrlFound.empty( ); ++d )
	{
		const HttpUrl candidateUrl = ParseHttpUrl( locations[d] );
		if ( candidateUrl.valid == false )
			continue;

		const std::string get = "GET " + candidateUrl.path + " HTTP/1.1\r\nHOST: " + candidateUrl.host
			+ "\r\nCONNECTION: close\r\n\r\n";
		const std::string xml = HttpExchange( candidateUrl.host, candidateUrl.port, get );

		for ( int i = 0; ( i < 2 ) && controlUrlFound.empty( ); ++i )
		{
			const std::string service = ControlUrlForService( xml, kServicesProbe[i] );
			if ( service.empty( ))
				continue;

			const std::string resolved = ResolveUrl( locations[d], service );

			// Resolved or not, it still came off the network -- so it goes through the same gate as
			// the location did rather than being trusted for having been mentioned in a document we
			// fetched.
			if ( IsAcceptableLocation( resolved ) == false )
				continue;

			controlUrlFound = resolved;
			serviceTypeFound = kServicesProbe[i];
			location = locations[d];
		}
	}

	// Nothing offered a WAN service. Fall back to the first responder purely as a NAT-PMP target:
	// a router that speaks NAT-PMP but not UPnP still answers a search, and asking costs one packet.
	if ( location.empty( ))
		location = locations[0];

	const HttpUrl locationUrl = ParseHttpUrl( location );
	const std::string routerHost = locationUrl.host;

	// The address the router should forward TO is whichever of ours reaches it.
	const std::string localAddress = LocalAddressFor( routerHost );
	if ( localAddress.empty( ))
	{
		g_State.store( static_cast<int>( PortMapState::Failed ));
		g_Busy.store( false );
		return;
	}

	// Already established above, while working out which responder was the gateway.
	const std::string controlUrl = controlUrlFound;
	const std::string serviceType = serviceTypeFound;

	MapResult udp = MapResult::Failed;
	MapResult tcp = MapResult::Failed;
	bool bNatPmp = false;

	if ( controlUrl.empty( ) == false )
	{
		udp = AddMapping( controlUrl, serviceType, localAddress, port, false, description );
		tcp = AddMapping( controlUrl, serviceType, localAddress, port, true, description );
	}

	// [rc4l] NAT-PMP is the fallback, not the first choice: UPnP is far commoner, and a router that
	// speaks both will have answered already. Both protocols have to succeed for the same reason
	// both are mapped at all.
	if (( udp != MapResult::Ok ) || ( tcp != MapResult::Ok ))
	{
		if ( TryNatPmp( routerHost, port, false ) && TryNatPmp( routerHost, port, true ))
		{
			bNatPmp = true;
			udp = MapResult::Ok;
			tcp = MapResult::Ok;
		}
	}

	{
		std::lock_guard<std::mutex> guard( g_Lock );
		g_ControlUrl = controlUrl;
		g_ServiceType = serviceType;
		g_RouterHost = routerHost;
		g_MappedPort = (( udp == MapResult::Ok ) && ( tcp == MapResult::Ok )) ? port : 0;
		g_UsedNatPmp = bNatPmp;
	}

	if (( udp == MapResult::Ok ) && ( tcp == MapResult::Ok ))
		g_State.store( static_cast<int>( PortMapState::Mapped ));
	else if (( udp == MapResult::Conflict ) || ( tcp == MapResult::Conflict ))
		g_State.store( static_cast<int>( PortMapState::Conflict ));
	else if (( udp == MapResult::Refused ) || ( tcp == MapResult::Refused ))
		g_State.store( static_cast<int>( PortMapState::Unsupported ));
	else
		g_State.store( static_cast<int>( PortMapState::Failed ));

	g_Busy.store( false );
}

void UnmapThread( std::string controlUrl, std::string serviceType, std::string routerHost, int port,
	bool bNatPmp )
{
	if ( bNatPmp )
	{
		// Lifetime zero is how NAT-PMP spells "remove".
		TryNatPmp( routerHost, port, false );
		TryNatPmp( routerHost, port, true );
	}

	if ( controlUrl.empty( ) == false )
	{
		DeleteMapping( controlUrl, serviceType, port, false );
		DeleteMapping( controlUrl, serviceType, port, true );
	}
}

} // namespace

//*****************************************************************************
//
void PortMapOpen( int port, const char *description )
{
	if (( port <= 0 ) || ( port > 65535 ))
		return;

	// One at a time. A second attempt while the first is in flight would race two threads onto the
	// same router and the same globals.
	bool expected = false;
	if ( g_Busy.compare_exchange_strong( expected, true ) == false )
		return;

	PortMapClose( );

	const std::string text = ( description != NULL ) ? description : "ZandroX";
	std::thread( MapThread, port, text ).detach( );
}

//*****************************************************************************
//
void PortMapClose( void )
{
	std::string controlUrl;
	std::string serviceType;
	std::string routerHost;
	int port = 0;
	bool bNatPmp = false;

	{
		std::lock_guard<std::mutex> guard( g_Lock );
		controlUrl = g_ControlUrl;
		serviceType = g_ServiceType;
		routerHost = g_RouterHost;
		port = g_MappedPort;
		bNatPmp = g_UsedNatPmp;

		g_ControlUrl.clear( );
		g_ServiceType.clear( );
		g_RouterHost.clear( );
		g_MappedPort = 0;
		g_UsedNatPmp = false;
	}

	g_State.store( static_cast<int>( PortMapState::Idle ));

	if ( port == 0 )
		return;

	// [rc4l] On its own thread: this is called from teardown paths, including the one that runs
	// while the player is watching a menu, and a router that has stopped answering must not hold
	// the game open while we wait for it.
	std::thread( UnmapThread, controlUrl, serviceType, routerHost, port, bNatPmp ).detach( );
}

PortMapState PortMapCurrentState( void )
{
	return static_cast<PortMapState>( g_State.load( ));
}

const char *PortMapStatusText( void )
{
	switch ( PortMapCurrentState( ))
	{
	case PortMapState::Idle:
		return "";

	case PortMapState::Trying:
		return "Asking your router to open the port";

	case PortMapState::Mapped:
		return "Your router opened the port";

	case PortMapState::Unsupported:
		// Not a fault. Automatic port opening is off by default on a lot of routers, deliberately.
		return "Your router will not open ports automatically";

	case PortMapState::Conflict:
		return "Something else is already using that port on your router";

	case PortMapState::Failed:
		return "Could not ask your router to open the port";
	}

	return "";
}

void PortMapShutdown( void )
{
	PortMapClose( );
}

} // namespace zx
