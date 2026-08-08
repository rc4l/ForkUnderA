//-----------------------------------------------------------------------------
//
// Skulltag Source
// Copyright (C) 2003 Brad Carney
// Copyright (C) 2007-2012 Skulltag Development Team
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// 3. Neither the name of the Skulltag Development Team nor the names of its
//    contributors may be used to endorse or promote products derived from this
//    software without specific prior written permission.
// 4. Redistributions in any form must be accompanied by information on how to
//    obtain complete source code for the software and any accompanying
//    software that uses the software. The source code must either be included
//    in the distribution or be available for no more than the cost of
//    distribution plus a nominal fee, and must be freely redistributable
//    under reasonable conditions. For an executable file, complete source
//    code means the source code for all modules it contains. It does not
//    include source code for modules or files that typically accompany the
//    major components of the operating system on which the executable file
//    runs.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//
//
// Filename: network.cpp
//
// Description: Contains network definitions and functions not specifically
// related to the server or client.
//
//-----------------------------------------------------------------------------

#include "../src/networkheaders.h"
#include "../src/networkshared.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <ctype.h>
#include <math.h>

#include "../src/huffman/huffman.h"
#include "network.h"

//*****************************************************************************
//	VARIABLES

// Buffer that holds the data from the most recently received packet.
static	NETBUFFER_s		g_NetworkMessage;

// Network address that the most recently received packet came from.
static	NETADDRESS_s	g_AddressFrom;

// Our network socket.
static	SOCKET			g_NetworkSocket;

// [rc4l] See network.h. Sends reachability probes from a port nobody announced to.
static	SOCKET			g_ProbeSocket;

// Our local port.
static	USHORT			g_usLocalPort;

// [rc4l] Whether our sockets ended up speaking both families. Two things need it: the bind has to
// hand a socket the matching kind of address, and every SEND to a v4 peer has to be dressed as
// ::ffff:a.b.c.d, because an AF_INET6 socket refuses a sockaddr_in outright.
static	bool			g_bSocketIsDualStack = false;

// Buffer for the Huffman encoding.
static	UCHAR			g_ucHuffmanBuffer[131072];

//*****************************************************************************
//	PROTOTYPES

static	void			network_Error( const char *pszError );
static	SOCKET			network_AllocateSocket( void );
static	bool			network_BindSocketToPort( SOCKET Socket, ULONG ulInAddr, USHORT usPort, bool bReUse );

//*****************************************************************************
//	FUNCTIONS

