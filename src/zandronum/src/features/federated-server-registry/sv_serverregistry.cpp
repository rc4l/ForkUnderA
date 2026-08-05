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
// File created:  8/27/03
//
//
// Filename: sv_serverregistry.cpp
//
// Description: Server-to-ServerRegistry and Server-to-Launcher protocol.
//
//-----------------------------------------------------------------------------

#ifndef _WIN32
#include <sys/utsname.h>
#endif
#include <map>
#include <cmath>
#include "networkheaders.h"
#include "c_dispatch.h"
#include "cooperative.h"
#include "deathmatch.h"
#include "doomstat.h"
#include "d_player.h"
#include "duel.h"
#include "g_game.h"
#include "gamemode.h"
#include "gi.h"
#include "g_level.h"
#include "i_system.h"
#include "lastmanstanding.h"
#include "team.h"
#include "network.h"
#include "sv_main.h"
#include "sv_ban.h"
#include "version.h"
#include "d_dehacked.h"
#include "v_text.h"
#include "voicechat.h"

// [SB] This is easier than updating the parameters for a load of functions every time I want to add something.
struct LauncherResponseContext
{
	BYTESTREAM_s *pByteStream;
	// Corrected flags.
	ULONG ulFlags, ulFlags2;
};

using LauncherFieldFunction = void(*)(const LauncherResponseContext &);

//--------------------------------------------------------------------------------------------------------------------------------------------------
//-- VARIABLES -------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------

// [rc4l] The ONE server registry this server announces to. Singular on purpose.
//
// Federation happens on the CLIENT: a browser queries many registries and unions the results
// (cl_fua_serverregistry_list), so a server is discoverable network-wide without announcing to more
// than one. Authority does not compose the way discovery does -- a registry pushes a ban list to the
// servers listed on it, and two registries pushing to the same server has no correct resolution:
// merge them and listing somewhere silently grants it authority over your server; take the last to
// arrive and which bans apply becomes a race between unsolicited UDP pushes on independent timers.
//
// One registry per server means one authority per server, which is a deal an operator can actually
// reason about: you accept the rules of the registry you chose, and moving is repointing one CVAR.
static	NETADDRESS_s		g_AddressServerRegistry;

// Message buffer for sending messages to the server registry.
static	NETBUFFER_s			g_ServerRegistryBuffer;
static	NETBUFFER_s			g_SegmentBuffer;

// Port the server registry is located on.
static	USHORT				g_usServerRegistryPort;

// List of IP address that this server has been queried by recently.
static	STORED_QUERY_IP_s	g_StoredQueryIPs[MAX_STORED_QUERY_IPS];

static	LONG				g_lStoredQueryIPHead;
static	LONG				g_lStoredQueryIPTail;
static	TArray<int>			g_OptionalWadIndices;

extern	NETADDRESS_s		g_LocalAddress;

FString g_VersionWithOS;

//*****************************************************************************
//	CONSOLE VARIABLES

//--------------------------------------------------------------------------------------------------------------------------------------------------
//-- FUNCTIONS -------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------
static void server_registry_WriteName( const LauncherResponseContext &ctx )
{
	// [rc4l] The name goes out WITH its colour codes. Upstream stripped them here ([AK]'s
	// V_RemoveColorCodes), because launchers of the day rendered the escape byte as a control
	// character rather than a colour -- Doomseeker still has no handling for them at all, so a
	// coloured name shows up there as boxes.
	//
	// We send them anyway. An operator who writes "\cdColourful" meant it to be colourful, and the
	// browser this engine ships renders it; the cost is that ZandroX servers look odd in a launcher
	// we are not building for. FFont::StringWidth already skips escapes when measuring, so the only
	// thing that needed care on the reading side was truncation -- see
	// features/server-browser/computation/colortext_compute.h.
	FString hostname = sv_hostname.GetGenericRep( CVAR_String ).String;
	V_ColorizeString( hostname );

	ctx.pByteStream->WriteString( hostname );
}

//*****************************************************************************
//
static void server_registry_WriteURL( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteString( sv_website );
}

//*****************************************************************************
//
static void server_registry_WriteEmail( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteString( sv_hostemail );
}

//*****************************************************************************
//
static void server_registry_WriteMapName( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteString( level.MapName.GetChars() );
}

//*****************************************************************************
//
static void server_registry_WriteMaxClients( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteByte( sv_maxclients );
}

//*****************************************************************************
//
static void server_registry_WriteMaxPlayers( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteByte( sv_maxplayers );
}

//*****************************************************************************
//
static void server_registry_WritePWADs( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteByte( NETWORK_GetPWADList().Size( ));

	for ( unsigned i = 0; i < NETWORK_GetPWADList().Size(); ++i )
		ctx.pByteStream->WriteString( NETWORK_GetPWADList()[i].name );
}

//*****************************************************************************
//
static void server_registry_WriteGameType( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteByte( GAMEMODE_GetCurrentMode( ));
	ctx.pByteStream->WriteByte( instagib );
	ctx.pByteStream->WriteByte( buckshot );
}

//*****************************************************************************
//
static void server_registry_WriteGameName( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteString( SERVER_SERVERREGISTRY_GetGameName( ));
}

//*****************************************************************************
//
static void server_registry_WriteIWAD( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteString( NETWORK_GetIWAD( ));
}

