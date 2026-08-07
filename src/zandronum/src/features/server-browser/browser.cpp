//-----------------------------------------------------------------------------
//
// Skulltag Source
// Copyright (C) 2002 Brad Carney
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
// Date created:  11/26/03
//
//
// Filename: browser.cpp
//
// Description: Contains browser global variables and functions
//
//-----------------------------------------------------------------------------

#include "networkheaders.h"
#include "features/server-browser/browser.h"
#include "features/server-browser/computation/launcherfields_compute.h"
#include "features/launcher-protocol/computation/segmentreassembly_compute.h"
#include "c_dispatch.h"
#include "cl_main.h"
#include "deathmatch.h"
#include "doomtype.h"
#include "gamemode.h"
#include "i_system.h"
#include "network.h"     // [rc4l] NETWORK_GetCountryCodeFromIndex, COUNTRYINDEX_LAN
#include "teaminfo.h"
#include "templates.h"
#include "version.h"
#include "features/federated-server-registry/zx_serverregistrylist.h"

//*****************************************************************************
//	VARIABLES

// List of all parsed servers.
static	SERVER_t		g_BrowserServerList[MAX_BROWSER_SERVERS];

// [rc4l] Every server registry this client queries. Plural here and singular on the server side, and
// that asymmetry is the design: discovery composes, authority does not.
//
// A server announces to ONE server registry (fua_serverregistry_host) so exactly one of them can
// push it a ban list. A client queries MANY and unions the results, so the player still sees the
// whole network no matter which server registry each server chose to live on. Reading a list of
// addresses carries no authority, so fanning it out costs nothing.
static	TArray<NETADDRESS_s>	g_ServerRegistryAddresses;

// Message buffer for sending messages to the server registry.
static	NETBUFFER_s		g_ServerRegistryBuffer;

// Message buffer for sending messages to each individual server.
static	NETBUFFER_s		g_ServerBuffer;

// Port the server registry is located on.
static	USHORT			g_usServerRegistryPort;

// Are we waiting for server registry response?
static	bool			g_bWaitingForServerRegistryResponse;

// [rc4l] Which server the player picked. Shared state rather than a menu's private variable, so the
// join command works whichever browser is on screen.
static	LONG			g_lSelectedServer = -1;

// [rc4l] When the outstanding query went out, and how many times we have sent it.
//
// UDP does not retransmit, so before this a single dropped packet left
// g_bWaitingForServerRegistryResponse stuck true forever -- and M_RefreshServers() returns early
// while it is true, so the browser stayed empty and refusing to refresh until the game restarted.
// That is not a rare case on a lossy connection; it is the normal one.
static	ULONG			g_ulServerRegistryQuerySentMS;
static	int				g_lServerRegistryAttempts;

// [rc4l] Four seconds, not the second and a half this started as.
//
// A server registry flood-blocks a repeat launcher challenge from the same address for 3 seconds and
// will not resend a list to it for 10. Retrying inside that window cannot succeed by construction:
// the best case is a REQUESTIGNORED reply, and the actual effect is putting ourselves on its flood
// queue. Retries are only useful when the registry never heard us -- and in that case no rate limit
// applies, so waiting out its window costs nothing.
//
// Retrying faster than the thing you are retrying against is willing to answer is not persistence,
// it is a self-inflicted denial of service.
static const ULONG		SERVERREGISTRY_QUERY_TIMEOUT_MS = 4000;
static const int		SERVERREGISTRY_QUERY_MAX_ATTEMPTS = 3;

// [CW] The amount of teams sent to us.
static ULONG			g_ulNumberOfTeams = 0;

//*****************************************************************************
//	CONSOLE VARIABLES

// [rc4l] Comma-separated list of server registries the browser asks for servers. Client-side, so it
// carries no authority: a server registry answering this query only hands back addresses, and every
// detail shown in the browser comes from querying each server directly afterwards.
//
// This is where federation lives. Entries here are ADDED to the curated list -- they do not replace
// it, so adding a server registry can never cost you the ones you already had. To run on yours and
// nothing else, also set cl_fua_serverregistrylist_fetch 0; that pair means "my list, exactly".
// Entries that fail to resolve are skipped, so one dead server registry costs you its servers and
// nothing more.
//
// The server-side counterpart, fua_serverregistry_host, is deliberately singular -- see
// sv_serverregistry.cpp.
CVAR( String, cl_fua_serverregistry_list, "registry.cantstopscrolling.net", CVAR_ARCHIVE|CVAR_GLOBALCONFIG )

//*****************************************************************************
//	PROTOTYPES

static	LONG	browser_GetNewListID( void );
static	LONG	browser_GetListIDByAddress( NETADDRESS_s Address );
static	void	browser_QueryServer( ULONG ulServer );
static	ULONG	browser_CountryIndexFromCode( const char *pszCode );
static	void	browser_ResolveServerRegistries( void );

//*****************************************************************************
//	FUNCTIONS

void BROWSER_Construct( void )
{
	const char *pszPort;

	g_bWaitingForServerRegistryResponse = false;

	// Setup our server registry message buffer.
	g_ServerRegistryBuffer.Init( MAX_UDP_PACKET, BUFFERTYPE_WRITE );

	// Setup our server message buffer.
	g_ServerBuffer.Init( MAX_UDP_PACKET, BUFFERTYPE_WRITE );

	// Allow the user to specify which port the server registry is on.
	pszPort = Args->CheckValue( "-serverregistryport" );
    if ( pszPort )
    {
       g_usServerRegistryPort = atoi( pszPort );
       Printf( PRINT_HIGH, "Alternate server registry port: %d.\n", g_usServerRegistryPort );
    }
	else 
	   g_usServerRegistryPort = DEFAULT_SERVERREGISTRY_PORT;

	// Initialize the browser list.
	BROWSER_ClearServerList( );

	// Call BROWSER_Destruct() when Skulltag closes.
	atterm( BROWSER_Destruct );
}

//*****************************************************************************
//
void BROWSER_Destruct( void )
{
	// Free our local buffers.
	g_ServerRegistryBuffer.Free();
	g_ServerBuffer.Free();
}

//*****************************************************************************
//*****************************************************************************
//
bool BROWSER_IsActive( ULONG ulServer )
{
	if ( ulServer >= MAX_BROWSER_SERVERS )
		return ( false );

	return ( g_BrowserServerList[ulServer].ulActiveState == AS_ACTIVE );
}

//*****************************************************************************
//
bool BROWSER_IsLAN( ULONG ulServer )
{
	if ( ulServer >= MAX_BROWSER_SERVERS )
		return ( false );

	return ( g_BrowserServerList[ulServer].bLAN );
}

//*****************************************************************************
//
NETADDRESS_s BROWSER_GetAddress( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
	{
		NETADDRESS_s	Dummy;
		Dummy.Clear();
		return ( Dummy );
	}

	return ( g_BrowserServerList[ulServer].Address );
}

//*****************************************************************************
//
const char *BROWSER_GetHostName( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( " " );

	return ( g_BrowserServerList[ulServer].HostName.GetChars( ));
}

//*****************************************************************************
//
const char *BROWSER_GetWadURL( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( " " );

	return ( g_BrowserServerList[ulServer].WadURL.GetChars( ));
}

//*****************************************************************************
//
const char *BROWSER_GetEmailAddress( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( " " );

	return ( g_BrowserServerList[ulServer].EmailAddress.GetChars( ));
}

//*****************************************************************************
//
const char *BROWSER_GetMapname( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( " " );

	return ( g_BrowserServerList[ulServer].Mapname.GetChars( ));
}

