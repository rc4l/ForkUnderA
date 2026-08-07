//-----------------------------------------------------------------------------
//
// Skulltag Master Server Source
// Copyright (C) 2004-2005 Brad Carney
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
// Date (re)created:  3/7/05
//
//
// Filename: main.cpp
//
// Description: 
//
//-----------------------------------------------------------------------------

#include "../src/networkheaders.h"
#include "../src/networkshared.h"
#include "version.h"
#include "network.h"
#include "main.h"
#include <sstream>
#include <set>
// [rc4l] The reach-cookie table. Named rather than leaned on transitively -- this builds on GCC in a
// container as well as MSVC, and the two disagree about what <set> drags in.
#include <string>
#include <vector>

// [BB] Needed for I_GetTime.
#ifdef _MSC_VER
#include <mmsystem.h>
#endif
#ifndef _WIN32
#include <sys/time.h>
#endif

//*****************************************************************************
//	VARIABLES

// [BB] Comparision function, necessary to put SERVER_s entries into a std::set.
class SERVERCompFunc
{
public:
	bool operator()(SERVER_s s1, SERVER_s s2) const
	{
		// [BB] NETWORK_AddressToString uses a static char array to generate the string, so we
		// need to cache the string conversion of the first string when comparing to two.
		// Note: Because we need a "<" comparison and not a "!=" comparison we can't use
		// NETWORK_CompareAddress.
		std::string address1 = s1.Address.ToString();
		return ( stricmp ( address1.c_str(), s2.Address.ToString() ) < 0 );
	}
};

// Global server list.
static	std::set<SERVER_s, SERVERCompFunc> g_Servers;
static	std::set<SERVER_s, SERVERCompFunc> g_UnverifiedServers;

// Message buffer we write our commands to.
static	NETBUFFER_s				g_MessageBuffer;

// This is the current time for the server registry.
static	long					g_lCurrentTime;

// Global list of banned IPs.
static	IPList					g_BannedIPs; // Banned entirely.
static	IPList					g_BlockedIPs; // Blocked from hosting, but can still browse servers.
static	IPList					g_BannedIPExemptions;
static	IPList					g_MultiServerExceptions;

// IPs of launchers that we've sent full lists to recently.
static	QueryIPQueue			g_queryIPQueue( 10 );

// [RC] IPs that are completely ignored.
static	QueryIPQueue			g_floodProtectionIPQueue( 10 );
static	QueryIPQueue			g_ShortFloodQueue( 3 );

// [BB] Do we want to hide servers that ignore our ban list?
static	bool					g_bHideBanIgnoringServers = false;

//*****************************************************************************
//	CLASSES

//==========================================================================
//
// BanlistPacketSender
//
// Sends registry bans and exemptions to a server. Automatically splits the
// data over several packets if necessary.
// 
// @author Benjamin Berkels
//
//==========================================================================

class BanlistPacketSender {
	const unsigned long _ulMaxPacketSize;
	unsigned long _ulPacketNum;
	unsigned long _ulSizeOfPacket;
	NETBUFFER_s _netBuffer;
	const SERVER_s &_destServer;

public:
	BanlistPacketSender ( const SERVER_s &DestServer )
		: _ulMaxPacketSize ( 1024 ),
			_ulPacketNum ( 0 ),
			_ulSizeOfPacket ( 0 ),
			_destServer ( DestServer )
	{
		_netBuffer.Init( MAX_UDP_PACKET, BUFFERTYPE_WRITE );
	}

	~BanlistPacketSender ( )
	{
		_netBuffer.Free();
	}

	void writeBanEntry ( const char *BanEntry, const int EntryType )
	{
		const unsigned long ulCommandSize = 1 + strlen ( BanEntry );

		// [BB] If this command doesn't fit into the current packet, start a new one.
		if ( _ulSizeOfPacket + ulCommandSize > _ulMaxPacketSize - 1 )
			finishCurrentAndStartNewPacket();

		_netBuffer.ByteStream.WriteByte( EntryType );
		_netBuffer.ByteStream.WriteString( BanEntry );
		_ulSizeOfPacket += ulCommandSize;
	}

	void start ( ) {
		startPacket ( );
	}

	void end ( ) {
		finishAndLaunchPacket ( true );
	}
private:
	void startPacket ( ) {
		_netBuffer.Clear();
		_netBuffer.ByteStream.WriteByte( SERVERREGISTRY_BANLISTPART );
		_netBuffer.ByteStream.WriteString( _destServer.RegistryBanlistVerificationString.c_str() );
		_netBuffer.ByteStream.WriteByte( _ulPacketNum );
		_ulSizeOfPacket = 2 + _destServer.RegistryBanlistVerificationString.length();
		++_ulPacketNum;
	}

	void finishAndLaunchPacket ( const bool bIsFinal ) {
		_netBuffer.ByteStream.WriteByte( bIsFinal ? MSB_ENDBANLIST : MSB_ENDBANLISTPART );
		NETWORK_LaunchPacket( &_netBuffer, _destServer.Address );
	}

