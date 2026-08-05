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
#include "features/server-browser/computation/browserchrome_compute.h"
#include "features/server-browser/computation/browserfocus_compute.h"
#include "features/server-browser/computation/bytesize_compute.h"
#include "features/server-browser/computation/browserhit_compute.h"
#include "features/server-browser/computation/colortext_compute.h"
#include "features/server-browser/computation/serverbrowser_compute.h"
#include "features/server-browser/computation/scrollbar_compute.h"
#include "features/server-browser/computation/serversort_compute.h"
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
// [rc4l] Derived from the rule below the tabs rather than hardcoded, so nothing ever sits on it.
#define SB_HEADER_Y			( SB_TAB_SEP_Y + 8 )
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

// [rc4l] The scrollbar track, and where the rows have to stop so they do not run under it. A row
// highlight drawn to SB_LIST_RIGHT covered the bar on the selected row, so the one row you were
// looking at was the one where the bar vanished.
#define SB_SCROLLBAR_X		( SB_LIST_RIGHT - 4 )
#define SB_SCROLLBAR_W		2
#define SB_ROW_RIGHT		( SB_SCROLLBAR_X - 3 )
// Clear of the rule under the tabs by the same padding the tabs use, so nothing touches it.
#define SB_DETAIL_TOP		( SB_TAB_SEP_Y + SB_TAB_PAD )
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

// [rc4l] How much of the panel the WAD list may take before it gives up and says "+N more". A server
// running thirty files would otherwise fill the panel and push the flag words out entirely.
#define SB_WADLIST_MAX_H	88

// [rc4l] The WAD list's scrollbar, on the inside edge of the detail panel. Two pixels wide, matching
// the server list's -- it is a position indicator that happens to be draggable, not a control the eye
// should be drawn to.
#define SB_WADBAR_W			2
#define SB_WADBAR_X			( SB_DETAIL_RIGHT - SB_DETAIL_PAD - SB_WADBAR_W )

#define SB_FOOTER_Y			( SB_ROWS_BOTTOM + 20 )

// [rc4l] The panel's content span, in virtual pixels. ComputePanelRect pads BOTH ends by the corner
// radius, so the visible gap to the screen edge is ( SB_CONTENT_TOP - radius ) above and
// ( SB_VIRT_H - SB_CONTENT_BOTTOM - radius ) below. Deriving the top from the bottom is what forces
// those two to be equal; hardcoding them separately is how the panel ended up 4px from the top edge
// and 28px from the bottom.
#define SB_CONTENT_BOTTOM	( SB_FOOTER_Y + 24 )
#define SB_CONTENT_TOP		( SB_VIRT_H - SB_CONTENT_BOTTOM )
// [rc4l] The tab row, where the "SERVERS" title used to be.
//
// A title that says SERVERS on the server browser is a word doing no work -- the player pressed a
// thing called Servers to get here. The same band now carries the filters instead, and everything
// below it keeps exactly the position it had, so the list and the detail panel are untouched.
//
// Only two tabs today. The row is deliberately left long: favourites, history and LAN all belong
// here eventually, and a layout that has to be redesigned to gain a third tab is one that will be.
// [rc4l] SB_TAB_PAD is the ONE number the band is spaced by, used above the tabs, below them, and
// below the rule. Three separate literals is how "roughly even" happens; deriving all three from one
// is how actually even happens, and it survives anyone changing the tab height later.
#define SB_TAB_PAD			6
#define SB_TAB_LEFT			48
#define SB_TAB_W			78
#define SB_TAB_GAP			6
#define SB_TAB_H			14
#define SB_TAB_TOP			( SB_CONTENT_TOP + SB_TAB_PAD )

// The rule under the row, spanning the whole content width so the tabs read as a header band over
// both the list and the detail panel rather than as something floating above the list alone.
#define SB_TAB_SEP_Y		( SB_TAB_TOP + SB_TAB_H + SB_TAB_PAD )

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

// [rc4l] The row the pointer is over, which is NOT the selected row. Hovering only hints; clicking
// selects. Reset every frame the pointer is somewhere else.
static	int				g_HoverRow = -1;

// [rc4l] Held while the scrollbar thumb is being dragged, so the pointer keeps controlling it even
// after it wanders off the bar -- which is what dragging means everywhere else.
static	bool			g_DraggingScrollbar = false;

// [rc4l] Set when the SELECTION moved and the view must follow it -- keyboard, or a click on a row.
// Scrolling does not set it: scrolling moves the view and leaves the selection alone, which is the
// whole point of them being separate.
static	bool			g_RevealSelection = false;

// [rc4l] Showing "cancel this download?". Drawn and answered by this menu rather than through
// M_StartMessage, so the browser keeps control of the pairing: the hold placed on the join resume
// when this goes up MUST be released on exactly one of the two answers, and a message box that can be
// dismissed by other menu machinery is a way for that to not happen.
static	bool			g_ConfirmCancel = false;

// [rc4l] Which tab is showing. Public is the default because it is what nearly everyone wants nearly
// all the time -- a private server is one you were told about, so you already know it is there.
enum class BrowserTab { Public, Private };
static	BrowserTab		g_Tab = BrowserTab::Public;
static	int				g_TabHot = -1;

// [rc4l] Which region the arrow keys are addressing. Starts on the tabs: that is the top of the loop
// and the only region that is occupiable before any server has answered.
static	zx::BrowserFocus	g_Focus = zx::BrowserFocus::Tabs;

// [rc4l] Where each tab was left. Two entries, indexed by BrowserTab.
static	int				g_TabScroll[2] = { 0, 0 };
static	int				g_TabSelected[2] = { -1, -1 };