//*****************************************************************************
//
LONG BROWSER_GetMaxClients( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( false );

	return ( g_BrowserServerList[ulServer].lMaxClients );
}

//*****************************************************************************
//
LONG BROWSER_GetNumPWADs( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( false );

	return ( g_BrowserServerList[ulServer].PWADNames.Size( ));
}

//*****************************************************************************
//
const char *BROWSER_GetPWADName( ULONG ulServer, ULONG ulWadIdx )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( " " );

	// [SB] Also check the index is valid.
	if ( ulWadIdx >= g_BrowserServerList[ulServer].PWADNames.Size())
		return ( " " );

	return ( g_BrowserServerList[ulServer].PWADNames[ulWadIdx].GetChars( ));
}

//*****************************************************************************
//
// [rc4l] Empty means "this server told us nothing", which is the normal case for a server that does
// not send SQF2_PWAD_HASHES. Never conflate that with a hash that matched.
const char *BROWSER_GetPWADHash( ULONG ulServer, ULONG ulWadIdx )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( "" );

	if ( ulWadIdx >= g_BrowserServerList[ulServer].PWADHashes.Size())
		return ( "" );

	return ( g_BrowserServerList[ulServer].PWADHashes[ulWadIdx].GetChars( ));
}

//*****************************************************************************
//
// [rc4l] 0 means "we do not know", which covers both a server that does not send
// SQF2_FUA_WAD_SIZES and one that could not stat the file. Never draw it as a size.
unsigned int BROWSER_GetPWADSize( ULONG ulServer, ULONG ulWadIdx )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( 0 );

	if ( ulWadIdx >= g_BrowserServerList[ulServer].PWADSizes.Size())
		return ( 0 );

	return ( g_BrowserServerList[ulServer].PWADSizes[ulWadIdx] );
}

//*****************************************************************************
//
unsigned int BROWSER_GetIWADSize( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( 0 );

	return ( g_BrowserServerList[ulServer].IWADSize );
}

//*****************************************************************************
//
// [rc4l] The host half comes from the address WE queried, never from anything the server said. A
// server that could name its own download host could name someone else's, and every client that
// joined would fetch from a machine that never agreed to serve them.
FString BROWSER_GetDirectDownloadURL( ULONG ulServer )
{
	FString url;

	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( url );

	if ( g_BrowserServerList[ulServer].usDirectDownloadPort == 0 )
		return ( url );

	url.Format( "http://%s:%u/", g_BrowserServerList[ulServer].Address.ToStringNoPort( ),
		static_cast<unsigned>( g_BrowserServerList[ulServer].usDirectDownloadPort ));
	return ( url );
}

//*****************************************************************************
//
bool BROWSER_PrefersMirrors( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( false );

	return ( g_BrowserServerList[ulServer].bPrefersMirrors );
}

//*****************************************************************************
//
// [rc4l] Empty means "this server told us nothing", which is the normal case for anything that has
// not heard of SQF2_FUA_IWAD_HASH. Never conflate that with a build that matched.
const char *BROWSER_GetIWADHash( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( "" );

	return ( g_BrowserServerList[ulServer].IWADHash.GetChars( ));
}

//*****************************************************************************
//
bool BROWSER_IsPasswordProtected( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( false );

	return ( g_BrowserServerList[ulServer].bForcePassword ||
		g_BrowserServerList[ulServer].bForceJoinPassword );
}

//*****************************************************************************
//
const char *BROWSER_GetIWADName( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( " " );

	return ( g_BrowserServerList[ulServer].IWADName.GetChars( ));
}

//*****************************************************************************
//
GAMEMODE_e BROWSER_GetGameMode( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( (GAMEMODE_e)false );

	return ( g_BrowserServerList[ulServer].GameMode );
}

//*****************************************************************************
//
LONG BROWSER_GetNumPlayers( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( false );

	return ( g_BrowserServerList[ulServer].lNumPlayers );
}

//*****************************************************************************
//
const char *BROWSER_GetPlayerName( ULONG ulServer, ULONG ulPlayer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( " " );

	if ( ulPlayer >= (ULONG)g_BrowserServerList[ulServer].lNumPlayers )
		return ( " " );

	return ( g_BrowserServerList[ulServer].Players[ulPlayer].Name.GetChars( ));
}

//*****************************************************************************
//
LONG BROWSER_GetPlayerFragcount( ULONG ulServer, ULONG ulPlayer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( false );

	if ( ulPlayer >= (ULONG)g_BrowserServerList[ulServer].lNumPlayers )
		return ( false );

	return ( g_BrowserServerList[ulServer].Players[ulPlayer].lFragcount );
}

//*****************************************************************************
//
LONG BROWSER_GetPlayerPing( ULONG ulServer, ULONG ulPlayer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( false );

	if ( ulPlayer >= (ULONG)g_BrowserServerList[ulServer].lNumPlayers )
		return ( false );

	return ( g_BrowserServerList[ulServer].Players[ulPlayer].lPing );
}

//*****************************************************************************
//
LONG BROWSER_GetPlayerSpectating( ULONG ulServer, ULONG ulPlayer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( false );

	if ( ulPlayer >= (ULONG)g_BrowserServerList[ulServer].lNumPlayers )
		return ( false );

	return ( g_BrowserServerList[ulServer].Players[ulPlayer].bSpectating );
}

//*****************************************************************************
//
// [rc4l] Bots are already told apart for the player COUNT; the detail panel needs the same fact per
// row, so it can say which of the names on a busy-looking server are people.
bool BROWSER_IsPlayerBot( ULONG ulServer, ULONG ulPlayer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( false );

	if ( ulPlayer >= (ULONG)g_BrowserServerList[ulServer].lNumPlayers )
		return ( false );

	return ( g_BrowserServerList[ulServer].Players[ulPlayer].bIsBot );
}

//*****************************************************************************
//
// [rc4l] Whether the server sent player rows at all. Distinct from "nobody is playing": a server that
// withheld the data and a server that is genuinely empty both report zero names, and a panel that
// showed the same thing for both would be inventing an empty server out of a silent one.
bool BROWSER_HasPlayerData( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( false );

	return ( g_BrowserServerList[ulServer].bHasPlayerData );
}

//*****************************************************************************
//
LONG BROWSER_GetPing( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( false );

	return ( g_BrowserServerList[ulServer].lPing );
}

//*****************************************************************************
//
const char *BROWSER_GetVersion( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( " " );

	return ( g_BrowserServerList[ulServer].Version.GetChars( ));
}

//*****************************************************************************
//
ULONG BROWSER_GetActiveState( ULONG ulServer )
{
	if ( ulServer >= MAX_BROWSER_SERVERS )
		return ( AS_INACTIVE );

	return ( g_BrowserServerList[ulServer].ulActiveState );
}