	void finishCurrentAndStartNewPacket ( ) {
		finishAndLaunchPacket ( false );
		startPacket ( );
	}
};

//*****************************************************************************
//	FUNCTIONS

// Returns time in seconds
int I_GetTime (void)
{
#ifdef _MSC_VER
	static DWORD  basetime;
	DWORD         tm;

	tm = timeGetTime();
	if (!basetime)
		basetime = tm;

	return (tm-basetime)/1000;
#else
	struct timeval tv;
	struct timezone tz;
	long long int thistimereply;
	static long long int basetime;	

	gettimeofday(&tv, &tz);

	thistimereply = tv.tv_sec * 1000 + tv.tv_usec / 1000;

	if (!basetime)
		basetime = thistimereply;

	return (thistimereply - basetime)/1000;
#endif
}

//*****************************************************************************
//
void SERVERREGISTRY_SendBanlistToServer( const SERVER_s &Server )
{
	// [BB] If the server supports it, potentially split the ban list over multiple packets.
	if ( Server.iServerRevision >= 2907 )
	{
		BanlistPacketSender sender ( Server );
		sender.start();

		// Write all the bans.
		for ( unsigned int i = 0; i < g_BannedIPs.size( ); ++i )
			sender.writeBanEntry ( g_BannedIPs.getEntryAsString( i, false, false, false ).c_str( ), MSB_BAN );

		// Write all the exceptions.
		for ( unsigned int i = 0; i < g_BannedIPExemptions.size( ); ++i )
			sender.writeBanEntry ( g_BannedIPExemptions.getEntryAsString( i, false, false, false ).c_str( ), MSB_BANEXEMPTION );

		sender.end();
	}
	else
	{
		g_MessageBuffer.Clear();
		g_MessageBuffer.ByteStream.WriteByte( SERVERREGISTRY_BANLIST );
		// [BB] If the server sent us a verification string, send it along with the ban list.
		// This allows the server to verify that the list actually was sent from our registry
		// (and is not just a packet with forged source IP).
		if ( Server.RegistryBanlistVerificationString.size() )
			g_MessageBuffer.ByteStream.WriteString( Server.RegistryBanlistVerificationString.c_str() );

		// Write all the bans.
		g_MessageBuffer.ByteStream.WriteLong( g_BannedIPs.size( ));
		for ( ULONG i = 0; i < g_BannedIPs.size( ); i++ )
			g_MessageBuffer.ByteStream.WriteString( g_BannedIPs.getEntryAsString( i, false, false, false ).c_str( ));

		// Write all the exceptions.
		g_MessageBuffer.ByteStream.WriteLong( g_BannedIPExemptions.size( ));
		for ( ULONG i = 0; i < g_BannedIPExemptions.size( ); i++ )
			g_MessageBuffer.ByteStream.WriteString( g_BannedIPExemptions.getEntryAsString( i, false, false, false ).c_str( ));

		NETWORK_LaunchPacket( &g_MessageBuffer, Server.Address );
	}
	Server.bHasLatestBanList = true;
	Server.bVerifiedLatestBanList = false;
	printf( "-> Banlist sent to %s.\n", Server.Address.ToString() );
}

//*****************************************************************************
//
//*****************************************************************************
//
// [rc4l] Cookies for reachability tests, held rather than computed.
//
// A stateless token would have to be a keyed hash, and a hand-rolled one is a cryptographic
// primitive written by someone who is not writing a cryptographic primitive. Holding them is duller
// and provably correct: the only costs are memory and expiry, and both are bounded here.
//
// The cap is what stops this being a memory-exhaustion lever. Past it we issue nothing, which fails
// the feature rather than the daemon -- a player briefly cannot pre-check a port, and everybody's
// server list keeps working.
namespace
{
struct REACHCOOKIE_s
{
	NETADDRESS_s	Address;
	std::string		Cookie;
	long			lIssuedTime;
};

// Ten seconds is far longer than the round trip and far shorter than a useful attack window.
const long kReachCookieLifetimeSeconds = 10;
const size_t kMaxReachCookies = 256;

std::vector<REACHCOOKIE_s> g_ReachCookies;

void ExpireReachCookies( long lNow )
{
	std::vector<REACHCOOKIE_s> kept;
	kept.reserve( g_ReachCookies.size() );

	for ( size_t i = 0; i < g_ReachCookies.size(); ++i )
	{
		if (( lNow - g_ReachCookies[i].lIssuedTime ) < kReachCookieLifetimeSeconds )
			kept.push_back( g_ReachCookies[i] );
	}

	g_ReachCookies.swap( kept );
}
} // namespace

// Returns the cookie issued, or "" when we are refusing to issue any more.
std::string SERVERREGISTRY_IssueReachCookie( const NETADDRESS_s &Address )
{
	ExpireReachCookies( g_lCurrentTime );

	if ( g_ReachCookies.size() >= kMaxReachCookies )
		return "";

	// One in flight per address. Without this a single client could fill the table by itself, and the
	// cap would then be a denial of service against everyone else rather than a defence.
	for ( size_t i = 0; i < g_ReachCookies.size(); ++i )
	{
		if ( g_ReachCookies[i].Address.Compare( Address ))
			return g_ReachCookies[i].Cookie;
	}

	REACHCOOKIE_s entry;
	entry.Address = Address;
	entry.lIssuedTime = g_lCurrentTime;

	char buffer[64];
	sprintf( buffer, "%x%x%x", rand(), rand(), rand() );
	entry.Cookie = buffer;

	g_ReachCookies.push_back( entry );
	return entry.Cookie;
}