void NETWORK_Construct( USHORT usPort, const char *pszIPAddress )
{
	char			szString[128];
	ULONG			ulArg;
	USHORT			usNewPort;
	NETADDRESS_s	LocalAddress;
	bool			bSuccess;

	// Initialize the Huffman buffer.
	HUFFMAN_Construct( );

#ifdef __WIN32__
	// [BB] Linux doesn't know WSADATA, so this may not be moved outside the ifdef.
	WSADATA			WSAData;
	if ( WSAStartup( 0x0101, &WSAData ))
		network_Error( "Winsock initialization failed!\n" );

	printf( "Winsock initialization succeeded!\n" );
#endif

	ULONG ulInAddr = INADDR_ANY;
	// [BB] An IP was specfied. Check if it's valid and if it is, try to bind our socket to it.
	if ( pszIPAddress )
	{
		ULONG requestedIP = inet_addr( pszIPAddress );
		if ( requestedIP == INADDR_NONE )
		{
			sprintf( szString, "NETWORK_Construct: %s is not a valid IP address\n", pszIPAddress );
			network_Error( szString );
		}
		else
			ulInAddr = requestedIP;
	}

	g_usLocalPort = usPort;

	// Allocate a socket, and attempt to bind it to the given port.
	g_NetworkSocket = network_AllocateSocket( );
	if ( network_BindSocketToPort( g_NetworkSocket, ulInAddr, g_usLocalPort, false ) == false )
	{
		bSuccess = true;
		bool bSuccessIP = true;
		usNewPort = g_usLocalPort;
		while ( network_BindSocketToPort( g_NetworkSocket, ulInAddr, ++usNewPort, false ) == false )
		{
			// Didn't find an available port. Oh well...
			if ( usNewPort == g_usLocalPort )
			{
				// [BB] We couldn't use the specified IP, so just try any.
				if ( ulInAddr != INADDR_ANY )
				{
					ulInAddr = INADDR_ANY;
					bSuccessIP = false;
					continue;
				}
				bSuccess = false;
				break;
			}
		}

		if ( bSuccess == false )
		{
			sprintf( szString, "NETWORK_Construct: Couldn't bind socket to port: %d\n", g_usLocalPort );
			network_Error( szString );
		}
		else if ( bSuccessIP == false )
		{
			sprintf( szString, "NETWORK_Construct: Couldn't bind socket to IP %s, using the default IP instead:\n", pszIPAddress );
			network_Error( szString );
		}
		else
		{
			printf( "NETWORK_Construct: Couldn't bind to %d. Binding to %d instead...\n", g_usLocalPort, usNewPort );
			g_usLocalPort = usNewPort;
		}
	}

	ulArg = true;
	if ( ioctlsocket( g_NetworkSocket, FIONBIO, &ulArg ) == -1 )
		printf( "network_AllocateSocket: ioctl FIONBIO: %s", strerror( errno ));

	// [rc4l] The probe socket. See network.h: verification sent from the socket servers announce to
	// proves only that WE can reach them, because their NAT already has a mapping for us.
	//
	// Port 0 asks the OS for any free port, which is all that matters -- it simply must not be the
	// one they talked to. A failure here is not fatal: probes fall back to the main socket, which is
	// how it behaved before, so the daemon keeps working and only the strictness is lost.
	g_ProbeSocket = network_AllocateSocket( );
	if ( network_BindSocketToPort( g_ProbeSocket, ulInAddr, 0, false ) == false )
	{
		printf( "NETWORK_Construct: couldn't open a probe socket; reachability checks will be weaker.\n" );
		g_ProbeSocket = g_NetworkSocket;
	}
	else if ( ioctlsocket( g_ProbeSocket, FIONBIO, &ulArg ) == -1 )
	{
		printf( "NETWORK_Construct: ioctl FIONBIO on probe socket: %s\n", strerror( errno ));
	}

	// Init our read buffer.
	// [BB] Vortex Cortex pointed us to the fact that the smallest huffman code is only 3 bits
	// and it turns into 8 bits when it's decompressed. Thus we need to allocate a buffer that
	// can hold the biggest possible size we may get after decompressing (aka Huffman decoding)
	// the incoming UDP packet.
	g_NetworkMessage.Init( ((MAX_UDP_PACKET * 8) / 3 + 1), BUFFERTYPE_READ );

	// [BB] Get and save our local IP.
	if ( ( ulInAddr == INADDR_ANY ) || ( pszIPAddress == NULL ) )
		LocalAddress = NETWORK_GetLocalAddress( );
	// [BB] We are using a specified IP, so we don't need to figure out what IP we have, but just use the specified one.
	else
	{
		LocalAddress.LoadFromString ( pszIPAddress );
		LocalAddress.usPort = htons ( NETWORK_GetLocalPort() );
	}

	// Print out our local IP address.
	printf( "IP address %s\n", LocalAddress.ToString() );

	printf( "UDP Initialized.\n" );
}

//*****************************************************************************
//
// [rc4l] One socket's worth of receiving. Returns the byte count, or -1 for "nothing to read", which
// covers both an empty non-blocking socket and the errors that are not worth stopping over.
//
// Split out from NETWORK_GetPackets because there are now two sockets to drain, and draining only the
// first one is exactly the bug this exists to prevent.
// [rc4l] sockaddr_storage, because a sockaddr is sixteen bytes and a sockaddr_in6 is twenty-eight.
// A dual-stack socket hands back the larger one, and the kernel refuses with EFAULT rather than
// truncating it, so a v4-sized buffer here does not lose the address: it loses every packet.
static LONG network_ReceiveFromSocket( SOCKET Socket, sockaddr_storage &SocketFrom )
{
	INT iSocketFromLength = sizeof( SocketFrom );
	LONG lNumBytes;

#ifdef	WIN32
	lNumBytes = recvfrom( Socket, (char *)g_ucHuffmanBuffer, sizeof( g_ucHuffmanBuffer ), 0, reinterpret_cast<sockaddr *>( &SocketFrom ), &iSocketFromLength );
#else
	lNumBytes = recvfrom( Socket, (char *)g_ucHuffmanBuffer, sizeof( g_ucHuffmanBuffer ), 0, reinterpret_cast<sockaddr *>( &SocketFrom ), (socklen_t *)&iSocketFromLength );
#endif

	// If the number of bytes returned is -1, an error has occured.
    if ( lNumBytes == -1 )
    {
#ifdef __WIN32__
        errno = WSAGetLastError( );

        if ( errno == WSAEWOULDBLOCK )
            return ( -1 );

		// Connection reset by peer. Doesn't mean anything to the server.
		if ( errno == WSAECONNRESET )
			return ( -1 );

        if ( errno == WSAEMSGSIZE )
		{
             printf( "NETWORK_GetPackets:  WARNING! Oversize packet from %s\n", g_AddressFrom.ToString() );
             return ( -1 );
        }

        printf( "NETWORK_GetPackets: WARNING!: Error #%d: %s\n", errno, strerror( errno ));
		return ( -1 );
#else
        if ( errno == EWOULDBLOCK )
            return ( -1 );

        if ( errno == ECONNREFUSED )
            return ( -1 );

        printf( "NETWORK_GetPackets: WARNING!: Error #%d: %s\n", errno, strerror( errno ));
        return ( -1 );
#endif
    }

	return ( lNumBytes );
}