//*****************************************************************************
//
// [rc4l] Give up on servers that never answered.
//
// The list previously had no way to stop waiting: a server asked once stayed AS_WAITINGFORREPLY for
// the rest of the session, so "still loading" was permanently true and the browser could not honestly
// say it had finished. A server that is listed but unreachable is a real and common situation --
// someone's port forward lapsed, or they went offline between the registry heartbeat and our query.
//
// Four seconds is deliberately generous. This is a timeout for "will never answer", not a quality
// bar; a slow satellite link should still make it in.
void BROWSER_QueryTick( void )
{
	const LONG lNow = I_MSTime( );

	for ( ULONG ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		// [rc4l] A server carried over from the last time the browser was open, being re-checked
		// while it stays listed. It answered once, so it is shown until it fails to answer again --
		// and then it goes, rather than lingering as a row nobody can join.
		//
		// Removed outright instead of marked AS_TIMEDOUT: that state means "we asked and never heard
		// anything", which is a thing worth saying about a server the registry still lists. This one
		// is simply gone, and the registry will not be offering it next time either.
		if ( g_BrowserServerList[ulIdx].bRefreshing )
		{
			if ( g_BrowserServerList[ulIdx].lRefreshMS <= 0 )
				continue;			// queued, not sent yet -- nothing to time out

			if (( lNow - g_BrowserServerList[ulIdx].lRefreshMS ) >= 4000 )
			{
				g_BrowserServerList[ulIdx].bRefreshing = false;
				g_BrowserServerList[ulIdx].lRefreshMS = 0;
				g_BrowserServerList[ulIdx].ulActiveState = AS_INACTIVE;
			}
			continue;
		}

		if ( g_BrowserServerList[ulIdx].ulActiveState != AS_WAITINGFORREPLY )
			continue;

		// Not yet queried: lMSTime is only stamped when a packet actually goes out.
		if ( g_BrowserServerList[ulIdx].lMSTime <= 0 )
			continue;

		if (( lNow - g_BrowserServerList[ulIdx].lMSTime ) >= 4000 )
			g_BrowserServerList[ulIdx].ulActiveState = AS_TIMEDOUT;
	}
}

//*****************************************************************************
//
void BROWSER_SetSelectedServer( LONG lServer )
{
	g_lSelectedServer = lServer;
}

//*****************************************************************************
//
LONG BROWSER_GetSelectedServer( void )
{
	return ( g_lSelectedServer );
}

//*****************************************************************************
//
// [rc4l] Resolve an alpha-3 country code to the GeoIP id that indexes the CTRYFLAG sheet.
//
// The wire carries a code and the flag sheet is indexed by number, so something has to bridge them.
// GeoIP ships the table both ways round but only exposes id -> code, hence the scan. It runs once
// per server on receipt, over ~250 entries, and the result is cached in the server record -- drawing
// never does this.
//
// Unknown codes are normal rather than exceptional: a server may send nothing, may predate the field,
// or may report something GeoIP does not carry. All of those mean "no flag", not "error".
static ULONG browser_CountryIndexFromCode( const char *pszCode )
{
	if (( pszCode == NULL ) || ( pszCode[0] == '\0' ))
		return ( COUNTRY_INDEX_UNKNOWN );

	// A LAN server reports this rather than a real country; it has its own well-known index.
	if ( stricmp( pszCode, "LAN" ) == 0 )
		return ( COUNTRYINDEX_LAN );

	for ( ULONG ulIdx = 0; ulIdx < COUNTRYINDEX_LAN; ulIdx++ )
	{
		const char *pszCandidate = NETWORK_GetCountryCodeFromIndex( ulIdx, true );

		if (( pszCandidate != NULL ) && ( stricmp( pszCandidate, pszCode ) == 0 ))
			return ( ulIdx );
	}

	return ( COUNTRY_INDEX_UNKNOWN );
}

//*****************************************************************************
//
const char *BROWSER_GetCountryCode( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( "" );

	return ( g_BrowserServerList[ulServer].CountryCode.GetChars( ));
}

//*****************************************************************************
//
ULONG BROWSER_GetCountryIndex( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( COUNTRY_INDEX_UNKNOWN );

	return ( g_BrowserServerList[ulServer].ulCountryIndex );
}

//*****************************************************************************
//
// [rc4l] Humans only. See the header for why this exists separately.
//
// Falls back to the raw count when the server sent no per-player data: an over-count is a better
// failure than reporting an empty server that is in fact full, since the second sends players away.
LONG BROWSER_GetNumHumanPlayers( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( 0 );

	const LONG lTotal = MIN( g_BrowserServerList[ulServer].lNumPlayers, static_cast<LONG>( MAXPLAYERS ));

	if ( g_BrowserServerList[ulServer].bHasPlayerData == false )
		return ( lTotal );

	LONG lHumans = 0;
	for ( LONG lIdx = 0; lIdx < lTotal; lIdx++ )
	{
		if ( g_BrowserServerList[ulServer].Players[lIdx].bIsBot == false )
			lHumans++;
	}

	return ( lHumans );
}

//*****************************************************************************
// [SB]
const char *BROWSER_GetGameModeName( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( " " );

	// [SB] If the server didn't specify the name, use the name we know this mode by.
	if ( g_BrowserServerList[ulServer].GameModeName.IsEmpty( ))
		return ( GAMEMODE_GetName( g_BrowserServerList[ulServer].GameMode ));

	return ( g_BrowserServerList[ulServer].GameModeName.GetChars( ));
}

//*****************************************************************************
// [SB]
const char *BROWSER_GetGameModeShortName( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) || ( g_BrowserServerList[ulServer].ulActiveState != AS_ACTIVE ))
		return ( " " );

	// [SB] If the server didn't specify the name, use the name we know this mode by.
	if ( g_BrowserServerList[ulServer].GameModeShortName.IsEmpty( ))
		return ( GAMEMODE_GetShortName( g_BrowserServerList[ulServer].GameMode ));

	return ( g_BrowserServerList[ulServer].GameModeShortName.GetChars( ));
}

//*****************************************************************************
//*****************************************************************************
//
// [rc4l] See browser.h. The list is resolved when the browser opens, so this is usually already
// populated; a probe asked for before any refresh simply gets false and reports that it could not
// reach us rather than guessing.
bool BROWSER_GetServerRegistryAddress( NETADDRESS_s &out )
{
	if ( g_ServerRegistryAddresses.Size( ) == 0 )
		browser_ResolveServerRegistries( );

	if ( g_ServerRegistryAddresses.Size( ) == 0 )
		return false;

	out = g_ServerRegistryAddresses[0];
	return true;
}

//*****************************************************************************
//
// [rc4l] See browser.h. Whether a packet came from a registry this client actually talks to.
//
// Deliberately the WHOLE list, because browser_QueryServerRegistries sends to the whole list. Judging
// a reply by one address, or by the server-side announce cvar, meant we could send to a registry and
// then throw its answer away.
bool BROWSER_IsServerRegistryAddress( const NETADDRESS_s &address )
{
	for ( unsigned int i = 0; i < g_ServerRegistryAddresses.Size( ); ++i )
	{
		if ( g_ServerRegistryAddresses[i].Compare( address ))
			return true;
	}

	return false;
}

//*****************************************************************************
//
// [rc4l] See browser.h. Marks every listed server for a re-check while leaving it on the list.
void BROWSER_RefreshListedServers( void )
{
	for ( ULONG ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( g_BrowserServerList[ulIdx].ulActiveState != AS_ACTIVE )
			continue;

		// SENT HERE, not queued for BROWSER_QueryAllServers. That pump only runs when the registry
		// answers (cl_main.cpp), so queueing would tie a re-check of servers we can already reach to
		// a reply from a machine we may not be able to reach at all -- and on a LAN with no registry,
		// the re-checks would simply never go out. This is the whole point of re-querying each server
		// at its own address, so it must not route through the registry's timing.
		browser_QueryServer( ulIdx );

		g_BrowserServerList[ulIdx].bRefreshing = true;
		g_BrowserServerList[ulIdx].lRefreshMS = I_MSTime( );
	}
}

