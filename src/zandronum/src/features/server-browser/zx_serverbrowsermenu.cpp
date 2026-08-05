// [rc4l] The server browser.
//
// Deliberately not the old one. That was a DOptionMenu with columns faked out of option items, eight
// fixed slots, page-at-a-time scrolling and a filter row -- and its worst property was silence: an
// empty list looked identical whether it was still querying, had given up, or genuinely had nothing.
// A player could not tell a broken network from a quiet night.
//
// So this one is built around saying which of those is happening. Everything that decides it lives in
// computation/serverbrowser_compute.h and is unit-tested; this file only draws and takes input.
//
// MVP scope, five columns, left to right: country, name, humans/max, version, ping. No filters, no
// sorting UI, no per-server info pane, no buttons. Selecting a server and joining is the only verb.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include <ctype.h>

#include "menu/menu.h"
#include "c_dispatch.h"
#include "v_font.h"
#include "v_video.h"
#include "v_text.h"
#include "gi.h"
#include "i_system.h"
#include "d_event.h"
#include "d_gui.h"
#include "textures/textures.h"
#include "r_data/r_translate.h"
#include "templates.h"

#include "features/server-browser/browser.h"
#include "features/server-browser/computation/serverbrowser_compute.h"
#include "features/updater/computation/promptpanel_compute.h"

//*****************************************************************************
//	CONSTANTS

// [rc4l] The browser draws in a 640x400 virtual space, not the 320x200 the rest of the menus use.
//
// At 320x200 a server name got about 140 pixels before it hit the players column, which truncated
// most real names to a couple of words -- and the name is the one column a player actually reads.
// Doubling the virtual space doubles the room without changing the physical size of the panel: text
// renders smaller relative to the screen, so more of it fits.
//
// Everything below is in these coordinates. Anything handed to the renderer must therefore say
// DTA_VirtualWidth/Height, and anything computing raw screen pixels must scale through ToScreenX/Y
// -- mixing the two is what put the flags in the corner of the screen the first time.
#define SB_VIRT_W			640
#define SB_VIRT_H			400

#define SB_PANEL_LEFT		36
#define SB_PANEL_RIGHT		604
#define SB_HEADER_Y			68
#define SB_FIRST_ROW_Y		92
#define SB_ROW_HEIGHT		20
#define SB_VISIBLE_ROWS		12

// [rc4l] The panel's content span, in virtual pixels. ComputePanelRect pads BOTH ends by the corner
// radius, so the visible gap to the screen edge is ( SB_CONTENT_TOP - radius ) above and
// ( SB_VIRT_H - SB_CONTENT_BOTTOM - radius ) below. Deriving the top from the bottom is what forces
// those two to be equal; hardcoding them separately is how the panel ended up 4px from the top edge
// and 28px from the bottom.
#define SB_CONTENT_BOTTOM	( SB_FIRST_ROW_Y + SB_VISIBLE_ROWS * SB_ROW_HEIGHT + 28 )
#define SB_CONTENT_TOP		( SB_VIRT_H - SB_CONTENT_BOTTOM )
// Title baseline, kept the same distance inside the panel's top edge as it was before.
#define SB_TITLE_Y			( SB_CONTENT_TOP + 12 )

// Column x positions (left edge of each), virtual pixels.
#define SB_COL_FLAG			48
#define SB_COL_NAME			84
#define SB_COL_PLAYERS		400
#define SB_COL_VERSION		470
#define SB_COL_PING			592

// Version gets the gap up to the ping column, less breathing room for the ping value itself.
#define SB_VERSION_MAX_WIDTH	( SB_COL_PING - SB_COL_VERSION - 56 )

// Name gets whatever is left before the players column, less a gap.
#define SB_NAME_MAX_WIDTH	( SB_COL_PLAYERS - SB_COL_NAME - 12 )

// The flag sheet is a square grid; the scoreboard uses the same constant.
#define SB_FLAGS_PER_SIDE	16

//*****************************************************************************
//	VARIABLES

