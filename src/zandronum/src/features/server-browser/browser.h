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
// Date created:  11/26/03
//
//
// Filename: browser.h
//
// Description: Contains browser structures and prototypes
//
//-----------------------------------------------------------------------------

#ifndef __BROWSER_H__
#define __BROWSER_H__

#include "gamemode.h"
#include "network.h"

//*****************************************************************************
//  DEFINES

// Maximum number of servers listed in the browser.
#define		MAX_BROWSER_SERVERS		1024

// [rc4l] No usable country for this server: it sent no code, or sent one we could not place. Drawing
// treats it as "no flag, no text" rather than guessing.
#define		COUNTRY_INDEX_UNKNOWN	0xFFFFFFFF

//*****************************************************************************
// [rc4l] The states below AS_ACTIVE are ours. Existing values keep their numbers so nothing that
// compares against AS_ACTIVE shifts meaning.
//
// Three states could not describe what a browser needs to say. "Never asked", "asked and waiting",
// "asked and it never answered" and "answered with nonsense" all collapsed into not-AS_ACTIVE, so a
// server that failed was indistinguishable from one still loading -- and the UI could only show
// nothing, with no way to explain itself. Doomseeker models the same distinctions (RESPONSE_TIMEOUT
// vs RESPONSE_BAD vs still refreshing) for exactly this reason.
enum
{
	AS_INACTIVE,			// never asked, or cleared
	AS_WAITINGFORREPLY,		// asked, still within the reply window
	AS_ACTIVE,				// answered, data is good

	AS_TIMEDOUT,			// asked, gave up waiting -- listed but unreachable
	AS_BADRESPONSE,			// answered, but we could not make sense of it

	NUM_ACTIVESTATES
};

//*****************************************************************************
//	STRUCTURES

//*****************************************************************************
typedef struct
{
	// Player's name.
	FString		Name;

	// Fragcount.
	LONG		lFragcount;

	// Ping.
	LONG		lPing;

	// Spectating?
	bool		bSpectating;

	// Is a bot?
	bool		bIsBot;

} SERVERPLAYER_t;