//*****************************************************************************
//
void BROWSER_ClearServerList( void )
{
	ULONG	ulIdx;

	for ( ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		g_BrowserServerList[ulIdx].ulActiveState = AS_INACTIVE;
		g_BrowserServerList[ulIdx].bRefreshing = false;
		g_BrowserServerList[ulIdx].lRefreshMS = 0;

		g_BrowserServerList[ulIdx].Address.Clear();

		// [rc4l] A reused slot must not inherit the previous occupant's flag or bot flags.
		g_BrowserServerList[ulIdx].CountryCode = "";
		g_BrowserServerList[ulIdx].ulCountryIndex = COUNTRY_INDEX_UNKNOWN;
		g_BrowserServerList[ulIdx].bHasPlayerData = false;

		// [rc4l] Nor a download port. Inheriting one would have us fetch from whoever last held the
		// slot -- a wrong address at best, and a stale one that happens to answer at worst.
		g_BrowserServerList[ulIdx].usDirectDownloadPort = 0;
		g_BrowserServerList[ulIdx].bPrefersMirrors = false;
		g_BrowserServerList[ulIdx].IWADHash = "";
		g_BrowserServerList[ulIdx].bForcePassword = false;
		g_BrowserServerList[ulIdx].bForceJoinPassword = false;
	}
}

//*****************************************************************************
//
void BROWSER_DeactivateAllServers( void )
{
	ULONG	ulIdx;

	for ( ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( g_BrowserServerList[ulIdx].ulActiveState == AS_ACTIVE )
			g_BrowserServerList[ulIdx].ulActiveState = AS_INACTIVE;
	}
}

//*****************************************************************************
//
void BROWSER_AddServerToList( const NETADDRESS_s &Address )
{
	// [rc4l] A server may be announced by more than one server registry, and each answers our query
	// independently, so the same address legitimately arrives several times. Without this the browser
	// listed it once per server registry -- the duplicates were harmless before only because there
	// was exactly one server registry to hear from.
	if ( browser_GetListIDByAddress( Address ) != -1 )
		return;

	const ULONG ulServer = browser_GetNewListID( );
	if ( ulServer >= MAX_BROWSER_SERVERS )
		I_Error( "BROWSER_GetServerList: Server limit exceeded (>=%d servers)", MAX_BROWSER_SERVERS );

	// This server is now active.
	g_BrowserServerList[ulServer].ulActiveState = AS_WAITINGFORREPLY;

	// Set the server address.
	g_BrowserServerList[ulServer].Address = Address;

	// [rc4l] Nothing is known about it yet, and browser_GetNewListID hands back reused slots.
	g_BrowserServerList[ulServer].CountryCode = "";
	g_BrowserServerList[ulServer].ulCountryIndex = COUNTRY_INDEX_UNKNOWN;
	g_BrowserServerList[ulServer].bHasPlayerData = false;

	// A slot arriving here is new, not being re-checked; inheriting a re-check would have this server
	// culled on the previous occupant's deadline.
	g_BrowserServerList[ulServer].bRefreshing = false;
	g_BrowserServerList[ulServer].lRefreshMS = 0;

	// Likewise the previous occupant's verdict about a build that has nothing to do with this one.
	g_BrowserServerList[ulServer].bVersionMismatch = false;
}

//*****************************************************************************
// [BB] Returns true if the server list packet was terminated by SRSC_ENDSERVERLIST,
// else it returns false.
bool BROWSER_GetServerList( BYTESTREAM_s *pByteStream )
{
	// No longer waiting for a server registry response.
	g_bWaitingForServerRegistryResponse = false;

	while ( true )
	{
		const LONG lCommand = pByteStream->ReadByte();

		switch ( lCommand )
		{
		case SRSC_SERVER:
			{
				// Read in address information.
				NETADDRESS_s serverAddress;
				serverAddress.ReadFromStream ( pByteStream );

				BROWSER_AddServerToList ( serverAddress );
			}
			break;

		case SRSC_SERVERBLOCK:
			{
				// Read in address information.
				NETADDRESS_s serverAddress;
				ULONG ulPorts = 0;
				while (( ulPorts = pByteStream->ReadByte() ))
				{
					serverAddress.ReadFromStream ( pByteStream, false );
					for ( ULONG ulIdx = 0; ulIdx < ulPorts; ++ulIdx )
					{
						serverAddress.usPort = htons( pByteStream->ReadShort());
						BROWSER_AddServerToList ( serverAddress );
					}
				}

			}
			break;

		case SRSC_ENDSERVERLISTPART:
			return false;

		case SRSC_ENDSERVERLIST:
			return true;

		default:

			Printf( "Unknown server list command from server registry: %d\n", static_cast<int> (lCommand) );
			return false;
		}
	}
}

//*****************************************************************************
//
// [rc4l] One reply being rebuilt per server. A datagram has a hard size ceiling, so a reply that
// outgrew one arrives in numbered pieces -- see features/launcher-protocol for the format and why
// every UDP query protocol ends up doing this.
//
// Kept here rather than in SERVER_t so browser.h, which is included widely, does not gain the
// dependency. Each entry is empty until a segmented reply actually arrives from that slot.
static zx::SegmentAssembly g_SegmentAssemblies[MAX_BROWSER_SERVERS];

void BROWSER_ParseServerQuerySegment( BYTESTREAM_s *pByteStream, bool bLAN )
{
	const LONG lServer = browser_GetListIDByAddress( NETWORK_GetFromAddress( ));

	// A piece from an address we never queried. Nothing to rebuild it into, and allocating on behalf
	// of an unsolicited packet is how a query port becomes a memory problem.
	if ( lServer == -1 )
		return;

	const size_t availableBytes = ( pByteStream->pbStreamEnd > pByteStream->pbStream )
		? static_cast<size_t>( pByteStream->pbStreamEnd - pByteStream->pbStream ) : 0;
	const unsigned char *base = reinterpret_cast<const unsigned char *>( pByteStream->pbStream );

	zx::SegmentHeader header;
	if ( zx::ReadSegmentHeader( base, availableBytes, header ) != zx::SegmentRead::Ok )
		return;

	zx::SegmentAssembly &assembly = g_SegmentAssemblies[lServer];
	const zx::SegmentAdd result = zx::AddSegment( assembly, header,
		base + zx::kSegmentHeaderBytes, availableBytes - zx::kSegmentHeaderBytes );

	if ( result != zx::SegmentAdd::Complete )
		return;

	// Whole again. A segmented reply omits the SERVER_LAUNCHER_CHALLENGE long that an unsegmented one
	// starts with -- see the `if ( !bSegmentedResponse )` guard on the server -- so the rebuilt buffer
	// begins exactly where BROWSER_ParseServerQuery expects to be handed the stream.
	BYTESTREAM_s assembled;
	assembled.pbStream = reinterpret_cast<BYTE *>( &assembly.data[0] );
	assembled.pbStreamEnd = assembled.pbStream + assembly.data.size( );

	BROWSER_ParseServerQuery( &assembled, bLAN );

	// Done with it either way: holding a completed reply only risks a later piece being merged into
	// something already consumed.
	zx::ResetAssembly( assembly );
}