// [rc4l] The drawable rows, rebuilt every tic from the browser's sparse array.
//
// Rebuilt rather than cached on purpose. The old browser kept a sorted index that was refreshed from
// several places and could fall out of step with the data, which is a class of bug that simply cannot
// happen if the list is derived where it is used.
static	TArray<int>		g_SortedServers;
static	int				g_Selected = -1;
static	int				g_ScrollFirst = 0;

//*****************************************************************************
//	FUNCTIONS

// [rc4l] Virtual coordinates to real screen pixels, for the things that cannot use DTA_Virtual*.
//
// Dim() and the flag's clip rectangle take screen pixels only, so they have to reproduce whatever
// mapping the renderer used for the text -- and that mapping is NOT a plain stretch. DTA_Virtual*
// corrects for aspect ratio, letterboxing the virtual space inside the window, so scaling by
// screenW/SB_VIRT_W put the panel edges and the row highlight in visibly different places from the
// text sitting on them.
//
// VirtualToRealCoords is the renderer's own conversion, so using it means there is one mapping rather
// than two that agree only on a 16:10 display.
static void serverbrowser_ToScreen( int vx, int vy, int vw, int vh, int &x, int &y, int &w, int &h )
{
	x = vx;
	y = vy;
	w = vw;
	h = vh;
	screen->VirtualToRealCoordsInt( x, y, w, h, SB_VIRT_W, SB_VIRT_H, false, true );
}

static int serverbrowser_ToScreenX( int vx )
{
	int x, y, w, h;
	serverbrowser_ToScreen( vx, 0, 0, 0, x, y, w, h );
	return x;
}

static int serverbrowser_ToScreenY( int vy )
{
	int x, y, w, h;
	serverbrowser_ToScreen( 0, vy, 0, 0, x, y, w, h );
	return y;
}

//*****************************************************************************
//
// [rc4l] Ping ascending, so the servers a player can actually enjoy are at the top. Ties broken by
// address so the order is stable -- an unstable sort makes rows swap places as pings jitter, which
// looks like the list is malfunctioning.
static int STACK_ARGS serverbrowser_ComparePing( const void *pA, const void *pB )
{
	const int lA = *reinterpret_cast<const int *>( pA );
	const int lB = *reinterpret_cast<const int *>( pB );

	const LONG lPingA = BROWSER_GetPing( lA );
	const LONG lPingB = BROWSER_GetPing( lB );

	if ( lPingA != lPingB )
		return ( lPingA - lPingB );

	return ( lA - lB );
}

//*****************************************************************************
//
static void serverbrowser_RebuildList( void )
{
	g_SortedServers.Clear();

	for ( ULONG ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( BROWSER_IsActive( ulIdx ))
			g_SortedServers.Push( static_cast<int>( ulIdx ));
	}

	if ( g_SortedServers.Size( ) > 1 )
		qsort( &g_SortedServers[0], g_SortedServers.Size( ), sizeof( int ), serverbrowser_ComparePing );

	g_Selected = zx::ComputeClampedSelection( g_Selected, static_cast<int>( g_SortedServers.Size( )));
}

//*****************************************************************************
//
static zx::BrowserCounts serverbrowser_CountStates( void )
{
	zx::BrowserCounts counts = { 0, 0, 0, 0 };

	for ( ULONG ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		switch ( BROWSER_GetActiveState( ulIdx ))
		{
		case AS_WAITINGFORREPLY:	counts.waiting++; break;
		case AS_ACTIVE:				counts.active++; break;
		case AS_TIMEDOUT:			counts.timedOut++; break;
		case AS_BADRESPONSE:		counts.badResponse++; break;
		default:					break;
		}
	}

	return counts;
}