//*****************************************************************************
typedef struct
{
	// What's the state of this server's activity?
	ULONG			ulActiveState;

	// Network address of this server.
	NETADDRESS_s	Address;

	// Name of the server.
	FString			HostName;

	// Website URL of the wad the server is using.
	FString			WadURL;

	// Host's email address.
	FString			EmailAddress;

	// Mapname of the level the server is currently on.
	FString			Mapname;

	// [rc4l] Alpha-3 country code as the server reported it ("USA", "GBR", "LAN"), and the GeoIP id
	// that indexes the CTRYFLAG sheet. The wire carries the code; the index is resolved once on
	// receipt so drawing never has to do a lookup. Index is COUNTRY_INDEX_UNKNOWN when we could not
	// place the code, which is normal -- an old server may not send one at all.
	FString			CountryCode;
	ULONG			ulCountryIndex;

	// Maximum number of players that can join the server.
	LONG			lMaxClients;

	// Names of each PWAD the server is using.
	// [SB] Converted to a TArray.
	TArray<FString>	PWADNames;

	// [rc4l] MD5 of each PWAD, as the server reports it (SQF2_PWAD_HASHES), parallel to PWADNames and
	// empty for a server that did not send them. Lets a download be checked against what the server
	// actually has, rather than trusting that a mirror served the right file under the right name.
	// MD5 because that is what the protocol carries -- see features/wad-download/zx_filehash.h.
	TArray<FString>	PWADHashes;

	// [rc4l] The server's gameplay flag words, as SQF_ALL_DMFLAGS sends them: dmflags, dmflags2,
	// zadmflags, compatflags, zacompatflags, compatflags2. Stored as a list rather than six named
	// fields because the wire format is length-prefixed on purpose -- a newer engine can add a
	// seventh, and a client that assumed six would silently drop it.
	TArray<int>		DMFlags;

	// Name of the IWAD being used.
	FString			IWADName;

	// Game mode of the server.
	GAMEMODE_e		GameMode;

	// Number of players on the server.
	LONG			lNumPlayers;

	// Player's playing on the server.
	SERVERPLAYER_t	Players[MAXPLAYERS];

	// [rc4l] Did the last response actually carry per-player data? We always ask for it, but a server
	// is free not to send it -- and without it the bot flags in Players[] are stale from whatever was
	// there before, so "count the humans" would be counting rubbish.
	bool			bHasPlayerData;

	// Version of the server.
	FString			Version;

	// Was this server broadcasted to us on a LAN?
	bool			bLAN;

	// MS time of when we queried this server.
	LONG			lMSTime;

	// Ping to this server.
	LONG			lPing;

	// [SB] Names of the server's current gamemode.
	FString			GameModeName;
	FString			GameModeShortName;

	// [rc4l] Where this server will serve its own WADs from (SQF2_FUA_DIRECT_DOWNLOAD). 0 means it
	// will not -- either an older server that never sent the field, or one with serving turned off.
	// The host is the address we queried, never anything the server told us: a server that could
	// nominate a download host could point every joiner at a third party's machine.
	USHORT			usDirectDownloadPort;

	// [rc4l] The operator would rather clients tried public mirrors before this server. Only a hint,
	// and only about ordering -- the file is served either way.
	bool			bPrefersMirrors;

	// [rc4l] Whether a password is needed to get in at all, and whether one is needed to join the
	// game once connected. The browser's Public/Private tabs treat either as private.
	bool			bForcePassword;
	bool			bForceJoinPassword;

	// [rc4l] MD5 of the IWAD this server runs (SQF2_FUA_IWAD_HASH), or "" from a server that did not
	// send it. SQF_IWAD carries only a name, and doom2.wad has shipped as nine-odd different builds
	// that are not interchangeable -- so the name alone cannot tell you whether the copy you own is
	// the one that will pass level authentication. Empty means "cannot tell", never "matches".
	FString			IWADHash;

} SERVER_t;

//*****************************************************************************
//	PROTOTYPES

void			BROWSER_Construct( void );
void			BROWSER_Destruct( void );

bool			BROWSER_IsActive( ULONG ulServer );
bool			BROWSER_IsLAN( ULONG ulServer );
NETADDRESS_s	BROWSER_GetAddress( ULONG ulServer );
const char		*BROWSER_GetHostName( ULONG ulServer );
const char		*BROWSER_GetWadURL( ULONG ulServer );
const char		*BROWSER_GetEmailAddress( ULONG ulServer );
const char		*BROWSER_GetMapname( ULONG ulServer );
LONG			BROWSER_GetMaxClients( ULONG ulServer );
LONG			BROWSER_GetNumPWADs( ULONG ulServer );
const char		*BROWSER_GetPWADName( ULONG ulServer, ULONG ulWadIdx );
// [rc4l] "" when the server sent no hashes, which is normal -- older servers and any server that
// chose not to. Callers must treat empty as "cannot check", never as "checked and fine".
const char		*BROWSER_GetPWADHash( ULONG ulServer, ULONG ulWadIdx );
// [rc4l] Gameplay flag words. Count is 0 for a server that did not send them (anything older than
// SQF_ALL_DMFLAGS, or one that chose not to) -- which must read as "unknown", never as "none set".
LONG			BROWSER_GetNumDMFlags( ULONG ulServer );
int				BROWSER_GetDMFlag( ULONG ulServer, ULONG ulIdx );
const char		*BROWSER_GetIWADName( ULONG ulServer );
GAMEMODE_e		BROWSER_GetGameMode( ULONG ulServer );
LONG			BROWSER_GetNumPlayers( ULONG ulServer );
// [rc4l] Humans only -- BROWSER_GetNumPlayers() counts bots, and a browser that advertises "8/8" for
// a server holding seven bots and one person is lying about the only number a player reads it for.
// SQF_PLAYERDATA is already requested, so the bot flag is there for the asking.
LONG			BROWSER_GetNumHumanPlayers( ULONG ulServer );
const char		*BROWSER_GetPlayerName( ULONG ulServer, ULONG ulPlayer );
LONG			BROWSER_GetPlayerFragcount( ULONG ulServer, ULONG ulPlayer );
LONG			BROWSER_GetPlayerPing( ULONG ulServer, ULONG ulPlayer );
LONG			BROWSER_GetPlayerSpectating( ULONG ulServer, ULONG ulPlayer );
LONG			BROWSER_GetPing( ULONG ulServer );
const char		*BROWSER_GetVersion( ULONG ulServer );
// [rc4l] Alpha-3 code ("USA"), or "" when the server sent none. Always safe to draw as text, which is
// what the browser falls back to when the CTRYFLAG sheet is absent from the game data.
const char		*BROWSER_GetCountryCode( ULONG ulServer );
// [rc4l] Index into the CTRYFLAG sheet, or COUNTRY_INDEX_UNKNOWN.
ULONG			BROWSER_GetCountryIndex( ULONG ulServer );
const char		*BROWSER_GetGameModeName( ULONG ulServer ); // [SB]
const char		*BROWSER_GetGameModeShortName( ULONG ulServer ); // [SB]

