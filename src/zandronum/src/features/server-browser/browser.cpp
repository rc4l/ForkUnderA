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
#include "features/server-browser/computation/registrystatus_compute.h"
#include "features/server-browser/computation/querypunch_compute.h"   // [rc4l] punch-on-query schedule
#include "features/server-browser/computation/rowlifetime_compute.h"  // [rc4l] what keeps a row mortal
#include "features/server-browser/computation/rowstep_compute.h"      // [rc4l] what to do with a row
#include "features/net/zx_resolve.h"                            // [rc4l] the registry's AAAA
#include "features/server-hosting/zx_punchclient.h"                   // [rc4l] PunchRequestFor
#include "features/launcher-protocol/computation/segmentreassembly_compute.h"
#include "features/server-hosting/zx_hosting.h" // [rc4l] which rows are the server WE started
#include "features/server-hosting/zx_reachprobe.h" // [rc4l] ReachProbePublicIp
#include "features/server-browser/computation/joinintent_compute.h" // [rc4l] RowIsOwnServer
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

// [rc4l] The registry address that last answered, so a punch is asked of a family we know works.
static	NETADDRESS_s	g_LastAnsweringRegistry;
static	bool			g_bHaveAnsweringRegistry = false;

// [rc4l] What became of each registry in the list, kept so the browser can SHOW it.
//
// One row per entry in cl_fua_serverregistry_list, including the ones that never produced an address:
// a name that will not look up is exactly the case that used to vanish into a console warning nobody
// reads, so it has to survive here or the bar for it can never be drawn. See
// computation/registrystatus_compute.h for why each status exists.
struct SERVERREGISTRYSTATE_s
{
	std::string			host;			// as written in the list
	int					port;			// 0 when the lookup never got that far
	NETADDRESS_s		address;
	bool				bResolved;
	zx::RegistryStatus	status;

	// [rc4l] When `status` was recorded. Only THROTTLED reads it, to know when the registry has
	// stopped ignoring us; every other status is a finished verdict that time does not change.
	ULONG				ulStatusMS;
};

static	std::vector<SERVERREGISTRYSTATE_s>	g_ServerRegistryStates;

// [rc4l] When a sweep last went out, or 0 for never. Stamped where the queries are SENT rather than
// where replies land: a refresh that found nothing still happened, and a player asking "is this list
// current" is asking when we last looked, not when somebody last answered.
static	ULONG			g_ulLastRefreshMS = 0;

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

// [rc4l] Punch-on-query budget for one refresh sweep. The registry rate-limits punch requests to
// five per ten seconds per client; four leaves one for the join the player is presumably about to
// make on whichever of those servers appears. Reset wherever a sweep of queries starts.
static	LONG			g_lPunchesThisSweep;
static	const LONG		kMaxPunchesPerSweep = 4;

//*****************************************************************************
//
// [rc4l] Addresses that went unanswered, a small ring because this is a hint about which servers need
// help rather than a database.
#define MAX_REMEMBERED_UNREACHABLE 32
static	NETADDRESS_s	g_UnreachableAddresses[MAX_REMEMBERED_UNREACHABLE];
static	ULONG			g_ulUnreachableNext = 0;

static bool browser_IsKnownUnreachable( const NETADDRESS_s &Address )
{
	for ( ULONG ulIdx = 0; ulIdx < MAX_REMEMBERED_UNREACHABLE; ulIdx++ )
	{
		if ( g_UnreachableAddresses[ulIdx].usPort == 0 )
			continue;

		// [rc4l] CompareNoPort, since a punch may have re-aimed the row at the port its NAT opened.
		if ( g_UnreachableAddresses[ulIdx].CompareNoPort( Address ))
			return true;
	}

	return false;
}

static void browser_RememberUnreachable( const NETADDRESS_s &Address )
{
	if ( browser_IsKnownUnreachable( Address ))
		return;

	g_UnreachableAddresses[g_ulUnreachableNext] = Address;
	g_ulUnreachableNext = ( g_ulUnreachableNext + 1 ) % MAX_REMEMBERED_UNREACHABLE;
}

static void browser_ForgetUnreachable( const NETADDRESS_s &Address )
{
	// [rc4l] It answered, so spending more punches on it would starve one that still needs them.
	for ( ULONG ulIdx = 0; ulIdx < MAX_REMEMBERED_UNREACHABLE; ulIdx++ )
	{
		if ( g_UnreachableAddresses[ulIdx].CompareNoPort( Address ))
			g_UnreachableAddresses[ulIdx].Clear( );
	}
}

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

// [rc4l] How long a re-check of an already-listed row may go unanswered. Generous on purpose: this
// is "will never answer", not a quality bar.
static const int		SERVERBROWSER_REFRESH_TIMEOUT_MS = 4000;

// [rc4l] How many re-checks in a row a listed server may miss before it comes off the list.
//
// Three, because one is packet loss and two is a bad moment. A server that has answered once and
// then ignores three separate challenges over twelve seconds has genuinely gone; before that, the
// row is worth keeping, since the cost of showing a dead server for a few seconds is a failed join
// and the cost of hiding a live one is that nobody can find it at all.
static const int		SERVERBROWSER_MAX_RECHECK_MISSES = 3;

