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
#include <string>
#include <vector>

#include "menu/menu.h"
#include "c_dispatch.h"
#include "v_font.h"
#include "v_video.h"
#include "v_text.h"
#include "gi.h"
#include "i_system.h"
#include "d_event.h"
#include "d_gui.h"
#include "d_main.h"		// D_AddFile, for the have/have-not colouring
#include "textures/textures.h"
#include "r_data/r_translate.h"
#include "templates.h"

#include "features/server-browser/browser.h"
#include "features/server-browser/computation/browserhit_compute.h"
#include "features/server-browser/computation/colortext_compute.h"
#include "features/server-browser/computation/serverbrowser_compute.h"
#include "features/server-browser/zx_joinserver.h"
#include "features/updater/computation/promptpanel_compute.h"
#include "features/wad-download/zx_waddownload.h"

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

// [rc4l] 16 rather than 20. The glyphs are eight units tall, so 20 left six clear above and below --
// generous to the point of wasting a row and a half of list. At 16 there are still four either side,
// which reads as comfortable rather than cramped, and the two rows it frees pay for the detail strip
// with one left over.
#define SB_ROW_HEIGHT		16
#define SB_VISIBLE_ROWS		14

// [rc4l] The details panel, beside the list rather than under it.
//
// Under it was the first attempt and it cost rows, which is the one thing this layout is short of.
// Beside it costs name-column width instead -- and a name that no longer fits is a name you can still
// read most of, whereas a row that does not fit is a server you cannot see at all.
#define SB_ROWS_BOTTOM		( SB_FIRST_ROW_Y + SB_VISIBLE_ROWS * SB_ROW_HEIGHT )
#define SB_DETAIL_LEFT		418
#define SB_DETAIL_RIGHT		596
// Where the list stops. The row highlight and the click hitbox both end here rather than at the
// panel edge -- otherwise a selected row's band runs on underneath the detail panel and shows through
// it, and clicking the panel selects whatever row happens to be level with the pointer.
#define SB_LIST_RIGHT		( SB_DETAIL_LEFT - 8 )
#define SB_DETAIL_TOP		62
#define SB_DETAIL_BOTTOM	( SB_ROWS_BOTTOM + 6 )
#define SB_DETAIL_PAD		10
#define SB_DETAIL_LINE		11
#define SB_DETAIL_TEXT_W	( SB_DETAIL_RIGHT - SB_DETAIL_LEFT - 2 * SB_DETAIL_PAD )

// [rc4l] The action button along the bottom of the detail panel: JOIN, or CANCEL while a download for
// this join is running. Enter has always joined, but a keyboard-only affordance is not one on a
// screen the player is driving with the mouse -- and cancelling had NO affordance at all, living only
// in a console command most players will never type.
#define SB_BUTTON_H			16
#define SB_BUTTON_LEFT		( SB_DETAIL_LEFT + SB_DETAIL_PAD )
#define SB_BUTTON_RIGHT		( SB_DETAIL_RIGHT - SB_DETAIL_PAD )
#define SB_BUTTON_TOP		( SB_DETAIL_BOTTOM - SB_DETAIL_PAD - SB_BUTTON_H )

// The WAD list stops above the button rather than under it.
#define SB_DETAIL_TEXT_BOTTOM	( SB_BUTTON_TOP - 6 )

#define SB_FOOTER_Y			( SB_ROWS_BOTTOM + 20 )

// [rc4l] The panel's content span, in virtual pixels. ComputePanelRect pads BOTH ends by the corner
// radius, so the visible gap to the screen edge is ( SB_CONTENT_TOP - radius ) above and
// ( SB_VIRT_H - SB_CONTENT_BOTTOM - radius ) below. Deriving the top from the bottom is what forces
// those two to be equal; hardcoding them separately is how the panel ended up 4px from the top edge
// and 28px from the bottom.
#define SB_CONTENT_BOTTOM	( SB_FOOTER_Y + 24 )
#define SB_CONTENT_TOP		( SB_VIRT_H - SB_CONTENT_BOTTOM )
// Title baseline, kept the same distance inside the panel's top edge as it was before.
#define SB_TITLE_Y			( SB_CONTENT_TOP + 12 )

// Column x positions (left edge of each), virtual pixels.
#define SB_COL_FLAG			48
#define SB_COL_NAME			84
#define SB_COL_PLAYERS		286
#define SB_COL_PING			398

// [rc4l] Version left the list and lives in the detail strip instead. It is the column a player reads
// least often and the one they need least urgently -- and the room it freed goes to the name, which
// is the column this whole 640-wide layout exists to give space to.
#define SB_VERSION_MAX_WIDTH	120

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

