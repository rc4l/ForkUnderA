// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The answers an offline build (ZX_ONLINE off) gives where shared code calls into the features that reach other machines: no servers, no host, no account, no download, no update.

#include "features/server-browser/browser.h"
#include "features/server-browser/zx_joinserver.h"
#include "features/identity/zx_identity.h"
#include "features/identity/zx_identitynet.h"
#include "features/server-hosting/zx_hosting.h"
#include "features/server-hosting/zx_hostwatchdog.h"
#include "features/server-hosting/zx_punchclient.h"
#include "features/server-hosting/zx_reachprobe.h"
#include "features/port-mapping/zx_portmap.h"
#include "features/addon-catalogue/zx_catalogue.h"
#include "features/net/zx_resolve.h"
#include "features/wad-download/zx_waddownload.h"
#include "features/wad-serve/zx_wadserve.h"
#include "features/updater/zx_updater.h"
#include "menu/menu.h"
#include "m_misc.h"
#include "v_video.h"
#include "v_font.h"

// [rc4l] The server browser: an empty list, and no packet ever arrives to parse.
void BROWSER_Construct( void ) {}
void BROWSER_BackgroundTick( void ) {}
void BROWSER_QueryAllServers( void ) {}
void BROWSER_RefreshListedServers( void ) {}
void BROWSER_RecheckServer( ULONG ) {}
void BROWSER_AddServerToList( const NETADDRESS_s & ) {}
void BROWSER_PunchKnockFrom( const NETADDRESS_s & ) {}
void BROWSER_ServerSaidItIsIgnoringUs( const NETADDRESS_s & ) {}
void BROWSER_ServerRegistryRefusedQuery( zx::RegistryStatus ) {}
void BROWSER_ParseServerQuery( BYTESTREAM_s *, bool ) {}
void BROWSER_ParseServerQuerySegment( BYTESTREAM_s *, bool ) {}
bool BROWSER_GetServerList( BYTESTREAM_s * ) { return false; }
bool BROWSER_IsActive( ULONG ) { return false; }
bool BROWSER_IsAwaitingReplyFrom( const NETADDRESS_s & ) { return false; }
bool BROWSER_IsServerRegistryAddress( const NETADDRESS_s & ) { return false; }
LONG BROWSER_GetListIDByAddress( const NETADDRESS_s & ) { return -1; }
const char *BROWSER_GetHostName( ULONG ) { return ""; }
LONG BROWSER_GetNumPWADs( ULONG ) { return 0; }
const char *BROWSER_GetPWADName( ULONG, ULONG ) { return ""; }
unsigned int BROWSER_GetServerRegistryCount( void ) { return 0; }
bool BROWSER_GetServerRegistryStatus( unsigned int, std::string &, int &, zx::RegistryStatus & ) { return false; }

// [rc4l] The account handshake, which needs a server to shake hands with.
void CLIENT_FuaAuthReset( void ) {}
void CLIENT_FuaAuthWriteHello( BYTESTREAM_s * ) {}
bool CLIENT_FuaAuthReadChallenge( BYTESTREAM_s * ) { return false; }
void CLIENT_FuaAuthWriteProof( BYTESTREAM_s * ) {}
bool SERVER_FuaAuthReadHello( ULONG, BYTESTREAM_s * ) { return false; }
void SERVER_FuaAuthWriteChallenge( ULONG, BYTESTREAM_s * ) {}
bool SERVER_FuaAuthReadProof( ULONG, BYTESTREAM_s * ) { return false; }
void SERVER_FuaAuthClearChallenge( ULONG ) {}
void SERVER_FUA_AnnounceReadyOnce( void ) {}

namespace zx {

// [rc4l] The player's folder, as the online build's identity gives it, since the continue feature keeps its saves there.
std::string Identity_ConfigRoot( void ) { return std::string( M_GetFuaUserPath( ).GetChars( )); }
int Identity_Instance( void ) { return 0; }
int Identity_InitClientHere( const char * ) { return 0; }
bool Identity_InitServer( const char * ) { return false; }
bool Identity_SwitchToSpare( void ) { return false; }
std::string Identity_ServerRegistryId( int ) { return std::string( ); }

// [rc4l] Hosting never starts, so there is no host to ask about.
bool HostStart( const HostConfig & ) { Printf( "This build plays offline, so it cannot host a game.\n" ); return false; }
void HostStop( void ) {}
void HostTick( void ) {}
void HostShutdown( void ) {}
HostState HostCurrentState( void ) { return HostState::Idle; }
bool HostIsActive( void ) { return false; }
unsigned HostGeneration( void ) { return 0; }
bool HostTakeReadyEdge( void ) { return false; }
FString HostConnectAddress( void ) { return FString( ); }
const char *HostRconSecret( void ) { return ""; }
bool HostOwnsAddress( const FString & ) { return false; }
void HostChildEcho( const char * ) {}
void HostChildAnnounceReachable( void ) {}
void HostWatchdogInit( void ) {}
bool PunchRequestFor( const NETADDRESS_s &, bool ) { return false; }
void PunchCookieArrived( const char * ) {}
void PunchResultArrived( int ) {}
void ReachProbeCookieArrived( const char * ) {}
void ReachProbeSetPublicIp( const char * ) {}
void PortMapShutdown( void ) {}
void CatalogueCheckAtStartup( void ) {}
bool ResolveHostV6( const char *, NETADDRESS_s & ) { return false; }

// [rc4l] Joining a server from the browser, which an offline build never does.
bool IsJoinInFlight( ) { return false; }
bool IsServerBrowserOpen( ) { return false; }
bool ConsumeJoinReady( ) { return false; }
void JoinTick( ) {}
void NoteJoinFailed( const char * ) {}
void DrawJoinReadyNotice( bool ) {}

namespace waddownload {
void Tick( ) {}
void RegisterDownloadDirEarly( ) {}
void MergeShippedDownloadSites( ) {}
FString FindLocalCopy( const char *, const char * ) { return FString( ); }
} // namespace waddownload

namespace wadserve {
int Port( ) { return 0; }
bool PrefersMirrors( ) { return false; }
void Tick( ) {}
} // namespace wadserve

namespace updater {
bool IsAvailable( ) { return false; }
const char *Tag( ) { return ""; }
void Tick( ) {}
} // namespace updater

} // namespace zx

// [rc4l] The class MENUDEF names for the server browser, which in an offline build only says there are no servers to find.
class DFUAServerBrowserMenu : public DOptionMenu
{
	DECLARE_CLASS( DFUAServerBrowserMenu, DOptionMenu )

public:
	void Drawer( )
	{
		static const char *const text = "This build plays offline.";
		screen->DrawText( SmallFont, CR_WHITE, ( screen->GetWidth( ) - SmallFont->StringWidth( text ) * CleanXfac ) / 2,
			screen->GetHeight( ) / 2, text, DTA_CleanNoMove, true, TAG_DONE );
	}
};
IMPLEMENT_CLASS( DFUAServerBrowserMenu )