// Whether this address really was issued this cookie. Consumed on success, so one cookie buys one
// probe and a replay earns nothing.
bool SERVERREGISTRY_ClaimReachCookie( const NETADDRESS_s &Address, const std::string &Cookie )
{
	ExpireReachCookies( g_lCurrentTime );

	if ( Cookie.empty() )
		return false;

	for ( size_t i = 0; i < g_ReachCookies.size(); ++i )
	{
		if ( g_ReachCookies[i].Address.Compare( Address ) && ( g_ReachCookies[i].Cookie == Cookie ))
		{
			g_ReachCookies.erase( g_ReachCookies.begin() + i );
			return true;
		}
	}

	return false;
}

//*****************************************************************************
//
void SERVERREGISTRY_RequestServerVerification( const SERVER_s &Server )
{
	g_MessageBuffer.Clear();
	g_MessageBuffer.ByteStream.WriteByte( SERVERREGISTRY_VERIFICATION );
	g_MessageBuffer.ByteStream.WriteString( Server.RegistryBanlistVerificationString.c_str() );
	g_MessageBuffer.ByteStream.WriteLong( Server.ServerVerificationInt );
	NETWORK_LaunchPacket( &g_MessageBuffer, Server.Address );
}
//*****************************************************************************
//
void SERVERREGISTRY_SendServerIPToLauncher( const NETADDRESS_s &Address, BYTESTREAM_s *pByteStream )
{
	// Tell the launcher the IP of this server on the list.
	pByteStream->WriteByte( SRSC_SERVER );
	Address.WriteToStream ( pByteStream );
}

//*****************************************************************************
//
unsigned long SERVERREGISTRY_CalcServerIPBlockNetSize( const NETADDRESS_s &Address, const std::vector<USHORT> &PortList )
{
	if ( PortList.size() == 0 )
		return 0;

	return ( 5 + 2 * PortList.size() ); // 5 = 4 (IP) + 1 (Number of ports of the server)
}

//*****************************************************************************
//
void SERVERREGISTRY_SendServerIPBlockToLauncher( const NETADDRESS_s &Address, const std::vector<USHORT> &PortList, BYTESTREAM_s *pByteStream )
{
	if ( PortList.size() == 0 )
		return;

	// Tell the launcher the IP and all ports of the servers on that IP.
	pByteStream->WriteByte( PortList.size() );
	Address.WriteToStream ( pByteStream, false );
	for ( unsigned int i = 0; i < PortList.size(); ++i )
		pByteStream->WriteShort( ntohs( PortList[i] ) );
}

//*****************************************************************************
//
unsigned long SERVERREGISTRY_NumServers ( void )
{
	return g_Servers.size();
}

//*****************************************************************************
//
bool SERVERREGISTRY_RefreshIPList( IPList &List, const char *FileName )
{
	std::stringstream oldIPs;

	for ( ULONG ulIdx = 0; ulIdx < List.size(); ulIdx++ )
		oldIPs << List.getEntryAsString ( ulIdx, false ).c_str() << "-";

	if ( !(List.clearAndLoadFromFile( FileName )) )
		std::cerr << List.getErrorMessage();

	std::stringstream newIPs;

	for ( ULONG ulIdx = 0; ulIdx < List.size(); ulIdx++ )
		newIPs << List.getEntryAsString ( ulIdx, false ).c_str() << "-";

	return ( strcmp ( newIPs.str().c_str(), oldIPs.str().c_str() ) != 0 );
}

//*****************************************************************************
//
void SERVERREGISTRY_InitializeBans( void )
{
	const bool BannedIPsChanged = SERVERREGISTRY_RefreshIPList ( g_BannedIPs, "banlist.txt" );
	const bool BannedIPExemptionsChanged = SERVERREGISTRY_RefreshIPList ( g_BannedIPExemptions, "whitelist.txt" );

	if ( !(g_MultiServerExceptions.clearAndLoadFromFile( "multiserver_whitelist.txt" )) )
		std::cerr << g_MultiServerExceptions.getErrorMessage();

	if ( !(g_BlockedIPs.clearAndLoadFromFile( "blocklist.txt" )) )
		std::cerr << g_BlockedIPs.getErrorMessage();

	std::cerr << "\nBan list: " << g_BannedIPs.size() << " banned IPs, " << g_BlockedIPs.size( ) << " blocked IPs, " << g_BannedIPExemptions.size() << " exemptions." << std::endl;
	std::cerr << "Multi-server exceptions: " << g_MultiServerExceptions.size() << "." << std::endl;

	if ( BannedIPsChanged || BannedIPExemptionsChanged )
	{
		// [BB] The ban list was changed, so no server has the latest list anymore.
		for( std::set<SERVER_s, SERVERCompFunc>::iterator it = g_Servers.begin(); it != g_Servers.end(); ++it )
		{
			it->bHasLatestBanList = false;
			it->bVerifiedLatestBanList = false;
		}

		std::cerr << "Ban lists were changed since last refresh\n";
	}
/*
  // [BB] Print all banned IPs, to make sure the IP list has been parsed successfully.
	std::cerr << "Entries in blacklist:\n";
	for ( ULONG ulIdx = 0; ulIdx < g_BannedIPs.size(); ulIdx++ )
		std::cerr << g_BannedIPs.getEntryAsString(ulIdx).c_str();

	// [BB] Print all exemption-IPs, to make sure the IP list has been parsed successfully.
	std::cerr << "Entries in whitelist:\n";
	for ( ULONG ulIdx = 0; ulIdx < g_BannedIPExemptions.size(); ulIdx++ )
		std::cerr << g_BannedIPExemptions.getEntryAsString(ulIdx).c_str();
*/
}