//*****************************************************************************
//
int NETWORK_GetPackets( void )
{
	LONG				lNumBytes;
	INT					iDecodedNumBytes = sizeof(g_ucHuffmanBuffer);
	sockaddr_storage	SocketFrom;

	lNumBytes = network_ReceiveFromSocket( g_NetworkSocket, SocketFrom );

	// [rc4l] The probe socket is NOT write-only, and treating it as though it were took the whole
	// registry down: verification requests leave by it, servers reply to whatever address the request
	// arrived from, and so every verification reply lands here and nowhere else. Draining only the
	// main socket meant no server could ever verify, and therefore none could ever be listed.
	if (( lNumBytes <= 0 ) && ( g_ProbeSocket != g_NetworkSocket ))
		lNumBytes = network_ReceiveFromSocket( g_ProbeSocket, SocketFrom );

	// No packets or an error, so don't process anything.
	if ( lNumBytes <= 0 )
		return ( 0 );

	// If the number of bytes we're receiving exceeds our buffer size, ignore the packet.
	if ( lNumBytes >= static_cast<LONG>(g_NetworkMessage.ulMaxSize) )
		return ( 0 );

	// Decode the huffman-encoded message we received.
	HUFFMAN_Decode( g_ucHuffmanBuffer, (unsigned char *)g_NetworkMessage.pbData, lNumBytes, &iDecodedNumBytes );
	g_NetworkMessage.ulCurrentSize = iDecodedNumBytes;
	g_NetworkMessage.ByteStream.pbStream = g_NetworkMessage.pbData;
	g_NetworkMessage.ByteStream.pbStreamEnd = g_NetworkMessage.ByteStream.pbStream + g_NetworkMessage.ulCurrentSize;

	// Store the IP address of the sender.
	g_AddressFrom.LoadFromSocketAddress( reinterpret_cast<const sockaddr &>( SocketFrom ));

	return ( g_NetworkMessage.ulCurrentSize );
}

//*****************************************************************************
//
NETADDRESS_s NETWORK_GetFromAddress( void )
{
	return ( g_AddressFrom );
}

//*****************************************************************************
//
// [rc4l] The higher of the two descriptors, for select's nfds argument. On Windows nfds is ignored,
// but it is computed there too rather than left as a platform-shaped difference to trip over later.
SOCKET NETWORK_MaxSocket( void )
{
	return (( g_ProbeSocket > g_NetworkSocket ) ? g_ProbeSocket : g_NetworkSocket );
}

//*****************************************************************************
//
// [rc4l] See network.h. Identical to NETWORK_LaunchPacket except for the socket it leaves by, which
// is the entire point: a different source port means no NAT mapping to ride in on.
void NETWORK_LaunchProbePacket( NETBUFFER_s *pBuffer, NETADDRESS_s Address )
{
	INT iNumBytesOut = sizeof( g_ucHuffmanBuffer );

	pBuffer->ulCurrentSize = pBuffer->CalcSize();
	if ( pBuffer->ulCurrentSize == 0 )
		return;

	// [rc4l] sockaddr_storage, big enough for a v6 address. sockaddr_in is 16 bytes and a v6
	// socket address is 28, so the old local could not hold what ToSocketAddress now writes.
	struct sockaddr_storage SocketAddress;
	const int iAddressLength = Address.ToSocketAddress( SocketAddress, g_bSocketIsDualStack );

	HUFFMAN_Encode( (unsigned char *)pBuffer->pbData, g_ucHuffmanBuffer, pBuffer->ulCurrentSize,
		&iNumBytesOut );

	sendto( g_ProbeSocket, (const char *)g_ucHuffmanBuffer, iNumBytesOut, 0,
		reinterpret_cast<sockaddr *>( &SocketAddress ), iAddressLength );
}