//*****************************************************************************
//
// [rc4l] Draw the country flag, falling back to the alpha-3 code as text.
//
// The fallback is the point, not a nicety. A fork that ships its own game data without Zandronum's
// pk3 has no CTRYFLAG lump, and the scoreboard's equivalent column responds to that by calling
// I_Error -- it takes the whole game down over a missing decoration. A browser must degrade instead.
static void serverbrowser_DrawCountry( int lServer, int x, int y )
{
	const ULONG ulIndex = BROWSER_GetCountryIndex( lServer );
	const char *pszCode = BROWSER_GetCountryCode( lServer );

	// [rc4l] The wire gives three raw bytes with no promise about their contents. A server whose GeoIP
	// lookup failed sends whatever was in the buffer, and drawing that produced literal garbage in the
	// column ("XIP"). Anything that is not three letters is not a country code.
	bool bCodeUsable = ( pszCode != NULL );
	for ( int i = 0; bCodeUsable && ( i < 3 ); i++ )
	{
		if ( isalpha( static_cast<unsigned char>( pszCode[i] )) == 0 )
			bCodeUsable = false;
	}

	// [rc4l] Unknown is a real answer and gets shown as one. A blank column reads as "the browser
	// failed to draw something", whereas "?" says we asked and nobody knows -- which is exactly what
	// XUN means, and what XIP means once our own lookup also comes up empty.
	if (( ulIndex == COUNTRY_INDEX_UNKNOWN ) && ( bCodeUsable == false ))
	{
		screen->DrawText( SmallFont, CR_DARKGRAY, x, y, "?", DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );
		return;
	}

	FTexture *pFlags = TexMan.FindTexture( "CTRYFLAG" );

	if (( pFlags != NULL ) && ( ulIndex != COUNTRY_INDEX_UNKNOWN ) && ( ulIndex < SB_FLAGS_PER_SIDE * SB_FLAGS_PER_SIDE ))
	{
		const int flagW = pFlags->GetScaledWidth( ) / SB_FLAGS_PER_SIDE;
		const int flagH = pFlags->GetScaledHeight( ) / SB_FLAGS_PER_SIDE;

		if (( flagW > 0 ) && ( flagH > 0 ))
		{
			// [rc4l] Everything here is in SCREEN pixels, deliberately.
			//
			// DTA_Clean positions in 320x200 virtual space, but the DTA_Clip* rectangle is always
			// screen pixels -- mixing them put the clip window in the corner of the display while the
			// draw went to the row, so the flags appeared stacked at the top-left. Converting by hand
			// keeps both halves in one coordinate system.
			//
			// The trick is to draw the WHOLE sheet, offset so the wanted cell lands at (px,py), then
			// clip to just that cell.
			// One flag cell occupies SB_ROW_HEIGHT-ish of virtual space; work out the scale that maps
			// the sheet's own pixels onto that, then express everything in screen pixels.
			const int px = serverbrowser_ToScreenX( x );
			const int py = serverbrowser_ToScreenY( y );
			const int cellWpx = serverbrowser_ToScreenX( x + flagW ) - px;
			const int cellHpx = serverbrowser_ToScreenY( y + flagH ) - py;

			const int cellX = ( ulIndex % SB_FLAGS_PER_SIDE ) * flagW;
			const int cellY = ( ulIndex / SB_FLAGS_PER_SIDE ) * flagH;

			screen->DrawTexture( pFlags,
				px - ( cellX * cellWpx ) / MAX( flagW, 1 ),
				py - ( cellY * cellHpx ) / MAX( flagH, 1 ),
				DTA_DestWidth, ( pFlags->GetScaledWidth( ) * cellWpx ) / MAX( flagW, 1 ),
				DTA_DestHeight, ( pFlags->GetScaledHeight( ) * cellHpx ) / MAX( flagH, 1 ),
				DTA_ClipLeft, px,
				DTA_ClipRight, px + cellWpx,
				DTA_ClipTop, py,
				DTA_ClipBottom, py + cellHpx,
				TAG_DONE );
			return;
		}
	}

	// No sheet, or a country we could not place: the code still tells the player what they need.
	if ( bCodeUsable )
		screen->DrawText( SmallFont, CR_DARKGRAY, x, y, pszCode, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );
}