//*****************************************************************************
//
void SERVERREGISTRY_AddServer( const SERVER_s &Server, std::set<SERVER_s, SERVERCompFunc> &ServerSet )
{
	std::set<SERVER_s, SERVERCompFunc>::iterator addedServer = ServerSet.insert ( Server ).first;

	if ( addedServer == ServerSet.end() )
		printf( "ERROR: Adding new entry to the set failed. This should not happen!\n" );
	else
	{
		addedServer->lLastReceived = g_lCurrentTime;						
		if ( &ServerSet == &g_Servers )
		{
			printf( "+ Adding %s (revision %d) to the server list.\n", addedServer->Address.ToString(), addedServer->iServerRevision );
			SERVERREGISTRY_SendBanlistToServer( *addedServer );
		}
		else
			printf( "+ Adding %s (revision %d) to the verification list.\n", addedServer->Address.ToString(), addedServer->iServerRevision );
	}
}

//*****************************************************************************
//
void SERVERREGISTRY_ParseCommands( BYTESTREAM_s *pByteStream )
{
	long			lCommand;
	NETADDRESS_s	AddressFrom;

	AddressFrom = NETWORK_GetFromAddress( );

	// [RC] If this IP is in our flood queue, ignore it completely.
	if ( g_floodProtectionIPQueue.addressInQueue( AddressFrom ) || g_ShortFloodQueue.addressInQueue( AddressFrom ))
	{
		while ( pByteStream->ReadByte() != -1 ) // [RC] Is this really necessary?
			;
		return;
	}

	// Is this IP banned? Send the user an explanation, and ignore the IP for 30 seconds.
	if ( !g_BannedIPExemptions.isIPInList( AddressFrom ) && g_BannedIPs.isIPInList( AddressFrom ))
	{
		g_MessageBuffer.Clear();
		g_MessageBuffer.ByteStream.WriteLong( SRSC_IPISBANNED );
		NETWORK_LaunchPacket( &g_MessageBuffer, AddressFrom );

		printf( "* Received challenge from banned IP (%s). Ignoring for 10 seconds.\n", AddressFrom.ToString() );
		g_queryIPQueue.addAddress( AddressFrom, g_lCurrentTime, &std::cerr );
		return;
	}

	lCommand = pByteStream->ReadLong();
	switch ( lCommand )
	{

	// Server is telling the registry of its existence.
	case SERVER_SERVERREGISTRY_CHALLENGE:
		{
			// Certain IPs can be blocked from just hosting.
			if ( !g_BannedIPExemptions.isIPInList( AddressFrom ) && g_BlockedIPs.isIPInList( AddressFrom ))
			{
				g_MessageBuffer.Clear();
				g_MessageBuffer.ByteStream.WriteLong( SRSC_IPISBANNED );
				NETWORK_LaunchPacket( &g_MessageBuffer, AddressFrom );

				printf( "* Received server challenge from blocked IP (%s). Ignoring for 10 seconds.\n", AddressFrom.ToString() );
				g_queryIPQueue.addAddress( AddressFrom, g_lCurrentTime, &std::cerr );
				return;
			}
			SERVER_s newServer;
			newServer.Address = AddressFrom;
			// [BB] If no verification string was send, NETWORK_ReadString just returns an empty string.
			// Thus, this is still compatible with older servers that don't send the string.
			newServer.RegistryBanlistVerificationString = pByteStream->ReadString();
			// [BB] If no value was send, NETWORK_ReadByte just returns -1.
			// Thus, this is still compatible with older servers that don't tell us whether they enforce our bans
			// and gives them the benefit of the doubt, i.e. it assumes that they enforce our bans.
			const int temp = pByteStream->ReadByte();
			newServer.bEnforcesBanList = ( temp != 0 );
			newServer.bNewFormatServer = ( temp != -1 );
			newServer.iServerRevision = ( ( pByteStream->pbStreamEnd - pByteStream->pbStream ) >= 4 ) ? pByteStream->ReadLong() : pByteStream->ReadShort();

			std::set<SERVER_s, SERVERCompFunc>::iterator currentServer = g_Servers.find ( newServer );

			// This is a new server; add it to the list.
			if ( currentServer == g_Servers.end() )
			{
				unsigned int iNumOtherServers = 0;

				// First count the number of servers from this IP.
				for( std::set<SERVER_s, SERVERCompFunc>::const_iterator it = g_Servers.begin(); it != g_Servers.end(); ++it )
				{
					if ( it->Address.CompareNoPort( AddressFrom ))
						iNumOtherServers++;
				}

				if ( iNumOtherServers >= 10 && !g_MultiServerExceptions.isIPInList( AddressFrom ))
					printf( "* More than 10 servers received from %s. Ignoring request...\n", AddressFrom.ToString() );
				else
				{
					// [BB] 3021 is 98d, don't put those servers on the list.
					if ( ( newServer.bNewFormatServer ) && ( newServer.iServerRevision != 3021 ) )
					{
						std::set<SERVER_s, SERVERCompFunc>::iterator currentUnverifiedServer = g_UnverifiedServers.find ( newServer );
						// [BB] This is a new server, but we still need to verify it.
						if ( currentUnverifiedServer == g_UnverifiedServers.end() )
						{
							srand ( static_cast<unsigned int>( time(NULL) ) );
							newServer.ServerVerificationInt = rand() + rand() * rand() + rand() * rand() * rand();
							// [BB] We don't send the ban list to unverified servers, so just pretent the server already has the list.
							newServer.bHasLatestBanList = true;

							SERVERREGISTRY_RequestServerVerification ( newServer );
							SERVERREGISTRY_AddServer( newServer, g_UnverifiedServers );
						}
					}
					else
					{
						printf( "* Received server challenge from old server (%s). Ignoring IP for 10 seconds.\n", newServer.Address.ToString() );
						g_queryIPQueue.addAddress( newServer.Address, g_lCurrentTime, &std::cerr );
					}
				}
			}

			// Command is from a server already on the list. It's just sending us a heartbeat.
			else
			{
				// [BB] Only if the verification string matches.
				if ( stricmp ( currentServer->RegistryBanlistVerificationString.c_str(), newServer.RegistryBanlistVerificationString.c_str() ) == 0 )
				{
					currentServer->lLastReceived = g_lCurrentTime;
					// [BB] The server possibly changed the ban setting, so update it.
					currentServer->bEnforcesBanList = newServer.bEnforcesBanList;
				}
			}

			// Ignore IP for 10 seconds.
		//	if ( !g_MultiServerExceptions.isIPInList( Address ) )
		//		g_floodProtectionIPQueue.addAddress( AddressFrom, g_lCurrentTime, &std::cerr );
			return;
		}
	case SERVER_SERVERREGISTRY_VERIFICATION:
		{
			SERVER_s newServer;
			newServer.Address = AddressFrom;
			newServer.RegistryBanlistVerificationString = pByteStream->ReadString();
			newServer.ServerVerificationInt = pByteStream->ReadLong();

			std::set<SERVER_s, SERVERCompFunc>::iterator currentServer = g_UnverifiedServers.find ( newServer );

			// [BB] Apparently, we didn't request any verification from this server, so ignore it.
			if ( currentServer == g_UnverifiedServers.end() )
				return;

			if ( ( stricmp ( newServer.RegistryBanlistVerificationString.c_str(), currentServer->RegistryBanlistVerificationString.c_str() ) == 0 )
				&& ( newServer.ServerVerificationInt == currentServer->ServerVerificationInt ) )
			{
				SERVERREGISTRY_AddServer( *currentServer, g_Servers );
				g_UnverifiedServers.erase ( currentServer );
			}
			return;
		}
	case SERVER_SERVERREGISTRY_BANLIST_RECEIPT:
		{
			SERVER_s server;
			server.Address = AddressFrom;
			server.RegistryBanlistVerificationString = pByteStream->ReadString();

			std::set<SERVER_s, SERVERCompFunc>::iterator currentServer = g_Servers.find ( server );

			// [BB] We don't know the server. Just ignore it.
			if ( currentServer == g_Servers.end() )
				return;

			if ( stricmp ( server.RegistryBanlistVerificationString.c_str(), currentServer->RegistryBanlistVerificationString.c_str() ) == 0 )
			{
				currentServer->bVerifiedLatestBanList = true;
				std::cerr << AddressFrom.ToString() << " acknowledged receipt of the banlist.\n";
			}
		}
		return;

	// [rc4l] A client asking whether we can reach it on a port, BEFORE it hosts anything there.
	//
	// The dangerous shape of this request is obvious once stated: it asks us to send a packet, and
	// anything that sends a packet on request is a weapon aimed at whoever the request names. Three
	// things keep it from being one, and none of them is optional.
	//
	//   THE ADDRESS IS NEVER TAKEN FROM THE PACKET. Only AddressFrom is ever used. A body-supplied
	//   address is what turns a service like this into a reflector.
	//
	//   A COOKIE COMES FIRST, because UDP sources are forgeable. The first request gets a cookie sent
	//   to the source and nothing else; only a second request echoing it earns an unsolicited packet.
	//   Someone spoofing a victim's address gets our cookie sent to that victim -- one small packet,
	//   no amplification -- and can go no further, because the reply went to the victim, not to them.
	//
	//   THE PROBE CARRIES THE CLIENT'S OWN NONCE, so the client can tell our packet from a stranger's
	//   and cannot be fooled into believing it is reachable when it is not.
	//
	// Flood protection above already applies: this arrives through the same queues as every other
	// command, so a client hammering it is throttled without anything special here.
	case CLIENT_SERVERREGISTRY_REACHTEST:
		{
			const char *pszCookie = pByteStream->ReadString();
			const std::string cookie = ( pszCookie != NULL ) ? pszCookie : "";
			const long lPort = pByteStream->ReadShort();
			const char *pszNonce = pByteStream->ReadString();
			const std::string nonce = ( pszNonce != NULL ) ? pszNonce : "";

			if ( cookie.empty() )
			{
				// First leg. Issue a cookie to the SOURCE address and stop there.
				const std::string issued = SERVERREGISTRY_IssueReachCookie( AddressFrom );
				if ( issued.empty() )
					return;			// too many in flight; saying nothing is the safe refusal

				// [rc4l] The observed source address rides along. We are the only party that can see
				// it, the client cannot learn it any other way, and it costs nothing to say -- it
				// also lets the client tell "same network as last time" from "the ISP moved me",
				// which is what makes caching a forwarding verdict safe.
				std::string seenAs = AddressFrom.ToString();
				const size_t colon = seenAs.find( ':' );
				if ( colon != std::string::npos )
					seenAs = seenAs.substr( 0, colon );

				g_MessageBuffer.Clear();
				g_MessageBuffer.ByteStream.WriteLong( SRSC_REACHCOOKIE );
				g_MessageBuffer.ByteStream.WriteString( issued.c_str() );
				g_MessageBuffer.ByteStream.WriteString( seenAs.c_str() );
				NETWORK_LaunchPacket( &g_MessageBuffer, AddressFrom );
				return;
			}

			// Second leg. The echo proves the sender is really at this address, because the cookie
			// only ever went there.
			if ( SERVERREGISTRY_ClaimReachCookie( AddressFrom, cookie ) == false )
				return;

			// A port of zero, or one we would not dial, is not worth a packet.
			if (( lPort <= 0 ) || ( lPort > 65535 ))
				return;

			NETADDRESS_s Target = AddressFrom;
			Target.SetPort( static_cast<USHORT>( lPort ));

			g_MessageBuffer.Clear();
			g_MessageBuffer.ByteStream.WriteByte( SERVERREGISTRY_REACHPROBE );
			g_MessageBuffer.ByteStream.WriteString( nonce.c_str() );
			NETWORK_LaunchPacket( &g_MessageBuffer, Target );

			printf( "-> Reach probe sent to %s.\n", Target.ToString() );
		}
		return;

	// Launcher is asking the registry for the server list.
	case LAUNCHER_SERVER_CHALLENGE:
	case LAUNCHER_SERVERREGISTRY_CHALLENGE:
		{
			g_MessageBuffer.Clear();

			// Did this IP query us recently? If so, send it an explanation, and ignore it completely for 3 seconds.
			if ( g_queryIPQueue.addressInQueue( AddressFrom ))
			{
				g_MessageBuffer.ByteStream.WriteLong( SRSC_REQUESTIGNORED );
				NETWORK_LaunchPacket( &g_MessageBuffer, AddressFrom );

				printf( "* Extra launcher challenge from %s. Ignoring for 3 seconds.\n", AddressFrom.ToString() );
				g_ShortFloodQueue.addAddress( AddressFrom, g_lCurrentTime, &std::cerr );
				return;
			}

			// [BB] The launcher only sends the protocol version with LAUNCHER_SERVERREGISTRY_CHALLENGE.
			if ( lCommand == LAUNCHER_SERVERREGISTRY_CHALLENGE )
			{
				// [BB] Check if the requested version of the protocol matches ours.
				const unsigned short usVersion = pByteStream->ReadShort();

				if ( usVersion != SERVERREGISTRY_VERSION )
				{
					g_MessageBuffer.ByteStream.WriteLong( SRSC_WRONGVERSION );
					NETWORK_LaunchPacket( &g_MessageBuffer, AddressFrom );
					return;
				}
			}

			printf( "-> Sending server list to %s.\n", AddressFrom.ToString() );

			// Wait 10 seconds before sending this IP the server list again.
			g_queryIPQueue.addAddress( AddressFrom, g_lCurrentTime, &std::cerr );

			switch ( lCommand )
			{
			case LAUNCHER_SERVER_CHALLENGE:
				// Send the list of servers.
				g_MessageBuffer.ByteStream.WriteLong( SRSC_BEGINSERVERLIST );
				for( std::set<SERVER_s, SERVERCompFunc>::const_iterator it = g_Servers.begin(); it != g_Servers.end(); ++it )
				{
					// [BB] Possibly omit servers that don't enforce our ban list.
					if ( ( it->bEnforcesBanList == true ) || ( g_bHideBanIgnoringServers == false ) )
						SERVERREGISTRY_SendServerIPToLauncher ( it->Address, &g_MessageBuffer.ByteStream );
				}

				// Tell the launcher that we're done sending servers.
				g_MessageBuffer.ByteStream.WriteByte( SRSC_ENDSERVERLIST );

				// Send the launcher our packet.
				NETWORK_LaunchPacket( &g_MessageBuffer, AddressFrom );
				return;

			case LAUNCHER_SERVERREGISTRY_CHALLENGE:

				const unsigned long ulMaxPacketSize = 1024;
				unsigned long ulPacketNum = 0;

				std::set<SERVER_s, SERVERCompFunc>::const_iterator it = g_Servers.begin();

				g_MessageBuffer.ByteStream.WriteLong( SRSC_BEGINSERVERLISTPART );
				g_MessageBuffer.ByteStream.WriteByte( ulPacketNum );
				g_MessageBuffer.ByteStream.WriteByte( SRSC_SERVERBLOCK );
				unsigned long ulSizeOfPacket = 6; // 4 (SRSC_BEGINSERVERLISTPART) + 1 (0) + 1 (SRSC_SERVERBLOCK)

				while ( it != g_Servers.end() )
				{
					NETADDRESS_s serverAddress = it->Address;
					std::vector<USHORT> serverPortList;

					do {
						// [BB] Possibly omit servers that don't enforce our ban list.
						if ( ( it->bEnforcesBanList == true ) || ( g_bHideBanIgnoringServers == false ) )
							serverPortList.push_back ( it->Address.usPort );
						++it;
					} while ( ( it != g_Servers.end() ) && it->Address.CompareNoPort( serverAddress ) );

					// [BB] All servers on this IP ignore the list, nothing to send.
					if ( serverPortList.size() == 0 )
						continue;

					const unsigned long ulServerBlockNetSize = SERVERREGISTRY_CalcServerIPBlockNetSize( serverAddress, serverPortList );

					// [BB] If sending this block would cause the current packet to exceed ulMaxPacketSize ...
					if ( ulSizeOfPacket + ulServerBlockNetSize > ulMaxPacketSize - 1 )
					{
						// [BB] ... close the current packet and start a new one.
						g_MessageBuffer.ByteStream.WriteByte( 0 ); // [BB] Terminate SRSC_SERVERBLOCK by sending 0 ports.
						g_MessageBuffer.ByteStream.WriteByte( SRSC_ENDSERVERLISTPART );
						NETWORK_LaunchPacket( &g_MessageBuffer, AddressFrom );

						g_MessageBuffer.Clear();
						++ulPacketNum;
						ulSizeOfPacket = 5;
						g_MessageBuffer.ByteStream.WriteLong( SRSC_BEGINSERVERLISTPART );
						g_MessageBuffer.ByteStream.WriteByte( ulPacketNum );
						g_MessageBuffer.ByteStream.WriteByte( SRSC_SERVERBLOCK );
					}
					ulSizeOfPacket += ulServerBlockNetSize;
					SERVERREGISTRY_SendServerIPBlockToLauncher ( serverAddress, serverPortList, &g_MessageBuffer.ByteStream );
				}
				g_MessageBuffer.ByteStream.WriteByte( 0 ); // [BB] Terminate SRSC_SERVERBLOCK by sending 0 ports.
				g_MessageBuffer.ByteStream.WriteByte( SRSC_ENDSERVERLIST );
				NETWORK_LaunchPacket( &g_MessageBuffer, AddressFrom );
				return;
			}
		}
	}

	printf( "* Received unknown challenge (%ld) from %s. Ignoring for 10 seconds...\n", lCommand, AddressFrom.ToString() );
	g_floodProtectionIPQueue.addAddress( AddressFrom, g_lCurrentTime, &std::cerr );
}