// [rc4l] Which row the mouse was pressed on, so a release somewhere else cancels instead of joining
// whatever ended up under the cursor. Reset whenever a release lands anywhere.
static	int				g_MousePressRow = -1;

// [rc4l] The action button at the bottom of the detail panel. Hot = pointer over it, pressed = held
// down on it, so dragging off before releasing cancels the way a button should.
static	bool			g_ButtonHot = false;
static	bool			g_ButtonPressed = false;

// [rc4l] Showing "cancel this download?". Drawn and answered by this menu rather than through
// M_StartMessage, so the browser keeps control of the pairing: the hold placed on the join resume
// when this goes up MUST be released on exactly one of the two answers, and a message box that can be
// dismissed by other menu machinery is a way for that to not happen.
static	bool			g_ConfirmCancel = false;

//*****************************************************************************
//
// [rc4l] Whether a transfer is in flight. Decides both what the button says and what pressing it
// does, so it is asked in one place rather than inferred twice.
static bool serverbrowser_DownloadRunning( void )
{
	return zx::waddownload::IsRunning( );
}

// [rc4l] The previous completed click, for detecting a double one. ZDoom's menu system has no
// double-click event -- MOUSE_Click2 is the RIGHT button, not the second click -- so the interval is
// timed here. 20 tics is a little under 0.6s at 35Hz, which is forgiving without joining a server
// because somebody clicked twice on purpose a moment apart.
#define SB_DOUBLECLICK_TICS	20
static	int				g_LastClickRow = -1;
static	int				g_LastClickTime = -1000;

// [rc4l] The selected server's file set and whether we already hold each one.
//
// Cached rather than recomputed per frame because "do we have it" is a filesystem search -- the same
// BaseFileSearch the join uses -- and doing that for every WAD of every frame would hit the disk sixty
// times a second to answer a question that only changes when the selection does.
static	int				g_DetailServer = -1;
static	TArray<FString>	g_DetailWads;
static	TArray<bool>	g_DetailHave;

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
// [rc4l] Vertical middle of a row, in virtual units.
//
// A row is the box DimRow draws: SB_ROW_HEIGHT tall, starting two above the line coordinate. What
// goes in it -- glyphs eight units tall, a flag around twelve -- is shorter than that, so drawing it
// AT the line coordinate left everything hugging the top edge with the slack underneath. Centring
// instead means the contents sit in the row rather than on it, and a taller font or a different flag
// sheet stays centred without anyone re-tuning a constant.
static int serverbrowser_RowMidY( int rowY )
{
	return rowY - 2 + SB_ROW_HEIGHT / 2;
}

// Where text of height `h` starts so that it is centred on that middle.
static int serverbrowser_RowTextY( int rowY, int h )
{
	return serverbrowser_RowMidY( rowY ) - h / 2;
}