// [rc4l] A message occupying the browser's own panel instead of a stock message box over the title
// screen. Same panel, same dimensions, so being refused reads as part of the same screen rather than
// as having been thrown out of it.
static	FString			g_Notice;

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

// [rc4l] Size in bytes beside each name, 0 where the server did not say (see SQF2_FUA_WAD_SIZES).
// Parallel to g_DetailWads, IWAD included: it is never downloaded, but it is listed with the rest,
// and one line silently lacking the number every other line carries reads as a bug.
static	TArray<unsigned int>	g_DetailWadSizes;

// [rc4l] The WAD list scrolls on its own. It USED to stop at whatever fitted and print "+7 more",
// which named a number the player then had no way to see -- the one place in the browser that
// admitted it was hiding something and offered nothing to do about it.
//
// Mouse only, deliberately: nothing in this list is selectable, so giving it keyboard focus would put
// a stop on the way to the JOIN button that does nothing when you get there.
static	int				g_WadScroll = 0;
static	bool			g_DraggingWadBar = false;

// Where the list was last DRAWN, in virtual coordinates, so the wheel and the drag can be tested
// against the same box the player is looking at. Recorded rather than derived because the list
// starts under a variable number of wrapped text lines -- its top is not a constant to test against.
static	int				g_WadListTop = 0;
static	int				g_WadListBottom = 0;
static	int				g_WadListRows = 0;

// [rc4l] The last position the pointer was reported at, in screen pixels. Wheel events do not carry
// one, and which list a notch belongs to is entirely a question of where the pointer is.
static	int				g_MouseX = -1;
static	int				g_MouseY = -1;

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
// [rc4l] Just the NAMES the server is running -- what it has, not what we have.
//
// This used to colour each entry green or red by asking D_AddFile whether we held it, which meant a
// filesystem search per WAD per server, a cache that then had to be invalidated whenever a download
// landed, and a list that was quietly wrong whenever that invalidation was missed. It was answering
// a question the player does not need answered here: if something is missing the join fetches it,
// and says so while it does. Removing the colour removed all of that machinery with it.
static void serverbrowser_RefreshWadCache( int lServer )
{
	if ( lServer == g_DetailServer )
		return;

	g_DetailServer = lServer;
	g_DetailWads.Clear( );
	g_DetailWadSizes.Clear( );

	// A different server means a different list, so the old scroll position describes nothing.
	g_WadScroll = 0;
	g_DraggingWadBar = false;

	const char *pszIwad = BROWSER_GetIWADName( lServer );
	if (( pszIwad != NULL ) && ( pszIwad[0] != 0 ))
	{
		g_DetailWads.Push( pszIwad );
		g_DetailWadSizes.Push( BROWSER_GetIWADSize( lServer ));
	}

	const LONG lPwads = BROWSER_GetNumPWADs( lServer );
	for ( LONG i = 0; i < lPwads; i++ )
	{
		const char *pszPwad = BROWSER_GetPWADName( lServer, i );
		if (( pszPwad != NULL ) && ( pszPwad[0] != 0 ))
		{
			g_DetailWads.Push( pszPwad );
			g_DetailWadSizes.Push( BROWSER_GetPWADSize( lServer, i ));
		}
	}
}

//*****************************************************************************
//
// [rc4l] Ping ascending, so the servers a player can actually enjoy are at the top. Ties broken by
// address so the order is stable -- an unstable sort makes rows swap places as pings jitter, which
// looks like the list is malfunctioning.
// [rc4l] Busiest first, then alphabetically -- see computation/serversort_compute.h for why, and for
// the two things the name comparison has to strip before it means anything.
//
// Humans only, matching the count the row draws: sorting a server to the top for holding seven bots
// would be ranking it by a number the player can already see is not people.
static int STACK_ARGS serverbrowser_CompareServers( const void *pA, const void *pB )
{
	const int lA = *reinterpret_cast<const int *>( pA );
	const int lB = *reinterpret_cast<const int *>( pB );

	const char *pszNameA = BROWSER_GetHostName( lA );
	const char *pszNameB = BROWSER_GetHostName( lB );

	const int lResult = zx::CompareServers(
		static_cast<int>( BROWSER_GetNumHumanPlayers( lA )), ( pszNameA != NULL ) ? pszNameA : "",
		static_cast<int>( BROWSER_GetNumHumanPlayers( lB )), ( pszNameB != NULL ) ? pszNameB : "" );

	// Two servers with the same name and the same population still need a stable order, or the list
	// reshuffles them on every refresh.
	return ( lResult != 0 ) ? lResult : ( lA - lB );
}