//*****************************************************************************
//
static void server_registry_WriteForcePassword( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteByte( sv_forcepassword );
}

//*****************************************************************************
//
static void server_registry_WriteForceJoinPassword( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteByte( sv_forcejoinpassword );
}

//*****************************************************************************
//
static void server_registry_WriteGameSkill( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteByte( gameskill );
}

//*****************************************************************************
//
static void server_registry_WriteBotSkill( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteByte( botskill );
}

//*****************************************************************************
//
static void server_registry_WriteDMFlags( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteLong( dmflags );
	ctx.pByteStream->WriteLong( dmflags2 );
	ctx.pByteStream->WriteLong( compatflags );
}

//*****************************************************************************
//
static void server_registry_WriteLimits( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteShort( fraglimit );
	ctx.pByteStream->WriteShort( static_cast<SHORT>(timelimit) );
	// [BB] We have to base the decision on whether to send "time left" on the same rounded
	// timelimit value we just sent to the client.
	if ( static_cast<SHORT>(timelimit) )
	{
		LONG	lTimeLeft;

		lTimeLeft = (LONG)( timelimit - ( level.time / ( TICRATE * 60 )));
		if ( lTimeLeft < 0 )
			lTimeLeft = 0;
		ctx.pByteStream->WriteShort( lTimeLeft );
	}
	ctx.pByteStream->WriteShort( duellimit );
	ctx.pByteStream->WriteShort( pointlimit );
	ctx.pByteStream->WriteShort( winlimit );
}

//*****************************************************************************
//
static void server_registry_WriteTeamDamage( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteFloat( teamdamage );
}

//*****************************************************************************
// [CW] This command is now deprecated as there are now more than two teams.
// Send the team scores.
static void server_registry_WriteTeamScores( const LauncherResponseContext &ctx )
{
	for ( ULONG ulIdx = 0; ulIdx < 2; ulIdx++ )
	{
		if ( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSEARNFRAGS )
			ctx.pByteStream->WriteShort( TEAM_GetFragCount( ulIdx ));
		else if ( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSEARNWINS )
			ctx.pByteStream->WriteShort( TEAM_GetWinCount( ulIdx ));
		else
			ctx.pByteStream->WriteShort( TEAM_GetPointCount( ulIdx ));
	}
}

//*****************************************************************************
//
static void server_registry_WriteNumPlayers( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteByte( SERVER_CountPlayers( true ));
}