//*****************************************************************************
//
// [rc4l] Rebuild the selected server's file list, and resolve each name to see whether the player
// already has it.
//
// D_AddFile is the same lookup the join performs, so the colours in the panel and the files the join
// actually downloads can never disagree -- a green entry that then downloaded, or a red one that did
// not, would be worse than showing nothing. It writes into a scratch array we throw away; only the
// true/false matters here.
static void serverbrowser_RefreshWadCache( int lServer )
{
	if ( lServer == g_DetailServer )
		return;

	g_DetailServer = lServer;
	g_DetailWads.Clear( );
	g_DetailHave.Clear( );

	const char *pszIwad = BROWSER_GetIWADName( lServer );
	if (( pszIwad != NULL ) && ( pszIwad[0] != 0 ))
		g_DetailWads.Push( pszIwad );

	const LONG lPwads = BROWSER_GetNumPWADs( lServer );
	for ( LONG i = 0; i < lPwads; i++ )
	{
		const char *pszPwad = BROWSER_GetPWADName( lServer, i );
		if (( pszPwad != NULL ) && ( pszPwad[0] != 0 ))
			g_DetailWads.Push( pszPwad );
	}

	for ( unsigned i = 0; i < g_DetailWads.Size( ); i++ )
	{
		TArray<FString> scratch;
		g_DetailHave.Push( D_AddFile( scratch, g_DetailWads[i].GetChars( )) );
	}
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
		screen->DrawText( SmallFont, CR_DARKGRAY, x, serverbrowser_RowTextY( y, SmallFont->GetHeight( )),
			"?", DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );
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
			// Centred on the row like everything else in it, rather than starting at the line.
			const int flagTop = serverbrowser_RowMidY( y ) - flagH / 2;
			const int px = serverbrowser_ToScreenX( x );
			const int py = serverbrowser_ToScreenY( flagTop );
			const int cellWpx = serverbrowser_ToScreenX( x + flagW ) - px;
			const int cellHpx = serverbrowser_ToScreenY( flagTop + flagH ) - py;

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
		screen->DrawText( SmallFont, CR_DARKGRAY, x, serverbrowser_RowTextY( y, SmallFont->GetHeight( )),
			pszCode, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );
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
// [rc4l] A server-supplied string, colour codes and all, cut to fit `maxWidth`.
//
// V_ColorizeString first, because the operator typed "\cd" and that is what arrives on the wire --
// without it the backslash and the letter are drawn literally. After that the renderer does the work:
// FFont::StringWidth already skips escapes when measuring and DrawText consumes them when drawing.
//
// The cutting is the part that needs care, and why the offsets come from a tested unit: shortening a
// byte at a time eventually lands between an escape and the character it takes, and the leftover
// escape then eats the following glyph as a colour code. See computation/colortext_compute.h.
static FString serverbrowser_FitName( const char *pszName, int maxWidth )
{
	FString name = pszName;
	V_ColorizeString( name );

	if ( SmallFont->StringWidth( name ) <= maxWidth )
		return name;

	// Room for the ellipsis BEFORE cutting, so the result including "..." fits.
	const int budget = maxWidth - SmallFont->StringWidth( "..." );
	const std::vector<size_t> cuts = zx::ComputeColorSafeCutPoints( std::string( name.GetChars( )));

	// Longest first: the widest cut that fits is the most of the name we can show.
	for ( size_t i = cuts.size( ); i-- > 0; )
	{
		FString candidate = name;
		candidate.Truncate( static_cast<long>( cuts[i] ));
		if ( SmallFont->StringWidth( candidate ) <= budget )
		{
			candidate += "...";
			return candidate;
		}
	}

	return FString( "..." );
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

	// [rc4l] The browser can be torn down by machinery that never saw the question -- a console
	// command, a restart. Leaving the hold in place would strand a finished download forever, so it
	// is released here as "keep going", which is what a player who never answered "stop" meant.
	void Destroy( )
	{
		if ( g_ConfirmCancel )
		{
			g_ConfirmCancel = false;
			zx::ReleaseJoinResume( true );
		}
		Super::Destroy( );
	}

	//*************************************************************************
	//
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

		DrawDetails( );
		DrawFooter( phase, counts );

		// Last, and over everything: a question the player has to answer before anything else happens.
		if ( g_ConfirmCancel )
			DrawCancelConfirm( );
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

			const int ty = serverbrowser_RowTextY( y, SmallFont->GetHeight( ));

			const FString name = serverbrowser_FitName( BROWSER_GetHostName( lServer ), SB_NAME_MAX_WIDTH );
			screen->DrawText( SmallFont, bSelected ? CR_WHITE : CR_UNTRANSLATED,
				SB_COL_NAME, ty, name, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );

			// Humans only -- a row reading 8/8 for seven bots and one person is a lie the player
			// only discovers after joining.
			FString players;
			players.Format( "%d/%d", static_cast<int>( BROWSER_GetNumHumanPlayers( lServer )),
				static_cast<int>( BROWSER_GetMaxClients( lServer )));
			screen->DrawText( SmallFont, CR_UNTRANSLATED, SB_COL_PLAYERS, ty, players, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );

			const int ping = static_cast<int>( BROWSER_GetPing( lServer ));
			FString pingText;
			pingText.Format( "%d", ping );
			DrawRightAligned( SmallFont, serverbrowser_PingColor( ping ), SB_COL_PING, ty, pingText );
		}

		// Only mention stragglers once there is something to compare them against.
		if ( zx::ComputeShowsProgress( BROWSER_WaitingForServerRegistryResponse( ), counts ))
		{
			FString more;
			more.Format( "%s  querying %d more", Spinner( ), counts.waiting );
			screen->DrawText( SmallFont, CR_DARKGRAY, SB_COL_NAME, SB_ROWS_BOTTOM + 2,
				more, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );
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
	// [rc4l] The selected server, in its own panel beside the list.
	//
	// Same rounded gradient as the main panel so it reads as part of the same surface, but black: it
	// sits ON that panel, and a second copy of the same blue-grey would have looked like a rendering
	// mistake rather than a nested surface.
	//
	// The WAD list is the reason this exists. Joining downloads, so "which of these do I already have"
	// is the question a player needs answered BEFORE committing, and no other Doom browser answers it
	// because no other Doom browser downloads. Green means it resolved locally, red means it is a
	// transfer you have not agreed to yet.
	void DrawDetailPanel( )
	{
		const int left = serverbrowser_ToScreenX( SB_DETAIL_LEFT );
		const int right = serverbrowser_ToScreenX( SB_DETAIL_RIGHT );
		const int top = serverbrowser_ToScreenY( SB_DETAIL_TOP );
		const int bottom = serverbrowser_ToScreenY( SB_DETAIL_BOTTOM );
		const int radius = serverbrowser_ToScreenY( 8 ) - serverbrowser_ToScreenY( 0 );

		const int w = right - left;
		const int h = bottom - top;
		if (( w <= 0 ) || ( h <= 0 ))
			return;

		// ComputePanelRect is not used here: it centres its rectangle on the screen, which is right for
		// the one panel and wrong for anything placed inside it. The rounding and gradient helpers are
		// the parts worth sharing, and they are.
		const zx::PanelColor topCol = { 0, 0, 0, 170 };
		const zx::PanelColor botCol = { 0, 0, 0, 205 };

		for ( int row = 0; row < h; ++row )
		{
			const int inset = zx::ComputeRoundedInset( row, h, radius );
			const int rowW = w - 2 * inset;
			if ( rowW <= 0 )
				continue;

			const zx::PanelColor c = zx::ComputePanelGradient( row, h, topCol, botCol );
			screen->Dim( PalEntry( c.r, c.g, c.b ), c.a / 255.f, left + inset, top + row, rowW, 1 );
		}
	}

	//*************************************************************************
	//
	// [rc4l] The confirmation, drawn over everything else. Cancelling a transfer that is most of the
	// way through a 200 MB modpack because of one stray click is worth a question.
	void DrawCancelConfirm( )
	{
		screen->Dim( PalEntry( 0, 0, 0 ), 0.6f, 0, 0, SCREENWIDTH, SCREENHEIGHT );

		const char *const lines[] = {
			"Cancel this download?",
			"",
			"Y - stop it        N - keep going",
		};

		int y = ( SB_VIRT_H / 2 ) - ( SmallFont->GetHeight( ) * 2 );
		for ( size_t i = 0; i < countof( lines ); ++i )
		{
			if ( lines[i][0] != '\0' )
			{
				screen->DrawText( SmallFont, ( i == 0 ) ? CR_WHITE : CR_GOLD,
					( SB_VIRT_W / 2 ) - ( SmallFont->StringWidth( lines[i] ) / 2 ), y, lines[i],
					DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );
			}
			y += SmallFont->GetHeight( ) + 3;
		}
	}

	//*************************************************************************
	//
	// [rc4l] Both answers, in one place, because the important part is that each releases the hold
	// exactly once. `stop` false is "keep going", which also covers the download having finished while
	// the question was on screen -- ReleaseJoinResume then runs the join it was holding.
	// [rc4l] Committing to the selected server. One implementation, reached from the keyboard and from
	// the button, so the two can never come to mean different things.
	void DoJoinSelected( )
	{
		const int total = static_cast<int>( g_SortedServers.Size( ));
		if (( g_Selected < 0 ) || ( g_Selected >= total ))
			return;

		S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
		BROWSER_SetSelectedServer( g_SortedServers[g_Selected] );
		// [rc4l] fua_ variant: resolves the server's WADs locally and joins through the validated
		// reload, instead of `restart -connect` tearing the game down first.
		AddCommandString( "fua_join_selected_server" );
	}

	// [rc4l] What the one button does, which depends on what is happening. Enter goes through here
	// too: the button reading CANCEL while Enter still joined would be two controls disagreeing about
	// the same slot.
	void PressActionButton( )
	{
		if ( serverbrowser_DownloadRunning( ))
		{
			// Held BEFORE the question goes up, not after it is answered. A transfer finishing in the
			// meantime would otherwise restart the engine for the reload while the player is still
			// reading -- the hold is what makes the answer land on something that still exists.
			zx::HoldJoinResume( );
			g_ConfirmCancel = true;
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
			return;
		}

		DoJoinSelected( );
	}

	void AnswerCancelConfirm( bool stop )
	{
		if ( !g_ConfirmCancel )
			return;

		g_ConfirmCancel = false;

		if ( stop )
			zx::waddownload::Cancel( );

		zx::ReleaseJoinResume( !stop );
	}

	//*************************************************************************
	//
	// [rc4l] JOIN, or CANCEL while a transfer for this join is running. One button rather than two,
	// because they are the same slot in the player's head -- "the thing I press to make this server
	// happen" and "the thing I press to stop it".
	void DrawActionButton( )
	{
		const int left = serverbrowser_ToScreenX( SB_BUTTON_LEFT );
		const int right = serverbrowser_ToScreenX( SB_BUTTON_RIGHT );
		const int top = serverbrowser_ToScreenY( SB_BUTTON_TOP );
		const int bottom = serverbrowser_ToScreenY( SB_BUTTON_TOP + SB_BUTTON_H );
		const int radius = serverbrowser_ToScreenY( 4 ) - serverbrowser_ToScreenY( 0 );

		const int w = right - left;
		const int h = bottom - top;
		if (( w <= 0 ) || ( h <= 0 ))
			return;

		const bool bCancel = serverbrowser_DownloadRunning( );

		// Lighter when the pointer is over it, lighter still while held -- the press feedback a button
		// needs to feel like one rather than like a label that happens to react.
		int base = g_ButtonHot ? 70 : 45;
		if ( g_ButtonHot && g_ButtonPressed )
			base = 95;

		const zx::PanelColor topCol = { static_cast<BYTE>( bCancel ? base + 30 : base ),
			static_cast<BYTE>( base ), static_cast<BYTE>( base ), 220 };
		const zx::PanelColor botCol = { static_cast<BYTE>( bCancel ? base + 15 : base / 2 ),
			static_cast<BYTE>( base / 2 ), static_cast<BYTE>( base / 2 ), 235 };

		for ( int row = 0; row < h; ++row )
		{
			const int inset = zx::ComputeRoundedInset( row, h, radius );
			const int rowW = w - 2 * inset;
			if ( rowW <= 0 )
				continue;

			const zx::PanelColor c = zx::ComputePanelGradient( row, h, topCol, botCol );
			screen->Dim( PalEntry( c.r, c.g, c.b ), c.a / 255.f, left + inset, top + row, rowW, 1 );
		}

		const char *const label = bCancel ? "CANCEL" : "JOIN";
		const int textY = SB_BUTTON_TOP + ( SB_BUTTON_H - SmallFont->GetHeight( )) / 2 + 1;
		screen->DrawText( SmallFont, bCancel ? CR_ORANGE : CR_WHITE,
			( SB_BUTTON_LEFT + SB_BUTTON_RIGHT ) / 2 - ( SmallFont->StringWidth( label ) / 2 ),
			textY, label, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );
	}

	//*************************************************************************
	//
	void DrawDetails( )
	{
		DrawDetailPanel( );

		const int total = static_cast<int>( g_SortedServers.Size( ));
		if (( g_Selected < 0 ) || ( g_Selected >= total ))
			return;

		const int lServer = g_SortedServers[g_Selected];
		serverbrowser_RefreshWadCache( lServer );

		const int x = SB_DETAIL_LEFT + SB_DETAIL_PAD;
		int y = SB_DETAIL_TOP + SB_DETAIL_PAD;

		// Title: the server's own name, wrapped rather than clipped -- it is the heading, and half a
		// name with an ellipsis tells you less than two short lines do. Colorized so an operator's
		// "\cd" reads as colour here exactly as it does in the list.
		FString title = BROWSER_GetHostName( lServer );
		V_ColorizeString( title );
		y = DrawWrapped( title, x, y, CR_WHITE );
		y += 3;

		FString sub;
		const char *pszMap = BROWSER_GetMapname( lServer );
		if (( pszMap != NULL ) && ( pszMap[0] != 0 ))
			sub << pszMap;
		const char *pszMode = BROWSER_GetGameModeName( lServer );
		if (( pszMode != NULL ) && ( pszMode[0] != 0 ))
			sub << ( sub.IsNotEmpty( ) ? "  -  " : "" ) << pszMode;
		if ( sub.IsNotEmpty( ))
			y = DrawWrapped( sub, x, y, CR_GOLD );

		// Trimmed at the first space, as the old VER column did: the server appends its OS and build
		// number, which is three wrapped lines of noise nobody chose a server on.
		const char *pszVer = BROWSER_GetVersion( lServer );
		if (( pszVer != NULL ) && ( pszVer[0] != 0 ))
			y = DrawWrapped( serverbrowser_ShortVersion( pszVer ), x, y, CR_DARKGRAY );

		y += 5;
		DrawSeparator( y );
		y += 6;
		DrawWadList( x, y );

		DrawActionButton( );
	}

	//*************************************************************************
	//
	// [rc4l] A horizontal rule inside the detail panel, fading out towards both ends.
	//
	// Separates what the server IS (name, map, mode, version) from what it wants you to LOAD, which
	// are different kinds of fact: one you read to choose, the other you read to find out what
	// choosing costs. A blank line was not enough to say that -- the WAD list just looked like a
	// fourth detail.
	void DrawSeparator( int vy )
	{
		const int left = serverbrowser_ToScreenX( SB_DETAIL_LEFT + SB_DETAIL_PAD );
		const int right = serverbrowser_ToScreenX( SB_DETAIL_RIGHT - SB_DETAIL_PAD );
		const int top = serverbrowser_ToScreenY( vy );
		// At least one physical pixel: a rule one virtual unit tall rounds to zero on a small window,
		// and a divider that vanishes at some resolutions is worse than none at all.
		const int h = MAX( 1, serverbrowser_ToScreenY( vy + 1 ) - top );
		const int w = right - left;

		for ( int i = 0; i < w; i++ )
		{
			const int a = zx::ComputeSeparatorAlpha( i, w, 130 );
			if ( a <= 0 )
				continue;
			screen->Dim( PalEntry( 150, 170, 215 ), a / 255.f, left + i, top, 1, h );
		}
	}

	//*************************************************************************
	//
	// [rc4l] Every string drawn inside the detail panel goes through here, clipped to the panel.
	//
	// The wrapping below TRIES to make text fit; this makes it impossible for it not to. Those are
	// different guarantees and the panel needs the second one: its contents are server-chosen strings,
	// so the failure case is not a long name someone picked badly but a name picked deliberately to
	// spill over the list beside it. Wrapping I can get wrong; a clip rectangle cannot be got wrong.
	//
	// DTA_Clip* is in SCREEN pixels even though the position is virtual -- the same asymmetry that put
	// the country flags in the corner of the display the first time. Hence ToScreen* on the bounds and
	// raw virtual coordinates on x/y.
	void DrawInPanel( EColorRange color, int x, int y, const char *pszText )
	{
		screen->DrawText( SmallFont, color, x, y, pszText,
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H,
			DTA_ClipLeft, serverbrowser_ToScreenX( SB_DETAIL_LEFT + 4 ),
			DTA_ClipRight, serverbrowser_ToScreenX( SB_DETAIL_RIGHT - 4 ),
			DTA_ClipTop, serverbrowser_ToScreenY( SB_DETAIL_TOP + 4 ),
			DTA_ClipBottom, serverbrowser_ToScreenY( SB_DETAIL_BOTTOM - 4 ),
			TAG_DONE );
	}

	//*************************************************************************
	//
	// Word-wrapped text inside the detail panel. Returns the y the next line should start at.
	int DrawWrapped( const char *pszText, int x, int y, EColorRange color )
	{
		if (( pszText == NULL ) || ( pszText[0] == 0 ))
			return y;

		FString line;
		const FString text = pszText;
		long start = 0;

		while ( start < static_cast<long>( text.Len( )))
		{
			if ( y + SB_DETAIL_LINE > SB_DETAIL_TEXT_BOTTOM )
				return y;					// out of panel; the clip would hide it anyway

			long space = text.IndexOf( " ", start );
			if ( space < 0 )
				space = static_cast<long>( text.Len( ));

			// A single word wider than the panel never fits by wrapping, so it is cut with an ellipsis
			// instead. Without this the loop emits it whole and leans on the clip, which slices it
			// mid-glyph and looks like a rendering fault rather than a truncation.
			FString word = text.Mid( start, space - start );
			if ( SmallFont->StringWidth( word ) > SB_DETAIL_TEXT_W )
				word = serverbrowser_FitName( word, SB_DETAIL_TEXT_W );

			FString candidate = line;
			if ( candidate.IsNotEmpty( ))
				candidate << " ";
			candidate << word;

			if ( line.IsNotEmpty( ) && ( SmallFont->StringWidth( candidate ) > SB_DETAIL_TEXT_W ))
			{
				DrawInPanel( color, x, y, line );
				y += SB_DETAIL_LINE;
				line = word;
			}
			else
			{
				line = candidate;
			}
			start = space + 1;
		}

		if ( line.IsNotEmpty( ) && ( y + SB_DETAIL_LINE <= SB_DETAIL_TEXT_BOTTOM ))
		{
			DrawInPanel( color, x, y, line );
			y += SB_DETAIL_LINE;
		}
		return y;
	}

	//*************************************************************************
	//
	// Each WAD in its own colour, so the list reads as a checklist rather than a sentence. Drawn name
	// by name with the comma attached, because the colour has to belong to the file rather than to the
	// whole line.
	void DrawWadList( int x, int y )
	{
		const int right = SB_DETAIL_RIGHT - SB_DETAIL_PAD;
		int cx = x;

		for ( unsigned i = 0; i < g_DetailWads.Size( ); i++ )
		{
			FString piece = g_DetailWads[i];
			if ( i + 1 < g_DetailWads.Size( ))
				piece << ",";

			// Same reasoning as the wrapper: a filename longer than the panel is cut deliberately rather
			// than left for the clip to sever.
			if ( SmallFont->StringWidth( piece ) > SB_DETAIL_TEXT_W )
				piece = serverbrowser_FitName( piece, SB_DETAIL_TEXT_W );

			const int w = SmallFont->StringWidth( piece );
			if (( cx > x ) && ( cx + w > right ))
			{
				cx = x;
				y += SB_DETAIL_LINE;
			}
			if ( y + SB_DETAIL_LINE > SB_DETAIL_TEXT_BOTTOM )
				return;					// ran out of panel; better truncated than spilling out of it

			DrawInPanel( g_DetailHave[i] ? CR_GREEN : CR_RED, cx, y, piece );
			cx += w + SmallFont->StringWidth( " " );
		}
	}

	//*************************************************************************
	//
	void DrawFooter( zx::BrowserPhase phase, const zx::BrowserCounts &counts )
	{
		const int y = SB_FOOTER_Y;
		FString text;

		// [rc4l] A download for a pending join takes over the footer. Joining leaves the browser open
		// on purpose (see features/server-browser/zx_joinserver.cpp) precisely so this line has
		// somewhere to live -- without it, "press enter, nothing happens, for four minutes" is what
		// the player experiences. The server count can wait; the transfer cannot.
		const FString download = zx::waddownload::StatusLine( );
		if ( download.IsNotEmpty( ))
		{
			screen->DrawText( SmallFont, CR_GOLD,
				( SB_VIRT_W / 2 ) - ( SmallFont->StringWidth( download ) / 2 ), y, download,
				DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );
			return;
		}

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
		const int right = serverbrowser_ToScreenX( SB_LIST_RIGHT );
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
	// [rc4l] Clicking a row.
	//
	// Hit-tested against serverbrowser_ToScreenX/Y with the SAME arguments DimRow draws the highlight
	// with, rather than by inverting the virtual-to-real mapping. VirtualToRealCoordsInt letterboxes
	// and rounds per call, so an inverse would agree with the drawn box on most resolutions and be a
	// pixel out on the rest -- and "the row I clicked is not the row that lit up" is a maddening bug
	// to be told about. Reusing the forward calls means the clickable box IS the visible box.
	//
	// Hover highlights, one click joins. An earlier version made the first click select and the second
	// join, reasoning that a slip should not restart the engine onto another server's WAD set. In use
	// that was just annoying: with no detail pane to show, the first click looks like it did nothing.
	//
	// Moving the highlight on hover is what makes one click safe instead -- you can see which row you
	// are about to commit to before you press, which is the feedback the doubled click was standing in
	// for. Pressing and dragging off the row still cancels, as a button should, so a genuine slip has
	// somewhere to go.
	bool MouseEvent( int type, int x, int y )
	{
		const int total = static_cast<int>( g_SortedServers.Size( ));

		// The confirmation owns the screen while it is up: clicking through it would be answering a
		// question by pressing something else.
		if ( g_ConfirmCancel )
			return true;

		// The action button, before the row hit test -- it lives inside the detail panel, which the
		// row test already excludes, but checking first keeps the two from ever both claiming a click.
		{
			const bool bOverButton = ( g_Selected >= 0 ) && ( g_Selected < total ) &&
				( x >= serverbrowser_ToScreenX( SB_BUTTON_LEFT )) &&
				( x < serverbrowser_ToScreenX( SB_BUTTON_RIGHT )) &&
				( y >= serverbrowser_ToScreenY( SB_BUTTON_TOP )) &&
				( y < serverbrowser_ToScreenY( SB_BUTTON_TOP + SB_BUTTON_H ));

			g_ButtonHot = bOverButton;

			if ( bOverButton )
			{
				if ( type == MOUSE_Click )
				{
					g_ButtonPressed = true;
					return true;			// arms the capture that makes MOUSE_Release arrive
				}

				if ( type == MOUSE_Release )
				{
					const bool bWasPressed = g_ButtonPressed;
					g_ButtonPressed = false;
					if ( bWasPressed )
						PressActionButton( );
					return true;
				}

				return true;				// hover
			}

			// Released away from the button: a drag off it, which cancels like any button.
			if ( type == MOUSE_Release )
				g_ButtonPressed = false;
		}

		const int left = serverbrowser_ToScreenX( SB_PANEL_LEFT + 4 );
		const int right = serverbrowser_ToScreenX( SB_LIST_RIGHT );

		if (( total > 0 ) && ( x >= left ) && ( x < right ))
		{
			for ( int slot = 0; slot < SB_VISIBLE_ROWS; ++slot )
			{
				const int vy = SB_FIRST_ROW_Y + slot * SB_ROW_HEIGHT;
				const int top = serverbrowser_ToScreenY( vy - 2 );
				const int bottom = serverbrowser_ToScreenY( vy - 2 + SB_ROW_HEIGHT );
				if (( y < top ) || ( y >= bottom ))
					continue;

				const int row = zx::ComputeServerAtSlot( slot, SB_VISIBLE_ROWS, g_ScrollFirst, total );
				if ( row < 0 )
					break;					// an empty slot past the last server

				// Hover moves the highlight, which both previews the row and fills the detail strip
				// below with what that server is running. The cursor sound is deliberately left off:
				// it would fire on every row the pointer crosses on the way down the list.
				if ( row != g_Selected )
					g_Selected = row;

				if ( type == MOUSE_Click )
				{
					g_MousePressRow = row;
					// True also arms the capture that makes MOUSE_Release arrive at all (DMenu::
					// Responder only forwards a release while captured).
					return true;
				}

				if ( type == MOUSE_Release )
				{
					// Only the row the press started on, so dragging off cancels.
					const bool bOnPressRow = ( row == g_MousePressRow );
					g_MousePressRow = -1;
					if ( !bOnPressRow )
						return true;

					// Now that a selected row shows details, a single click has something to say --
					// so joining moves to the second click. Same reasoning as any list with a preview
					// pane: one click to look, two to commit.
					const int now = static_cast<int>( DMenu::MenuTime );
					const bool bDouble = ( row == g_LastClickRow ) &&
						(( now - g_LastClickTime ) <= SB_DOUBLECLICK_TICS );
					g_LastClickRow = row;
					g_LastClickTime = now;

					if ( bDouble )
					{
						g_LastClickRow = -1;		// so a third click is not a second double
						return MenuEvent( MKEY_Enter, false );
					}
					return true;
				}

				return true;				// a hover, already handled above
			}
		}

		if ( type == MOUSE_Release )
			g_MousePressRow = -1;
		return Super::MouseEvent( type, x, y );
	}

	//*************************************************************************
	//
	// [rc4l] Y and N for the confirmation. Menu navigation arrives as MKEY_*, but plain letters do
	// not, so the character events have to be taken here before DOptionMenu does anything else with
	// them.
	bool Responder( event_t *ev )
	{
		if ( g_ConfirmCancel && ( ev != NULL ) && ( ev->type == EV_GUI_Event ) &&
			(( ev->subtype == EV_GUI_KeyDown ) || ( ev->subtype == EV_GUI_KeyRepeat ) ||
			 ( ev->subtype == EV_GUI_Char )))
		{
			const int key = ev->data1;

			if (( key == 'y' ) || ( key == 'Y' ))
			{
				AnswerCancelConfirm( true );
				return true;
			}
			if (( key == 'n' ) || ( key == 'N' ))
			{
				AnswerCancelConfirm( false );
				return true;
			}

			// Anything else is swallowed. A question on screen means the next keypress answers it,
			// not that it does something underneath.
			return true;
		}

		return Super::Responder( ev );
	}

	//*************************************************************************
	//
	bool MenuEvent( int mkey, bool fromcontroller )
	{
		const int total = static_cast<int>( g_SortedServers.Size( ));

		// [rc4l] While the question is up it is the only thing that answers: Enter stops the download,
		// Escape keeps it going. Both release the hold, which is the part that must not be skipped.
		if ( g_ConfirmCancel )
		{
			if ( mkey == MKEY_Enter )
				AnswerCancelConfirm( true );
			else if ( mkey == MKEY_Back )
				AnswerCancelConfirm( false );
			return true;
		}

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
			PressActionButton( );
			return true;

		default:
			return Super::MenuEvent( mkey, fromcontroller );
		}
	}
};

IMPLEMENT_CLASS( DFUAServerBrowserMenu )
