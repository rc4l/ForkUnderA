//-----------------------------------------------------------------------------
//
// Skulltag Source
// Copyright (C) 2007 Brad Carney, Benjamin Berkels
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
// Filename: networkshared.h
//
// Description: Contains shared network code shared between Skulltag and its satellites (server registry, statsmaker, rcon utility, etc).
//
//-----------------------------------------------------------------------------

#ifndef __NETWORKSHARED_H__
#define __NETWORKSHARED_H__

#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <iostream>
#include <vector>
#include <list>
#include <time.h>
#include <ctype.h>
#include <math.h>

//--------------------------------------------------------------------------------------------------------------------------------------------------
//-- DEFINES ---------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------

// Maximum size of the packets sent out by the server.
#define	MAX_UDP_PACKET				8192

// [BB] Number of packets that are stored to recover from packet loss.
#define PACKET_BUFFER_SIZE			2048

//*****************************************************************************
enum BUFFERTYPE_e
{
	BUFFERTYPE_READ,
	BUFFERTYPE_WRITE,

};

//*****************************************************************************
enum
{
	SRSC_BEGINSERVERLIST,
	SRSC_SERVER,
	SRSC_ENDSERVERLIST,
	SRSC_IPISBANNED,
	SRSC_REQUESTIGNORED,
	SRSC_WRONGVERSION,
	SRSC_BEGINSERVERLISTPART,
	SRSC_ENDSERVERLISTPART,
	SRSC_SERVERBLOCK,

	// [rc4l] The cookie leg of a reachability test, answered to a launcher rather than to a server --
	// which is why it lives here and not with SERVERREGISTRY_*, whose commands are read as bytes by
	// servers. This one is read as a long by the client, on the socket that asked.
	SRSC_REACHCOOKIE,

	// [rc4l] The registry's answer to a punch request: whether it introduced us, and if not, why.
	//
	// A reason rather than a bool because saying no FAST is the point. Every Zandronum server ever
	// shipped is a NoSupport, and a client that learns that immediately connects the ordinary way
	// instead of sitting out a timeout to discover what the registry already knew.
	SRSC_PUNCHRESULT,

	// [rc4l] Written in the verdict slot of SRSC_PUNCHRESULT to mean "this is the cookie leg,
	// echo it back" rather than a decision. Negative so it can never collide with a PunchVerdict,
	// which is an enum counting up from zero.
	SERVERREGISTRY_PUNCH_COOKIE = -1,

};

//*****************************************************************************
enum
{
	// Server is letting server registry of its existence.
	SERVER_SERVERREGISTRY_CHALLENGE = 5660020,

	// [RC] This is no longer used.
	/*
		// Server is letting server registry of its existence, along with sending an IP the server registry
		// should use for this server.
		SERVER_SERVERREGISTRY_CHALLENGE_OVERRIDE = 5660021,
	*/

	// Server is sending some statistics to the server registry.
	SERVER_SERVERREGISTRY_STATISTICS = 5660022,

	// Server is sending its info to the launcher.
	SERVER_LAUNCHER_CHALLENGE,

	// Server is telling a launcher that it's ignoring it.
	SERVER_LAUNCHER_IGNORING,

	// Server is telling a launcher that his IP is banned from the server.
	SERVER_LAUNCHER_BANNED,

	// Client is trying to create a new account with the server registry.
	CLIENT_SERVERREGISTRY_NEWACCOUNT,

	// Client is trying to log in with the server registry.
	CLIENT_SERVERREGISTRY_LOGIN,

	// [BB] Launcher is querying the server registry for a full server list, possibly split into several packets.
	LAUNCHER_SERVERREGISTRY_CHALLENGE,

	// [BB] Server is answering a ServerRegistryBanlistVerificationString verification request.
	SERVER_SERVERREGISTRY_VERIFICATION,

	// [BB] Server is acknowledging the receipt of a ban list.
	SERVER_SERVERREGISTRY_BANLIST_RECEIPT,

	// [SB] Server is sending a launcher a segmented response.
	// Skipped 5660031 for compatiblity with old segmented implementation.
	SERVER_LAUNCHER_CHALLENGE_SEGMENTED = 5660032,

	// [rc4l] A client asking the registry "can you reach me on this port?", before it hosts anything.
	//
	// Two of these are sent. The first carries an empty cookie and gets one back; the second echoes
	// that cookie, which is what proves the sender really is at the source address rather than having
	// forged it -- see features/server-hosting/computation/reachprobe_compute.h. Only after the echo
	// does the registry send anything unsolicited.
	CLIENT_SERVERREGISTRY_REACHTEST = 5660033,