//*****************************************************************************
//
static void server_registry_WritePlayerData( const LauncherResponseContext &ctx )
{
	for ( ULONG ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
	{
		if ( playeringame[ulIdx] == false )
			continue;

		ctx.pByteStream->WriteString( players[ulIdx].userinfo.GetName() );
		if ( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSEARNPOINTS )
			ctx.pByteStream->WriteShort( players[ulIdx].lPointCount );
		else if ( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSEARNWINS )
			ctx.pByteStream->WriteShort( players[ulIdx].ulWins );
		else if ( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSEARNFRAGS )
			ctx.pByteStream->WriteShort( players[ulIdx].fragcount );
		else
			ctx.pByteStream->WriteShort( players[ulIdx].killcount );

		ctx.pByteStream->WriteShort( players[ulIdx].ulPing );
		ctx.pByteStream->WriteByte( PLAYER_IsTrueSpectator( &players[ulIdx] ));
		ctx.pByteStream->WriteByte( players[ulIdx].bIsBot );

		if ( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSONTEAMS )
		{
			if ( players[ulIdx].bOnTeam == false )
				ctx.pByteStream->WriteByte( 255 );
			else
				ctx.pByteStream->WriteByte( players[ulIdx].Team );
		}

		ctx.pByteStream->WriteByte( players[ulIdx].ulTime / ( TICRATE * 60 ));
	}
}

//*****************************************************************************
//
static void server_registry_WriteTeamInfoNumber( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteByte( TEAM_GetNumAvailableTeams( ));
}

//*****************************************************************************
//
static void server_registry_WriteTeamInfoName( const LauncherResponseContext &ctx )
{
	for ( ULONG ulIdx = 0; ulIdx < TEAM_GetNumAvailableTeams( ); ulIdx++ )
		ctx.pByteStream->WriteString( TEAM_GetName( ulIdx ));
}

//*****************************************************************************
//
static void server_registry_WriteTeamInfoColor( const LauncherResponseContext &ctx )
{
	for ( ULONG ulIdx = 0; ulIdx < TEAM_GetNumAvailableTeams( ); ulIdx++ )
		ctx.pByteStream->WriteLong( TEAM_GetColor( ulIdx ));
}

//*****************************************************************************
//
static void server_registry_WriteTeamInfoScore( const LauncherResponseContext &ctx )
{
	for ( ULONG ulIdx = 0; ulIdx < TEAM_GetNumAvailableTeams( ); ulIdx++ )
	{
		if ( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSEARNFRAGS )
			ctx.pByteStream->WriteShort( TEAM_GetFragCount( ulIdx ));
		else if ( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSEARNWINS )
			ctx.pByteStream->WriteShort( TEAM_GetWinCount( ulIdx ));
		else
			ctx.pByteStream->WriteShort( TEAM_GetPointCount( ulIdx ));
	}
}

//*****************************************************************************
// [BB] Testing server and what's the binary name?
static void server_registry_WriteTestingServer( const LauncherResponseContext &ctx )
{
#if ( BUILD_ID == BUILD_RELEASE )
	ctx.pByteStream->WriteByte( 0 );
	ctx.pByteStream->WriteString( "" );
#else
	ctx.pByteStream->WriteByte( 1 );
	// [BB] Name of the testing binary archive found in http://zandronum.com/
	FString testingBinary;
	testingBinary.Format ( "downloads/testing/%s/ZandroDev%s-%swindows.zip", GAMEVER_STRING, GAMEVER_STRING, GetGitTime() );
	ctx.pByteStream->WriteString( testingBinary.GetChars() );
#endif
}

//*****************************************************************************
// [BB] We don't have a mandatory main data file anymore, so just send an empty string.
static void server_registry_WriteDataMD5Sum( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteString( "" );
}

//*****************************************************************************
// [BB] Send all dmflags and compatflags.
static void server_registry_WriteAllDMFlags( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteByte( 6 );
	ctx.pByteStream->WriteLong( dmflags );
	ctx.pByteStream->WriteLong( dmflags2 );
	ctx.pByteStream->WriteLong( zadmflags );
	ctx.pByteStream->WriteLong( compatflags );
	ctx.pByteStream->WriteLong( zacompatflags );
	ctx.pByteStream->WriteLong( compatflags2 );
}

//*****************************************************************************
// [BB] Send special security settings like sv_fua_serverregistry_enforcebans.
static void server_registry_WriteSecuritySettings( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteByte( sv_fua_serverregistry_enforcebans );
}

//*****************************************************************************
// [TP] Send optional wad indices.
static void server_registry_WriteOptionalWADs( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteByte( g_OptionalWadIndices.Size() );

	for ( unsigned i = 0; i < g_OptionalWadIndices.Size(); ++i )
		ctx.pByteStream->WriteByte( g_OptionalWadIndices[i] );
}

//*****************************************************************************
// [TP] Send deh patches
static void server_registry_WriteDEH( const LauncherResponseContext &ctx )
{
	const TArray<FString>& names = D_GetDehFileNames();
	ctx.pByteStream->WriteByte( names.Size() );

	for ( unsigned i = 0; i < names.Size(); ++i )
		ctx.pByteStream->WriteString( names[i] );
}

//*****************************************************************************
// [SB] This now just sends the flags; the actual extended fields are handled by the packet assembly code
static void server_registry_WriteExtendedInfo( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteLong( ctx.ulFlags2 );
}

//*****************************************************************************
// [SB] send MD5 hashes of PWADs
static void server_registry_WritePWADHashes( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteByte( NETWORK_GetPWADList().Size( ) );

	for ( unsigned i = 0; i < NETWORK_GetPWADList().Size(); ++i )
		ctx.pByteStream->WriteString( NETWORK_GetPWADList()[i].checksum );
}

//*****************************************************************************
// [SB] send the server's country code
static void server_registry_WriteCountry( const LauncherResponseContext &ctx )
{
	// [SB] The value of this field will always be 3 characters, so we can just use and
	// send a char[3]
	const size_t codeSize = 3;
	char code[codeSize];

	FString countryCode = sv_country.GetGenericRep( CVAR_String ).String;
	countryCode.ToUpper();

	// [SB] ISO 3166-1 alpha-3 codes in the range XAA-XZZ will never be allocated to actual
	// countries. Therefore, we use these for our special codes:
	//     XIP  -  launcher should try and use IP geolocation
	//     XUN  -  launcher should display a generic unknown flag

	if ( countryCode.CompareNoCase( "automatic" ) == 0 )
	{
		memcpy( code, "XIP", codeSize );
	}
	// [SB] We assume any 3 character long value is a valid country code, and leave it up
	// the launcher to check if they have a flag for it
	else if ( countryCode.Len() == 3 )
	{
		memcpy( code, countryCode.GetChars(), codeSize );
	}
	// [SB] Any other value results in the "unknown" value
	else
	{
		memcpy( code, "XUN", codeSize );
	}

	ctx.pByteStream->WriteBuffer( code, codeSize );
}

//*****************************************************************************
// [SB] Send the current game mode's name and short name.
static void server_registry_WriteGameModeName( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteString( GAMEMODE_GetName( GAMEMODE_GetCurrentMode() ));
}

static void server_registry_WriteGameModeShortName( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteString( GAMEMODE_GetShortName( GAMEMODE_GetCurrentMode() ));
}

//*****************************************************************************
// Send voice chat setting
static void server_registry_WriteVoicechat( const LauncherResponseContext &ctx )
{
	ctx.pByteStream->WriteByte( sv_allowvoicechat );
}

//*****************************************************************************
// [SB] And now the big maps of functions.
static const std::map<ULONG, LauncherFieldFunction> ResponseFunctions[] =
{
	{
		{ SQF_NAME,					server_registry_WriteName },
		{ SQF_URL,					server_registry_WriteURL },
		{ SQF_EMAIL,				server_registry_WriteEmail },
		{ SQF_MAPNAME,				server_registry_WriteMapName },
		{ SQF_MAXCLIENTS,			server_registry_WriteMaxClients },
		{ SQF_MAXPLAYERS,			server_registry_WriteMaxPlayers },
		{ SQF_PWADS,				server_registry_WritePWADs },
		{ SQF_GAMETYPE,				server_registry_WriteGameType },
		{ SQF_GAMENAME,				server_registry_WriteGameName },
		{ SQF_IWAD,					server_registry_WriteIWAD },
		{ SQF_FORCEPASSWORD,		server_registry_WriteForcePassword },
		{ SQF_FORCEJOINPASSWORD,	server_registry_WriteForceJoinPassword },
		{ SQF_GAMESKILL,			server_registry_WriteGameSkill },
		{ SQF_BOTSKILL,				server_registry_WriteBotSkill },
		{ SQF_DMFLAGS,				server_registry_WriteDMFlags },
		{ SQF_LIMITS,				server_registry_WriteLimits },
		{ SQF_TEAMDAMAGE,			server_registry_WriteTeamDamage },
		{ SQF_TEAMSCORES,			server_registry_WriteTeamScores },
		{ SQF_NUMPLAYERS,			server_registry_WriteNumPlayers },
		{ SQF_PLAYERDATA,			server_registry_WritePlayerData },
		{ SQF_TEAMINFO_NUMBER,		server_registry_WriteTeamInfoNumber },
		{ SQF_TEAMINFO_NAME,		server_registry_WriteTeamInfoName },
		{ SQF_TEAMINFO_COLOR,		server_registry_WriteTeamInfoColor },
		{ SQF_TEAMINFO_SCORE,		server_registry_WriteTeamInfoScore },
		{ SQF_TESTING_SERVER,		server_registry_WriteTestingServer },
		{ SQF_DATA_MD5SUM,			server_registry_WriteDataMD5Sum },
		{ SQF_ALL_DMFLAGS,			server_registry_WriteAllDMFlags },
		{ SQF_SECURITY_SETTINGS,	server_registry_WriteSecuritySettings },
		{ SQF_OPTIONAL_WADS,		server_registry_WriteOptionalWADs },
		{ SQF_DEH,					server_registry_WriteDEH },
		{ SQF_EXTENDED_INFO,		server_registry_WriteExtendedInfo },
	},

	{
		{ SQF2_PWAD_HASHES,			server_registry_WritePWADHashes },
		{ SQF2_COUNTRY,				server_registry_WriteCountry },
		{ SQF2_GAMEMODE_NAME,		server_registry_WriteGameModeName },
		{ SQF2_GAMEMODE_SHORTNAME,	server_registry_WriteGameModeShortName },
		{ SQF2_VOICECHAT,			server_registry_WriteVoicechat },
	}
};

//*****************************************************************************
//
void SERVER_SERVERREGISTRY_Construct( void )
{
	const char *pszPort;

	// Setup our message buffer.
	g_ServerRegistryBuffer.Init( MAX_UDP_PACKET, BUFFERTYPE_WRITE );

	// [SB] Buffer for assembling segments.
	g_SegmentBuffer.Init( MAX_UDP_PACKET, BUFFERTYPE_WRITE );

	// Allow the user to specify which port the server registry is on.
	pszPort = Args->CheckValue( "-serverregistryport" );
    if ( pszPort )
    {
       g_usServerRegistryPort = atoi( pszPort );
       Printf( PRINT_HIGH, "Alternate server registry port: %d.\n", g_usServerRegistryPort );
    }
	else 
	   g_usServerRegistryPort = DEFAULT_SERVERREGISTRY_PORT;

	g_lStoredQueryIPHead = 0;
	g_lStoredQueryIPTail = 0;

#ifndef _WIN32
	struct utsname u_name;
	if ( uname(&u_name) < 0 )
		g_VersionWithOS.Format ( "%s", GetFuaVersionTag() ); //error, no data
	else
		g_VersionWithOS.Format ( "%s on %s %s", GetFuaVersionTag(), u_name.sysname, u_name.release ); // "Linux 2.6.32.5-amd64" or "FreeBSD 9.0-RELEASE" etc
#endif

	// [TP] Which wads will we broadcast as optional to launchers?
	for( unsigned i = 0; i < NETWORK_GetPWADList().Size(); ++i )
	{
		if ( Wads.IsWadOptional( NETWORK_GetPWADList()[i].wadnum ))
			g_OptionalWadIndices.Push( i );
	}

	// Call SERVER_SERVERREGISTRY_Destruct() when Skulltag closes.
	atterm( SERVER_SERVERREGISTRY_Destruct );
}

//*****************************************************************************
//
void SERVER_SERVERREGISTRY_Destruct( void )
{
	// Free our local buffer.
	g_ServerRegistryBuffer.Free();
}

//*****************************************************************************
//
void SERVER_SERVERREGISTRY_Tick( void )
{
	while (( g_lStoredQueryIPHead != g_lStoredQueryIPTail ) && ( gametic >= g_StoredQueryIPs[g_lStoredQueryIPHead].lNextAllowedGametic ))
	{
		g_lStoredQueryIPHead++;
		g_lStoredQueryIPHead = g_lStoredQueryIPHead % MAX_STORED_QUERY_IPS;
	}

	// Send an update to the server registry every 30 seconds.
	if ( gametic % ( TICRATE * 30 ))
		return;

	// User doesn't wish to update the server registry.
	if ( sv_fua_serverregistry_announce == false )
		return;

	g_ServerRegistryBuffer.Clear();

	// [BB] If we can't find the registry address, we can't announce to it.
	if ( g_AddressServerRegistry.LoadFromString( fua_serverregistry_host ) == false )
	{
		Printf( "Warning: Can't find fua_serverregistry_host %s! Either correct fua_serverregistry_host or set sv_fua_serverregistry_announce to false.\n", *fua_serverregistry_host );
		return;
	}

	// [rc4l] Only default the port if the entry did not carry one.
	if ( g_AddressServerRegistry.usPort == 0 )
		g_AddressServerRegistry.SetPort( g_usServerRegistryPort );

	// Write to our packet a challenge to the server registry.
	g_ServerRegistryBuffer.ByteStream.WriteLong( SERVER_SERVERREGISTRY_CHALLENGE );
	// [BB] Also send a string that will allow us to verify that a registry banlist was actually sent from the registry.
	g_ServerRegistryBuffer.ByteStream.WriteString( SERVER_GetServerRegistryBanlistVerificationString().GetChars() );
	// [BB] Also tell the registry whether we are enforcing its ban list.
	g_ServerRegistryBuffer.ByteStream.WriteByte( sv_fua_serverregistry_enforcebans );
	// [BB] And tell which code revision number the server was built with.
	g_ServerRegistryBuffer.ByteStream.WriteLong( GetRevisionNumber() );

	// Send the server registry our packet.
	NETWORK_LaunchPacket( &g_ServerRegistryBuffer, g_AddressServerRegistry );
}

//*****************************************************************************
//
void SERVER_SERVERREGISTRY_Broadcast( void )
{
	// Send an update to the server registry every second.
	if ( gametic % TICRATE )
		return;

	// User doesn't wish to broadcast this server.
	if (( sv_broadcast == false ) || ( Args->CheckParm( "-nobroadcast" )))
		return;

//	g_ServerRegistryBuffer.Clear();

	sockaddr_in broadcast_addr;
	broadcast_addr.sin_family = AF_INET;
	broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;
	broadcast_addr.sin_port = htons( DEFAULT_BROADCAST_PORT );
	NETADDRESS_s AddressBroadcast;
	AddressBroadcast.LoadFromSocketAddress( reinterpret_cast<const sockaddr&> ( broadcast_addr ) );

	// [BB] Under all Windows versions broadcasts to INADDR_BROADCAST seem to work fine
	// while class A broadcasts don't work under Vista/7. So just use INADDR_BROADCAST.
#ifndef _WIN32
	// [BB] Based on the local adress, we find out the class
	// of the network, we are in and set the broadcast address
	// accordingly. Broadcasts to INADDR_BROADCAST = 255.255.255.255
	// should be circumvented if possible and is seem that they
	// aren't	even permitted in the Linux kernel at all.
	// If the server has the ip A.B.C.D depending on the class
	// broadcasts should go to:
	// Class A: A.255.255.255
	// Class B: A. B .255.255
	// Class C: A. B . C .255
	// 
	// Class A comprises networks 1.0.0.0 through 127.0.0.0. The network number is contained in the first octet.
	// Class B contains networks 128.0.0.0 through 191.255.0.0; the network number is in the first two octets.
	// Class C networks range from 192.0.0.0 through 223.255.255.0, with the network number contained in the first three octets.

	int classIndex = 0;

	const int locIP0 = g_LocalAddress.abIP[0];
	if ( (locIP0 >= 1) && (locIP0 <= 127) )
		classIndex = 1;
	else if ( (locIP0 >= 128 ) && (locIP0 <= 191) )
		classIndex = 2;
	else if ( (locIP0 >= 192 ) && (locIP0 <= 223) )
		classIndex = 3;

	for( int i = 0; i < classIndex; i++ )
		AddressBroadcast.abIP[i] = g_LocalAddress.abIP[i];
#endif

	// Broadcast our packet.
	SERVER_SERVERREGISTRY_SendServerInfo( AddressBroadcast, SQF_ALL, 0, SQF2_ALL, true, false );
//	NETWORK_WriteLong( &g_ServerRegistryBuffer, SERVERREGISTRY_CHALLENGE );
//	NETWORK_LaunchPacket( g_ServerRegistryBuffer, AddressBroadcast, true );
}

//*****************************************************************************
//
void SERVER_SERVERREGISTRY_SendServerInfo( NETADDRESS_s Address, ULONG ulFlags, ULONG ulTime, ULONG ulFlags2, bool bBroadcasting, bool bSegmentedResponse )
{
	IPStringArray szAddress;
	ULONG		ulIdx;
	ULONG		ulBits;
	ULONG 		ulBits2 = 0;

	// Let's just use the server registry buffer! It gets cleared again when we need it anyway!
	g_ServerRegistryBuffer.Clear();

	if ( bBroadcasting == false )
	{
		// First, check to see if we've been queried by this address recently.
		if ( g_lStoredQueryIPHead != g_lStoredQueryIPTail )
		{
			ulIdx = g_lStoredQueryIPHead;
			while ( ulIdx != (ULONG)g_lStoredQueryIPTail )
			{
				// Check to see if this IP exists in our stored query IP list. If it does, then
				// ignore it, since it queried us less than 10 seconds ago.
				if ( Address.CompareNoPort( g_StoredQueryIPs[ulIdx].Address ))
				{
					// Write our header.
					g_ServerRegistryBuffer.ByteStream.WriteLong( SERVER_LAUNCHER_IGNORING );

					// Send the time the launcher sent to us.
					g_ServerRegistryBuffer.ByteStream.WriteLong( ulTime );

					// Send the packet.
	//				NETWORK_LaunchPacket( &g_ServerRegistryBuffer, Address, true );
					NETWORK_LaunchPacket( &g_ServerRegistryBuffer, Address );

					if ( sv_showlauncherqueries )
						Printf( "Ignored IP launcher challenge.\n" );

					// Nothing more to do here.
					return;
				}

				ulIdx++;
				ulIdx = ulIdx % MAX_STORED_QUERY_IPS;
			}
		}
	
		// Now, check to see if this IP has been banend from this server.
		szAddress.SetFrom ( Address );
		if ( SERVERBAN_IsIPBanned( szAddress ))
		{
			// Write our header.
			g_ServerRegistryBuffer.ByteStream.WriteLong( SERVER_LAUNCHER_BANNED );

			// Send the time the launcher sent to us.
			g_ServerRegistryBuffer.ByteStream.WriteLong( ulTime );

			// Send the packet.
			NETWORK_LaunchPacket( &g_ServerRegistryBuffer, Address );

			if ( sv_showlauncherqueries )
				Printf( "Denied BANNED IP launcher challenge.\n" );

			// Nothing more to do here.
			return;
		}

		// This IP didn't exist in the list. and it wasn't banned. 
		// So, add it, and keep it there for 10 seconds.
		g_StoredQueryIPs[g_lStoredQueryIPTail].Address = Address;
		g_StoredQueryIPs[g_lStoredQueryIPTail].lNextAllowedGametic = gametic + ( TICRATE * ( sv_queryignoretime ));

		g_lStoredQueryIPTail++;
		g_lStoredQueryIPTail = g_lStoredQueryIPTail % MAX_STORED_QUERY_IPS;
		if ( g_lStoredQueryIPTail == g_lStoredQueryIPHead )
			Printf( "SERVER_SERVERREGISTRY_SendServerInfo: WARNING! g_lStoredQueryIPTail == g_lStoredQueryIPHead\n" );
	}

	// Write our header.
	// [SB] But skip the response code in the segmented response as it's unneeded.
	if ( !bSegmentedResponse )
	{
		g_ServerRegistryBuffer.ByteStream.WriteLong( SERVER_LAUNCHER_CHALLENGE );
	}

	// Send the time the launcher sent to us.
	g_ServerRegistryBuffer.ByteStream.WriteLong( ulTime );

	// Send our version. [K6] ...with OS
	g_ServerRegistryBuffer.ByteStream.WriteString( g_VersionWithOS.GetChars() );

	// Send the information about the data that will be sent.
	ulBits = ulFlags;

	// [BB] Remove all unknown flags from our answer.
	ulBits &= SQF_ALL;

	// If the launcher desires to know the team damage, but we're not in a game mode where
	// team damage applies, then don't send back team damage information.
	if (( teamplay || teamgame || teamlms || teampossession || (( deathmatch == false ) && ( teamgame == false ))) == false )
	{
		if ( ulBits & SQF_TEAMDAMAGE )
			ulBits &= ~SQF_TEAMDAMAGE;
	}

	// If the launcher desires to know the team score, but we're not in a game mode where
	// teams have scores, then don't send back team score information.
	if (( GAMEMODE_GetCurrentFlags() & GMF_PLAYERSONTEAMS ) == false )
		ulBits &= ~( SQF_TEAMSCORES | SQF_TEAMINFO_NUMBER | SQF_TEAMINFO_NAME | SQF_TEAMINFO_COLOR | SQF_TEAMINFO_SCORE );

	// If the launcher wants to know player data, then we have to tell them how many players
	// are in the server.
	if ( ulBits & SQF_PLAYERDATA )
		ulBits |= SQF_NUMPLAYERS;

	// [TP] Don't send optional wads if there isn't any.
	if ( g_OptionalWadIndices.Size() == 0 )
		ulBits &= ~SQF_OPTIONAL_WADS;

	// [TP] Don't send deh files if there aren't any.
	if ( D_GetDehFileNames().Size() == 0 )
		ulBits &= ~SQF_DEH;

	// [SB] Validate the extended flags
	if ( ulBits & SQF_EXTENDED_INFO )
	{
		ulBits2 = ulFlags2;
		ulBits2 &= SQF2_ALL;

		// [SB] If there are no extended flags to return, don't send any extended info
		if ( ulBits2 == 0 )
			ulBits &= ~SQF_EXTENDED_INFO;
	}

	const ULONG flags[] = { ulBits, ulBits2 }; // [SB] The bits for each field set we'll be sending.
	ULONG ulCurrentSetNum = 0; // [SB] Current field set. 0 -> SQF_, 1 -> SQF2_
	const LauncherResponseContext ctx{ &g_ServerRegistryBuffer.ByteStream, ulBits, ulBits2 };

	g_ServerRegistryBuffer.ByteStream.WriteLong( ulBits );

	// [SB] Reworked the packet assembly logic so that it tests each field and calls the relevant function,
	// instead of being a giant list of bit-testing if statements.
	for ( ULONG ulBit = 0; ulBit < 32; )
	{
		const ULONG ulCurrentSetValue = flags[ulCurrentSetNum];
		const ULONG ulField = 1U << ulBit;

		if ( ulCurrentSetValue & ulField )
		{
			const auto &map = ResponseFunctions[ulCurrentSetNum];

			if ( map.count( ulField ) )
			{
				const auto pFunction = map.at( ulField );
				pFunction( ctx );
			}
		}

		// [SB] We exhausted all the bits in this set.
		if ( ulBit == 31 )
		{
			// [SB] Move onto the next set of fields, if there is one.
			if ( ulCurrentSetNum < countof( flags ) - 1 )
			{
				ulBit = 0;
				ulCurrentSetNum++;
			}
			else
			{
				// [SB] Nothing more we can send.
				break;
			}
		}
		else
		{
			ulBit++;
		}
	}

	// [SB] Handle a segmented response.
	if ( bSegmentedResponse )
	{
		// [SB] Size of the segment header, as written in the loop below.
		constexpr LONG segmentHeaderSize = 12;

		const LONG sourceBufferSize = g_ServerRegistryBuffer.CalcSize();
		const LONG segmentMaxSize = static_cast<LONG>( sv_maxpacketsize ) - segmentHeaderSize;
		const LONG numSegments = static_cast<LONG>( std::ceil( static_cast<double>( sourceBufferSize ) / static_cast<double>( segmentMaxSize ) ) );

		LONG segmentNumber = 0;
		LONG offset = 0;

		// [SB] Now assemble segments until we've exhausted the buffer.
		while ( offset < sourceBufferSize )
		{
			// [SB] (std::min) prevents macro expansion of min.
			const LONG readSize = (std::min)( segmentMaxSize, sourceBufferSize - offset );

			g_SegmentBuffer.Clear();

			// [SB] segmentHeaderSize must be equal to the byte size of this header, including the challenge.
			g_SegmentBuffer.ByteStream.WriteLong( SERVER_LAUNCHER_CHALLENGE_SEGMENTED );
			g_SegmentBuffer.ByteStream.WriteByte( segmentNumber );
			g_SegmentBuffer.ByteStream.WriteByte( numSegments );
			g_SegmentBuffer.ByteStream.WriteShort( offset );
			g_SegmentBuffer.ByteStream.WriteShort( readSize );
			g_SegmentBuffer.ByteStream.WriteShort( sourceBufferSize );

			// [SB] Read from the server registry buffer directly into the segment buffer.
			memcpy( g_SegmentBuffer.ByteStream.pbStream, g_ServerRegistryBuffer.pbData + offset, readSize );
			offset += readSize;
			g_SegmentBuffer.ByteStream.pbStream += readSize;

			NETWORK_LaunchPacket( &g_SegmentBuffer, Address );
			segmentNumber++;
		}
	}
	else
	{
		NETWORK_LaunchPacket( &g_ServerRegistryBuffer, Address );
	}
}

//*****************************************************************************
//
const char *SERVER_SERVERREGISTRY_GetGameName( void )
{	
	switch ( gameinfo.gametype )
	{
	case GAME_Doom:

		if ( !(gameinfo.flags & GI_MAPxx) )
			return ( "DOOM" );
		else
			return ( "DOOM II" );
		break;
	case GAME_Heretic:

		return ( "Heretic" );
		break;
	case GAME_Hexen:

		return ( "Hexen" );
		break;
	default:
		
		return ( "ERROR!" );
		break;
	}
}

//*****************************************************************************
//
// [rc4l] Is this packet from OUR registry? Named as a question rather than exposing the address,
// because every caller is asking the same trust question: it gates whether an incoming ban list is
// accepted (sv_main.cpp). Handing out the address invited callers to compare it themselves, which is
// how you end up with a check that is subtly wrong in one of two places.
bool SERVER_SERVERREGISTRY_IsAddress( const NETADDRESS_s &Address )
{
	return Address.Compare( g_AddressServerRegistry );
}

//*****************************************************************************
//
void SERVER_SERVERREGISTRY_HandleVerificationRequest( BYTESTREAM_s *pByteStream  )
{
	LONG lVerificationNumber = pByteStream->ReadLong();

	g_ServerRegistryBuffer.Clear();
	g_ServerRegistryBuffer.ByteStream.WriteLong( SERVER_SERVERREGISTRY_VERIFICATION );
	g_ServerRegistryBuffer.ByteStream.WriteString( SERVER_GetServerRegistryBanlistVerificationString().GetChars() );
	g_ServerRegistryBuffer.ByteStream.WriteLong( lVerificationNumber );

	// [rc4l] Answer the registry that ASKED, not a canonical one. With a federation, replying to the
	// wrong registry means the one waiting on us never sees a verification and hides this server.
	// Safe to trust the sender here: the caller only reaches this inside the
	// SERVER_SERVERREGISTRY_IsAddress() check in sv_main.cpp.
	NETWORK_LaunchPacket( &g_ServerRegistryBuffer, NETWORK_GetFromAddress() );
}

//*****************************************************************************
//
void SERVER_SERVERREGISTRY_SendBanlistReceipt ( void )
{
	g_ServerRegistryBuffer.Clear();
	g_ServerRegistryBuffer.ByteStream.WriteLong( SERVER_SERVERREGISTRY_BANLIST_RECEIPT );
	g_ServerRegistryBuffer.ByteStream.WriteString( SERVER_GetServerRegistryBanlistVerificationString().GetChars() );

	// [rc4l] Acknowledge to the registry whose ban list we just received. Sending the receipt to a
	// different one leaves the sender believing we ignored it -- and a registry that hides
	// ban-ignoring servers would then hide us. Sender is already verified by the caller.
	NETWORK_LaunchPacket( &g_ServerRegistryBuffer, NETWORK_GetFromAddress() );
}

//--------------------------------------------------------------------------------------------------------------------------------------------------
//-- CONSOLE ---------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------

// [BB] Unless all the declarations of the SERVERCONSOLE_* functions are moved to a platfrom independent header,
// we need to keep those awful declarations everywhere.
void SERVERCONSOLE_UpdateBroadcasting( void );
void SERVERCONSOLE_UpdateTitleString( const char *pszString );
// Should the server inform the server registry of its existence?
CUSTOM_CVAR( Bool, sv_fua_serverregistry_announce, true, CVAR_SERVERINFO|CVAR_NOSETBYACS )
{
	SERVERCONSOLE_UpdateBroadcasting( );
}

// Should the server broadcast so LAN clients can hear it?
CUSTOM_CVAR( Bool, sv_broadcast, true, CVAR_ARCHIVE|CVAR_NOSETBYACS )
{
	SERVERCONSOLE_UpdateBroadcasting( );
}

// Name of this server on launchers.
CUSTOM_CVAR( String, sv_hostname, "Unnamed " GAMENAME " server", CVAR_ARCHIVE|CVAR_NOSETBYACS|CVAR_SERVERINFO )
{
	FString tempHostname = self.GetGenericRep( CVAR_String ).String;
	FString cleanedHostname;

	// [AK] Uncolorize the string, just in case, before we clean it up.
	V_UnColorizeString( tempHostname );

	// [AK] Remove any unacceptable characters from the string.
	for ( unsigned int i = 0; i < tempHostname.Len( ); i++ )
	{
		// [AK] Don't accept undisplayable system ASCII.
		if ( tempHostname[i] <= 31 )
			continue;

		// [AK] Don't accept escape codes unless they're used before color codes (e.g. '\c').
		if (( tempHostname[i] == 92 ) && (( i >= tempHostname.Len( ) - 1 ) || ( tempHostname[i+1] != 'c' )))
			continue;

		cleanedHostname += tempHostname[i];
	}

	// [AK] Truncate incredibly long hostnames. Whatever limit that we allow should be more than enough.
	cleanedHostname.Truncate( MAX_HOSTNAME_LENGTH );

	// [AK] Finally, remove any trailing crap from the cleaned hostname string.
	V_RemoveTrailingCrapFromFString( cleanedHostname );

	// [AK] If the string is empty, then there was only crap. Reset sv_hostname back to default.
	// Likewise, if the string is different from the original, set sv_hostname to the cleaned string.
	if ( cleanedHostname.IsEmpty( ))
	{
		self.ResetToDefault( );
		return;
	}
	else if ( tempHostname.Compare( cleanedHostname ) != 0 )
	{
		self = cleanedHostname;
		return;
	}

	SERVERCONSOLE_UpdateTitleString( (const char *)self );

	// [AK] Notify the clients about the new hostname.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_SetCVar( self );
}

// Website that has the wad this server is using, possibly with other info.
CVAR( String, sv_website, "", CVAR_ARCHIVE|CVAR_NOSETBYACS|CVAR_SERVERINFO )

// E-mail address of the person running this server.
CVAR( String, sv_hostemail, "", CVAR_ARCHIVE|CVAR_NOSETBYACS|CVAR_SERVERINFO )

// [SB] The country in which this server is located.
CVAR( String, sv_country, "automatic", CVAR_ARCHIVE|CVAR_NOSETBYACS|CVAR_SERVERINFO )

// IP address of the server registry.
// [BB] Client and server use this now, therefore the name doesn't begin with "sv_"
// [rc4l] The ONE registry this server announces to. Not a list: see g_AddressServerRegistry for
// why authority is kept singular while discovery federates on the client
// (cl_fua_serverregistry_list).
//
// Deliberately no longer master.zandronum.com: this engine announces to its own network. Point it
// back if you want to appear on theirs.
CVAR( String, fua_serverregistry_host, "registry.cantstopscrolling.net", CVAR_ARCHIVE|CVAR_GLOBALCONFIG|CVAR_NOSETBYACS )

CCMD( wads )
{
	Printf( "IWAD: %s\n", NETWORK_GetIWAD( ) );
	Printf( "Num PWADs: %d\n", NETWORK_GetPWADList().Size() );

	for ( unsigned int i = 0; i < NETWORK_GetPWADList().Size(); ++i )
	{
		const NetworkPWAD& pwad = NETWORK_GetPWADList()[i];
		Printf( "PWAD: %s - %s%s\n", pwad.name.GetChars(), pwad.checksum.GetChars(),
			( Wads.IsWadOptional( pwad.wadnum ) ? " (optional)" : "" ));
	}
}
