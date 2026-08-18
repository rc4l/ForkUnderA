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

#include "features/server-browser/computation/registrystatus_compute.h"
#include "features/server-browser/computation/versionrelation_compute.h"

#include <string>

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
	// [rc4l] Answered perfectly, and not one we can play on: its engine version is not ours. Listed
	// rather than hidden, because "the servers vanished" and "the servers are down" look identical
	// from the outside, and a release used to empty the browser for everyone who had not updated.
	AS_VERSIONMISMATCH,		// answered, but built from a different engine version

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

	// [rc4l] Size in bytes of each PWAD (SQF2_FUA_WAD_SIZES), parallel to PWADNames and empty for a
	// server that did not send them. 0 for a file the server could not measure -- which reads the same
	// as "not sent", deliberately: both mean we cannot say how big it is, and neither is a small file.
	TArray<unsigned int>	PWADSizes;

	// [rc4l] And the IWAD's, which is never downloaded but is listed beside them -- one line lacking
	// the number every other line has reads as a bug, not as a distinction.
	unsigned int	IWADSize;

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

	// [rc4l] A re-query sent while this server was ALREADY listed, so the row stays on screen and
	// stays joinable while it is checked. lRefreshMS is when the packet went out, or 0 for "queued,
	// not sent yet"; both only mean anything while bRefreshing.
	//
	// This is what lets the browser reopen without emptying itself. The ordinary states cannot carry
	// it: AS_WAITINGFORREPLY is how a server that has never answered is drawn, so reusing it for a
	// re-check would make every known server vanish for four seconds -- which is the exact problem
	// this exists to remove.
	bool			bRefreshing;
	LONG			lRefreshMS;

	// [rc4l] How many re-checks in a row this server has failed to answer. Reset to zero the moment
	// it answers anything. A row is only dropped once this passes the limit in browser.cpp, because
	// a single unanswered datagram is ordinary packet loss and used to delete a live server.
	LONG			lRecheckMisses;

	// [rc4l] Punch-on-query state (computation/querypunch_compute.h). A registry-listed server
	// behind carrier NAT drops our direct query, so after a moment the browser asks the registry to
	// have it punch toward us and re-sends the challenge into the hole. These carry that ladder's
	// position between tics; both only mean anything while AS_WAITINGFORREPLY.
	bool			bPunchRequested;
	LONG			lPunchResendsSent;

	// [rc4l] This row punched before it spoke, because a challenge sent first is tracked by the host's
	// router even as it drops it and takes the very tuple the punch then needs.
	bool			bPunchLed;
	bool			bFirstChallengeSent;
	LONG			lPunchLedMS;

	// [rc4l] The other address this server is listed under, believed only from the registry because
	// claiming to be somebody else's machine is how you would hide their row.
	bool			bHasGroupPeer;
	NETADDRESS_s	GroupPeer;

	// [rc4l] This server answered, and we hid it because it runs a different build.
	//
	// Kept separately because the hiding is done by setting AS_INACTIVE, which is also what an empty
	// slot looks like -- so once hidden, a real server that replied is indistinguishable from a slot
	// nobody ever used. That made the list drop servers with no count, no message and no way to tell
	// "nobody is hosting" from "everyone here is on another version".
	bool			bVersionMismatch;
	// [rc4l] WHICH WAY the mismatch goes, which the boolean cannot say. Older means the host has not
	// updated and the player can do nothing; newer means we have not, and an update reaches it. The
	// two sort and read differently, so the browser needs the direction and not just the fact.
	zx::VersionRelation	versionRelation;

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
// [rc4l] Everything worth DRAWING, which is wider than what can be joined: a server on another
// engine version answered us perfectly, it just is not one we can play on today.
bool			BROWSER_IsListable( ULONG ulServer );
zx::VersionRelation	BROWSER_GetVersionRelation( ULONG ulServer );
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

// [rc4l] Size in bytes, or 0 when the server did not say. See PWADSizes.
unsigned int	BROWSER_GetPWADSize( ULONG ulServer, ULONG ulWadIdx );
unsigned int	BROWSER_GetIWADSize( ULONG ulServer );
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
// [rc4l] One resolved server registry to talk to, or false when none could be resolved.
//
// Exposed for the reachability probe, which needs somewhere to ask "can you reach me?" and has no
// business re-resolving a list the browser has already built. First entry rather than all of them:
// one answer is the answer, and asking several would only mean several strangers sending unsolicited
// packets at a port we opened for two seconds.
bool			BROWSER_GetServerRegistryAddress( NETADDRESS_s &out );

// [rc4l] Whether a reply came from a registry this client actually queries.
//
// The receive path used to judge that by fua_serverregistry_host, which is the SERVER's announce
// target and a different setting entirely. Point the two at different hosts, as any local-registry
// test does, and every reply from the real registry was silently dropped: the reachability cookie
// never arrived and the INTERNET option stayed white with nothing to say why.
bool			BROWSER_IsServerRegistryAddress( const NETADDRESS_s &address );