//*****************************************************************************
//
void BROWSER_ParseServerQuery( BYTESTREAM_s *pByteStream, bool bLAN )
{
	GAMEMODE_e	GameMode = GAMEMODE_COOPERATIVE;
	ULONG		ulIdx;
	LONG		lServer;
	ULONG		ulFlags;
	ULONG 		ulFlags2;
	bool		bResortList = true;

	lServer = browser_GetListIDByAddress( NETWORK_GetFromAddress( ));

	// If this is a LAN server and it's already on the list, there's no
	// need to resort the server list.
	if ( bLAN && ( lServer != -1 ))
		bResortList = false;

	// If this is a LAN server, and it doesn't exist on the server list, add it.
	if (( lServer == -1 ) && bLAN )
		lServer = browser_GetNewListID( );

	// [BB] If we didn't find the server, the SERVER_LAUNCHER_CHALLENGE came from an
	// address we didn't query, so just ignore whatever was sent to us.
	if ( lServer == -1 )
	{
		while ( 1 )
		{
			if ( pByteStream->ReadByte() == -1 )
				return;
		}
	}

	// This server is now active.
	g_BrowserServerList[lServer].ulActiveState = AS_ACTIVE;

	// [rc4l] It answered, so any re-check outstanding against it is settled. Clearing this is what
	// stops BROWSER_QueryTick from dropping a server that replied perfectly well.
	g_BrowserServerList[lServer].bRefreshing = false;
	g_BrowserServerList[lServer].lRefreshMS = 0;

	// Is this a LAN server?
	g_BrowserServerList[lServer].bLAN = bLAN;

	// We heard back from this server, so calculate ping right away.
	if ( bLAN )
	{
		// If this is a LAN server, the IP address has not be set up yet.
		g_BrowserServerList[lServer].Address = NETWORK_GetFromAddress( );
		g_BrowserServerList[lServer].lPing = 0;
	}
	else
		g_BrowserServerList[lServer].lPing = I_MSTime( ) - g_BrowserServerList[lServer].lMSTime;

	// Read in the time we sent to the server.
	pByteStream->ReadLong();

	// Read in the version.
	g_BrowserServerList[lServer].Version = pByteStream->ReadString();

	// If the version doesn't match ours, remove it from the list.
	{
		// [rc4l] Compare ZandroX versions, not Zandronum ones. GetVersionStringRev() is identical
		// across every ZandroX release, so this check used to pass for builds that cannot actually
		// play together -- it was verifying the thing we inherited rather than the thing we are.
		//
		// Must stay in step with what the server sends (sv_serverregistry.cpp / i_system.cpp); the
		// two halves of this comparison are one decision expressed in two places.
		FString ourVersion = GetFuaVersionTag();
		if ( ourVersion[ourVersion.Len()-1] == 'M' )
			ourVersion = ourVersion.Left ( ourVersion.Len()-1 );

		// [BB] Check whether the server version starts with our version.
		if ( g_BrowserServerList[lServer].Version.IndexOf ( ourVersion ) != 0 )
		{
			// [rc4l] Remember WHY this one went quiet, so the footer can say so. Hiding a server that
			// answered us, with no trace that it did, is how "I can see it and you cannot" turns into
			// a bug report about the server registry.
			g_BrowserServerList[lServer].bVersionMismatch = true;
			g_BrowserServerList[lServer].ulActiveState = AS_INACTIVE;
			while ( 1 )
			{
				if ( pByteStream->ReadByte() == -1 )
					return;
			}
		}
	}

	// Read in the data that will be sent to us.
	ulFlags = pByteStream->ReadLong();

	// Read the server name.
	if ( ulFlags & SQF_NAME )
		g_BrowserServerList[lServer].HostName = pByteStream->ReadString();

	// Read the website URL.
	if ( ulFlags & SQF_URL )
		g_BrowserServerList[lServer].WadURL = pByteStream->ReadString();

	// Read the host's e-mail address.
	if ( ulFlags & SQF_EMAIL )
		g_BrowserServerList[lServer].EmailAddress = pByteStream->ReadString();

	if ( ulFlags & SQF_MAPNAME )
		g_BrowserServerList[lServer].Mapname = pByteStream->ReadString();
	if ( ulFlags & SQF_MAXCLIENTS )
		g_BrowserServerList[lServer].lMaxClients = pByteStream->ReadByte();

	// Maximum slots.
	if ( ulFlags & SQF_MAXPLAYERS )
		pByteStream->ReadByte();

	// Read in the PWAD information.
	if ( ulFlags & SQF_PWADS )
	{
		ULONG ulNumPWADs = static_cast<ULONG>( pByteStream->ReadByte() );
		if ( ulNumPWADs > 0 )
		{
			g_BrowserServerList[lServer].PWADNames.Resize( ulNumPWADs );
			for ( ulIdx = 0; ulIdx < ulNumPWADs; ulIdx++ )
				g_BrowserServerList[lServer].PWADNames[ulIdx] = pByteStream->ReadString();
		}
	}

	if ( ulFlags & SQF_GAMETYPE )
	{
		g_BrowserServerList[lServer].GameMode = (GAMEMODE_e)pByteStream->ReadByte();
		pByteStream->ReadByte();
		pByteStream->ReadByte();
	}

	// Game name.
	if ( ulFlags & SQF_GAMENAME )
		pByteStream->ReadString();

	// Read in the IWAD name.
	if ( ulFlags & SQF_IWAD )
		g_BrowserServerList[lServer].IWADName = pByteStream->ReadString();

	// [rc4l] Kept rather than discarded: the browser's Public/Private tabs sort on it. Either kind of
	// password makes a server private -- one gates connecting and the other gates joining the game,
	// and from the outside both mean "you need to have been told something to get in".
	if ( ulFlags & SQF_FORCEPASSWORD )
		g_BrowserServerList[lServer].bForcePassword = !!pByteStream->ReadByte();

	if ( ulFlags & SQF_FORCEJOINPASSWORD )
		g_BrowserServerList[lServer].bForceJoinPassword = !!pByteStream->ReadByte();

	// Game skill.
	if ( ulFlags & SQF_GAMESKILL )
		pByteStream->ReadByte();

	// Bot skill.
	if ( ulFlags & SQF_BOTSKILL )
		pByteStream->ReadByte();

	if ( ulFlags & SQF_DMFLAGS )
	{
		// DMFlags.
		pByteStream->ReadLong();

		// DMFlags2.
		pByteStream->ReadLong();

		// Compatflags.
		pByteStream->ReadLong();
	}

	if ( ulFlags & SQF_LIMITS )
	{
		// Fraglimit.
		pByteStream->ReadShort();

		// Timelimit.
		if ( pByteStream->ReadShort())
		{
			// Time left.
			pByteStream->ReadShort();
		}

		// Duellimit.
		pByteStream->ReadShort();

		// Pointlimit.
		pByteStream->ReadShort();

		// Winlimit.
		pByteStream->ReadShort();
	}

	// Team damage scale.
	if ( ulFlags & SQF_TEAMDAMAGE )
		pByteStream->ReadFloat();

	// [CW] Deprecated!
	if ( ulFlags & SQF_TEAMSCORES )
	{
		// Blue score.
		pByteStream->ReadShort();

		// Red score.
		pByteStream->ReadShort();
	}

	// Read in the number of players.
	if ( ulFlags & SQF_NUMPLAYERS )
		g_BrowserServerList[lServer].lNumPlayers = pByteStream->ReadByte();

	// [rc4l] Recorded per response, not once: a server that stops sending player data must not leave
	// us counting bot flags left over from an earlier reply.
	g_BrowserServerList[lServer].bHasPlayerData = !!( ulFlags & SQF_PLAYERDATA );

	if ( ulFlags & SQF_PLAYERDATA )
	{
		if ( g_BrowserServerList[lServer].lNumPlayers > 0 )
		{
			for ( ulIdx = 0; ulIdx < (ULONG)g_BrowserServerList[lServer].lNumPlayers; ulIdx++ )
			{
				// Read in this player's name.
				g_BrowserServerList[lServer].Players[ulIdx].Name = pByteStream->ReadString();

				// Read in "fragcount" (could be frags, points, etc.)
				g_BrowserServerList[lServer].Players[ulIdx].lFragcount = pByteStream->ReadShort();

				// Read in the player's ping.
				g_BrowserServerList[lServer].Players[ulIdx].lPing = pByteStream->ReadShort();

				// Read in whether or not the player is spectating.
				g_BrowserServerList[lServer].Players[ulIdx].bSpectating = !!pByteStream->ReadByte();

				// Read in whether or not the player is a bot.
				g_BrowserServerList[lServer].Players[ulIdx].bIsBot = !!pByteStream->ReadByte();

				if ( GAMEMODE_GetFlags( g_BrowserServerList[lServer].GameMode ) & GMF_PLAYERSONTEAMS )
				{
					// Team.
					pByteStream->ReadByte();
				}

				// Time.
				pByteStream->ReadByte();
			}
		}
	}

	// [CW] Read in the number of the teams.
	// [BB] Make sure that the number is valid!
	if ( ulFlags & SQF_TEAMINFO_NUMBER )
		g_ulNumberOfTeams = clamp ( pByteStream->ReadByte(), 2, MAX_TEAMS );

	// [CW] Read in the name of the teams.
	if ( ulFlags & SQF_TEAMINFO_NAME )
	{
		for ( ULONG ulIdx = 0; ulIdx < g_ulNumberOfTeams; ulIdx++ )
			pByteStream->ReadString();
	}

	// [CW] Read in the color of the teams.
	if ( ulFlags & SQF_TEAMINFO_COLOR )
	{
		for ( ULONG ulIdx = 0; ulIdx < g_ulNumberOfTeams; ulIdx++ )
			pByteStream->ReadLong();
	}

	// [CW] Read in the score of the teams.
	if ( ulFlags & SQF_TEAMINFO_SCORE )
	{
		for ( ULONG ulIdx = 0; ulIdx < g_ulNumberOfTeams; ulIdx++ )
			pByteStream->ReadShort();
	}

	// [BB] Testing server and what's the binary name?
	if ( ulFlags & SQF_TESTING_SERVER )
	{
		pByteStream->ReadByte();
		pByteStream->ReadString();
	}

	// [BB] MD5 sum of the main data file (skulltag.wad / skulltag_data.pk3).
	if ( ulFlags & SQF_DATA_MD5SUM )
		pByteStream->ReadString();

	// [BB] All dmflags and compatflags.
	// [rc4l] Read and DISCARDED. We no longer ask for this -- the detail panel listed the numbers
	// briefly and they were not worth the room -- but a field still has to be consumed if it turns
	// up, or everything after it desynchronises. That is not a hypothetical: skipping one byte of
	// SQF2_VOICECHAT is what made a download port read as 6400 instead of 10777.
	//
	// Read by the count the server sent rather than an assumed six, so a newer engine adding a word
	// does not desynchronise us either.
	if ( ulFlags & SQF_ALL_DMFLAGS )
	{
		const ULONG ulNumFlags = pByteStream->ReadByte();
		for ( ULONG ulIdx = 0; ulIdx < ulNumFlags; ulIdx++ )
			pByteStream->ReadLong( );
	}

	// [BB] Get special security settings like sv_fua_serverregistry_enforcebans.
	if ( ulFlags & SQF_SECURITY_SETTINGS )
		pByteStream->ReadByte();

	// [TP] Optional wads
	if ( ulFlags & SQF_OPTIONAL_WADS )
	{
		for ( int i = pByteStream->ReadByte(); i > 0; --i )
			pByteStream->ReadByte();
	}

	// [TP] Dehacked patches
	if ( ulFlags & SQF_DEH )
	{
		for ( int i = pByteStream->ReadByte(); i > 0; --i )
			pByteStream->ReadString();
	}

	// [SB] Extended server info
	// [rc4l] The field walk itself lives in computation/launcherfields_compute.h, which is a compute
	// unit and not three more lines here for a specific reason: these fields are VARIABLE LENGTH, so
	// getting one width wrong silently corrupts every field after it, and the corruption reads as a
	// plausible value rather than as garbage. Skipping the one-byte SQF2_VOICECHAT field made a
	// server's download port read as 6400 instead of 10777 and took a long time to find. Parsing
	// where it can be tested against a writer, for every combination of flags, is the fix for the
	// class rather than the instance.
	if ( ulFlags & SQF_EXTENDED_INFO )
	{
		ulFlags2 = pByteStream->ReadLong();

		const size_t availableBytes = ( pByteStream->pbStreamEnd > pByteStream->pbStream )
			? static_cast<size_t>( pByteStream->pbStreamEnd - pByteStream->pbStream ) : 0;

		zx::LauncherExtendedInfo extended;
		size_t consumedBytes = 0;
		const zx::ExtendedParse parseResult = zx::ParseExtendedInfo(
			reinterpret_cast<const unsigned char *>( pByteStream->pbStream ), availableBytes,
			static_cast<unsigned>( ulFlags2 ), extended, consumedBytes );

		if ( parseResult != zx::ExtendedParse::Ok )
		{
			// Refused rather than half-applied. A block we could not walk tells us nothing, and
			// whatever this entry already held is more trustworthy than values read from a position
			// we are no longer sure of.
			return;
		}

		pByteStream->pbStream += consumedBytes;

		// [rc4l] The server's own MD5 for each of its PWADs -- what lets a download be verified
		// against what the server actually has, instead of trusting a mirror served the right bytes
		// under the right name.
		if ( ulFlags2 & SQF2_PWAD_HASHES )
		{
			g_BrowserServerList[lServer].PWADHashes.Clear( );
			for ( size_t i = 0; i < extended.pwadHashes.size( ); ++i )
				g_BrowserServerList[lServer].PWADHashes.Push( extended.pwadHashes[i].c_str( ));
		}

		if ( ulFlags2 & SQF2_COUNTRY )
		{
			// [rc4l] ISO 3166-1 reserves XAA-XZZ, and the protocol uses two of them as instructions
			// to the launcher rather than as places:
			//
			//   XIP  the server declines to say; work it out from its address yourself
			//   XUN  unknown, and it does not want us guessing either
			//
			// A server whose own GeoIP lookup failed sends XIP, which is the common case rather than
			// an edge one -- taking it literally is how "XIP" ended up drawn in the country column.
			const char *code = extended.countryCode.c_str( );

			if ( stricmp( code, "XIP" ) == 0 )
			{
				g_BrowserServerList[lServer].CountryCode = "";
				g_BrowserServerList[lServer].ulCountryIndex =
					NETWORK_GetCountryIndexFromAddress( g_BrowserServerList[lServer].Address );
			}
			else if ( stricmp( code, "XUN" ) == 0 )
			{
				g_BrowserServerList[lServer].CountryCode = "";
				g_BrowserServerList[lServer].ulCountryIndex = COUNTRY_INDEX_UNKNOWN;
			}
			else
			{
				g_BrowserServerList[lServer].CountryCode = code;
				g_BrowserServerList[lServer].ulCountryIndex = browser_CountryIndexFromCode( code );
			}
		}

		if ( ulFlags2 & SQF2_GAMEMODE_NAME )
			g_BrowserServerList[lServer].GameModeName = extended.gameModeName.c_str( );

		if ( ulFlags2 & SQF2_GAMEMODE_SHORTNAME )
			g_BrowserServerList[lServer].GameModeShortName = extended.gameModeShortName.c_str( );

		// [rc4l] Port 0 is how "not serving" is spelled -- the field is always present when asked
		// for, because a field that is sometimes there is what desynchronises a stream.
		if ( ulFlags2 & SQF2_FUA_DIRECT_DOWNLOAD )
		{
			g_BrowserServerList[lServer].usDirectDownloadPort =
				static_cast<USHORT>( extended.directDownloadPort );
			g_BrowserServerList[lServer].bPrefersMirrors = extended.prefersMirrors;
		}

		// [rc4l] Which BUILD of the IWAD, not just its name.
		if ( ulFlags2 & SQF2_FUA_IWAD_HASH )
			g_BrowserServerList[lServer].IWADHash = extended.iwadHash.c_str( );

		// [rc4l] What each download would actually cost, which the browser draws beside the filename.
		if ( ulFlags2 & SQF2_FUA_WAD_SIZES )
		{
			g_BrowserServerList[lServer].IWADSize = static_cast<unsigned int>( extended.iwadSize );
			g_BrowserServerList[lServer].PWADSizes.Clear( );
			for ( size_t i = 0; i < extended.pwadSizes.size( ); ++i )
				g_BrowserServerList[lServer].PWADSizes.Push( static_cast<unsigned int>( extended.pwadSizes[i] ));
		}
	}

	// [rc4l] The old browser cached a sorted index that had to be rebuilt from here whenever a reply
	// landed. The MVP browser derives its list every tic in serverbrowser_RebuildList(), precisely so
	// the data and the view cannot fall out of step -- so there is nothing left to poke.
	(void)bResortList;
}