	// [rc4l] A client asking the registry to introduce it to a server it cannot otherwise reach.
	//
	// Two legs, exactly like REACHTEST above and for exactly the same reason: the first carries an
	// empty cookie and gets one back, the second echoes it, and only then will the registry send
	// anything unsolicited. Without that echo the source address could be forged and the HOST would
	// be aimed at a victim who never asked for a packet.
	//
	// Carries the address of the server to be introduced to. That address is only ever used to look
	// up a server we have already verified; it is never dialled on the strength of the client saying
	// so. The only address anybody dials is the observed source of this request.
	CLIENT_SERVERREGISTRY_PUNCH = 5660034,
};

// [BB] Protocol version of the server registry, currently only used in conjunction with LAUNCHER_SERVERREGISTRY_CHALLENGE.
#define SERVERREGISTRY_VERSION		2

// Launcher is querying the server, or server registry.
#define	LAUNCHER_SERVER_CHALLENGE	199

enum
{
	// Server registry is sending its banlist to a server.
	SERVERREGISTRY_BANLIST = 205,

	// [BB] Server registry is asking the server to verify its ServerRegistryBanlistVerificationString.
	SERVERREGISTRY_VERIFICATION,

	// [BB] Server registry is sending a part of its banlist to a server.
	SERVERREGISTRY_BANLISTPART,

	// [rc4l] The probe itself: sent UNSOLICITED to the source address on the port it named, carrying
	// the client's own nonce. Arriving at all is the answer.
	//
	// A byte, like its neighbours here, because it lands on a bare socket the client opened for this
	// and nothing else -- there is no launcher framing around it to match. (The cookie leg answers a
	// launcher instead, so it lives in the SRSC_ enum and is read as a long.)
	SERVERREGISTRY_REACHPROBE,

	// [rc4l] The registry telling a SERVER that somebody wants in, and where they are.
	//
	// The server answers by sending one small packet to that address. The packet's content does not
	// matter and it will very likely be dropped at the far end; sending it is the entire point,
	// because that is what makes the server's own router accept the reply that follows.
	//
	// A server built before this ignores the unknown byte, which is exactly the desired behaviour:
	// the punch is an optimisation attempted alongside the ordinary connection, never instead of it.
	SERVERREGISTRY_PUNCH,
};

// [BB] Various enums used in SERVERREGISTRY_BANLISTPART packets.
enum
{
	MSB_BAN,
	MSB_BANEXEMPTION,
	MSB_ENDBANLISTPART,
	MSB_ENDBANLIST,
};

#define	DEFAULT_SERVER_PORT			10666
#define	DEFAULT_CLIENT_PORT			10667
#define	DEFAULT_SERVERREGISTRY_PORT			15300
#define	DEFAULT_BROADCAST_PORT		15101
#define	DEFAULT_STATS_PORT			15201

// This is the longest possible string we can pass over the network.
#define	MAX_NETWORK_STRING			2048

//--------------------------------------------------------------------------------------------------------------------------------------------------
//-- STRUCTURES ------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------

struct NETADDRESS_s;
struct BYTESTREAM_s;

//==========================================================================
//
// IPStringArray
//
// @author Benjamin Berkels
//
//==========================================================================

// [rc4l] STILL IPv4 ONLY, deliberately and temporarily.
//
// Four decimal octets, so a ban can be written 1.2.3.* and matched a field at a time. v6 does not
// fit that shape: it has eight groups, and the thing people ban is a /64 prefix, which is a length
// rather than a wildcard. Replacing this means storing a prefix and a length and rewriting the ban
// FILE, which is a format change and wants deciding rather than inventing halfway through a socket
// migration.
//
// Until then SetFrom refuses a v6 address rather than flattening one into 0.0.0.0, so a v6 player is
// unbannable here instead of being wrongly matched against somebody else's rule. That is the safe
// direction of the two, and it is the whole reason this note exists rather than a silent gap.
class IPStringArray
{
private:
	char szAddress[4][4];

public:

	void Clear()
	{
		for ( int i = 0; i < 4; ++i )
			szAddress[i][0] = 0;
	}

	void SetToZeroes ( )
	{
		for ( int i = 0; i < 4; ++i )
			sprintf( szAddress[i], "0" );
	}

	void SetFrom ( const NETADDRESS_s &Address );

	bool SetFromString ( const char *pszAddressString );

	bool IsEqualTo ( const IPStringArray& other ) const
	{
		for ( int i = 0; i < 4; ++i )
		{
			if ( stricmp ( szAddress[i], other[i] ) != 0 )
				return false;
		}
		return true;
	}