//*****************************************************************************
//
void NETWORK_LaunchPacket( NETBUFFER_s *pBuffer, NETADDRESS_s Address )
{
	LONG				lNumBytes;
	INT					iNumBytesOut = sizeof(g_ucHuffmanBuffer);

	pBuffer->ulCurrentSize = pBuffer->CalcSize();

	// Nothing to do.
	if ( pBuffer->ulCurrentSize == 0 )
		return;

	// Convert the IP address to a socket address.
	// [rc4l] sockaddr_storage, big enough for a v6 address. sockaddr_in is 16 bytes and a v6
	// socket address is 28, so the old local could not hold what ToSocketAddress now writes.
	struct sockaddr_storage SocketAddress;
	const int iAddressLength = Address.ToSocketAddress( SocketAddress, g_bSocketIsDualStack );

	HUFFMAN_Encode( (unsigned char *)pBuffer->pbData, g_ucHuffmanBuffer, pBuffer->ulCurrentSize, &iNumBytesOut );

	lNumBytes = sendto( g_NetworkSocket, (const char*)g_ucHuffmanBuffer, iNumBytesOut, 0, reinterpret_cast<sockaddr*>(&SocketAddress), iAddressLength );

	// If sendto returns -1, there was an error.
	if ( lNumBytes == -1 )
	{
#ifdef __WIN32__
		INT	iError = WSAGetLastError( );

		// Wouldblock is silent.
		if ( iError == WSAEWOULDBLOCK )
			return;

		switch ( iError )
		{
		case WSAEACCES:

			printf( "NETWORK_LaunchPacket: Error #%d, WSAEACCES: Permission denied for address: %s\n", iError, Address.ToString() );
			return;
		case WSAEADDRNOTAVAIL:

			printf( "NETWORK_LaunchPacket: Error #%d, WSAEADDRENOTAVAIL: Address %s not available\n", iError, Address.ToString() );
			return;
		case WSAEHOSTUNREACH:

			printf( "NETWORK_LaunchPacket: Error #%d, WSAEHOSTUNREACH: Address %s unreachable\n", iError, Address.ToString() );
			return;				
		default:

			printf( "NETWORK_LaunchPacket: Error #%d\n", iError );
			return;
		}
#else
	if ( errno == EWOULDBLOCK )
return;

          if ( errno == ECONNREFUSED )
              return;

		printf( "NETWORK_LaunchPacket: %s\n", strerror( errno ));
		printf( "NETWORK_LaunchPacket: Address %s\n", Address.ToString() );

#endif
	}
}

//*****************************************************************************
//
NETADDRESS_s NETWORK_GetLocalAddress( void )
{
	char				szBuffer[512];
	struct sockaddr_in	SocketAddress;
	int					iNameLength;

#ifndef __WINE__
	gethostname( szBuffer, 512 );
#endif
	szBuffer[512-1] = 0;

	// Convert the host name to our local 
	NETADDRESS_s Address ( szBuffer );

	iNameLength = sizeof( SocketAddress );
#ifndef	WIN32
	if ( getsockname ( g_NetworkSocket, (struct sockaddr *)&SocketAddress, (socklen_t *)&iNameLength) == -1 )
#else
	if ( getsockname ( g_NetworkSocket, (struct sockaddr *)&SocketAddress, &iNameLength ) == -1 )
#endif
	{
		printf( "NETWORK_GetLocalAddress: Error getting socket name: %s", strerror( errno ));
	}

	Address.usPort = SocketAddress.sin_port;
	return ( Address );
}

//*****************************************************************************
//
NETBUFFER_s *NETWORK_GetNetworkMessageBuffer( void )
{
	return ( &g_NetworkMessage );
}

//*****************************************************************************
//
ULONG NETWORK_ntohs( ULONG ul )
{
	return ( ntohs( (u_short)ul ));
}

//*****************************************************************************
//
USHORT NETWORK_GetLocalPort( void )
{
	return ( g_usLocalPort );
}

//*****************************************************************************
//*****************************************************************************
//
void network_Error( const char *pszError )
{
	printf( "\\cd%s\n", pszError );
}

//*****************************************************************************
//
// [rc4l] Whether our sockets speak both families. The bind below has to hand a socket the matching
// kind of address, and only this knows which kind it got.