//*****************************************************************************
//
// [rc4l] Rebuild the query list: the player's own entries first, then the fetched/cached/built-in
// ones. See features/federated-server-registry/zx_serverregistrylist.h for where those come from.
//
// An entry that fails to resolve is skipped with a warning rather than aborting the rest: one dead
// server registry must not stop a player seeing the servers on the others, which is the entire point
// of querying several.
static void browser_ResolveServerRegistries( void )
{
	g_ServerRegistryAddresses.Clear();

	// Kick a background refresh if the cache is stale. It never blocks and never affects THIS query --
	// a newly fetched list takes effect the next time the browser is opened.
	zx::ServerRegistryList_MaybeRefresh();

	const std::vector<zx::ServerRegistryEntry> entries =
		zx::ServerRegistryList_Resolve( *cl_fua_serverregistry_list );

	for ( size_t i = 0; i < entries.size( ); ++i )
	{
		NETADDRESS_s address;
		if ( address.LoadFromString( entries[i].host.c_str( )) == false )
		{
			Printf( "Warning: can't resolve server registry \"%s\" -- skipping it.\n", entries[i].host.c_str( ));
			continue;
		}

		// [rc4l] The parser already split any ":port" off the host, so an entry either carries its own
		// port or takes the default. LoadFromString never sees a port here.
		address.SetPort( entries[i].port != 0 ? static_cast<USHORT>( entries[i].port ) : g_usServerRegistryPort );

		g_ServerRegistryAddresses.Push( address );
	}

	if ( g_ServerRegistryAddresses.Size( ) == 0 )
		Printf( "Warning: no server registry could be resolved -- check cl_fua_serverregistry_list.\n" );
}