//*****************************************************************************
//
static void serverbrowser_RebuildList( void )
{
	g_SortedServers.Clear();

	// [rc4l] The tab is a filter over the same list rather than a second list: everything is still
	// queried, so switching tabs is instant and never re-fetches anything.
	const bool bWantPrivate = ( g_Tab == BrowserTab::Private );

	for ( ULONG ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( BROWSER_IsActive( ulIdx ) == false )
			continue;
		if ( BROWSER_IsPasswordProtected( ulIdx ) != bWantPrivate )
			continue;

		g_SortedServers.Push( static_cast<int>( ulIdx ));
	}

	if ( g_SortedServers.Size( ) > 1 )
		qsort( &g_SortedServers[0], g_SortedServers.Size( ), sizeof( int ), serverbrowser_CompareServers );

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
		g_Focus = zx::BrowserFocus::Tabs;

		// Per-tab memory is per-VISIT: the list is being requeried from nothing, so a position saved
		// against the last set of servers describes rows that are not there yet.
		g_TabScroll[0] = g_TabScroll[1] = 0;
		g_TabSelected[0] = g_TabSelected[1] = -1;

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
	// [rc4l] What is on screen is decided in ONE place -- computation/browserchrome_compute -- and both
	// drawing and hit-testing read the same answer, so a control can never be invisible and still
	// clickable.
	unsigned VisibleParts( const zx::BrowserCounts &counts )
	{
		const bool bWaitingRegistry = BROWSER_WaitingForServerRegistryResponse( );
		const zx::BrowserPhase phase = zx::ComputeBrowserPhase( bWaitingRegistry, counts );
		const int total = static_cast<int>( g_SortedServers.Size( ));

		return zx::ComputeVisibleParts( phase, ( g_Selected >= 0 ) && ( g_Selected < total ),
			serverbrowser_DownloadRunning( ));
	}

	void Drawer( )
	{
		const zx::BrowserCounts counts = serverbrowser_CountStates( );
		const bool bWaitingRegistry = BROWSER_WaitingForServerRegistryResponse( );
		const zx::BrowserPhase phase = zx::ComputeBrowserPhase( bWaitingRegistry, counts );
		const unsigned parts = VisibleParts( counts );

		DrawPanel( );

		if ( parts & zx::kPartTabs )
			DrawTabs( );
		if ( parts & zx::kPartList )
			DrawRows( counts );
		if ( parts & zx::kPartPlaceholder )
			DrawPlaceholder( phase );
		if ( parts & zx::kPartDetail )
			DrawDetails( );
		if ( parts & zx::kPartFooter )
			DrawFooter( phase, counts );

		// Last, and over everything: something the player has to deal with before anything else.
		if ( g_Notice.IsNotEmpty( ))
			DrawNotice( );
		else if ( g_ConfirmCancel )
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
	// [rc4l] The list scrollbar: a track down the right edge of the list with a thumb sized to the
	// fraction visible and placed by how far down we are.
	//
	// The list has always scrolled -- ComputeRowWindow has done that from the start -- but with no
	// indication that it was. Fourteen rows of a twenty-server list looked exactly like a
	// twenty-server list that was fourteen long, so there was nothing to tell a player there was more
	// below. Drawn only when it means something: a list that fits needs no bar.
	void DrawListScrollbar( int total, int first )
	{
		if ( total <= SB_VISIBLE_ROWS )
			return;

		const int vTop = SB_FIRST_ROW_Y - 2;
		const int vHeight = SB_VISIBLE_ROWS * SB_ROW_HEIGHT;

		const int left = serverbrowser_ToScreenX( SB_SCROLLBAR_X );
		const int width = MAX( 1, serverbrowser_ToScreenX( SB_SCROLLBAR_X + SB_SCROLLBAR_W ) - left );
		const int top = serverbrowser_ToScreenY( vTop );
		const int height = serverbrowser_ToScreenY( vTop + vHeight ) - top;
		if ( height <= 0 )
			return;

		screen->Dim( PalEntry( 120, 140, 180 ), 0.14f, left, top, width, height );

		// Geometry via computation/scrollbar_compute, which the hit test also uses -- the two working
		// it out separately is exactly how clicking the bar came to jump somewhere the thumb was not.
		const int minThumb = serverbrowser_ToScreenY( 8 ) - serverbrowser_ToScreenY( 0 );
		const int thumbH = zx::ComputeThumbHeight( height, SB_VISIBLE_ROWS, total, minThumb );
		const int thumbY = top + zx::ComputeThumbTop( height, thumbH, first, total - SB_VISIBLE_ROWS );

		screen->Dim( PalEntry( 170, 190, 230 ), 0.55f, left, thumbY, width, thumbH );
	}

	//*************************************************************************
	//
	void DrawRows( const zx::BrowserCounts &counts )
	{
		const int total = static_cast<int>( g_SortedServers.Size( ));

		// [rc4l] The SCROLL POSITION is the source of truth for what is on screen, and the selection
		// only drags it when the selection itself moved.
		//
		// It used to be the other way round -- the window was derived from the selection every frame
		// -- which crossed two separate things. Scrolling then had to move the selection to have any
		// effect, so the wheel and the scrollbar both changed what was picked; and worse, they could
		// not actually reach the end of the list, because "keep row 13 visible" is already satisfied
		// by showing rows 0-13, so dragging the thumb to the bottom moved the selection and left the
		// view exactly where it was. Both complaints were the same mistake.
		if ( g_RevealSelection )
		{
			const zx::RowWindow window = zx::ComputeRowWindow( total, SB_VISIBLE_ROWS, g_Selected,
				g_ScrollFirst );
			g_ScrollFirst = window.first;
			g_RevealSelection = false;
		}

		// Every frame, not just after a scroll: servers time out and drop out of the list while it is
		// open, so a position that was in range a second ago may not be one now.
		g_ScrollFirst = zx::ComputeRestoredScroll( g_ScrollFirst, total, SB_VISIBLE_ROWS );

		zx::RowWindow window;
		window.first = g_ScrollFirst;
		window.count = total - g_ScrollFirst;
		if ( window.count > SB_VISIBLE_ROWS )
			window.count = SB_VISIBLE_ROWS;
		if ( window.count < 0 )
			window.count = 0;

		DrawListScrollbar( total, window.first );

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
			{
				DimRow( y );
			}
			else if ( row == g_HoverRow )
			{
				// [rc4l] Hover is a HINT, not a selection. It used to move the selection outright,
				// which meant sweeping the pointer across the list repainted every row it crossed and
				// rewrote the whole detail panel each time -- names flicking between red and white,
				// the panel churning through servers nobody asked about. A faint band says "this is
				// what you would be clicking" without claiming anything happened.
				screen->Dim( PalEntry( 150, 170, 215 ), 0.06f,
					serverbrowser_ToScreenX( SB_PANEL_LEFT + 4 ),
					serverbrowser_ToScreenY( y - 2 ),
					serverbrowser_ToScreenX( SB_ROW_RIGHT ) - serverbrowser_ToScreenX( SB_PANEL_LEFT + 4 ),
					serverbrowser_ToScreenY( y - 2 + SB_ROW_HEIGHT ) - serverbrowser_ToScreenY( y - 2 ));
			}

			serverbrowser_DrawCountry( lServer, SB_COL_FLAG, y );

			const int ty = serverbrowser_RowTextY( y, SmallFont->GetHeight( ));

			// [rc4l] White whether selected or not. CR_UNTRANSLATED is the font's own colour, which for
			// SmallFont is Doom red -- so every unselected server read as a warning about itself, and
			// the highlight had to carry the selection on its own anyway.
			const FString name = serverbrowser_FitName( BROWSER_GetHostName( lServer ), SB_NAME_MAX_WIDTH );
			screen->DrawText( SmallFont, CR_WHITE,
				SB_COL_NAME, ty, name, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );

			// Humans only -- a row reading 8/8 for seven bots and one person is a lie the player
			// only discovers after joining.
			const int humans = static_cast<int>( BROWSER_GetNumHumanPlayers( lServer ));
			const int slots = static_cast<int>( BROWSER_GetMaxClients( lServer ));

			FString players;
			players.Format( "%d/%d", humans, slots );

			// [rc4l] Colour only where it means something, the same way ping does: full is the one
			// state that changes what you can do about the row, so it is the only one worth marking.
			const EColorRange playersColor = (( slots > 0 ) && ( humans >= slots )) ? CR_RED : CR_WHITE;
			screen->DrawText( SmallFont, playersColor, SB_COL_PLAYERS, ty, players, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );

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
	// [rc4l] The tab row: oval buttons where the title used to be, and the rule beneath them.
	void DrawTabs( )
	{
		static const char *const labels[] = { "PUBLIC", "PRIVATE" };

		for ( int i = 0; i < 2; ++i )
		{
			const int vLeft = SB_TAB_LEFT + i * ( SB_TAB_W + SB_TAB_GAP );
			const int left = serverbrowser_ToScreenX( vLeft );
			const int right = serverbrowser_ToScreenX( vLeft + SB_TAB_W );
			const int top = serverbrowser_ToScreenY( SB_TAB_TOP );
			const int bottom = serverbrowser_ToScreenY( SB_TAB_TOP + SB_TAB_H );

			const int w = right - left;
			const int h = bottom - top;
			if (( w <= 0 ) || ( h <= 0 ))
				continue;

			// Fully oval: the radius is half the height, so the ends are semicircles rather than the
			// softened corners the panels use. Different shape, different job -- these switch what you
			// are looking at, they are not another surface to put things on.
			const int radius = h / 2;
			const bool bSelected = ( static_cast<int>( g_Tab ) == i );

			// Keyboard focus lights the tab the same way the pointer does, so "what does an arrow key
			// do right now" is answered by looking at the screen rather than by pressing one.
			const bool bHot = ( g_TabHot == i ) ||
				(( g_Focus == zx::BrowserFocus::Tabs ) && bSelected );

			const int base = bSelected ? 96 : ( bHot ? 62 : 38 );
			const zx::PanelColor topCol = { static_cast<BYTE>( base ), static_cast<BYTE>( base ),
				static_cast<BYTE>( base + 24 ), static_cast<BYTE>( bSelected ? 235 : 190 ) };
			const zx::PanelColor botCol = { static_cast<BYTE>( base / 2 ), static_cast<BYTE>( base / 2 ),
				static_cast<BYTE>( base / 2 + 18 ), static_cast<BYTE>( bSelected ? 245 : 205 ) };

			for ( int row = 0; row < h; ++row )
			{
				const int inset = zx::ComputeRoundedInset( row, h, radius );
				const int rowW = w - 2 * inset;
				if ( rowW <= 0 )
					continue;

				const zx::PanelColor c = zx::ComputePanelGradient( row, h, topCol, botCol );
				screen->Dim( PalEntry( c.r, c.g, c.b ), c.a / 255.f, left + inset, top + row, rowW, 1 );
			}

			const int textY = SB_TAB_TOP + ( SB_TAB_H - SmallFont->GetHeight( )) / 2 + 1;
			screen->DrawText( SmallFont, bSelected ? CR_WHITE : CR_DARKGRAY,
				vLeft + ( SB_TAB_W / 2 ) - ( SmallFont->StringWidth( labels[i] ) / 2 ), textY,
				labels[i], DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );
		}

		DrawSeparatorSpan( SB_TAB_SEP_Y, SB_PANEL_LEFT + 12, SB_DETAIL_RIGHT );
	}

	//*************************************************************************
	//
	// [rc4l] A refusal, on the browser's own panel.
	//
	// M_StartMessage draws a stock box over whatever is behind the menu -- which, when the browser
	// reached it through a console command or a failed join, was the title screen. Being told "that
	// server is full" while staring at Freedoom's cover art reads as having been thrown out of the
	// browser rather than answered by it. Same panel, same dimensions, so it is the same screen.
	void DrawNotice( )
	{
		// Dim what is behind it rather than hiding it: the list stays visible underneath, which keeps
		// the sense of place the stock box loses.
		screen->Dim( PalEntry( 0, 0, 0 ), 0.72f,
			serverbrowser_ToScreenX( SB_PANEL_LEFT ), serverbrowser_ToScreenY( SB_CONTENT_TOP ),
			serverbrowser_ToScreenX( SB_DETAIL_RIGHT ) - serverbrowser_ToScreenX( SB_PANEL_LEFT ),
			serverbrowser_ToScreenY( SB_CONTENT_BOTTOM ) - serverbrowser_ToScreenY( SB_CONTENT_TOP ));

		// Wrapped to the panel, so a server name in the text cannot push a line off the side.
		const int wrapWidth = SB_DETAIL_RIGHT - SB_PANEL_LEFT - 48;
		TArray<FBrokenLines *> dummy;
		FBrokenLines *lines = V_BreakLines( SmallFont, wrapWidth, g_Notice.GetChars( ));
		(void)dummy;

		int count = 0;
		while ( lines[count].Width >= 0 )
			count++;

		int y = ( SB_VIRT_H / 2 ) - (( count * ( SmallFont->GetHeight( ) + 2 )) / 2 );
		for ( int i = 0; i < count; ++i )
		{
			screen->DrawText( SmallFont, CR_WHITE,
				(( SB_PANEL_LEFT + SB_DETAIL_RIGHT ) / 2 ) - ( lines[i].Width / 2 ), y,
				lines[i].Text, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );
			y += SmallFont->GetHeight( ) + 2;
		}
		V_FreeBrokenLines( lines );

		y += 6;
		const char *const dismiss = "press a key";
		screen->DrawText( SmallFont, CR_DARKGRAY,
			(( SB_PANEL_LEFT + SB_DETAIL_RIGHT ) / 2 ) - ( SmallFont->StringWidth( dismiss ) / 2 ), y,
			dismiss, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );
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
	// [rc4l] Switching tabs resets the selection and the scroll: the row that was highlighted belongs
	// to a list that is no longer on screen, and carrying its INDEX across would highlight whatever
	// unrelated server happened to land in that position.
	void SelectTab( BrowserTab tab )
	{
		if ( g_Tab == tab )
			return;

		// [rc4l] Each tab keeps its own place. Coming back to a tab and finding it scrolled to the
		// top again means losing your spot every time you glance at the other one -- and the row
		// INDEX cannot simply be carried across, because it points into a list that is no longer
		// there and would land on an unrelated server.
		const int leaving = static_cast<int>( g_Tab );
		g_TabScroll[leaving] = g_ScrollFirst;
		g_TabSelected[leaving] = g_Selected;

		g_Tab = tab;

		const int entering = static_cast<int>( tab );
		g_ScrollFirst = g_TabScroll[entering];
		g_Selected = g_TabSelected[entering];

		// Rebuild first, then let the clamp in DrawRows deal with a remembered position that no
		// longer fits: servers come and go between visits, so the spot we saved may be past the end.
		serverbrowser_RebuildList( );
		S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
	}

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
		const bool bLit = g_ButtonHot || ( g_Focus == zx::BrowserFocus::Action );

		int base = bLit ? 70 : 45;
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
	// [rc4l] Only called when kPartDetail says so, which already means there is a selection -- the
	// bounds check stays as the thing that makes the indexing below safe, not as a policy decision.
	void DrawDetails( )
	{
		const int total = static_cast<int>( g_SortedServers.Size( ));
		if (( g_Selected < 0 ) || ( g_Selected >= total ))
			return;

		DrawDetailPanel( );

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
	// [rc4l] Spanning an arbitrary width, so the same rule serves the detail panel and the band under
	// the tab row. Same helper, same fade, so the two read as the same kind of division.
	void DrawSeparatorSpan( int vy, int vLeft, int vRight )
	{
		const int left = serverbrowser_ToScreenX( vLeft );
		const int right = serverbrowser_ToScreenX( vRight );
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

	void DrawSeparator( int vy )
	{
		DrawSeparatorSpan( vy, SB_DETAIL_LEFT + SB_DETAIL_PAD, SB_DETAIL_RIGHT - SB_DETAIL_PAD );
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
	// [rc4l] One file per line, with what it would cost to fetch it.
	//
	// This used to flow the names into a paragraph and, when it ran out of room, print "+7 more" --
	// naming a number the player then had no way to see. It scrolls now instead, which is also why one
	// per line: a size belongs to a file, and a size floating in the middle of a wrapped sentence
	// belongs to whichever filename you guess it does.
	//
	// Returns the y it finished on, so what follows can be placed under it. Still capped in height: a
	// server running thirty files must not push the JOIN button off the panel.
	int DrawWadList( int x, int y )
	{
		const int total = static_cast<int>( g_DetailWads.Size( ));
		const int bottom = ( y + SB_WADLIST_MAX_H < SB_DETAIL_TEXT_BOTTOM )
			? ( y + SB_WADLIST_MAX_H ) : SB_DETAIL_TEXT_BOTTOM;

		int rows = ( bottom - y ) / SB_DETAIL_LINE;
		if ( rows < 1 )
			rows = 1;

		// Same clamp the server list uses, and for the same reason: the list can change under a scroll
		// position that was in range when it was set.
		g_WadScroll = zx::ComputeRestoredScroll( g_WadScroll, total, rows );

		// Recorded for the wheel and the drag, which cannot work this out for themselves -- the list
		// begins under a variable number of wrapped lines above it.
		g_WadListTop = y;
		g_WadListBottom = y + rows * SB_DETAIL_LINE;
		g_WadListRows = rows;

		const bool bScrolls = ( total > rows );
		const int textW = SB_DETAIL_TEXT_W - ( bScrolls ? ( SB_WADBAR_W + 3 ) : 0 );

		for ( int i = 0; ( i < rows ) && (( g_WadScroll + i ) < total ); i++ )
		{
			const int entry = g_WadScroll + i;
			const int lineY = y + i * SB_DETAIL_LINE;

			// The size first: it is fixed and short, so the name gets whatever is left rather than the
			// other way round -- truncating "23mb" to save room in a filename helps nobody.
			FString size;
			if ( g_DetailWadSizes[entry] > 0 )
				size.Format( "(%s)", zx::FormatByteSize( g_DetailWadSizes[entry] ).c_str( ));

			const int sizeW = size.IsNotEmpty( ) ? SmallFont->StringWidth( size ) : 0;
			const int gap = size.IsNotEmpty( ) ? SmallFont->StringWidth( " " ) : 0;

			FString name = g_DetailWads[entry];
			if ( SmallFont->StringWidth( name ) > ( textW - sizeW - gap ))
				name = serverbrowser_FitName( name, textW - sizeW - gap );

			// CR_WHITE, not CR_UNTRANSLATED: untranslated means the font's own colour, and SmallFont's
			// own colour is Doom red -- which is exactly the "missing" colour this stopped using.
			DrawInPanel( CR_WHITE, x, lineY, name );

			if ( size.IsNotEmpty( ))
				DrawInPanel( CR_DARKGRAY, x + SmallFont->StringWidth( name ) + gap, lineY, size );
		}

		if ( bScrolls )
			DrawWadScrollbar( total, rows );

		return g_WadListBottom;
	}

	//*************************************************************************
	//
	// [rc4l] The WAD list's own bar, on the inside edge of the panel. Same geometry unit as the server
	// list's -- if the two worked their thumbs out separately, they would drift apart the moment one
	// of them was adjusted.
	void DrawWadScrollbar( int total, int rows )
	{
		const int left = serverbrowser_ToScreenX( SB_WADBAR_X );
		const int width = MAX( 1, serverbrowser_ToScreenX( SB_WADBAR_X + SB_WADBAR_W ) - left );
		const int top = serverbrowser_ToScreenY( g_WadListTop );
		const int height = serverbrowser_ToScreenY( g_WadListBottom ) - top;
		if ( height <= 0 )
			return;

		screen->Dim( PalEntry( 120, 140, 180 ), 0.14f, left, top, width, height );

		const int minThumb = serverbrowser_ToScreenY( 6 ) - serverbrowser_ToScreenY( 0 );
		const int thumbH = zx::ComputeThumbHeight( height, rows, total, minThumb );
		const int thumbY = top + zx::ComputeThumbTop( height, thumbH, g_WadScroll, total - rows );

		screen->Dim( PalEntry( 170, 190, 230 ), 0.55f, left, thumbY, width, thumbH );
	}

	//*************************************************************************
	//
	void DrawFooter( zx::BrowserPhase phase, const zx::BrowserCounts &counts )
	{
		const int y = SB_FOOTER_Y;
		FString text;

		// [rc4l] The transfer used to be drawn here as well, because this was the only screen that had
		// anywhere to put it. It is now in the band at the top of the screen, which is drawn over
		// everything including this menu -- so repeating it here would just be the same sentence twice
		// on one screen. See zx::DrawJoinReadyNotice.

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
		const int right = serverbrowser_ToScreenX( SB_ROW_RIGHT );
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

		// The same answer the drawing uses. A tab that is not on screen must not be clickable, which is
		// exactly what happens the moment these two work it out separately.
		const unsigned parts = VisibleParts( serverbrowser_CountStates( ));

		// Remembered for the wheel, which arrives as a GUI event carrying no position of its own --
		// and a wheel notch has to scroll whichever list the pointer is sitting over.
		g_MouseX = x;
		g_MouseY = y;

		// Cleared here and set again below only if the pointer is actually over a row, so the hint
		// does not linger on a row the pointer left.
		g_HoverRow = -1;

		// A notice or a question owns the screen while it is up: clicking through would be answering
		// by pressing something else entirely.
		if ( g_Notice.IsNotEmpty( ))
		{
			if ( type == MOUSE_Release )
				g_Notice = "";
			return true;
		}
		if ( g_ConfirmCancel )
			return true;

		// The tabs.
		if ( parts & zx::kPartTabs )
		{
			g_TabHot = -1;
			for ( int i = 0; i < 2; ++i )
			{
				const int vLeft = SB_TAB_LEFT + i * ( SB_TAB_W + SB_TAB_GAP );
				if (( x < serverbrowser_ToScreenX( vLeft )) ||
					( x >= serverbrowser_ToScreenX( vLeft + SB_TAB_W )) ||
					( y < serverbrowser_ToScreenY( SB_TAB_TOP )) ||
					( y >= serverbrowser_ToScreenY( SB_TAB_TOP + SB_TAB_H )))
				{
					continue;
				}

				g_TabHot = i;
				if ( type == MOUSE_Release )
				{
					// The pointer moves the keyboard's focus with it, so picking something up with the
					// mouse and then reaching for the arrows carries on from where you are looking
					// rather than from wherever the keyboard was left.
					g_Focus = zx::BrowserFocus::Tabs;
					SelectTab( static_cast<BrowserTab>( i ));
				}
				return true;
			}
		}

		// The action button, before the row hit test -- it lives inside the detail panel, which the
		// row test already excludes, but checking first keeps the two from ever both claiming a click.
		if ( parts & zx::kPartDetail )
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
					{
						g_Focus = zx::BrowserFocus::Action;
						PressActionButton( );
					}
					return true;
				}

				return true;				// hover
			}

			// Released away from the button: a drag off it, which cancels like any button.
			if ( type == MOUSE_Release )
				g_ButtonPressed = false;
		}

		// [rc4l] The WAD list's bar, which lives inside the detail panel and so is tested before the
		// server list's -- the two boxes never overlap, but a held drag has to keep being answered by
		// the bar that started it rather than by whichever one the pointer has wandered over.
		if ( parts & zx::kPartDetail )
		{
			const int wadTotal = static_cast<int>( g_DetailWads.Size( ));
			const int height = serverbrowser_ToScreenY( g_WadListBottom ) - serverbrowser_ToScreenY( g_WadListTop );

			const bool bOverBar = ( wadTotal > g_WadListRows ) && ( g_WadListRows > 0 ) &&
				( x >= serverbrowser_ToScreenX( SB_WADBAR_X - 3 )) &&
				( x < serverbrowser_ToScreenX( SB_WADBAR_X + SB_WADBAR_W + 3 )) &&
				( y >= serverbrowser_ToScreenY( g_WadListTop )) &&
				( y < serverbrowser_ToScreenY( g_WadListBottom ));

			if ( type == MOUSE_Click )
				g_DraggingWadBar = bOverBar;

			if ( g_DraggingWadBar && ( height > 0 ) && ( wadTotal > g_WadListRows ))
			{
				const int top = serverbrowser_ToScreenY( g_WadListTop );
				const int minThumb = serverbrowser_ToScreenY( 6 ) - serverbrowser_ToScreenY( 0 );
				const int thumbH = zx::ComputeThumbHeight( height, g_WadListRows, wadTotal, minThumb );

				g_WadScroll = zx::ComputeFirstFromPointer( y - top, height, thumbH,
					wadTotal - g_WadListRows );
			}

			if ( g_DraggingWadBar )
			{
				if ( type == MOUSE_Release )
					g_DraggingWadBar = false;
				return true;
			}
		}

		// [rc4l] The scrollbar, BEFORE the rows. The row hit box used to run all the way to
		// SB_LIST_RIGHT, which is past the bar -- so every click meant for the thumb landed on
		// whatever row was level with it and the bar could not be grabbed at all.
		if ( parts & zx::kPartList )
		{
			const bool bOverBar = ( total > SB_VISIBLE_ROWS ) &&
				( x >= serverbrowser_ToScreenX( SB_SCROLLBAR_X - 2 )) &&
				( x < serverbrowser_ToScreenX( SB_SCROLLBAR_X + SB_SCROLLBAR_W + 2 )) &&
				( y >= serverbrowser_ToScreenY( SB_FIRST_ROW_Y - 2 )) &&
				( y < serverbrowser_ToScreenY( SB_FIRST_ROW_Y - 2 + SB_VISIBLE_ROWS * SB_ROW_HEIGHT ));

			if ( type == MOUSE_Click )
				g_DraggingScrollbar = bOverBar;

			if ( g_DraggingScrollbar )
			{
				// Track the pointer for as long as the button is held, even once it wanders off the
				// bar -- that is what dragging a scrollbar means everywhere else.
				const int top = serverbrowser_ToScreenY( SB_FIRST_ROW_Y - 2 );
				const int height = serverbrowser_ToScreenY( SB_FIRST_ROW_Y - 2 + SB_VISIBLE_ROWS * SB_ROW_HEIGHT ) - top;

				if ( height > 0 )
				{
					// Same geometry the drawing uses, so the thumb lands where it was grabbed.
					const int minThumb = serverbrowser_ToScreenY( 8 ) - serverbrowser_ToScreenY( 0 );
					const int thumbH = zx::ComputeThumbHeight( height, SB_VISIBLE_ROWS, total, minThumb );

					// Moves the VIEW only. What is selected is none of the scrollbar's business.
					g_ScrollFirst = zx::ComputeFirstFromPointer( y - top, height, thumbH,
						total - SB_VISIBLE_ROWS );
				}

				if ( type == MOUSE_Release )
					g_DraggingScrollbar = false;
				return true;
			}
		}

		const int left = serverbrowser_ToScreenX( SB_PANEL_LEFT + 4 );
		const int right = serverbrowser_ToScreenX( SB_ROW_RIGHT );

		if (( parts & zx::kPartList ) && ( total > 0 ) && ( x >= left ) && ( x < right ))
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

				// [rc4l] Hover marks the row and nothing more. Moving the SELECTION on hover meant
				// dragging the pointer down the list rewrote the detail panel for every row on the
				// way, so the panel churned through servers nobody had asked about and names flicked
				// colour under the cursor. Selecting is what a click is for.
				g_HoverRow = row;

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

					// One click to look, two to commit -- the ordinary shape of a list with a preview
					// pane, and now the only way the selection moves by mouse at all.
					g_Selected = row;
					g_RevealSelection = true;
					g_Focus = zx::BrowserFocus::Rows;

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

		// [rc4l] The wheel. DOptionMenu's own wheel handling scrolls ITS item list, which this menu
		// does not use -- so without this the bar was drawn, the list scrolled by keyboard, and the
		// wheel did nothing at all.
		//
		// Scrolls the VIEW and nothing else. Picking a server is what clicking and the arrow keys are
		// for; a wheel notch changing what you have selected is how you end up joining something you
		// only scrolled past. Three rows a notch, the usual amount.
		if (( ev != NULL ) && ( ev->type == EV_GUI_Event ) && !g_ConfirmCancel && g_Notice.IsEmpty( ))
		{
			const int total = static_cast<int>( g_SortedServers.Size( ));
			if (( ev->subtype == EV_GUI_WheelUp ) || ( ev->subtype == EV_GUI_WheelDown ))
			{
				const int step = ( ev->subtype == EV_GUI_WheelUp ) ? -3 : 3;

				// Over the WAD list, the notch belongs to the WAD list. Two scrollable things on one
				// screen and one wheel: the only sane rule is that it drives whichever one you are
				// pointing at.
				const int wadTotal = static_cast<int>( g_DetailWads.Size( ));
				if (( g_WadListRows > 0 ) && ( wadTotal > g_WadListRows ) &&
					( g_MouseY >= serverbrowser_ToScreenY( g_WadListTop )) &&
					( g_MouseY < serverbrowser_ToScreenY( g_WadListBottom )) &&
					( g_MouseX >= serverbrowser_ToScreenX( SB_DETAIL_LEFT )) &&
					( g_MouseX < serverbrowser_ToScreenX( SB_DETAIL_RIGHT )))
				{
					g_WadScroll = zx::ComputeRestoredScroll( g_WadScroll + step, wadTotal, g_WadListRows );
					return true;
				}

				const int maxFirst = ( total > SB_VISIBLE_ROWS ) ? ( total - SB_VISIBLE_ROWS ) : 0;
				int next = g_ScrollFirst + step;

				if ( next < 0 )
					next = 0;
				if ( next > maxFirst )
					next = maxFirst;

				g_ScrollFirst = next;
				return true;
			}
		}

		return Super::Responder( ev );
	}

	//*************************************************************************
	//
	// [rc4l] One arrow key, applied to whichever region has focus. The rule itself lives in
	// computation/browserfocus_compute -- everything here is the part that touches the engine: moving
	// the selection, switching the tab, and making a noise about it.
	bool Navigate( zx::NavKey key, int total )
	{
		// Nothing to steer while the browser is still looking: there are no tabs on screen and no rows
		// to be on. Swallowed rather than passed up, so an arrow key does not escape into the menu
		// machinery underneath and move something the player cannot see.
		if (( VisibleParts( serverbrowser_CountStates( )) & zx::kPartTabs ) == 0 )
			return true;

		const zx::NavResult nav = zx::ComputeNav( g_Focus, key, total > 0 );
		const zx::BrowserFocus was = g_Focus;
		g_Focus = nav.focus;

		if ( nav.tabStep != 0 )
		{
			// Two tabs, so either step is the other one. Wraps rather than stopping at the ends,
			// because with two of them "the other tab" is what both keys mean anyway.
			SelectTab(( g_Tab == BrowserTab::Public ) ? BrowserTab::Private : BrowserTab::Public );
			return true;
		}

		if ( nav.rowStep != 0 )
		{
			// The keyboard is the one thing that MOVES the selection through the list, so it is the
			// one thing that drags the view along with it. Scrolling leaves the selection alone.
			if ( nav.rowStep < 0 )
				g_Selected = ( g_Selected <= 0 ) ? total - 1 : g_Selected - 1;
			else
				g_Selected = ( g_Selected >= total - 1 ) ? 0 : g_Selected + 1;

			g_RevealSelection = true;
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
			return true;
		}

		if ( g_Focus != was )
		{
			// Entering the list with nothing picked yet has to pick something, or the panel and the
			// button below it have nothing to describe.
			if (( g_Focus == zx::BrowserFocus::Rows ) && ( g_Selected < 0 ) && ( total > 0 ))
			{
				g_Selected = 0;
				g_RevealSelection = true;
			}

			S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
		}

		return true;
	}

	//*************************************************************************
	//
	bool MenuEvent( int mkey, bool fromcontroller )
	{
		const int total = static_cast<int>( g_SortedServers.Size( ));

		// A notice is dismissed by anything, and consumes it -- so the key that clears it does not
		// also do whatever it would normally have done underneath.
		if ( g_Notice.IsNotEmpty( ))
		{
			g_Notice = "";
			return true;
		}

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
		case MKEY_Up:		return Navigate( zx::NavKey::Up, total );
		case MKEY_Down:		return Navigate( zx::NavKey::Down, total );
		case MKEY_Left:		return Navigate( zx::NavKey::Left, total );
		case MKEY_Right:	return Navigate( zx::NavKey::Right, total );

		// [rc4l] Enter acts on whatever has focus. On the tabs that is the tab -- which is already
		// selected, so it is the way into the list without reaching for Down.
		case MKEY_Enter:
			if ( g_Focus == zx::BrowserFocus::Tabs )
			{
				if ( total > 0 )
				{
					g_Focus = zx::BrowserFocus::Rows;
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
				}
				return true;
			}

			PressActionButton( );
			return true;

		default:
			return Super::MenuEvent( mkey, fromcontroller );
		}
	}
};

IMPLEMENT_CLASS( DFUAServerBrowserMenu )

//*****************************************************************************
//
namespace zx
{

void ShowBrowserNotice( const char *text )
{
	g_Notice = ( text != NULL ) ? text : "";
}

bool IsServerBrowserOpen( void )
{
	return ( DMenu::CurrentMenu != NULL ) &&
		( DMenu::CurrentMenu->IsKindOf( RUNTIME_CLASS( DFUAServerBrowserMenu )));
}


} // namespace zx