//*****************************************************************************
//
// [rc4l] A socket that hears v6 AND v4, matching what the engine does.
//
// The registry has to be reachable by every server and every launcher, so it is the last thing that
// should be picky about families. Turning IPV6_V6ONLY off makes one v6 socket accept v4 peers too,
// arriving as ::ffff:a.b.c.d, which NETADDRESS_s::LoadFromSocketAddress puts straight back into v4 so
// nothing downstream sees the mapped form. The ban list, the server set and the reach cookies all
// key on addresses, and every one of them would break if the same host were two different addresses
// depending on which way it came in.
//
// Falls back to a plain v4 socket for the same reasons it does in the engine: the option defaults
// differently across platforms, the stack can be disabled outright, and a registry that only hears
// v6 would be invisible to every server that exists today.
static SOCKET network_AllocateSocket( void )
{
	SOCKET	Socket;

	Socket = socket( PF_INET6, SOCK_DGRAM, IPPROTO_UDP );
	if ( Socket != INVALID_SOCKET )
	{
		int off = 0;
		if ( setsockopt( Socket, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&off, sizeof( off )) == 0 )
		{
			g_bSocketIsDualStack = true;
			return ( Socket );
		}

		closesocket( Socket );
	}

	g_bSocketIsDualStack = false;

	// Allocate a socket.
	Socket = socket( PF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( Socket == INVALID_SOCKET )
		network_Error( "network_AllocateSocket: Couldn't create socket!" );

	return ( Socket );
}

//*****************************************************************************
//
bool network_BindSocketToPort( SOCKET Socket, ULONG ulInAddr, USHORT usPort, bool bReUse )
{
	int		iErrorCode;

	// setsockopt needs an int, bool won't work
	int		enable = 1;

	// Allow the network socket to broadcast. Meaningless on a v6 socket, which has multicast and no
	// broadcast at all, and harmless: the call simply fails there.
	setsockopt( Socket, SOL_SOCKET, SO_BROADCAST, (const char *)&enable, sizeof( enable ));
	if ( bReUse )
		setsockopt( Socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&enable, sizeof( enable ));

	// [rc4l] Bind to match the SOCKET'S family. in6addr_any covers every v4 address as well on a
	// dual-stack socket; handing it a sockaddr_in fails outright and the registry never comes up.
	if ( g_bSocketIsDualStack )
	{
		struct sockaddr_in6 address6;
		memset( &address6, 0, sizeof( address6 ));
		address6.sin6_family = AF_INET6;
		address6.sin6_addr = in6addr_any;
		address6.sin6_port = htons( usPort );

		iErrorCode = bind( Socket, (sockaddr *)&address6, sizeof( address6 ));
		return ( iErrorCode != SOCKET_ERROR );
	}

	struct sockaddr_in address;
	memset (&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = ulInAddr;
	address.sin_port = htons( usPort );

	iErrorCode = bind( Socket, (sockaddr *)&address, sizeof( address ));
	if ( iErrorCode == SOCKET_ERROR )
		return ( false );

	return ( true );
}


#ifndef	WIN32
extern int	stdin_ready;
extern int	do_stdin;
#endif

// [BB] We only need this for the server console input under Linux.
void I_DoSelect (void)
{
#ifdef		WIN32
	// [BC] We need this code here to be executed. The point of this function is to
	// make the thread sleep until a packet is received. That way, the thread doesn't
	// use 100% of the CPU.
    struct timeval   timeout;
    fd_set           fdset;

    FD_ZERO(&fdset);
    FD_SET(g_NetworkSocket, &fdset);
    // [rc4l] Verification replies arrive on the probe socket, so it has to be waited on too. The
    // one second timeout below would have polled it eventually, but "eventually" against a four
    // second verification window is not a margin worth keeping.
    FD_SET(g_ProbeSocket, &fdset);
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    if (select (static_cast<int>(NETWORK_MaxSocket())+1, &fdset, NULL, NULL, &timeout) == -1)
        return;
#else
    struct timeval   timeout;
    fd_set           fdset;

    FD_ZERO(&fdset);
    if (do_stdin)
    	FD_SET(0, &fdset);

    FD_SET(g_NetworkSocket, &fdset);
    // [rc4l] Same reason as the Windows branch above: replies to a verification request come back on
    // the probe socket, so it belongs in the wait set.
    FD_SET(g_ProbeSocket, &fdset);
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    if (select (static_cast<int>(NETWORK_MaxSocket())+1, &fdset, NULL, NULL, &timeout) == -1)
        return;

    stdin_ready = FD_ISSET(0, &fdset);
#endif
} 