//*****************************************************************************
//
void SERVERREGISTRY_CheckTimeouts( std::set<SERVER_s, SERVERCompFunc> &ServerSet )
{
	// [BB] Because we are erasing entries from the set, the iterator has to be incremented inside
	// the loop, depending on whether and element was erased or not.
	for( std::set<SERVER_s, SERVERCompFunc>::iterator it = ServerSet.begin(); it != ServerSet.end(); )
	{
		// If the server has timed out, make it an open slot!
		if (( g_lCurrentTime - it->lLastReceived ) >= 60 )
		{
			printf( "- %server at %s timed out.\n", ( &ServerSet == &g_UnverifiedServers ) ? "Unverified s" : "S", it->Address.ToString() );
			// [BB] The standard does not require set::erase to return the incremented operator,
			// that's why we must use the post increment operator here.
			ServerSet.erase ( it++ );
			continue;
		}
		else
		{
			// [BB] If the server doesn't have the latest ban list, send it now.
			// This construction has the drawback that all servers are updated at once.
			// Possibly it will be necessary to do this differently.
			if ( it->bHasLatestBanList == false )
				SERVERREGISTRY_SendBanlistToServer( *it );

			++it;
		}
	}
}

//*****************************************************************************
//
void SERVERREGISTRY_CheckTimeouts( void )
{
	SERVERREGISTRY_CheckTimeouts ( g_Servers );
	SERVERREGISTRY_CheckTimeouts ( g_UnverifiedServers );
}