// [rc4l] How long a THROTTLED verdict keeps meaning something. The registry's own ignore window is
// three seconds ("Ignoring for 3 seconds" in its log), so this sits just past it: long enough not to
// clear a refusal that is still in force, short enough that the bar recovers while you are looking.
static const int		SERVERREGISTRY_THROTTLE_CLEAR_MS = 4000;
static const int		SERVERREGISTRY_QUERY_MAX_ATTEMPTS = 3;

// Mark every registry we are still waiting on, without disturbing one that already answered.
static void browser_SetPendingRegistryStates( void )
{
	for ( size_t i = 0; i < g_ServerRegistryStates.size( ); ++i )
	{
		if ( g_ServerRegistryStates[i].bResolved )
		{
			g_ServerRegistryStates[i].status = zx::RegistryStatus::Pending;
			g_ServerRegistryStates[i].ulStatusMS = I_MSTime( );
		}
	}
}

// [rc4l] Turn "still waiting" into "nothing came back" once no further attempt will be made to that
// registry, and the last one we did make has timed out.
//
// Two ways there will be no further attempt: we ran out of retries, or somebody else answered and the
// retry loop stopped. Both leave a silent registry that deserves a verdict.
static void browser_ExpirePendingRegistries( void )
{
	// [rc4l] Let a THROTTLED bar recover on its own, before the early returns below.
	//
	// Nothing ever cleared it: this pass only turned Pending into NoAnswer, so a single
	// REQUESTIGNORED left the status bar orange until some later reply happened to overwrite it --
	// long after the registry had gone back to answering. Reported as a bar stuck on REG_THROTTLED
	// while the browser was working. The rule and its window live in registrystatus_compute.
	for ( size_t i = 0; i < g_ServerRegistryStates.size( ); ++i )
	{
		g_ServerRegistryStates[i].status = zx::AgeRegistryStatus(
			g_ServerRegistryStates[i].status,
			static_cast<int>( I_MSTime( ) - g_ServerRegistryStates[i].ulStatusMS ),
			SERVERREGISTRY_THROTTLE_CLEAR_MS );
	}

	const bool bNoMoreTries = ( g_bWaitingForServerRegistryResponse == false ) ||
		( g_lServerRegistryAttempts >= SERVERREGISTRY_QUERY_MAX_ATTEMPTS );

	if ( bNoMoreTries == false )
		return;

	// Unsigned arithmetic, so this stays correct across the I_MSTime() wrap.
	if (( I_MSTime( ) - g_ulServerRegistryQuerySentMS ) < SERVERREGISTRY_QUERY_TIMEOUT_MS )
		return;

	for ( size_t i = 0; i < g_ServerRegistryStates.size( ); ++i )
	{
		if ( g_ServerRegistryStates[i].status == zx::RegistryStatus::Pending )
			g_ServerRegistryStates[i].status = zx::RegistryStatus::NoAnswer;
	}
}

//*****************************************************************************
//
// [rc4l] Record what one registry did, found by the address the packet came from. A sender we do not
// have a row for is ignored rather than guessed at.
static void browser_NoteRegistryStatus( const NETADDRESS_s &from, zx::RegistryStatus status )
{
	for ( size_t i = 0; i < g_ServerRegistryStates.size( ); ++i )
	{
		if ( g_ServerRegistryStates[i].bResolved && g_ServerRegistryStates[i].address.Compare( from ))
		{
			g_ServerRegistryStates[i].status = status;
			g_ServerRegistryStates[i].ulStatusMS = I_MSTime( );
			return;
		}
	}
}

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
static	void	browser_MirrorAnswerOntoOurOtherRows( LONG lAnswered );
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
bool BROWSER_IsListable( ULONG ulServer )
{
	if ( ulServer >= MAX_BROWSER_SERVERS )
		return ( false );

	const ULONG ulState = g_BrowserServerList[ulServer].ulActiveState;
	if (( ulState != AS_ACTIVE ) && ( ulState != AS_VERSIONMISMATCH ))
		return ( false );

	// [rc4l] One server, one row, hidden only once the v6 row has ANSWERED so a player with no IPv6
	// keeps the row they can actually use.
	if ( g_BrowserServerList[ulServer].bHasGroupPeer && ( g_BrowserServerList[ulServer].Address.bIsIPv6 == false ))
	{
		const LONG lPeer = browser_GetListIDByAddress( g_BrowserServerList[ulServer].GroupPeer );

		if (( lPeer >= 0 ) && g_BrowserServerList[lPeer].Address.bIsIPv6
			&& ( g_BrowserServerList[lPeer].ulActiveState == AS_ACTIVE ))
		{
			return ( false );
		}
	}

	return ( true );
}