//*****************************************************************************
//
// [rc4l] Send the launcher challenge to every known server registry and start the retry clock.
//
// They do not know about each other, so a server listed on more than one arrives more than once --
// BROWSER_AddServerToList de-duplicates by address. Re-sending on timeout has the same property, so
// a retry that crosses a slow reply costs nothing but a duplicate packet.
static void browser_SendServerRegistryQuery( void )
{
	g_ServerRegistryBuffer.Clear();
	g_ServerRegistryBuffer.ByteStream.WriteLong( LAUNCHER_SERVERREGISTRY_CHALLENGE );
	g_ServerRegistryBuffer.ByteStream.WriteShort( SERVERREGISTRY_VERSION );

	for ( unsigned int i = 0; i < g_ServerRegistryAddresses.Size( ); ++i )
		NETWORK_LaunchPacket( &g_ServerRegistryBuffer, g_ServerRegistryAddresses[i] );

	g_ulServerRegistryQuerySentMS = I_MSTime( );
	g_lServerRegistryAttempts++;
}

//*****************************************************************************
//
void BROWSER_QueryServerRegistry( void )
{
	browser_ResolveServerRegistries();

	if ( g_ServerRegistryAddresses.Size( ) == 0 )
		return;

	// We are currently waiting to hear back from the server registries.
	g_bWaitingForServerRegistryResponse = true;
	g_lServerRegistryAttempts = 0;

	browser_SendServerRegistryQuery();
}

//*****************************************************************************
//
// [rc4l] Retry, then give up out loud. Called every tic while the browser menu is open.
//
// Giving up MUST clear g_bWaitingForServerRegistryResponse: M_RefreshServers() refuses to do anything
// while that flag is set, so leaving it set is indistinguishable to the player from the browser being
// broken. Better to say nothing came back and let them press refresh again.
void BROWSER_ServerRegistryTick( void )
{
	if ( g_bWaitingForServerRegistryResponse == false )
		return;

	// Unsigned arithmetic, so this stays correct across the I_MSTime() wrap.
	if (( I_MSTime( ) - g_ulServerRegistryQuerySentMS ) < SERVERREGISTRY_QUERY_TIMEOUT_MS )
		return;

	if ( g_lServerRegistryAttempts < SERVERREGISTRY_QUERY_MAX_ATTEMPTS )
	{
		browser_SendServerRegistryQuery();
		return;
	}

	g_bWaitingForServerRegistryResponse = false;

	FString names;
	for ( unsigned int i = 0; i < g_ServerRegistryAddresses.Size( ); ++i )
	{
		if ( names.IsNotEmpty( ))
			names += ", ";
		names += g_ServerRegistryAddresses[i].ToString( );
	}
	Printf( "No response from %s after %d tries. The server list may be incomplete.\n",
		names.GetChars( ), SERVERREGISTRY_QUERY_MAX_ATTEMPTS );
}

//*****************************************************************************
//
// [rc4l] Stop retrying: the server registry answered, just not with a list.
//
// REQUESTIGNORED, IPISBANNED and WRONGVERSION are all definitive -- the packet arrived, was
// understood, and was refused. Retrying can only produce the same refusal, and in the
// REQUESTIGNORED case it actively makes things worse by landing us on the registry's flood queue.
//
// Before this, those three replies only printed a message and left the query outstanding, so the
// retry loop kept firing at a registry that had already said no.
void BROWSER_ServerRegistryRefusedQuery( void )
{
	g_bWaitingForServerRegistryResponse = false;
}

//*****************************************************************************
//
bool BROWSER_WaitingForServerRegistryResponse( void )
{
	return ( g_bWaitingForServerRegistryResponse );
}

//*****************************************************************************
//
void BROWSER_QueryAllServers( void )
{
	ULONG	ulIdx;

	for ( ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( g_BrowserServerList[ulIdx].ulActiveState == AS_WAITINGFORREPLY )
			browser_QueryServer( ulIdx );
	}
}

//*****************************************************************************
//
LONG BROWSER_CalcNumServers( void )
{
	ULONG	ulIdx;
	ULONG	ulNumServers;

	ulNumServers = 0;
	for ( ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( g_BrowserServerList[ulIdx].ulActiveState == AS_ACTIVE )
			ulNumServers++;
	}

	return ( ulNumServers );
}

//*****************************************************************************
//*****************************************************************************
//
static LONG browser_GetNewListID( void )
{
	ULONG	ulIdx;

	for ( ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( g_BrowserServerList[ulIdx].ulActiveState == AS_INACTIVE )
			return ( ulIdx );
	}

	return ( -1 );
}

//*****************************************************************************
//
static LONG browser_GetListIDByAddress( NETADDRESS_s Address )
{
	ULONG	ulIdx;

	for ( ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( g_BrowserServerList[ulIdx].Address.Compare( Address ))
			return ( ulIdx );
	}

	return ( -1 );
}