//*****************************************************************************
//
// [rc4l] The version column wants a version, not a build banner.
//
// BROWSER_GetVersion() hands back the whole thing -- "3.2.1-r36 0804-0249M on windows" -- which ran
// clean off the panel and over the ping column. Everything after the first space is build metadata
// that answers a question nobody is asking while picking a server; what matters is whether their
// version matches yours.
static FString serverbrowser_ShortVersion( const char *pszVersion )
{
	FString version = pszVersion;

	const long space = version.IndexOf( ' ' );
	if ( space > 0 )
		version.Truncate( space );

	if ( SmallFont->StringWidth( version ) <= SB_VERSION_MAX_WIDTH )
		return version;

	// Still too wide (a fork with a verbose scheme). Leave room for the ellipsis BEFORE cutting, so
	// the result including "..." fits -- otherwise the marker itself is what overruns into the ping
	// column, which is exactly the bleed it is meant to prevent.
	const int ellipsisWidth = SmallFont->StringWidth( "..." );
	while (( version.Len( ) > 1 ) && ( SmallFont->StringWidth( version ) > SB_VERSION_MAX_WIDTH - ellipsisWidth ))
		version.Truncate( version.Len( ) - 1 );

	version += "...";
	return version;
}

//*****************************************************************************
//
static EColorRange serverbrowser_PingColor( int ping )
{
	switch ( zx::ComputePingBucket( ping ))
	{
	case zx::PingBucket::Good:	return CR_GREEN;
	case zx::PingBucket::Fair:	return CR_GOLD;
	case zx::PingBucket::Poor:	return CR_RED;
	default:					return CR_DARKGRAY;
	}
}

//*****************************************************************************
//
// [rc4l] Truncate to fit, measuring the COLOURED string.
//
// Server names carry \c escapes, which occupy no pixels but do occupy characters. Truncating by
// character count would cut names arbitrarily early and could sever an escape mid-sequence, leaving
// the rest of the row tinted. So measure the visible width and cut on that.
static FString serverbrowser_FitName( const char *pszName, int maxWidth )
{
	FString name = pszName;
	V_ColorizeString( name );

	if ( SmallFont->StringWidth( name ) <= maxWidth )
		return name;

	while (( name.Len( ) > 1 ) && ( SmallFont->StringWidth( name ) > maxWidth - SmallFont->StringWidth( "..." )))
		name.Truncate( name.Len( ) - 1 );

	name += "...";
	return name;
}

// =================================================================================================
//
// [rc4l] DFUAServerBrowserMenu
//
// =================================================================================================

class DFUAServerBrowserMenu : public DOptionMenu
{
	DECLARE_CLASS( DFUAServerBrowserMenu, DOptionMenu )

public:

	DFUAServerBrowserMenu( ) { }

	void Init( DMenu *parent = NULL, FOptionMenuDescriptor *desc = NULL )
	{
		Super::Init( parent, desc );

		g_Selected = -1;
		g_ScrollFirst = 0;

		BROWSER_ClearServerList( );
		BROWSER_QueryServerRegistry( );
	}

	void Ticker( )
	{
		Super::Ticker( );

		// Drives the registry query's retry/give-up clock and ages out servers that never answered.
		BROWSER_ServerRegistryTick( );
		BROWSER_QueryTick( );

		serverbrowser_RebuildList( );
	}

	//*************************************************************************
	//
	void Drawer( )
	{
		const zx::BrowserCounts counts = serverbrowser_CountStates( );
		const bool bWaitingRegistry = BROWSER_WaitingForServerRegistryResponse( );
		const zx::BrowserPhase phase = zx::ComputeBrowserPhase( bWaitingRegistry, counts );

		DrawPanel( );

		screen->DrawText( BigFont, CR_UNTRANSLATED,
			( SB_VIRT_W / 2 ) - ( BigFont->StringWidth( "SERVERS" ) / 2 ), SB_TITLE_Y, "SERVERS", DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );

		if ( phase == zx::BrowserPhase::Ready )
			DrawRows( counts );
		else
			DrawPlaceholder( phase );

		DrawFooter( phase, counts );
	}