//*****************************************************************************
//
zx::VersionRelation BROWSER_GetVersionRelation( ULONG ulServer )
{
	if ( ulServer >= MAX_BROWSER_SERVERS )
		return ( zx::VersionRelation::Unknown );

	return ( g_BrowserServerList[ulServer].versionRelation );
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
//*****************************************************************************
//
void BROWSER_QueryTick( void )
{
	const LONG lNow = I_MSTime( );

	for ( ULONG ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		// [rc4l] Read the row, ask what to do, do it. No judgement lives here any more.
		//
		// It used to be a chain of early returns, and the shape of that chain hid two live bugs in
		// one session: a re-checked row skipped the punch ladder entirely, and a mirrored row could
		// end up in a state no timeout could reach. Both were dispatch, not arithmetic, so no
		// predicate could catch them -- rowstep_compute takes the whole state and answers with the
		// one thing to do, and its tests walk the entire space rather than the cases anyone thought
		// of.
		zx::RowStepIn in;
		in.refreshing       = g_BrowserServerList[ulIdx].bRefreshing;
		in.waitingForReply  = ( g_BrowserServerList[ulIdx].ulActiveState == AS_WAITINGFORREPLY );
		in.lan              = g_BrowserServerList[ulIdx].bLAN;
		in.punchRequested   = g_BrowserServerList[ulIdx].bPunchRequested;
		in.msSinceQuery     = static_cast<int>( lNow - g_BrowserServerList[ulIdx].lMSTime );
		in.msSinceRefresh   = static_cast<int>( lNow - g_BrowserServerList[ulIdx].lRefreshMS );
		in.resends          = static_cast<int>( g_BrowserServerList[ulIdx].lPunchResendsSent );
		in.punchBudgetLeft  = ( g_lPunchesThisSweep < kMaxPunchesPerSweep );
		in.refreshTimeoutMs = SERVERBROWSER_REFRESH_TIMEOUT_MS;
		in.recheckFailures  = static_cast<int>( g_BrowserServerList[ulIdx].lRecheckMisses );
		in.maxRecheckFailures = SERVERBROWSER_MAX_RECHECK_MISSES;

		// A row whose clock never started reads as not-sent, which StepRow treats as "waiting on us".
		if ( g_BrowserServerList[ulIdx].lMSTime <= 0 )
			in.msSinceQuery = 0;
		if ( g_BrowserServerList[ulIdx].lRefreshMS <= 0 )
			in.msSinceRefresh = 0;

		const zx::RowStepOut act = zx::StepRow( in );

		if ( act.requestPunch )
		{
			// Budget is spent only on an ask that actually went out -- PunchRequestFor declines
			// local addresses and registry-less sessions on its own.
			if ( zx::PunchRequestFor( g_BrowserServerList[ulIdx].Address, true ))
			{
				g_BrowserServerList[ulIdx].bPunchRequested = true;
				g_lPunchesThisSweep++;
			}
		}
		else if ( act.resendChallenge )
		{
			// Re-send WITHOUT restamping lMSTime: the ladder is positioned by time since the FIRST
			// challenge, and browser_QueryServer stamps unconditionally.
			const LONG lFirstMS = g_BrowserServerList[ulIdx].lMSTime;
			browser_QueryServer( ulIdx );
			g_BrowserServerList[ulIdx].lMSTime = lFirstMS;
			g_BrowserServerList[ulIdx].lPunchResendsSent++;
		}
		else if ( act.recheckMissed )
		{
			// [rc4l] Stop chasing this one and count the miss. The row stays listed and joinable on
			// what it last told us, which is very probably still true: it answered a moment ago.
			g_BrowserServerList[ulIdx].bRefreshing = false;
			g_BrowserServerList[ulIdx].lRefreshMS = 0;
			g_BrowserServerList[ulIdx].lRecheckMisses++;

			if ( act.dropFromList )
			{
				// Missed its whole allowance in a row. Now it is gone rather than unlucky.
				g_BrowserServerList[ulIdx].lRecheckMisses = 0;
				g_BrowserServerList[ulIdx].ulActiveState = AS_INACTIVE;
			}
		}
		else if ( act.markTimedOut )
		{
			g_BrowserServerList[ulIdx].ulActiveState = AS_TIMEDOUT;

			// [rc4l] Remember it, so the next sweep punches before it challenges.
			if ( g_BrowserServerList[ulIdx].bLAN == false )
				browser_RememberUnreachable( g_BrowserServerList[ulIdx].Address );
		}

		// [rc4l] The held-back first challenge, once the punch has had its head start.
		if ( zx::FirstChallengeDue( g_BrowserServerList[ulIdx].bPunchLed,
				g_BrowserServerList[ulIdx].bFirstChallengeSent,
				static_cast<int>( lNow - g_BrowserServerList[ulIdx].lPunchLedMS )))
		{
			browser_QueryServer( ulIdx );
			g_BrowserServerList[ulIdx].bFirstChallengeSent = true;
		}
	}
}

//*****************************************************************************
//
// [rc4l] Seconds since the last sweep, or -1 if there has never been one.
LONG BROWSER_SecondsSinceRefresh( void )
{
	if ( g_ulLastRefreshMS == 0 )
		return ( -1 );

	return ( static_cast<LONG>(( I_MSTime( ) - g_ulLastRefreshMS ) / 1000 ));
}

//*****************************************************************************
//
// [rc4l] The same answer in milliseconds, for the gate that decides whether REFRESH may be pressed
// again. Seconds are what a player is told; a floor measured in them would be off by up to a second
// in whichever direction happened to suit, which on a ten second floor is a tenth of the rule.
LONG BROWSER_MSSinceRefresh( void )
{
	if ( g_ulLastRefreshMS == 0 )
		return ( -1 );

	return ( static_cast<LONG>( I_MSTime( ) - g_ulLastRefreshMS ));
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

	// [rc4l] The one that answered, since holding two families means [0] is no longer a statement
	// about which of them this client can actually reach.
	out = g_bHaveAnsweringRegistry ? g_LastAnsweringRegistry : g_ServerRegistryAddresses[0];
	return true;
}

//*****************************************************************************
//
// [rc4l] See browser.h. Whether a packet came from a registry this client actually talks to.
//
// Deliberately the WHOLE list, because browser_QueryServerRegistries sends to the whole list. Judging
// a reply by one address, or by the server-side announce cvar, meant we could send to a registry and
// then throw its answer away.
void BROWSER_NoteRegistryAnswered( const NETADDRESS_s &address )
{
	g_LastAnsweringRegistry = address;
	g_bHaveAnsweringRegistry = true;
}

//*****************************************************************************
//
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
	g_ulLastRefreshMS = I_MSTime( );

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
// [rc4l] See browser.h. One row, re-challenged on its own, without troubling the registry.
//
// g_ulLastRefreshMS is deliberately NOT stamped: that clock answers "how old is this LIST", and one
// row having been asked a moment ago says nothing about the other forty. Stamping it here would let
// a player keep the age line reading "just now" by poking a single server, and would spend the
// whole-list floor on work that never went near the registry.
void BROWSER_RecheckServer( ULONG ulServer )
{
	if (( ulServer >= MAX_BROWSER_SERVERS ) ||
		( g_BrowserServerList[ulServer].ulActiveState == AS_INACTIVE ))
	{
		return;
	}

	browser_QueryServer( ulServer );

	g_BrowserServerList[ulServer].bRefreshing = true;
	g_BrowserServerList[ulServer].lRefreshMS = I_MSTime( );

	// The strikes start again. A player asking about this specific row is entitled to an answer
	// about this specific row, not to inherit two misses from a sweep they may not have watched.
	g_BrowserServerList[ulServer].lRecheckMisses = 0;
}

//*****************************************************************************
//
// [rc4l] See browser.h. Only slots that ASKED for a punch are eligible: an unsolicited packet must
// not be able to redirect a row we never invited, and a spoofed knock against an invited row costs
// one wasted re-query and its own timeout -- the same as any dropped packet.
void BROWSER_PunchKnockFrom( const NETADDRESS_s &From )
{
	for ( ULONG ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		// The eligibility rule lives in querypunch_compute, where its security property -- an
		// uninvited row can never be redirected -- is asserted for every state.
		if ( zx::ShouldAdoptPunchKnock(
				g_BrowserServerList[ulIdx].ulActiveState == AS_WAITINGFORREPLY,
				g_BrowserServerList[ulIdx].bPunchRequested,
				g_BrowserServerList[ulIdx].Address.CompareNoPort( From )) == false )
		{
			continue;
		}

		// The knock's source port is the mapping the server's NAT actually opened; the listed port
		// is only what its NAT once told the registry. Adopt the real one.
		g_BrowserServerList[ulIdx].Address.usPort = From.usPort;

		// Re-send into the open hole NOW rather than waiting for the next ladder rung -- the
		// mapping is freshest this instant. Preserve the first-challenge stamp; the ladder's
		// position is measured from it.
		const LONG lFirstMS = g_BrowserServerList[ulIdx].lMSTime;
		browser_QueryServer( ulIdx );
		if ( lFirstMS > 0 )
			g_BrowserServerList[ulIdx].lMSTime = lFirstMS;
		return;
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
		g_BrowserServerList[ulIdx].lRecheckMisses = 0;
		g_BrowserServerList[ulIdx].bHasGroupPeer = false;
		g_BrowserServerList[ulIdx].GroupPeer.Clear();

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
// The port out of "1.2.3.4:5678", or 0 when there is none to read.
static int browser_PortOfAddress( const FString &address )
{
	const long colon = address.LastIndexOf( ":" );
	if ( colon < 0 )
		return 0;

	return atoi( address.GetChars( ) + colon + 1 );
}

// Whether this row is the server we are running, by the same rule the menu uses.
static bool browser_RowIsOurs( LONG lServer, int hostPort, const FString &localIp )
{
	if (( lServer < 0 ) || ( lServer >= MAX_BROWSER_SERVERS ))
		return false;

	const FString full = g_BrowserServerList[lServer].Address.ToString( );
	if ( full.IsEmpty( ))
		return false;

	return zx::RowIsOwnServer( g_BrowserServerList[lServer].Address.ToStringNoPort( ),
		browser_PortOfAddress( full ), hostPort, localIp.GetChars( ),
		zx::ReachProbePublicIp( ));
}

// [rc4l] Give every other row for the SAME MACHINE the answer this one just got.
//
// Only ever our own server, because it is the only machine the browser can prove it knows twice:
// HostOwnsAddress is bound to the server we started. Two unrelated addresses that happen to be one
// box are not something we can detect, and guessing would merge strangers' servers together.
//
// Copied wholesale rather than field by field. The rows describe one process, so anything true of it
// through one address is true through the other, and picking a subset is how they drift.
static void browser_MirrorAnswerOntoOurOtherRows( LONG lAnswered )
{
	if (( lAnswered < 0 ) || ( lAnswered >= MAX_BROWSER_SERVERS ))
		return;

	// HostOwnsAddress is NOT the check to use here. It is bound to the loopback address we join our
	// own server on, so it matches neither the LAN row nor the public one -- a first attempt at this
	// used it and mirrored nothing at all. RowIsOwnServer is the wider question, and the one the menu
	// already asks to decide whether JOIN means "go there" or "you are already here".
	FString localIp;
	if ( NETWORK_GetState( ) != NETSTATE_SINGLE )
		localIp = NETWORK_GetLocalAddress( ).ToStringNoPort( );

	const int hostPort = browser_PortOfAddress( zx::HostConnectAddress( ));
	if ( hostPort <= 0 )
		return;			// we are not hosting, so no row can be ours

	if ( browser_RowIsOurs( lAnswered, hostPort, localIp ) == false )
		return;

	for ( ULONG ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( static_cast<LONG>( ulIdx ) == lAnswered )
			continue;

		// Never touch a slot that is not one of ours, and never one already holding a fresh answer:
		// the row that DID get through this refresh is the better copy.
		if ( g_BrowserServerList[ulIdx].ulActiveState == AS_ACTIVE )
			continue;

		if ( browser_RowIsOurs( static_cast<LONG>( ulIdx ), hostPort, localIp ) == false )
			continue;

		// [rc4l] Three things belong to the ROW, not to the machine, and must survive the copy.
		//
		// The address is how the player joins and which of the two rows this is. bLAN is how it was
		// found, and it decides the badge. The country is derived from the address, so copying it
		// hands the public row the LAN row's answer -- which is no country at all, because a private
		// address cannot be placed. Doing that turned both rows into LAN badges and lost the flag
		// this whole exercise is about.
		const NETADDRESS_s keepAddress = g_BrowserServerList[ulIdx].Address;
		const bool keepLAN = g_BrowserServerList[ulIdx].bLAN;

		// [rc4l] And the row's own re-check bookkeeping, which is what keeps it MORTAL.
		//
		// Copying the answered row's copy of these brought bRefreshing=false with it, and
		// BROWSER_QueryTick only ever expires a row that is refreshing or still waiting. So every
		// row this function touched became permanently active: never re-checked, never removable,
		// still listed long after the server behind it had gone. Changing the host port was enough
		// to strand a pair of them, because the old rows stopped matching "ours" and nothing else
		// could reach them. Keeping the row's own deadline means it stays visible while our server
		// keeps answering, and ages out on the ordinary four second timeout once it stops.
		const bool keepRefreshing = zx::RefreshingAfterMirror(
			g_BrowserServerList[ulIdx].bRefreshing, g_BrowserServerList[lAnswered].bRefreshing );
		const LONG keepRefreshMS = g_BrowserServerList[ulIdx].lRefreshMS;

		g_BrowserServerList[ulIdx] = g_BrowserServerList[lAnswered];
		g_BrowserServerList[ulIdx].Address = keepAddress;
		g_BrowserServerList[ulIdx].bLAN = keepLAN;
		g_BrowserServerList[ulIdx].bRefreshing = keepRefreshing;
		g_BrowserServerList[ulIdx].lRefreshMS = keepRefreshMS;
		g_BrowserServerList[ulIdx].ulActiveState = AS_ACTIVE;

		// Re-derived rather than kept, so a row that has never been placed still gets its flag: the
		// answer depends only on the address, and this row has its own.
		g_BrowserServerList[ulIdx].CountryCode = "";
		g_BrowserServerList[ulIdx].ulCountryIndex =
			NETWORK_GetCountryIndexFromAddress( keepAddress );
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
	const LONG lExisting = browser_GetListIDByAddress( Address );
	if ( lExisting != -1 )
	{
		// [rc4l] A slot that gave up earlier keeps its address, so this dedupe used to eat the
		// registry's RE-announcement of it -- one missed reply window and the server was gone for
		// the whole session, however many refreshes followed. If the registry still lists it, it is
		// still worth asking: re-arm the slot for this sweep's query, with a fresh punch ladder.
		// The rule itself lives in querypunch_compute, where every state is asserted.
		if ( zx::ShouldRearmListedSlot(
				g_BrowserServerList[lExisting].ulActiveState == AS_TIMEDOUT,
				g_BrowserServerList[lExisting].ulActiveState == AS_INACTIVE,
				g_BrowserServerList[lExisting].bRefreshing ))
		{
			g_BrowserServerList[lExisting].ulActiveState = AS_WAITINGFORREPLY;
			g_BrowserServerList[lExisting].lMSTime = 0;
			g_BrowserServerList[lExisting].bPunchRequested = false;
			g_BrowserServerList[lExisting].lPunchResendsSent = 0;

			// [rc4l] The lead state too, or a row that once held a challenge is skipped as
			// already-led and refused as already-sent, and is never challenged again.
			g_BrowserServerList[lExisting].bPunchLed = false;
			g_BrowserServerList[lExisting].bFirstChallengeSent = false;
			g_BrowserServerList[lExisting].lPunchLedMS = 0;
		}
		return;
	}

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
	g_BrowserServerList[ulServer].bPunchRequested = false;
	g_BrowserServerList[ulServer].lPunchResendsSent = 0;

	// A slot arriving here is new, not being re-checked; inheriting a re-check would have this server
	// culled on the previous occupant's deadline.
	g_BrowserServerList[ulServer].bRefreshing = false;
	g_BrowserServerList[ulServer].lRefreshMS = 0;
	g_BrowserServerList[ulServer].lRecheckMisses = 0;

	// Likewise the previous occupant's verdict about a build that has nothing to do with this one.
	g_BrowserServerList[ulServer].bVersionMismatch = false;
	g_BrowserServerList[ulServer].versionRelation = zx::VersionRelation::Unknown;
}

//*****************************************************************************
// [BB] Returns true if the server list packet was terminated by SRSC_ENDSERVERLIST,
// else it returns false.
bool BROWSER_GetServerList( BYTESTREAM_s *pByteStream )
{
	// No longer waiting for a server registry response.
	g_bWaitingForServerRegistryResponse = false;

	// This one answered, whatever the others are doing.
	browser_NoteRegistryStatus( NETWORK_GetFromAddress( ), zx::RegistryStatus::Ok );

	// [rc4l] And it is the family a punch should be asked of, since it is the one proven to reach us.
	BROWSER_NoteRegistryAnswered( NETWORK_GetFromAddress( ));

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

		case SRSC_SERVERGROUP:
			{
				// [rc4l] Believed only from the registry, since claiming to be somebody else's machine
				// is how you would hide their row.
				const int count = pByteStream->ReadByte();

				NETADDRESS_s first;
				for ( int i = 0; i < count; ++i )
				{
					NETADDRESS_s address;
					address.ReadFromStream( pByteStream );

					if ( i == 0 )
						first = address;
					else
						BROWSER_MarkSameServer( first, address );
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

	// [rc4l] It answered, so stop treating it as needing a punch ahead of every challenge. Leaving it
	// remembered would spend the sweep's small budget on a server that is reachable and starve one
	// that is not.
	browser_ForgetUnreachable( g_BrowserServerList[lServer].Address );

	// [rc4l] ONE MACHINE, TWO ROWS, ONE ANSWER.
	//
	// Your own server is in the list twice: LAN discovery finds it at the private address and the
	// registry hands back the public one. Querying both means two launcher challenges arriving at one
	// process from one IP, and a server refuses the second for sv_queryignoretime -- ten seconds, keyed
	// on the address WITHOUT the port, so this is not avoidable by asking from elsewhere.
	//
	// Whichever query lands first wins and the other row goes blank. The LAN path is shorter, so it
	// usually wins, which is why the public row is the one seen flickering in and out on every refresh.
	//
	// The refusal cannot be attributed back to the row that lost, either: a hairpinned reply arrives
	// with the server's LAN source address, so the packet says nothing about which address was asked.
	// That is what makes reacting to the refusal the wrong shape of fix.
	//
	// So the answer is copied instead of asked for twice. One reply describes the machine, and every
	// row pointing at that machine is that machine, whatever address the row is written with.
	browser_MirrorAnswerOntoOurOtherRows( lServer );

	// [rc4l] It answered, so any re-check outstanding against it is settled. Clearing this is what
	// stops BROWSER_QueryTick from dropping a server that replied perfectly well.
	g_BrowserServerList[lServer].bRefreshing = false;
	g_BrowserServerList[lServer].lRefreshMS = 0;

	// The strikes are CONSECUTIVE, so one answer wipes them. A server that misses a datagram now and
	// then must never accumulate its way off the list over an evening of being perfectly reachable.
	g_BrowserServerList[lServer].lRecheckMisses = 0;

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

	// [rc4l] Which way our versions differ, if they do. Listed either way; see AS_VERSIONMISMATCH.
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

		// [rc4l] A comparison rather than the prefix test this used to be. "Different" was one
		// answer where the browser needs three: an OLDER server is one the host must fix and the
		// player cannot, a NEWER one is a destination an update reaches. They sort and read
		// differently, so the direction has to survive to the row.
		const zx::VersionRelation rel = zx::CompareFuaVersions(
			g_BrowserServerList[lServer].Version.GetChars( ), ourVersion.GetChars( ));
		g_BrowserServerList[lServer].versionRelation = rel;

		if ( zx::VersionRelationCanJoin( rel ) == false )
		{
			// Kept for the footer's count, which still explains a list holding no JOINABLE servers.
			g_BrowserServerList[lServer].bVersionMismatch = true;
			// [rc4l] Flagged and KEPT. Draining the packet here dropped the server, so a release made
			// every not-yet-updated server vanish at once -- and an empty browser reads as "nobody is
			// hosting", never as "you are ahead of them". Parsing continues so the row has a name, a
			// player count and a version to show; the state is set after AS_ACTIVE was assigned above,
			// so it stands.
			g_BrowserServerList[lServer].ulActiveState = AS_VERSIONMISMATCH;
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
	g_ServerRegistryStates.clear();

	// Kick a background refresh if the cache is stale. It never blocks and never affects THIS query --
	// a newly fetched list takes effect the next time the browser is opened.
	zx::ServerRegistryList_MaybeRefresh();

	const std::vector<zx::ServerRegistryEntry> entries =
		zx::ServerRegistryList_Resolve( *cl_fua_serverregistry_list );

	for ( size_t i = 0; i < entries.size( ); ++i )
	{
		// [rc4l] Recorded BEFORE the lookup, so an entry that never produces an address still has a
		// row. It used to be skipped outright, which is why "your registry name is wrong" and "there
		// are no servers" looked identical on screen.
		SERVERREGISTRYSTATE_s state;
		state.host = entries[i].host;
		state.port = 0;
		state.bResolved = false;
		state.status = zx::RegistryStatus::LookupFailed;
		state.ulStatusMS = I_MSTime( );

		NETADDRESS_s address;
		if ( address.LoadFromString( entries[i].host.c_str( )) == false )
		{
			Printf( "Warning: can't resolve server registry \"%s\" -- skipping it.\n", entries[i].host.c_str( ));
			g_ServerRegistryStates.push_back( state );
			continue;
		}

		// [rc4l] The parser already split any ":port" off the host, so an entry either carries its own
		// port or takes the default. LoadFromString never sees a port here.
		address.SetPort( entries[i].port != 0 ? static_cast<USHORT>( entries[i].port ) : g_usServerRegistryPort );

		// [rc4l] And the AAAA, because LoadFromString above asks only for an A record -- which is why
		// a client with no IPv4 at all could not reach any registry and saw an empty browser.
		NETADDRESS_s v6;
		if ( zx::ResolveHostV6( entries[i].host.c_str( ), v6 ))
		{
			v6.SetPort( entries[i].port != 0 ? static_cast<USHORT>( entries[i].port ) : g_usServerRegistryPort );
			g_ServerRegistryAddresses.Push( v6 );
		}

		state.address = address;
		state.bResolved = true;
		state.port = ( entries[i].port != 0 ) ? entries[i].port : static_cast<int>( g_usServerRegistryPort );
		state.status = zx::RegistryStatus::Pending;

		g_ServerRegistryStates.push_back( state );
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

	// [rc4l] Say we understand grouping, opt-in because the list is positional and an older client
	// would read the new opcode as an address.
	g_ServerRegistryBuffer.ByteStream.WriteByte( 1 );

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

	// [rc4l] Stamped here as well as in BROWSER_RefreshListedServers, because a sweep can start with
	// either. The menu does both together, but the background query at startup asks the registry on
	// its own -- and if that registry never answers, the per-server pass never runs, so without this
	// a client that had been trying for a minute would still say "never refreshed". We looked; that
	// is what the line reports.
	g_ulLastRefreshMS = I_MSTime( );

	// We are currently waiting to hear back from the server registries.
	g_bWaitingForServerRegistryResponse = true;
	g_lServerRegistryAttempts = 0;
	g_lPunchesThisSweep = 0;	// [rc4l] fresh refresh, fresh punch budget
	browser_SetPendingRegistryStates( );

	browser_SendServerRegistryQuery();
}

//*****************************************************************************
//
// [rc4l] Ask the registries once, a few seconds in, so the browser opens onto a list instead of
// starting the conversation when you get there.
//
// The delay is jittered off the launch clock rather than a random number: every copy of the game
// starting at once would otherwise land on the registry together, and its flood queues are small.
static LONG g_lBackgroundQueryAtMS = 0;
static bool g_bBackgroundQuerySent = false;

void BROWSER_BackgroundTick( void )
{
	const LONG lNow = I_MSTime( );

	if ( g_lBackgroundQueryAtMS == 0 )
		g_lBackgroundQueryAtMS = lNow + 3000 + ( lNow % 4000 );

	if (( g_bBackgroundQuerySent == false ) && ( lNow >= g_lBackgroundQueryAtMS ))
	{
		g_bBackgroundQuerySent = true;

		// A server does not browse, and a client already has the list it is playing on.
		if ( NETWORK_GetState( ) == NETSTATE_SINGLE )
			BROWSER_QueryServerRegistry( );
	}

	// These used to be pumped only by the browser menu's Ticker, so a reply that arrived while the
	// menu was shut had nothing to retry it or time it out. One pump, always running.
	BROWSER_ServerRegistryTick( );
	BROWSER_QueryTick( );
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
	// [rc4l] Registry bars age out on their own clock, BEFORE the early return below.
	//
	// g_bWaitingForServerRegistryResponse is one flag for the whole fan-out, and it clears the moment
	// any single registry answers. Hanging the expiry off it meant that with several configured, the
	// first reply stopped the clock for everybody: a dead registry sat on "waiting for an answer"
	// forever and its bar never went red. Caught with a real list of three, one of them 127.0.0.1.
	browser_ExpirePendingRegistries( );

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

	// browser_ExpirePendingRegistries above has already given the silent ones their verdict.
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
void BROWSER_ServerRegistryRefusedQuery( zx::RegistryStatus why )
{
	g_bWaitingForServerRegistryResponse = false;

	// [rc4l] Against the address it came from, so with several registries configured the bar that
	// turns red is the one that actually refused. The refusal codes are the registry's own words, and
	// until now they reached a Printf and nothing else.
	browser_NoteRegistryStatus( NETWORK_GetFromAddress( ), why );
}

//*****************************************************************************
//
// [rc4l] BEING IGNORED IS PROOF THE SERVER IS THERE.
//
// A server refuses a second launcher query from the same IP within sv_queryignoretime, ten seconds
// by default, and the refusal is keyed on the ADDRESS WITHOUT THE PORT. Your own server is therefore
// guaranteed to trip it: the browser knows that machine twice, once on the LAN address and once on
// the public one the registry hands back, and both queries reach the same process from the same IP.
// One is answered and the other is refused, and which one wins is a race between a local hop and a
// round trip through the router.
//
// The refusal used to reach a Printf and nothing else, so the losing row stayed AS_INACTIVE from the
// refresh that had just deactivated everything, and vanished. That is the flicker: host a server,
// open the browser repeatedly, and watch your own entry come and go depending on which query landed
// first. The LAN row usually wins because its path is shorter, which is exactly why the public row is
// the one people notice disappearing.
//
// The packet itself is the evidence. A server that is gone sends nothing; a server that sends
// "I am ignoring you" has told us it is alive, running, and reachable at this address. Treating that
// as absence throws away the one thing it proves. So the row goes back to what we last knew, which is
// the same bargain the rest of the browser makes: a moment-old truth beats a fresh blank.
void BROWSER_ServerSaidItIsIgnoringUs( const NETADDRESS_s &Address )
{
	const LONG lServer = browser_GetListIDByAddress( Address );
	if ( lServer == -1 )
		return;

	// AS_WAITINGFORREPLY is the state a queried row is really in: the query set it, and the refresh's
	// deactivation only moves ACTIVE to INACTIVE, so it never touches a row already out for reply.
	// Checking only for INACTIVE is the mistake that made a first attempt at this fix do nothing at
	// all, which is worth naming because both states look equally plausible from the call site.
	//
	// Only ever back to ACTIVE, and only for a row we have really seen answer before. A server we
	// have never had data for has nothing to restore, and drawing it from an empty slot would put a
	// blank row on screen on the strength of a refusal.
	const ULONG state = g_BrowserServerList[lServer].ulActiveState;

	if (( state == AS_INACTIVE ) || ( state == AS_WAITINGFORREPLY ))
	{
		if ( g_BrowserServerList[lServer].HostName.IsNotEmpty( ))
			g_BrowserServerList[lServer].ulActiveState = AS_ACTIVE;
	}
}

//*****************************************************************************
//
bool BROWSER_GetServerRegistryStatus( unsigned int index, std::string &host, int &port,
	zx::RegistryStatus &status )
{
	if ( index >= g_ServerRegistryStates.size( ))
		return false;

	host = g_ServerRegistryStates[index].host;
	port = g_ServerRegistryStates[index].port;
	status = g_ServerRegistryStates[index].status;
	return true;
}

//*****************************************************************************
//
unsigned int BROWSER_GetServerRegistryCount( void )
{
	return static_cast<unsigned int>( g_ServerRegistryStates.size( ));
}

//*****************************************************************************
//
bool BROWSER_WaitingForServerRegistryResponse( void )
{
	return ( g_bWaitingForServerRegistryResponse );
}

//*****************************************************************************
//
void BROWSER_MarkSameServer( const NETADDRESS_s &First, const NETADDRESS_s &Second )
{
	const LONG lFirst = browser_GetListIDByAddress( First );
	const LONG lSecond = browser_GetListIDByAddress( Second );

	// [rc4l] Both rows must exist, and IsListable re-checks the peer rather than trusting the flag.
	if (( lFirst < 0 ) || ( lSecond < 0 ) || ( lFirst == lSecond ))
		return;

	// [rc4l] Both directions, since either row may be asked about first.
	g_BrowserServerList[lFirst].bHasGroupPeer = true;
	g_BrowserServerList[lFirst].GroupPeer = Second;
	g_BrowserServerList[lSecond].bHasGroupPeer = true;
	g_BrowserServerList[lSecond].GroupPeer = First;
}

//*****************************************************************************
//
void BROWSER_PunchBrokered( void )
{
	// [rc4l] Every held row, because the verdict carries no address and a sweep holds at most four.
	for ( ULONG ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if (( g_BrowserServerList[ulIdx].ulActiveState != AS_WAITINGFORREPLY )
			|| ( g_BrowserServerList[ulIdx].bPunchLed == false )
			|| g_BrowserServerList[ulIdx].bFirstChallengeSent )
		{
			continue;
		}

		browser_QueryServer( ulIdx );
		g_BrowserServerList[ulIdx].bFirstChallengeSent = true;
	}
}

//*****************************************************************************
//
void BROWSER_QueryAllServers( void )
{
	ULONG	ulIdx;

	g_lPunchesThisSweep = 0;	// [rc4l] fresh sweep, fresh punch budget

	for ( ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( g_BrowserServerList[ulIdx].ulActiveState != AS_WAITINGFORREPLY )
			continue;

		g_BrowserServerList[ulIdx].bPunchLed = false;
		g_BrowserServerList[ulIdx].bFirstChallengeSent = false;
		g_BrowserServerList[ulIdx].lPunchLedMS = 0;
	}

	// [rc4l] Two passes so the small budget goes to rows known to need a punch before speculative
	// ones, or the first four rows in the list spend it regardless.
	for ( int pass = 0; pass < 2; ++pass )
	{
		for ( ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
		{
			if (( g_BrowserServerList[ulIdx].ulActiveState != AS_WAITINGFORREPLY )
				|| g_BrowserServerList[ulIdx].bPunchLed )
			{
				continue;
			}

			const bool bBudget = ( g_lPunchesThisSweep < kMaxPunchesPerSweep );
			const bool bWant = ( pass == 0 )
				? zx::ShouldPunchBeforeFirstChallenge( g_BrowserServerList[ulIdx].bLAN,
					browser_IsKnownUnreachable( g_BrowserServerList[ulIdx].Address ), bBudget )
				: zx::ShouldPunchOnFirstContact( g_BrowserServerList[ulIdx].bLAN, bBudget );

			if ( bWant == false )
				continue;

			if ( zx::PunchRequestFor( g_BrowserServerList[ulIdx].Address, true ) == false )
				continue;

			g_BrowserServerList[ulIdx].bPunchRequested = true;
			g_BrowserServerList[ulIdx].bPunchLed = true;
			g_BrowserServerList[ulIdx].lPunchLedMS = I_MSTime( );
			g_lPunchesThisSweep++;
		}
	}

	// [rc4l] Everything that did not lead speaks now, exactly as it always did.
	for ( ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if (( g_BrowserServerList[ulIdx].ulActiveState != AS_WAITINGFORREPLY )
			|| g_BrowserServerList[ulIdx].bPunchLed )
		{
			continue;
		}

		browser_QueryServer( ulIdx );
		g_BrowserServerList[ulIdx].bFirstChallengeSent = true;
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