//*****************************************************************************
//
int main( int argc, char **argv )
{
	BYTESTREAM_s	*pByteStream;

	// [rc4l] Unbuffer stdout so the log is usable in a container.
	//
	// The per-event lines below go through printf, and C stdio switches to FULL buffering when stdout
	// is a pipe -- which is exactly what Docker gives it. So `docker compose logs` showed the startup
	// banner (std::cerr, unbuffered) and then nothing, while several KB of the actually interesting
	// lines sat in a buffer.
	//
	// Those lines are the only way to tell an operator whether a server's port forward works: reaching
	// "verification list" means the announce arrived, and reaching "server list" means we got a reply
	// BACK to it. Seeing the first without the second is the signature of a blocked inbound port. That
	// diagnostic is worthless if it does not appear until the buffer happens to fill.
	//
	// _IONBF rather than _IOLBF on purpose: MSVC maps _IOLBF to full buffering anyway, and rejects the
	// (NULL, 0) pairing outright -- it crashed the process on Windows. Unbuffered costs a write syscall
	// per line, which is nothing at a few lines per heartbeat, and behaves the same everywhere.
	setvbuf( stdout, NULL, _IONBF, 0 );

	std::cerr << "=== ZandroX Server Registry ===\n";
	std::cerr << "Revision: " << GetGitTime() << "\n";

	std::cerr << "Port: " << DEFAULT_SERVERREGISTRY_PORT << std::endl << std::endl;

	// Initialize the network system.
	NETWORK_Construct( DEFAULT_SERVERREGISTRY_PORT, ( ( argc >= 4 ) && ( stricmp ( argv[2], "-useip" ) == 0 ) ) ? argv[3] : NULL );

	// Initialize the message buffer we send messages to the launcher in.
	g_MessageBuffer.Init ( MAX_UDP_PACKET, BUFFERTYPE_WRITE );

	// Initialize the bans subsystem.
	std::cerr << "Initializing ban list...\n";
	SERVERREGISTRY_InitializeBans( );
	int lastParsingTime = I_GetTime( );
	int lastBanlistVerificationTimeout = lastParsingTime;

	// [BB] Do we want to hide servers that ignore our ban list?
	if ( ( argc >= 2 ) && ( stricmp ( argv[1], "-DontHideBanIgnoringServers" ) == 0 ) )
	{
		std::cerr << "Note: Servers that do not enforce our ban list are shown." << std::endl;
		g_bHideBanIgnoringServers = false;
	}
	else
	{
		std::cerr << "Note: Servers that do not enforce our ban list are hidden." << std::endl;
		g_bHideBanIgnoringServers = true;
	}

	// Done setting up!
	std::cerr << "\n=== Server registry started! ===\n";

	while ( 1 )
	{
		g_lCurrentTime = I_GetTime( );
		I_DoSelect( );
	
		while ( NETWORK_GetPackets( ))
		{
			// Set up our byte stream.
			pByteStream = &NETWORK_GetNetworkMessageBuffer( )->ByteStream;
			pByteStream->pbStream = NETWORK_GetNetworkMessageBuffer( )->pbData;
			pByteStream->pbStreamEnd = pByteStream->pbStream + NETWORK_GetNetworkMessageBuffer( )->ulCurrentSize;

			// Now parse the packet.
			SERVERREGISTRY_ParseCommands( pByteStream );
		}

		// Update the ignore queues.
		g_queryIPQueue.adjustHead ( g_lCurrentTime );
		g_floodProtectionIPQueue.adjustHead ( g_lCurrentTime );
		g_ShortFloodQueue.adjustHead ( g_lCurrentTime );

		// See if any servers have timed out.
		SERVERREGISTRY_CheckTimeouts( );

		if ( g_lCurrentTime > lastBanlistVerificationTimeout + 10 )
		{
			for( std::set<SERVER_s, SERVERCompFunc>::iterator it = g_Servers.begin(); it != g_Servers.end(); ++it )
			{
				if ( ( it->bVerifiedLatestBanList == false ) && ( it->bNewFormatServer == true ) )
				{
					it->bHasLatestBanList = false;
					std::cerr << "No receipt received from " << it->Address.ToString() << ". Resending banlist.\n";
				}
			}
			lastBanlistVerificationTimeout = g_lCurrentTime;
		}

		// [BB] Reparse the ban list every 15 minutes.
		if ( g_lCurrentTime > lastParsingTime + 15*60 )
		{
			std::cerr << "~ Reparsing the ban lists...\n";
			SERVERREGISTRY_InitializeBans( );
			lastParsingTime = g_lCurrentTime;
		}
	}	
	
	return ( 0 );
}