	//*************************************************************************
	//
	// [rc4l] Same rounded panel and gradient as the update notice and the link prompt, so the browser
	// belongs to the same interface rather than looking like a different program.
	void DrawPanel( )
	{
		const int screenW = screen->GetWidth( ), screenH = screen->GetHeight( );

		const int panelWpx = serverbrowser_ToScreenX( SB_PANEL_RIGHT ) - serverbrowser_ToScreenX( SB_PANEL_LEFT );
		const int topPx = serverbrowser_ToScreenY( SB_CONTENT_TOP );
		const int bottomPx = serverbrowser_ToScreenY( SB_CONTENT_BOTTOM );
		const int radiusPx = serverbrowser_ToScreenY( 12 ) - serverbrowser_ToScreenY( 0 );

		zx::PanelRect r = zx::ComputePanelRect( screenW, screenH, panelWpx, topPx, bottomPx, radiusPx, radiusPx );
		const zx::PanelColor topCol = { 26, 28, 40, 236 };
		const zx::PanelColor botCol = { 8, 9, 15, 248 };

		for ( int row = 0; row < r.h; ++row )
		{
			const int inset = zx::ComputeRoundedInset( row, r.h, r.radius );
			const int rowW = r.w - 2 * inset;
			if ( rowW <= 0 )
				continue;

			const zx::PanelColor c = zx::ComputePanelGradient( row, r.h, topCol, botCol );
			screen->Dim( PalEntry( c.r, c.g, c.b ), c.a / 255.f, r.x + inset, r.y + row, rowW, 1 );
		}
	}

	//*************************************************************************
	//
	void DrawRows( const zx::BrowserCounts &counts )
	{
		const int total = static_cast<int>( g_SortedServers.Size( ));
		const zx::RowWindow window = zx::ComputeRowWindow( total, SB_VISIBLE_ROWS, g_Selected, g_ScrollFirst );
		g_ScrollFirst = window.first;

		// Column headings, dim so they never compete with the data.
		screen->DrawText( SmallFont, CR_DARKGRAY, SB_COL_NAME, SB_HEADER_Y, "SERVER", DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );
		screen->DrawText( SmallFont, CR_DARKGRAY, SB_COL_PLAYERS, SB_HEADER_Y, "PLRS", DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );
		screen->DrawText( SmallFont, CR_DARKGRAY, SB_COL_VERSION, SB_HEADER_Y, "VER", DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );
		DrawRightAligned( SmallFont, CR_DARKGRAY, SB_COL_PING, SB_HEADER_Y, "PING" );

		for ( int i = 0; i < window.count; i++ )
		{
			const int row = window.first + i;
			const int lServer = g_SortedServers[row];
			const int y = SB_FIRST_ROW_Y + i * SB_ROW_HEIGHT;
			const bool bSelected = ( row == g_Selected );

			if ( bSelected )
				DimRow( y );

			serverbrowser_DrawCountry( lServer, SB_COL_FLAG, y );

			const FString name = serverbrowser_FitName( BROWSER_GetHostName( lServer ), SB_NAME_MAX_WIDTH );
			screen->DrawText( SmallFont, bSelected ? CR_WHITE : CR_UNTRANSLATED,
				SB_COL_NAME, y, name, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );

			// Humans only -- a row reading 8/8 for seven bots and one person is a lie the player
			// only discovers after joining.
			FString players;
			players.Format( "%d/%d", static_cast<int>( BROWSER_GetNumHumanPlayers( lServer )),
				static_cast<int>( BROWSER_GetMaxClients( lServer )));
			screen->DrawText( SmallFont, CR_UNTRANSLATED, SB_COL_PLAYERS, y, players, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );

			screen->DrawText( SmallFont, CR_DARKGRAY, SB_COL_VERSION, y,
				serverbrowser_ShortVersion( BROWSER_GetVersion( lServer )), DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );

			const int ping = static_cast<int>( BROWSER_GetPing( lServer ));
			FString pingText;
			pingText.Format( "%d", ping );
			DrawRightAligned( SmallFont, serverbrowser_PingColor( ping ), SB_COL_PING, y, pingText );
		}

		// Only mention stragglers once there is something to compare them against.
		if ( zx::ComputeShowsProgress( BROWSER_WaitingForServerRegistryResponse( ), counts ))
		{
			FString more;
			more.Format( "%s  querying %d more", Spinner( ), counts.waiting );
			screen->DrawText( SmallFont, CR_DARKGRAY, SB_COL_NAME,
				SB_FIRST_ROW_Y + SB_VISIBLE_ROWS * SB_ROW_HEIGHT + 2, more, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );
		}
	}