	int CompareForSort ( const IPStringArray& other )
	{
		for ( int i = 0; i < 4; ++i )
		{
			if ( atoi( szAddress[i] ) != atoi( other[i] ) )
				return ( atoi( szAddress[i] ) < atoi ( other[i] ) ) ? -1 : 1;
		}
		return 0;
	}

	bool Matches ( const IPStringArray& otherWithWildcards ) const
	{
		for ( int i = 0; i < 4; ++i )
		{
			if (( otherWithWildcards[i][0] != '*' ) && ( stricmp( szAddress[i], otherWithWildcards[i] ) != 0 ) )
				return false;
		}
		return true;
	}

	void copyFrom ( const IPStringArray& other )
	{
		for (int i = 0; i < 4; ++i)
			sprintf ( szAddress[i], "%s", other[i] );
	}

	std::ostream& print ( std::ostream& os ) const
	{
		return os << szAddress[0] << "." << szAddress[1] << "." << szAddress[2] << "." << szAddress[3];
	}

	const char* operator[] ( int i ) const
	{
		if (( i < 0 ) || ( i >= 4 ))
			return nullptr;

		return szAddress[i];
	}

	operator std::string () const
	{
		std::string stringRepresentation;
		stringRepresentation = stringRepresentation + szAddress[0] + "." + szAddress[1] + "." + szAddress[2] + "." + szAddress[3];
		return stringRepresentation;
	}
};

extern std::ostream &operator<< ( std::ostream &os, const IPStringArray &input );

//*****************************************************************************
struct NETADDRESS_s
{
public:
	// Four digit IP address. Meaningful only while bIsIPv6 is false.
	BYTE		abIP[4];

	// [rc4l] The v6 address, and which of the two families this is.
	//
	// A second field rather than a widened first one, deliberately. Five files read abIP a byte at a
	// time, and every one of those reads is about a v4 address in a v4 context: a ban wildcard, a
	// private-range test, a broadcast address. Reinterpreting that storage would have made all of
	// them silently wrong for v6 rather than loudly absent, and quiet wrongness in the network layer
	// is the worst outcome available. Costing 17 bytes per address to keep them honest is nothing.
	BYTE		abIP6[16];
	bool		bIsIPv6;

	// The IP address's port extension.
	USHORT		usPort;

	NETADDRESS_s();
	explicit NETADDRESS_s ( const char* string, bool* ok = NULL );

	void Clear ();
	bool Compare ( const NETADDRESS_s& other, bool ignorePort = false ) const;
	bool CompareNoPort ( const NETADDRESS_s& other ) const { return Compare( other, true ); }
	// [rc4l] sockaddr_storage, NOT sockaddr. The latter is 16 bytes and a v6 socket address is 28,
	// so the old signature could not hold what this now writes: passing the narrow type would have
	// overwritten whatever sat after it on the caller's stack, which is a corruption bug that
	// reproduces as unrelated networking nonsense somewhere else entirely.
	// [rc4l] `bMapV4ToV6` is what the SOCKET needs, not what the address is.
	//
	// A dual-stack socket is AF_INET6 and speaks nothing else, so an ordinary v4 destination has to be
	// dressed as ::ffff:a.b.c.d before it can be sent to. Handing such a socket a sockaddr_in does not
	// quietly work, it fails with EAFNOSUPPORT and the packet never leaves.
	//
	// Returns how many bytes of SocketAddress are meaningful, because that is what sendto and bind
	// actually want: sizeof(sockaddr_storage) is 128 bytes of mostly padding and passing it is wrong
	// even where it is tolerated.
	int ToSocketAddress( struct sockaddr_storage &SocketAddress, bool bMapV4ToV6 = false ) const;
	void SetPort ( USHORT port );
	const char* ToString() const;

	// The v6 address as text, unbracketed and without a port. Writes into the caller's buffer rather
	// than a static one, so ToString can build the bracketed form without the two fighting over it.
	const char* AddressToStringNoPort( char *buffer, size_t len ) const;
	const char* ToStringNoPort() const;
	bool LoadFromString( const char* string );
	void LoadFromSocketAddress ( const struct sockaddr& sockaddr );
	bool IsSet () const;
	void WriteToStream ( BYTESTREAM_s *pByteStream, bool IncludePort = true ) const;
	void ReadFromStream ( BYTESTREAM_s *pByteStream, bool IncludePort = true );

private:
	bool operator==( const NETADDRESS_s& );
	bool operator!=( const NETADDRESS_s& );