//*****************************************************************************
//
// [rc4l] Is a launcher reply from this address something we are waiting for?
//
// The client's packet loop needs this: a reply from the server we are PLAYING on arrives from the
// same address as game traffic, so it has to be told apart from it, and "we asked this address a
// question and have not had an answer" is the only honest way to tell.
// [rc4l] Is anything actually being checked right now?
//
// Exists so the refresh button can show it. The re-check runs silently underneath a list that keeps
// its rows, which is the right behaviour and looks exactly like nothing happening -- and a player
// who cannot see the work concludes the list is a stale cache and asks for a refresh button. This
// is what makes the button they press honest about the work that was already under way.
bool BROWSER_IsRefreshInFlight( void )
{
	if ( g_bWaitingForServerRegistryResponse )
		return ( true );

	for ( ULONG ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( g_BrowserServerList[ulIdx].bRefreshing )
			return ( true );

		if ( g_BrowserServerList[ulIdx].ulActiveState == AS_WAITINGFORREPLY )
			return ( true );
	}

	return ( false );
}

//*****************************************************************************
//
// [rc4l] How many servers answered and were hidden for running a different build.
LONG BROWSER_CountVersionMismatched( void )
{
	LONG lCount = 0;

	for ( ULONG ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( g_BrowserServerList[ulIdx].bVersionMismatch )
			lCount++;
	}

	return ( lCount );
}

//*****************************************************************************
//
bool BROWSER_IsAwaitingReplyFrom( const NETADDRESS_s &Address )
{
	const LONG lServer = browser_GetListIDByAddress( Address );
	if ( lServer == -1 )
		return ( false );

	return ( g_BrowserServerList[lServer].ulActiveState == AS_WAITINGFORREPLY );
}

//*****************************************************************************
//
static void browser_QueryServer( ULONG ulServer )
{
	// [rc4l] The server we are connected to USED to be skipped here, and that is how a server you
	// were standing on appeared in your own browser as "did not respond".
	//
	// The slot has already been marked AS_WAITINGFORREPLY by the caller, so returning without
	// sending anything did not skip the server, it condemned it: nothing could ever answer, and the
	// row aged out on its own timeout and was counted as a failure. A player would join a server,
	// open the list while playing on it, and be told it was not there.
	//
	// So it is queried like any other. The reply arrives from the address the game connection also
	// uses, which the client's packet loop now separates by asking BROWSER_IsAwaitingReplyFrom.

	// Clear out the buffer, and write out launcher challenge.
	// [SB] Added extended flags that we want.
	g_ServerBuffer.Clear();
	g_ServerBuffer.ByteStream.WriteLong( LAUNCHER_SERVER_CHALLENGE );
	// [rc4l] SQF_FORCEPASSWORD / SQF_FORCEJOINPASSWORD added: the Public/Private tabs sort on them.
	// The parse already consumed both bytes to keep its place; now it keeps the values too.
	g_ServerBuffer.ByteStream.WriteLong( SQF_NAME|SQF_URL|SQF_EMAIL|SQF_MAPNAME|SQF_MAXCLIENTS|SQF_PWADS|SQF_GAMETYPE|SQF_IWAD|SQF_FORCEPASSWORD|SQF_FORCEJOINPASSWORD|SQF_NUMPLAYERS|SQF_PLAYERDATA|SQF_EXTENDED_INFO );
	g_ServerBuffer.ByteStream.WriteLong( I_MSTime( ));
	// [rc4l] SQF2_COUNTRY added: the flag column needs it, and the server has always been willing to
	// send it -- the old browser asked for everything except the one field it then read and discarded.
	// [rc4l] SQF2_PWAD_HASHES added: the downloader verifies each fetched PWAD against the server's
	// own MD5, so a mirror cannot hand us the wrong file (or a stale version) under the right name.
	// [rc4l] SQF2_FUA_DIRECT_DOWNLOAD added: tells us whether this server will serve its own WADs and
	// on what port, which is the only way to reach a file that exists on no mirror.
	// [rc4l] SQF2_FUA_IWAD_HASH added: tells us which BUILD of the IWAD the server runs, so we can
	// load the copy that will actually pass level authentication.
	// [rc4l] SQF2_FUA_WAD_SIZES added: how big each PWAD is, so the browser can say what agreeing to
	// a download costs before the player agrees to it.
	g_ServerBuffer.ByteStream.WriteLong( SQF2_GAMEMODE_NAME|SQF2_GAMEMODE_SHORTNAME|SQF2_COUNTRY|SQF2_PWAD_HASHES|SQF2_FUA_DIRECT_DOWNLOAD|SQF2_FUA_IWAD_HASH|SQF2_FUA_WAD_SIZES );

	// [rc4l] Ask for a segmented reply if it does not fit one datagram. The server enables it on a
	// trailing byte of exactly 2 (sv_main.cpp), and an older server simply never reads this far --
	// which is why the opt-in is a byte on the end rather than a flag in the middle.
	//
	// Worth asking for even though most replies fit today: the field set has only ever grown, and the
	// failure when it stops fitting is a silently truncated reply rather than an error.
	g_ServerBuffer.ByteStream.WriteByte( 2 );

	// Send the server our packet.
	NETWORK_LaunchPacket( &g_ServerBuffer, g_BrowserServerList[ulServer].Address );

	// Keep track of the time we queried this server at.
	g_BrowserServerList[ulServer].lMSTime = I_MSTime( );
}

//*****************************************************************************
//	CONSOLE VARIABLES/COMMANDS

//*****************************************************************************
//

CCMD( dumpserverlist )
{
	ULONG	ulIdx;

	for ( ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( g_BrowserServerList[ulIdx].ulActiveState != AS_ACTIVE )
			continue;

		Printf( "\nServer #%d\n----------------\n", static_cast<unsigned int> (ulIdx) );
		Printf( "Name: %s\n", g_BrowserServerList[ulIdx].HostName.GetChars() );
		Printf( "Address: %s\n", g_BrowserServerList[ulIdx].Address.ToString() );
		Printf( "Gametype: %d (%s)\n", g_BrowserServerList[ulIdx].GameMode, BROWSER_GetGameModeName(ulIdx) );
		Printf( "Num PWADs: %d\n", static_cast<int> (g_BrowserServerList[ulIdx].PWADNames.Size()) );
		Printf( "Players: %d/%d\n", static_cast<int> (g_BrowserServerList[ulIdx].lNumPlayers), static_cast<int> (g_BrowserServerList[ulIdx].lMaxClients) );
		Printf( "Ping: %d\n", static_cast<int> (g_BrowserServerList[ulIdx].lPing) );

		// [rc4l] Whether this server will serve its own WADs, and from where. Worth printing: when a
		// download fails, the first question is whether the client ever learned an endpoint at all,
		// and that is otherwise invisible.
		// [rc4l] What the Public/Private tabs sort on, printed because "why is that server in the
		// wrong tab" is otherwise unanswerable from outside.
		Printf( "Password: %s%s\n",
			g_BrowserServerList[ulIdx].bForcePassword ? "connect " : "",
			g_BrowserServerList[ulIdx].bForceJoinPassword ? "join" :
				( g_BrowserServerList[ulIdx].bForcePassword ? "" : "no" ));

		const FString directUrl = BROWSER_GetDirectDownloadURL( ulIdx );
		Printf( "Direct download: %s%s\n", directUrl.IsEmpty() ? "(none advertised)" : directUrl.GetChars(),
			( !directUrl.IsEmpty() && g_BrowserServerList[ulIdx].bPrefersMirrors ) ? " (prefers mirrors)" : "" );
	}
}