// [rc4l] True if we have an outstanding launcher query to this address. Lets the client's packet
// loop tell a launcher reply from game traffic when both arrive from the server we are playing on.
bool			BROWSER_IsAwaitingReplyFrom( const NETADDRESS_s &Address );

// [rc4l] How many servers answered and were then hidden for running a different build.
LONG			BROWSER_CountVersionMismatched( void );

// [rc4l] True while any server is being re-checked or a registry query is outstanding. Lets the
// refresh button show the work that otherwise happens invisibly.
bool			BROWSER_IsRefreshInFlight( void );

// [rc4l] Re-check every server already on the list, WITHOUT emptying it.
//
// Opening the browser used to clear the list and query from nothing, so the player watched a spinner
// before they could click anything -- including a server they had just downloaded files for and were
// only coming back to join. The rows are still good the moment the menu opens; they are simply not
// known to be current, which is a reason to verify them, not to hide them.
//
// Each carried-over server is re-queried at its own address, so culling a dead one does not wait on
// the registry: it fails its own check and drops out on its own timeout.
void			BROWSER_RefreshListedServers( void );

// [rc4l] Re-check ONE listed server, at its own address, without asking the registry anything.
//
// A whole-list sweep is a poor answer to "is that one still there": it costs a packet per row and a
// registry request, so it has to be rationed, and the rationing then stands between the player and
// the single question they actually asked. This is the cheap version, and being cheap is why it
// does not need a floor of its own.
void			BROWSER_RecheckServer( ULONG ulServer );

// [rc4l] A punch packet knocked on our socket. The server we asked the registry to punch sends its
// packets from whatever public port ITS NAT hands out -- under endpoint-dependent (carrier) NAT
// that is a DIFFERENT port from the one the registry listed, so the challenges we aim at the
// listed port keep missing. The knock's source is the server's real, open endpoint: re-aim the
// waiting slot at it and re-send the challenge immediately. Joins then use the same corrected
// address, which is the one that actually works.
void			BROWSER_PunchKnockFrom( const NETADDRESS_s &From );

// [rc4l] Send the held challenges now, while the server's punch is in flight, because whichever
// packet lands first takes the tuple the other one needs.
void			BROWSER_PunchBrokered( void );

// [rc4l] The registry saying two addresses are one server, which only it can know and which nothing
// here may guess at.
void			BROWSER_MarkSameServer( const NETADDRESS_s &First, const NETADDRESS_s &Second );

// [rc4l] Per-row version of the fact BROWSER_GetNumHumanPlayers already uses in aggregate.
bool			BROWSER_IsPlayerBot( ULONG ulServer, ULONG ulPlayer );
// [rc4l] Did the server send player rows at all? A server that withheld them and one that is empty
// both report zero names, and they do not mean the same thing.
bool			BROWSER_HasPlayerData( ULONG ulServer );
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
// [rc4l] Runs every frame whether or not the browser is open: asks the registries once, a few
// seconds into the session, and pumps the clocks that were previously only pumped by the menu.
void			BROWSER_BackgroundTick( void );
// [rc4l] The registry answered but refused; stop retrying (see browser.cpp).
// [rc4l] Takes the reason now, rather than just "it said no". All three refusals are the registry's
// own words about why, and throwing that away at the door is why a refused query and an unreachable
// one looked identical on screen.
void			BROWSER_ServerRegistryRefusedQuery( zx::RegistryStatus why );

// [rc4l] A server told us it is ignoring our query. That is not silence: it ANSWERED, so we know it
// is alive, and the row must survive the refresh with whatever we last knew about it.
void			BROWSER_ServerSaidItIsIgnoringUs( const NETADDRESS_s &Address );
bool			BROWSER_WaitingForServerRegistryResponse( void );

// [rc4l] What became of each registry in cl_fua_serverregistry_list, in list order, so the browser can
// draw one bar per registry. Includes entries whose name never looked up: those used to be dropped
// silently, which made a mistyped registry indistinguishable from an empty network.
unsigned int	BROWSER_GetServerRegistryCount( void );
bool			BROWSER_GetServerRegistryStatus( unsigned int index, std::string &host, int &port,
					zx::RegistryStatus &status );
void			BROWSER_QueryAllServers( void );
// [rc4l] Seconds since the last sweep went out, or -1 for never. Sent-time, not reply-time: a
// refresh that found nothing still happened.
LONG			BROWSER_SecondsSinceRefresh( void );
// [rc4l] The same, in milliseconds, for the press floor. See BROWSER_MSSinceRefresh in browser.cpp.
LONG			BROWSER_MSSinceRefresh( void );
LONG			BROWSER_CalcNumServers( void );

//*****************************************************************************
//	EXTERNAL CONSOLE VARIABLES

#endif // __BROWSER_H__