	friend class IPStringArray;
};

//*****************************************************************************
struct IPADDRESSBAN_s
{
	// The IP address in char form (can be a number or a wildcard).
	IPStringArray szIP;

	// Comment regarding the banned address.
	char		szComment[128];

	// [RC] Time that the ban expires, or NULL for an infinite ban.
	time_t		tExpirationDate;

	// [AK] Returns the expiration date and time as a string.
	std::string GetExpirationAsString( void ) const;
};

//*****************************************************************************
struct BYTESTREAM_s
{
	BYTESTREAM_s();
	void EnsureBitSpace( int bits, bool writing );

	int	ReadByte();
	int ReadShort();
	int	ReadLong();
	float ReadFloat();
	const char* ReadString();
	bool ReadBit();
	int ReadVariable();
	int ReadShortByte( int bits );
	void ReadBuffer( void* buffer, size_t length );

	void WriteByte( int Byte );
	void WriteShort( int Short );
	void WriteLong( int Long );
	void WriteFloat( float Float );
	void WriteString( const char *pszString );
	void WriteBit( bool bit );
	void WriteVariable( int value );
	void WriteShortByte( int value, int bits );
	void WriteBuffer( const void *pvBuffer, int nLength );

	void WriteHeader( int Byte );

	// Pointer to our stream of data.
	BYTE		*pbStream;

	// Pointer to the end of the stream. When pbStream > pbStreamEnd, the
	// entire stream has been read.
	BYTE		*pbStreamEnd;

	BYTE		*bitBuffer;
	int			bitShift;

#ifdef CREATE_PACKET_LOG
	// [RC] Pointer to the start of the stream.
	BYTE		*pbStreamBeginning;

	// [RC] Whether or not we've logged this.
	bool		bPacketAlreadyLogged;
#endif
	void AdvancePointer( const int NumBytes, const bool OutboundTraffic );
};


//*****************************************************************************
struct NETBUFFER_s
{
	// This is the data in our packet.
	BYTE			*pbData;

	// The maximum amount of data this packet can hold.
	ULONG			ulMaxSize;

	// How much data is currently in this packet?
	ULONG			ulCurrentSize;

	// Byte stream for this buffer for managing our current position and where
	// the end of the stream is.
	BYTESTREAM_s	ByteStream;

	// Is this a buffer that we write to, or read from?
	BUFFERTYPE_e	BufferType;

	NETBUFFER_s ( );
	NETBUFFER_s ( const NETBUFFER_s &Buffer );

	void			Init( ULONG ulLength, BUFFERTYPE_e BufferType );
	void			Free();
	void			Clear();
	LONG			CalcSize() const;
	LONG			WriteTo( BYTESTREAM_s &ByteStream ) const;
};

//--------------------------------------------------------------------------------------------------------------------------------------------------
//-- PROTOTYPES ------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------

void			NETWORK_StartTrafficMeasurement ( );
int				NETWORK_StopTrafficMeasurement ( );

//--------------------------------------------------------------------------------------------------------------------------------------------------
//-- CLASSES ---------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------

//==========================================================================
//
// IPFileParser
//
// Reads a list of IPs from a file (ie, a banlist). Supports wildcards and comments.
// @author Benjamin Berkels, Brad Carney
//
//==========================================================================

class IPFileParser
{
	const unsigned int	_listLength;
	ULONG				_numberOfEntries;
	char				_errorMessage[1024];

//*************************************************************************
public:
	IPFileParser( const int IPListLength ) : _listLength( IPListLength )
	{
		_errorMessage[0] = '\0';
	}
		
	const char* getErrorMessage( )
	{
		return _errorMessage;
	}

	ULONG getNumberOfEntries ( )
	{
		return _numberOfEntries;
	}

	bool parseIPList( const char* FileName, std::vector<IPADDRESSBAN_s> &IPArray );

//*************************************************************************
private:
	char		skipWhitespace( FILE *pFile );
	char		skipComment( FILE *pFile );
	void		readReason( FILE *pFile, char *Reason, const int MaxReasonLength );
	time_t		readExpirationDate( FILE *pFile );
	bool		parseNextLine( FILE *pFile, IPADDRESSBAN_s &IP, ULONG &BanIdx );
};

//==========================================================================
//
// IPList
//
// Stores a list of IPs. Supports wildcards.
// @author Benjamin Berkels
//
//==========================================================================