	//*************************************************************************
	//
	// [rc4l] What the old browser could not do: say which kind of nothing this is.
	void DrawPlaceholder( zx::BrowserPhase phase )
	{
		const int y = SB_FIRST_ROW_Y + ( SB_VISIBLE_ROWS * SB_ROW_HEIGHT ) / 2 - 4;
		FString text;

		if ( phase == zx::BrowserPhase::Loading )
			text.Format( "%s  Looking for servers", Spinner( ));
		else
			text = "No servers found";

		screen->DrawText( SmallFont, phase == zx::BrowserPhase::Loading ? CR_UNTRANSLATED : CR_DARKGRAY,
			( SB_VIRT_W / 2 ) - ( SmallFont->StringWidth( text ) / 2 ), y, text, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );
	}

	//*************************************************************************
	//
	void DrawFooter( zx::BrowserPhase phase, const zx::BrowserCounts &counts )
	{
		const int y = SB_FIRST_ROW_Y + SB_VISIBLE_ROWS * SB_ROW_HEIGHT + 12;
		FString text;

		if ( phase == zx::BrowserPhase::Ready )
			text.Format( "%d servers", static_cast<int>( g_SortedServers.Size( )));
		else if ( phase == zx::BrowserPhase::Empty )
		{
			// Distinguish "nobody is hosting" from "we asked and got nowhere" -- identical-looking
			// outcomes with completely different remedies.
			if (( counts.timedOut > 0 ) || ( counts.badResponse > 0 ))
				text.Format( "%d did not respond", counts.timedOut + counts.badResponse );
			else
				text = "Nothing is being hosted right now";
		}

		if ( text.IsNotEmpty( ))
			screen->DrawText( SmallFont, CR_DARKGRAY,
				( SB_VIRT_W / 2 ) - ( SmallFont->StringWidth( text ) / 2 ), y, text, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );
	}

	//*************************************************************************
	//
	const char *Spinner( )
	{
		static const char *const frames[] = { "|", "/", "-", "\\" };
		return frames[zx::ComputeSpinnerFrame( static_cast<int>( DMenu::MenuTime ), 4, 4 )];
	}

	//*************************************************************************
	//
	void DimRow( int y )
	{
		const int left = serverbrowser_ToScreenX( SB_PANEL_LEFT + 4 );
		const int right = serverbrowser_ToScreenX( SB_PANEL_RIGHT - 4 );
		const int top = serverbrowser_ToScreenY( y - 2 );
		const int bottom = serverbrowser_ToScreenY( y - 2 + SB_ROW_HEIGHT );

		screen->Dim( PalEntry( 120, 150, 220 ), 0.28f, left, top, right - left, bottom - top );
	}

	//*************************************************************************
	//
	void DrawRightAligned( FFont *font, EColorRange color, int right, int y, const char *text )
	{
		screen->DrawText( font, color, right - font->StringWidth( text ), y, text, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );
	}

	//*************************************************************************
	//
	bool MenuEvent( int mkey, bool fromcontroller )
	{
		const int total = static_cast<int>( g_SortedServers.Size( ));

		switch ( mkey )
		{
		case MKEY_Up:
			if ( total > 0 )
			{
				g_Selected = ( g_Selected <= 0 ) ? total - 1 : g_Selected - 1;
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
			}
			return true;

		case MKEY_Down:
			if ( total > 0 )
			{
				g_Selected = ( g_Selected >= total - 1 ) ? 0 : g_Selected + 1;
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
			}
			return true;

		case MKEY_Enter:
			if (( g_Selected >= 0 ) && ( g_Selected < total ))
			{
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
				BROWSER_SetSelectedServer( g_SortedServers[g_Selected] );
				// [rc4l] fua_ variant: resolves the server's WADs locally and joins through the
				// validated reload, instead of `restart -connect` tearing the game down first.
				AddCommandString( "fua_join_selected_server" );
			}
			return true;

		default:
			return Super::MenuEvent( mkey, fromcontroller );
		}
	}
};

IMPLEMENT_CLASS( DFUAServerBrowserMenu )