// [rc4l] "http://<address>:<port>/" for a server that serves its own WADs, or "" for one that does
// not. Built from the address we queried rather than anything the server sent.
FString			BROWSER_GetDirectDownloadURL( ULONG ulServer );

// [rc4l] Whether that URL should be tried after the public mirrors rather than before them.
bool			BROWSER_PrefersMirrors( ULONG ulServer );

// [rc4l] MD5 of the IWAD build this server runs, or "" when it did not say. Never NULL.
const char		*BROWSER_GetIWADHash( ULONG ulServer );

// [rc4l] Whether getting in needs a password of either kind. What the Public/Private tabs sort on.
bool			BROWSER_IsPasswordProtected( ULONG ulServer );

// [rc4l] The raw state, for callers that need to tell the failure modes apart rather than just
// "drawable or not". BROWSER_IsActive() answers the narrower question.
ULONG			BROWSER_GetActiveState( ULONG ulServer );

// [rc4l] Age out servers that were asked and never answered. Call once per tic while a browser is
// open. Without this they sit in AS_WAITINGFORREPLY forever and the UI can never stop loading.
void			BROWSER_QueryTick( void );

// [rc4l] Which server the player has picked. Lives here rather than inside a menu so that more than
// one menu can share it -- the join command reads it, and previously it was a file-static that only
// the old browser could reach.
void			BROWSER_SetSelectedServer( LONG lServer );
LONG			BROWSER_GetSelectedServer( void );

void			BROWSER_ClearServerList( void );
void			BROWSER_DeactivateAllServers( void );
bool			BROWSER_GetServerList( BYTESTREAM_s *pByteStream );
void			BROWSER_ParseServerQuery( BYTESTREAM_s *pByteStream, bool bLAN );

// [rc4l] One piece of a reply too big for a single datagram. Rebuilds it and, once whole, hands it to
// BROWSER_ParseServerQuery. See features/launcher-protocol for the wire format.
void			BROWSER_ParseServerQuerySegment( BYTESTREAM_s *pByteStream, bool bLAN );
void			BROWSER_QueryServerRegistry( void );
// [rc4l] Drives the query retry/give-up clock; call once per tic while the browser is open.
void			BROWSER_ServerRegistryTick( void );
// [rc4l] The registry answered but refused; stop retrying (see browser.cpp).
void			BROWSER_ServerRegistryRefusedQuery( void );
bool			BROWSER_WaitingForServerRegistryResponse( void );
void			BROWSER_QueryAllServers( void );
LONG			BROWSER_CalcNumServers( void );

//*****************************************************************************
//	EXTERNAL CONSOLE VARIABLES

#endif // __BROWSER_H__