class IPList
{
	std::vector<IPADDRESSBAN_s>		_ipVector;
	std::string						_filename;
	std::string						_error;

//*************************************************************************
public:
	bool			clearAndLoadFromFile( const char *Filename );
	ULONG			getFirstMatchingEntryIndex( const IPStringArray &szAddress ) const;
	ULONG			getFirstMatchingEntryIndex( const NETADDRESS_s &Address ) const;
	bool			isIPInList( const IPStringArray &szAddress ) const;
	bool			isIPInList( const NETADDRESS_s &Address ) const;
	ULONG			doesEntryExist( const IPStringArray &szAddress ) const;
	IPADDRESSBAN_s	getEntry( const ULONG ulIdx ) const;
	std::string		getEntryAsString( const ULONG ulIdx, bool bIncludeComment = true, bool bIncludeExpiration = true, bool bInludeNewline = true ) const;
	ULONG			getEntryIndex( const NETADDRESS_s &Address ) const; // [RC]
	const char		*getEntryComment( const NETADDRESS_s &Address ) const; // [RC]
	time_t			getEntryExpiration( const NETADDRESS_s &Address ) const; // [RC]
	void			addEntry( const IPStringArray &szAddress, const char *pszPlayerName, const char *pszComment, std::string &Message, time_t tExpiration );
	void			addEntry( const char *pszIPAddress, const char *pszPlayerName, const char *pszComment, std::string &Message, time_t tExpiration );
	void			removeEntry( const IPStringArray &szAddress, std::string &Message );
	void			removeEntry( const char *pszIPAddress, std::string &Message );
	void			removeEntry( ULONG ulEntryIdx ); // [RC]
	void			copy( IPList &destination ); // [RC]
	void			sort(); // [RC]
	void			removeExpiredEntries( void ); // [RC]

	unsigned int	size() const { return static_cast<unsigned int>( _ipVector.size( )); }
	void			clear() { _ipVector.clear(); }
	void			push_back ( IPADDRESSBAN_s &IP ) { _ipVector.push_back(IP); }
	const char		*getErrorMessage() const { return _error.c_str(); }
	const char		*getFilename() const { return _filename.c_str(); } // [AK]

	std::vector<IPADDRESSBAN_s>&	getVector() { return _ipVector; }

//*************************************************************************
private:
	bool rewriteListToFile ();
};

//==========================================================================
//
// QueryIPQueue
//
// Stores IPs that have recently queried us to prevent flooding.
// @author Benjamin Berkels, Rivecoder
//
//==========================================================================

class QueryIPQueue
{
	//*************************************************************************
	struct STORED_QUERY_IP_t
	{
		// The IP address.
		NETADDRESS_s		Address;

		// Expiration date.
		unsigned long		nextAllowedTime;

	};

	// The maximum number of entries that we can store.
	static const unsigned int	MAX_QUERY_IPS = 512;

	// The array of IPs.
	STORED_QUERY_IP_t			_IPQueue[MAX_QUERY_IPS];

	// Head and tail of the queue.
	unsigned int				_queueHead;
	unsigned int				_queueTail;

	// How long entries will last (seconds).
	unsigned int				_entryLength;

//*************************************************************************
public:
	QueryIPQueue( int entryLength ) : _queueHead( 0 ), _queueTail( 0 ), _entryLength( entryLength )
	{
	}

	void	adjustHead( const unsigned long currentTime );
	bool	addressInQueue( const NETADDRESS_s AddressFrom ) const;
	void	addAddress( const NETADDRESS_s AddressFrom, const unsigned long currentTime, std::ostream *errorOut = NULL );
	bool	isFull( ) const;
};

//==========================================================================
//
// RingBuffer
//
// @author Benjamin Berkels
//
//==========================================================================
template<typename DataType, int Length>
class RingBuffer
{
	DataType* _data;
	// Position of the entry that will be overwritten next AKA the oldest entry.
	unsigned int _position;
public:
	RingBuffer ( ) : _data ( NULL )
	{
		clear();
	}
	~RingBuffer ( )
	{
		if ( _data != NULL )
			delete[] _data;
	}
	void clear ( )
	{
		if ( _data != NULL )
			delete[] _data;

		// Make sure to initialize also built-in types by appending "()".
		_data = new DataType[Length]();
		_position = 0;
	}
	void put ( DataType Entry )
	{
		_data[_position] = Entry;
		_position = (_position+1) % Length;
	}
	DataType getOldestEntry ( unsigned int Offset = 0 ) const
	{
		return _data[ ( _position + Offset ) % Length ];
	}
	unsigned int getPosition( void )
	{
		return _position;
	}
	void setPosition( unsigned int pos )
	{
		_position = pos % Length;
	}
};

#endif	// __NETWORKSHARED_H__
