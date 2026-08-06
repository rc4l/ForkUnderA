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
#include "i_input.h"		// [rc4l] I_PutInClipboard / I_GetFromClipboard, for the search box
#include "features/server-browser/computation/browserchrome_compute.h"
#include "features/server-browser/computation/glowtravel_compute.h"
#include "features/server-browser/computation/serversearch_compute.h"
#include "features/server-browser/computation/textinput_compute.h"
#include "features/server-browser/computation/tooltip_compute.h"
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
// [rc4l] The search box, sharing the tab row and ending where the list does. Right-of-centre in the
// space the tabs leave, which is the only clear room on that line -- and it reads as belonging to the
// list underneath it rather than to the panel on the right.
#define SB_SEARCH_RIGHT		SB_DETAIL_RIGHT
#define SB_SEARCH_W			150
#define SB_SEARCH_LEFT		( SB_SEARCH_RIGHT - SB_SEARCH_W )
#define SB_SEARCH_TOP		SB_TAB_TOP
#define SB_SEARCH_H			SB_TAB_H
#define SB_SEARCH_PAD		5

// A query longer than the box can show is a query nobody can read back. 40 is comfortably past any
// server name worth typing a fragment of.
#define SB_SEARCH_MAXLEN	40

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

// [rc4l] Where the focus glow goes this frame, in virtual coordinates. Set by whichever component
// owns the focus as it draws itself; see FocusAnchor.
static	int				g_FocusGlowX = 0;
static	int				g_FocusGlowY = 0;
static	bool			g_FocusGlowValid = false;

// Where the glow actually IS, as opposed to where it belongs. It travels between the two -- see
// computation/glowtravel_compute.h for why a marker that teleports has to be found again after every
// keypress, and one that slides is simply followed.
static	zx::GlowTravel	g_GlowTravel;
static	zx::GlowPos		g_GlowAt;
static	bool			g_GlowPlaced = false;

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

// [rc4l] What is typed in the search box, and where the caret is in it. The editing rules are
// computation/textinput_compute; this is only the value they operate on.
//
// Kept ACROSS tab switches on purpose: "show me the brutal doom servers" is a question about servers,
// not about a tab, and having to retype it to look at the other tab would make the box feel like it
// belonged to one of them.
static	zx::TextInput		g_Search;
static	bool				g_SearchHot = false;

// [rc4l] Held while the pointer is dragging out a selection in the search box, so a drag that
// wanders off the field keeps selecting -- which is what dragging means everywhere else.
static	bool				g_SearchDragging = false;

// How much of the query is scrolled off the left of the box. Recorded by the drawing, because it is
// the drawing that decides it, and read by the hit test so a click lands on the character under it
// rather than the one that would be there if nothing had scrolled.
static	int					g_SearchFirstChar = 0;

// When the last click in the box landed, so a quick second one is a double-click.
static	int					g_SearchClickTime = -1000;

// Modifiers from the most recent GUI event, captured in Responder because MouseEvent is handed only
// a type and a position.
static	int					g_LastModifiers = 0;

// [rc4l] The tooltip registry. See computation/tooltip_compute.h for why it is shaped like this.
//
// CLEARED AT THE START OF EVERY FRAME and appended to by whatever draws each element, as it draws
// it. A region exists here only because something put it here this frame, which is what makes a
// lingering tooltip impossible: close the menu, reload the WADs, scroll the row away, switch tabs --
// whatever stops being drawn stops being registered, in the same frame, with nothing to remember to
// tear down.
struct BrowserTip
{
	int x, y, w, h;			// virtual coordinates, the same ones the drawing used
	FString text;
};

static	TArray<BrowserTip>	g_Tips;
static	int					g_TipPointerX = -1;
static	int					g_TipPointerY = -1;

// Whether the pointer has been seen at all. A keyboard-only player never moves it, and a tooltip
// parked wherever the mouse happened to be left is exactly the ghost this must not produce.
static	bool				g_TipPointerValid = false;

// [rc4l] Register a hoverable region, in the same virtual coordinates the drawing used. Called by
// the draw code as it draws; see the registry's comment for why that is the whole trick.
//
// A free function rather than a menu method so that anything which draws can register one -- the
// country flag is drawn by a helper outside the class, and a WAD row is not a control at all.
static void serverbrowser_Tip( int x, int y, int w, int h, const char *text )
{
	if (( text == NULL ) || ( text[0] == 0 ) || ( w <= 0 ) || ( h <= 0 ))
		return;

	BrowserTip tip;
	tip.x = x;
	tip.y = y;
	tip.w = w;
	tip.h = h;
	tip.text = text;
	g_Tips.Push( tip );
}

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

// [rc4l] The server's MD5 for each PWAD, for the tooltip. Empty where it did not send one, and empty
// for the IWAD -- SQF2_PWAD_HASHES excludes it, and its build is identified by SQF2_FUA_IWAD_HASH.
static	TArray<FString>		g_DetailWadHashes;

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

// [rc4l] Screen pixels back to virtual units, for the one thing that genuinely needs it: the mouse
// arrives in screen pixels and the tooltip is laid out in virtual ones.
//
// DERIVED FROM THE FORWARD MAPPING rather than reimplemented. VirtualToRealCoordsInt is affine --
// a scale and an offset -- so evaluating it at two points recovers both, and the inverse is then
// exact by construction. Reimplementing it would be a second copy of the letterboxing rule that
// agrees with the first only until one of them is touched, which is precisely the bug the forward
// helpers were written to end.
static int serverbrowser_ToVirtualX( int px )
{
	const int at0 = serverbrowser_ToScreenX( 0 );
	const int at100 = serverbrowser_ToScreenX( 100 );
	if ( at100 == at0 )
		return 0;

	return (( px - at0 ) * 100 ) / ( at100 - at0 );
}

static int serverbrowser_ToVirtualY( int py )
{
	const int at0 = serverbrowser_ToScreenY( 0 );
	const int at100 = serverbrowser_ToScreenY( 100 );
	if ( at100 == at0 )
		return 0;

	return (( py - at0 ) * 100 ) / ( at100 - at0 );
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
	g_DetailWadHashes.Clear( );

	// A different server means a different list, so the old scroll position describes nothing.
	g_WadScroll = 0;
	g_DraggingWadBar = false;

	const char *pszIwad = BROWSER_GetIWADName( lServer );
	if (( pszIwad != NULL ) && ( pszIwad[0] != 0 ))
	{
		g_DetailWads.Push( pszIwad );
		g_DetailWadSizes.Push( BROWSER_GetIWADSize( lServer ));
		g_DetailWadHashes.Push( BROWSER_GetIWADHash( lServer ));
	}

	const LONG lPwads = BROWSER_GetNumPWADs( lServer );
	for ( LONG i = 0; i < lPwads; i++ )
	{
		const char *pszPwad = BROWSER_GetPWADName( lServer, i );
		if (( pszPwad != NULL ) && ( pszPwad[0] != 0 ))
		{
			g_DetailWads.Push( pszPwad );
			g_DetailWadSizes.Push( BROWSER_GetPWADSize( lServer, i ));
			g_DetailWadHashes.Push( BROWSER_GetPWADHash( lServer, i ));
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
// [rc4l] How many servers answered at all, before the tab and the search narrow them. The footer
// needs it to say what a filter is hiding; nothing else does.
static int serverbrowser_CountActive( void )
{
	int count = 0;
	for ( ULONG ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( BROWSER_IsActive( ulIdx ))
			++count;
	}
	return count;
}

//*****************************************************************************
//
static void serverbrowser_RebuildList( void )
{
	g_SortedServers.Clear();

	// [rc4l] The tab is a filter over the same list rather than a second list: everything is still
	// queried, so switching tabs is instant and never re-fetches anything.
	const bool bWantPrivate = ( g_Tab == BrowserTab::Private );

	// [rc4l] Folded once here rather than once per server: the query does not change while we walk
	// the list, and ServerMatchesSearch is called MAX_BROWSER_SERVERS times.
	const std::string searchKey = zx::SearchKey( g_Search.text );

	for ( ULONG ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( BROWSER_IsActive( ulIdx ) == false )
			continue;
		if ( BROWSER_IsPasswordProtected( ulIdx ) != bWantPrivate )
			continue;

		// The search is a filter over the same list, exactly as the tab is -- nothing is re-queried
		// and the sort below is untouched, so typing narrows the list without reordering what is left.
		const char *pszName = BROWSER_GetHostName( ulIdx );
		if ( !zx::ServerMatchesSearch(( pszName != NULL ) ? pszName : "", searchKey ))
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

	// [rc4l] The flag is a picture of a fact nobody can read off a picture. Two letters of a code, or
	// twenty pixels of a flag, is not a country you can name -- so hovering says it in words.
	{
		FString where;
		if ( ulIndex != COUNTRY_INDEX_UNKNOWN )
		{
			const char *pszName = NETWORK_GetCountryNameFromIndex( ulIndex );
			if (( pszName != NULL ) && ( pszName[0] != 0 ))
				where = pszName;
		}

		if ( where.IsEmpty( ) && bCodeUsable )
			where.Format( "%c%c%c", pszCode[0], pszCode[1], pszCode[2] );

		if ( where.IsEmpty( ))
			where = "Where this server is, nobody could say";

		// [rc4l] The FLAG's cell and nothing else. It used to run all the way to the players column,
		// which meant hovering a server's NAME produced a sentence about which country it was in --
		// a region far bigger than the thing it describes is the same bug as a tooltip on the wrong
		// control.
		serverbrowser_Tip( x - 1, y - 2, SB_COL_NAME - x, SB_ROW_HEIGHT, where );
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

		// A query left over from last time would silently hide most of the list before the player has
		// typed anything, and the box that explains it is one line they have no reason to read yet.
		g_Search = zx::ClearInput( );
		g_SearchHot = false;
		g_SearchDragging = false;
		g_SearchFirstChar = 0;

		// A pointer position remembered from the last visit would put a tooltip on screen before the
		// mouse has been touched. Forgotten until it moves again.
		g_TipPointerValid = false;
		g_Tips.Clear( );
		g_GlowPlaced = false;

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

		// [rc4l] The focus glow travels HERE, on the tic, not in Drawer.
		//
		// Drawer runs once per rendered frame, so advancing there made the speed a function of the
		// frame rate: the same slide took a fifth of a second at 240fps and most of a second at 50,
		// and on a fast machine it was over before the eye could follow it -- which defeats the point
		// of having it travel at all. The tic is 35Hz whatever the display is doing, and it is already
		// what the breathing pulse is measured against, so the two now agree.
		//
		// The anchor is whatever the last frame drew. One frame stale at worst, and invisible: the
		// glow is chasing a target it will reach over the following dozen tics anyway.
		if ( g_FocusGlowValid && g_GlowPlaced )
		{
			const zx::GlowPos want( g_FocusGlowX, g_FocusGlowY );

			// The focus can move again mid-flight. Setting out afresh FROM WHERE THE GLOW IS -- rather
			// than from where the last journey began -- is what stops it snapping backwards when the
			// player changes their mind halfway through.
			if (( g_GlowTravel.to.x != want.x ) || ( g_GlowTravel.to.y != want.y ))
				g_GlowTravel = zx::BeginGlowTravel( g_GlowAt, want );

			g_GlowTravel = zx::StepGlowTravel( g_GlowTravel );
			g_GlowAt = zx::GlowTravelPoint( g_GlowTravel );
		}
	}

	//*************************************************************************
	//
	// [rc4l] What is on screen is decided in ONE place -- computation/browserchrome_compute -- and both
	// drawing and hit-testing read the same answer, so a control can never be invisible and still
	// clickable.
	// [rc4l] The phase, with the SEARCH taken into account.
	//
	// ComputeBrowserPhase counts servers that answered, which is the right question for "are we still
	// looking" and the wrong one once a filter is on: twenty servers can have answered and none of
	// them match what was typed. Ready with nothing to draw would leave a blank slab where the list
	// goes and no word about why, so a filtered-to-nothing list is an empty list.
	zx::BrowserPhase EffectivePhase( const zx::BrowserCounts &counts )
	{
		const bool bWaitingRegistry = BROWSER_WaitingForServerRegistryResponse( );
		const zx::BrowserPhase phase = zx::ComputeBrowserPhase( bWaitingRegistry, counts );

		if (( phase == zx::BrowserPhase::Ready ) && ( g_SortedServers.Size( ) == 0 ))
			return zx::BrowserPhase::Empty;

		return phase;
	}

	unsigned VisibleParts( const zx::BrowserCounts &counts )
	{
		const int total = static_cast<int>( g_SortedServers.Size( ));

		return zx::ComputeVisibleParts( EffectivePhase( counts ),
			( g_Selected >= 0 ) && ( g_Selected < total ), serverbrowser_DownloadRunning( ));
	}

	void Drawer( )
	{
		const zx::BrowserCounts counts = serverbrowser_CountStates( );
		const zx::BrowserPhase phase = EffectivePhase( counts );
		const unsigned parts = VisibleParts( counts );

		// Every frame, before anything is drawn. Nothing survives from the last one.
		g_Tips.Clear( );
		g_FocusGlowValid = false;

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
		else
		{
			// Over the browser but under a question, because a question is the thing being answered
			// and the glow would be pointing at a control the player cannot reach until they have.
			if ( g_FocusGlowValid )
			{
				const zx::GlowPos want( g_FocusGlowX, g_FocusGlowY );

				// The FIRST placement snaps. There is nowhere for it to have travelled from, and
				// sliding in from a stale position left over from the last visit would be a lie about
				// where the focus had been. Every step after that is Ticker's job -- see there for why
				// it is not done per frame.
				if ( !g_GlowPlaced )
				{
					g_GlowAt = want;
					g_GlowTravel = zx::BeginGlowTravel( want, want );
					g_GlowPlaced = true;
				}

				DrawFocusGlow( g_GlowAt.x, g_GlowAt.y );
			}
			else
			{
				// Nothing has focus, so there is nothing to travel from next time either.
				g_GlowPlaced = false;
			}

			DrawTooltip( );
		}
	}

	//*************************************************************************
	//
	// [rc4l] Whichever registered region the pointer is inside, drawn where Windows would put it.
	//
	// Searched in REVERSE, so something drawn on top of something else wins -- the same order the eye
	// resolves them in, and it costs nothing to get right.
	void DrawTooltip( )
	{
		if ( !g_TipPointerValid || ( g_Tips.Size( ) == 0 ))
			return;

		const BrowserTip *found = NULL;
		for ( int i = static_cast<int>( g_Tips.Size( )) - 1; i >= 0; --i )
		{
			const BrowserTip &tip = g_Tips[i];
			if ( zx::TooltipRectContains( serverbrowser_ToScreenX( tip.x ), serverbrowser_ToScreenY( tip.y ),
				serverbrowser_ToScreenX( tip.x + tip.w ) - serverbrowser_ToScreenX( tip.x ),
				serverbrowser_ToScreenY( tip.y + tip.h ) - serverbrowser_ToScreenY( tip.y ),
				g_TipPointerX, g_TipPointerY ))
			{
				found = &tip;
				break;
			}
		}

		if ( found == NULL )
			return;

		const std::vector<std::string> lines = zx::TooltipLines( found->text.GetChars( ));
		if ( lines.empty( ))
			return;

		// Sized to its content, in virtual units, so a tooltip can be as long or as tall as whatever
		// it has to say -- a WAD's full name, its hash and its size is three lines and nothing had to
		// be told about it in advance.
		const int lineH = SmallFont->GetHeight( ) + 1;
		const int padX = 4;
		const int padY = 3;

		int contentW = 0;
		for ( size_t i = 0; i < lines.size( ); ++i )
			contentW = MAX( contentW, SmallFont->StringWidth( lines[i].c_str( )));

		const int boxW = contentW + 2 * padX;
		const int boxH = static_cast<int>( lines.size( )) * lineH + 2 * padY;

		// The pointer is in screen pixels and the box is laid out in virtual ones, so the placement is
		// done in virtual space -- the same space the text will be drawn in.
		const int pointerVX = serverbrowser_ToVirtualX( g_TipPointerX );
		const int pointerVY = serverbrowser_ToVirtualY( g_TipPointerY );

		const zx::TooltipBox box = zx::ComputeTooltipPlacement( pointerVX, pointerVY, boxW, boxH,
			SB_VIRT_W, SB_VIRT_H, 10, 3 );

		// A panel of its own, darker and more opaque than anything under it, so the text on it is
		// readable over the list, the detail panel or the game behind both.
		const int left = serverbrowser_ToScreenX( box.x );
		const int top = serverbrowser_ToScreenY( box.y );
		const int right = serverbrowser_ToScreenX( box.x + box.w );
		const int bottom = serverbrowser_ToScreenY( box.y + box.h );

		screen->Dim( PalEntry( 18, 19, 26 ), 0.94f, left, top, right - left, bottom - top );

		// A hairline edge, because a dark box on a dark list needs a boundary to read as a box.
		screen->Dim( PalEntry( 120, 130, 165 ), 0.55f, left, top, right - left, 1 );
		screen->Dim( PalEntry( 120, 130, 165 ), 0.55f, left, bottom - 1, right - left, 1 );
		screen->Dim( PalEntry( 120, 130, 165 ), 0.55f, left, top, 1, bottom - top );
		screen->Dim( PalEntry( 120, 130, 165 ), 0.55f, right - 1, top, 1, bottom - top );

		int y = box.y + padY;
		for ( size_t i = 0; i < lines.size( ); ++i )
		{
			// First line white, the rest dimmer: the thing hovered, then what is known about it.
			screen->DrawText( SmallFont, ( i == 0 ) ? CR_WHITE : CR_GRAY,
				box.x + padX, y, lines[i].c_str( ),
				DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );
			y += lineH;
		}
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

				// [rc4l] And the glow, only when the ARROW KEYS are on the list. The highlight marks
				// what is selected and stays put while you click around with the mouse; this marks
				// where the keyboard is, which is a different question and used to have no answer.
				FocusAnchor( zx::BrowserFocus::Rows, SB_PANEL_LEFT + 9, serverbrowser_RowTextY( y, 0 ) + 1 );
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
		else if ( !g_Search.text.empty( ))
		{
			// There may be plenty of servers -- just none matching what was typed. Saying "no servers
			// found" here would send the player looking for a network problem they do not have.
			text.Format( "No servers match \"%s\"", g_Search.text.c_str( ));
		}
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

			// Hover only. Keyboard focus gets a RING instead, because a brighter fill is already what
			// selected looks like and one picture cannot mean both.
			const bool bHot = ( g_TabHot == i );

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

			if ( bSelected )
				FocusAnchor( zx::BrowserFocus::Tabs, vLeft - 5, SB_TAB_TOP + SB_TAB_H / 2 );

			serverbrowser_Tip( vLeft, SB_TAB_TOP, SB_TAB_W, SB_TAB_H, ( i == 0 )
				? "Servers anyone can join"
				: "These servers are password-protected" );
		}

		DrawSearchBox( );
		DrawSeparatorSpan( SB_TAB_SEP_Y, SB_PANEL_LEFT + 12, SB_DETAIL_RIGHT );
	}

	//*************************************************************************
	//
	// [rc4l] The search box, sharing the tab row.
	//
	// A sunken field rather than another oval: the tabs beside it are things you PRESS, and a control
	// you type into should not look like one of them. Same rounded corners so it still belongs to the
	// row, but the gradient runs the other way -- dark at the top -- which is how every toolkit has
	// drawn a text field since Motif, and it costs nothing to borrow.
	void DrawSearchBox( )
	{
		const int left = serverbrowser_ToScreenX( SB_SEARCH_LEFT );
		const int right = serverbrowser_ToScreenX( SB_SEARCH_RIGHT );
		const int top = serverbrowser_ToScreenY( SB_SEARCH_TOP );
		const int bottom = serverbrowser_ToScreenY( SB_SEARCH_TOP + SB_SEARCH_H );

		const int w = right - left;
		const int h = bottom - top;
		if (( w <= 0 ) || ( h <= 0 ))
			return;

		const bool bFocused = ( g_Focus == zx::BrowserFocus::Search );
		const int radius = h / 3;

		serverbrowser_Tip( SB_SEARCH_LEFT, SB_SEARCH_TOP, SB_SEARCH_W, SB_SEARCH_H,
			"Filter the list by name\nUpper and lower case are the same" );

		// Lighter when it has the keyboard, the same lift the tabs and the button use for the same
		// reason: "what would a key do right now" should be answerable by looking.
		const int base = bFocused ? 30 : ( g_SearchHot ? 22 : 16 );
		const zx::PanelColor topCol = { static_cast<BYTE>( base ), static_cast<BYTE>( base ),
			static_cast<BYTE>( base + 10 ), 225 };
		const zx::PanelColor botCol = { static_cast<BYTE>( base + 18 ), static_cast<BYTE>( base + 18 ),
			static_cast<BYTE>( base + 30 ), 210 };

		for ( int row = 0; row < h; ++row )
		{
			const int inset = zx::ComputeRoundedInset( row, h, radius );
			const int rowW = w - 2 * inset;
			if ( rowW <= 0 )
				continue;

			const zx::PanelColor c = zx::ComputePanelGradient( row, h, topCol, botCol );
			screen->Dim( PalEntry( c.r, c.g, c.b ), c.a / 255.f, left + inset, top + row, rowW, 1 );
		}

		FocusAnchor( zx::BrowserFocus::Search, SB_SEARCH_LEFT - 5, SB_SEARCH_TOP + SB_SEARCH_H / 2 );

		const int textY = SB_SEARCH_TOP + ( SB_SEARCH_H - SmallFont->GetHeight( )) / 2 + 1;
		const int textX = SB_SEARCH_LEFT + SB_SEARCH_PAD;
		const int textW = SB_SEARCH_W - 2 * SB_SEARCH_PAD;

		if ( g_Search.text.empty( ) && !bFocused )
		{
			// A prompt rather than a blank box: an empty rounded rectangle says nothing about what it
			// is for, and this one is not obviously a search box until something is in it.
			screen->DrawText( SmallFont, CR_DARKGRAY, textX, textY, "Search",
				DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );
			return;
		}

		// Scrolled to keep the CARET visible rather than the start of the string: once the query is
		// longer than the box, what matters is the end you are typing at.
		FString shown = g_Search.text.c_str( );
		int first = 0;
		while (( shown.Len( ) > 0 ) && ( SmallFont->StringWidth( shown ) > textW ))
		{
			shown = shown.Mid( 1 );
			++first;
		}
		g_SearchFirstChar = first;

		int caretChars = static_cast<int>( g_Search.caret ) - first;
		if ( caretChars < 0 )
			caretChars = 0;

		// The selection, under the text: a band behind the characters rather than an inversion of
		// them, so the letters keep the colour they had and stay readable either way.
		if ( zx::HasSelection( g_Search ))
		{
			int from = static_cast<int>( zx::SelectionStart( g_Search )) - first;
			int to = static_cast<int>( zx::SelectionEnd( g_Search )) - first;
			if ( from < 0 )
				from = 0;
			if ( to > static_cast<int>( shown.Len( )))
				to = static_cast<int>( shown.Len( ));

			if ( to > from )
			{
				const int selX = textX + SmallFont->StringWidth( shown.Left( from ));
				const int selW = SmallFont->StringWidth( shown.Mid( from, to - from ));

				const int sx = serverbrowser_ToScreenX( selX );
				const int sw = MAX( 1, serverbrowser_ToScreenX( selX + selW ) - sx );
				const int sy = serverbrowser_ToScreenY( textY - 1 );
				const int sh = serverbrowser_ToScreenY( textY + SmallFont->GetHeight( )) - sy;

				screen->Dim( PalEntry( 70, 95, 165 ), 0.85f, sx, sy, sw, sh );
			}
		}

		screen->DrawText( SmallFont, CR_WHITE, textX, textY, shown,
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, TAG_DONE );

		if ( bFocused )
		{
			// Blinks, so a focused empty box is still visibly a place where typing goes.
			if ((( DMenu::MenuTime / 16 ) % 2 ) == 0 )
			{
				const FString before = shown.Left( caretChars );
				const int caretX = textX + SmallFont->StringWidth( before );
				const int cx = serverbrowser_ToScreenX( caretX );
				const int cw = MAX( 1, serverbrowser_ToScreenX( caretX + 1 ) - cx );
				const int cy = serverbrowser_ToScreenY( textY );
				const int ch = serverbrowser_ToScreenY( textY + SmallFont->GetHeight( )) - cy;

				screen->Dim( PalEntry( 235, 235, 245 ), 0.85f, cx, cy, cw, ch );
			}
		}
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
		const bool bLit = g_ButtonHot;

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

		serverbrowser_Tip( SB_BUTTON_LEFT, SB_BUTTON_TOP, SB_BUTTON_RIGHT - SB_BUTTON_LEFT, SB_BUTTON_H, bCancel
			? "Stop the download\nYou will be asked to confirm"
			: "Join this server\nAnything missing is downloaded first" );

		FocusAnchor( zx::BrowserFocus::Action, SB_BUTTON_LEFT - 5, SB_BUTTON_TOP + SB_BUTTON_H / 2 );

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
		const int total = static_cast<int>( g_SortedServers.Size( ));

		// [rc4l] No selection, but we are here -- which kPartDetail only allows while a transfer is
		// running. The server that started it has gone (timed out, shut down, whatever), and the panel
		// is on screen for one reason: to carry the button that stops the download. Say so, and draw
		// the button, and nothing else -- every other line in here describes a server that is no
		// longer there to describe.
		if (( g_Selected < 0 ) || ( g_Selected >= total ))
		{
			DrawDetailPanel( );

			const int x = SB_DETAIL_LEFT + SB_DETAIL_PAD;
			int y = SB_DETAIL_TOP + SB_DETAIL_PAD;
			y = DrawWrapped( "That server is no longer listed.", x, y, CR_WHITE );
			y += 3;
			DrawWrapped( "The download is still running -- stop it, or let it finish and use the file.",
				x, y, CR_DARKGRAY );

			DrawActionButton( );
			return;
		}

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
			// [rc4l] The row is not a control -- there is nothing to click -- but it IS a rectangle, so
			// it can explain itself. The full name matters because the drawn one is truncated to fit;
			// the exact byte count matters because the drawn one is rounded to the nearest whole unit.
			{
				FString tip;
				tip << g_DetailWads[entry];

				if ( g_DetailWadHashes[entry].IsNotEmpty( ))
					tip << "\nMD5 " << g_DetailWadHashes[entry];

				if ( g_DetailWadSizes[entry] > 0 )
				{
					// The unit first, because that is the part anyone reads: "402mb" answers "is this
					// worth waiting for" and "421527552" does not. The exact count follows it for the
					// one person who wants to check a byte-for-byte match.
					FString exact;
					exact.Format( "%s  (%u bytes)",
						zx::FormatByteSize( g_DetailWadSizes[entry] ).c_str( ), g_DetailWadSizes[entry] );
					tip << "\n" << exact;
				}
				else
					tip << "\nSize not reported by this server";

				serverbrowser_Tip( x, lineY, textW, SB_DETAIL_LINE, tip );
			}

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
		{
			// [rc4l] With a filter on, the count is of what SURVIVED it -- saying "6 servers" over a
			// list showing two would be counting something the player cannot see.
			if ( !g_Search.text.empty( ))
				text.Format( "%d of %d servers", static_cast<int>( g_SortedServers.Size( )),
					serverbrowser_CountActive( ));
			else
				text.Format( "%d servers", static_cast<int>( g_SortedServers.Size( )));
		}
		else if (( phase == zx::BrowserPhase::Empty ) && !g_Search.text.empty( ))
		{
			// The placeholder already said nothing matched. Repeating "nothing is being hosted" under
			// it would be a second, wrong answer to the same question -- there ARE servers.
			text.Format( "%d hidden by the search", serverbrowser_CountActive( ));
		}
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
	// [rc4l] "The keyboard is HERE": a soft dot of light beside whatever has focus.
	//
	// A brighter fill was doing this job and doing it badly, because a brighter fill is also what
	// SELECTED looks like -- the tab you were on and the tab the arrows would act on were the same
	// picture. A hard outline fixed the ambiguity and read as a border rather than a marker.
	//
	// So: a firefly. One glow, always in the same relation to the thing it marks, drawn concentrically
	// with the alpha falling off towards the edge so it has no boundary to be mistaken for a frame. It
	// breathes slowly, because a light that moves is followed by the eye without being looked for --
	// which is the whole job.
	//
	// Procedural like everything else here, so there is no lump to go missing. That is also why the
	// classic skull cursor was not the answer: this browser has no art dependency and the country flag
	// already needed a fallback for exactly that reason.
	// EACH COMPONENT SAYS WHERE ITS OWN GLOW GOES, in its own coordinates, as it draws itself -- the
	// same shape as the tooltip registry a few functions up, and for the same reason. A tab knows it
	// wants the light off its left edge at half its height; a row knows it wants it in the margin
	// beside the text. Nothing central has to hold a table of offsets that goes stale the moment one
	// of them moves, and a new focusable thing is one call rather than a case in a switch.
	//
	// Recorded rather than drawn on the spot so the glow lands ON TOP of everything: it is drawn once
	// at the end of the frame, not buried under whatever the component painted after it.
	void FocusAnchor( zx::BrowserFocus owner, int vcx, int vcy )
	{
		if ( g_Focus != owner )
			return;

		g_FocusGlowX = vcx;
		g_FocusGlowY = vcy;
		g_FocusGlowValid = true;
	}

	void DrawFocusGlow( int vcx, int vcy )
	{
		// Slow breath, never all the way out -- a marker that vanishes is a marker you have to hunt
		// for on the frame it happens to be invisible.
		const double phase = ( DMenu::MenuTime % 70 ) / 70.0;
		const float breath = 0.72f + 0.28f * static_cast<float>( fabs( 1.0 - 2.0 * phase ));

		const int cx = serverbrowser_ToScreenX( vcx );
		const int cy = serverbrowser_ToScreenY( vcy );

		// [rc4l] The scale is measured over a LONG span, not from one virtual pixel.
		//
		// ToScreenX(vcx + 1) - ToScreenX(vcx) looks like the obvious way to ask "how big is a virtual
		// pixel here", and it is wrong: the mapping is fractional -- the panel's 640 units cover about
		// 940 real ones -- so that difference rounds to 1 at some x and 2 at others. The glow was
		// therefore HALF THE SIZE depending on where it landed, and flickered between the two sizes
		// every tic while it travelled, because travelling is exactly changing x.
		//
		// Measuring across 100 units divides the same rounding error by a hundred, so the radius is
		// the same wherever the glow is. Same trick as serverbrowser_ToVirtualX, for the same reason.
		const int span = MAX( 1, serverbrowser_ToScreenX( 100 ) - serverbrowser_ToScreenX( 0 ));

		// Four shells, widest and faintest first. Concentric rather than a true radial ramp because
		// Dim takes rectangles, and at this size the difference is not visible -- what IS visible is
		// the absence of a hard edge.
		const int kShells = 4;
		for ( int shell = kShells - 1; shell >= 0; --shell )
		{
			// Virtual radii of 3, 6, 9, 12 -- fixed in the coordinate space the browser is laid out
			// in, so the glow is the same size beside a tab as beside a row.
			const int radius = MAX( 1, ( span * ( shell + 1 ) * 3 ) / 100 );
			const float alpha = breath * ( 0.10f + 0.16f * ( kShells - 1 - shell ));

			for ( int dy = -radius; dy <= radius; ++dy )
			{
				// A circle, row by row: half-width falls off as the square root, which is what stops
				// the shells reading as stacked squares.
				const int half = static_cast<int>( sqrt( double( radius * radius - dy * dy )));
				if ( half <= 0 )
					continue;

				screen->Dim( PalEntry( 190, 225, 255 ), alpha, cx - half, cy + dy, half * 2, 1 );
			}
		}
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

		// And for the tooltip, which is a question about where the pointer is and nothing else.
		g_TipPointerX = x;
		g_TipPointerY = y;
		g_TipPointerValid = true;

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

		// The search box, which shares the row with the tabs.
		g_SearchHot = false;
		{
			const bool bOverSearch = (( parts & zx::kPartTabs ) != 0 ) &&
				( x >= serverbrowser_ToScreenX( SB_SEARCH_LEFT )) &&
				( x < serverbrowser_ToScreenX( SB_SEARCH_RIGHT )) &&
				( y >= serverbrowser_ToScreenY( SB_SEARCH_TOP )) &&
				( y < serverbrowser_ToScreenY( SB_SEARCH_TOP + SB_SEARCH_H ));

			g_SearchHot = bOverSearch;

			// [rc4l] A click anywhere else lets the box go -- a row, a tab, the button, or the empty
			// space below the list, which is the one that made this feel broken. Clicking away from a
			// field is how every interface says "I am done with that", and a caret still blinking in a
			// box you have visibly left is a lie about where the next keystroke will land.
			//
			// Done HERE, before any of the handlers below, so it applies even to clicks that land on
			// nothing at all and are otherwise ignored.
			if ( !bOverSearch && ( type == MOUSE_Click ))
			{
				if ( g_Focus == zx::BrowserFocus::Search )
					SetFocus( zx::BrowserFocus::Tabs );
				g_SearchDragging = false;
			}
		}

		if ( parts & zx::kPartTabs )
		{
			const bool bOverSearch =
				( x >= serverbrowser_ToScreenX( SB_SEARCH_LEFT )) &&
				( x < serverbrowser_ToScreenX( SB_SEARCH_RIGHT )) &&
				( y >= serverbrowser_ToScreenY( SB_SEARCH_TOP )) &&
				( y < serverbrowser_ToScreenY( SB_SEARCH_TOP + SB_SEARCH_H ));

			if ( bOverSearch && ( type == MOUSE_Click ))
			{
				const int now = static_cast<int>( DMenu::MenuTime );
				const bool bDouble = (( now - g_SearchClickTime ) < 15 );
				g_SearchClickTime = now;

				SetFocus( zx::BrowserFocus::Search );

				if ( bDouble )
				{
					// Double-click takes the word under the pointer, or everything when there is no word
					// there -- see SelectWordOrAll. No drag afterwards: a second press that started
					// selecting again would undo what the player just asked for before they let go.
					g_Search = zx::SelectWordOrAll( g_Search, SearchCharAt( x ));
					g_SearchDragging = false;
				}
				else
				{
					// Press puts the caret and arms a drag; the drag is what turns it into a selection.
					g_SearchDragging = true;
					g_Search = zx::SetCaret( g_Search, SearchCharAt( x ), bShiftHeld( ));
				}
				return true;
			}

			if ( g_SearchDragging )
			{
				// Tracked even once the pointer leaves the box -- that is what dragging means
				// everywhere else, and a selection that stops the moment you overshoot the last
				// character is one you can never make in a single gesture.
				g_Search = zx::SetCaret( g_Search, SearchCharAt( x ), true );

				if ( type == MOUSE_Release )
					g_SearchDragging = false;
				return true;
			}

			if ( bOverSearch )
				return true;
		}

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
					SetFocus( zx::BrowserFocus::Tabs );
					SelectTab( static_cast<BrowserTab>( i ));
				}
				return true;
			}
		}

		// The action button, before the row hit test -- it lives inside the detail panel, which the
		// row test already excludes, but checking first keeps the two from ever both claiming a click.
		if ( parts & zx::kPartDetail )
		{
			// A running transfer keeps the button live even with nothing selected -- that is the whole
			// point of the panel still being here. See computation/browserchrome_compute.h.
			const bool bHaveAction = (( g_Selected >= 0 ) && ( g_Selected < total )) ||
				serverbrowser_DownloadRunning( );

			const bool bOverButton = bHaveAction &&
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
						SetFocus( zx::BrowserFocus::Action );
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
					SetFocus( zx::BrowserFocus::Rows );

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
		if (( ev != NULL ) && ( ev->type == EV_GUI_Event ))
			g_LastModifiers = ev->data3;

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

		// [rc4l] Typing into the search box.
		//
		// Taken here, ahead of everything, because a focused text field owns the keyboard: 'y' is a
		// letter while you are typing a query, not an answer to a question that is not on screen, and
		// a printable key must never also be a menu shortcut. Only characters and the editing keys are
		// claimed -- the arrows still navigate, which is what moves focus back OUT of the box.
		if (( ev != NULL ) && ( ev->type == EV_GUI_Event ) && !g_ConfirmCancel && g_Notice.IsEmpty( ) &&
			( g_Focus == zx::BrowserFocus::Search ))
		{
			if ( EditSearchField( ev ))
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
	// [rc4l] Every assignment to the focus goes through here.
	//
	// Landing in the search box turns TranslateKeyboardEvents off, and the key that BROUGHT us here is
	// still physically down. Its release will not be translated either, so the framework never
	// unlatches it and M_Ticker repeats it forever -- which is a right-arrow shoving the focus around
	// while the player is trying to type. Letting go of every menu key at the moment we take the
	// keyboard is the fix, and it is the same loop M_StartControlPanel already runs when a menu opens.
	void SetFocus( zx::BrowserFocus focus )
	{
		if (( focus == zx::BrowserFocus::Search ) && ( g_Focus != zx::BrowserFocus::Search ))
			M_ReleaseMenuButtons( );

		g_Focus = focus;
	}

	//*************************************************************************
	//
	// [rc4l] Raw keys, but only while the search box has focus.
	//
	// This is the engine's OWN mechanism for text entry -- DTextEnterMenu returns false here for
	// exactly the same reason -- and finding it explained two bugs at once. M_Responder otherwise
	// translates keys into MKEY_* before Responder ever runs, and GK_BACKSPACE becomes MKEY_Clear:
	// the backspace handler was not merely watching for the wrong constant, it was in a function the
	// key never reached. EV_GUI_KeyRepeat is swallowed in the same place, which is why holding a key
	// did nothing either.
	//
	// Only while focused, because the translated events are what the rest of the browser navigates
	// with. A menu that took raw keys all the time would have to reimplement the arrow handling the
	// framework already does.
	bool TranslateKeyboardEvents( )
	{
		return ( g_Focus != zx::BrowserFocus::Search ) || g_ConfirmCancel || g_Notice.IsNotEmpty( );
	}

	//*************************************************************************
	//
	// [rc4l] Every key a text field is expected to answer, in one place.
	//
	// BACKSPACE ARRIVES AS A CHARACTER, not as a named key: the engine sends EV_GUI_KeyDown with
	// data1 == '', exactly as DTextEnterMenu reads it. Watching for GK_BACKSPACE instead is why it
	// did nothing at all -- the case simply never matched.
	//
	// Returns true when the key belonged to the field. Arrows without shift are deliberately NOT
	// claimed: they are how focus leaves the box, and a field you cannot get out of is worse than one
	// without a caret. With shift they select, which is a thing only the field can mean.
	bool EditSearchField( event_t *ev )
	{
		// [rc4l] Cmd counts as Ctrl. The Cocoa layer already reports it as GKM_META, so honouring both
		// here is the whole of macOS support -- Cmd+A, Cmd+C, Cmd+V and Cmd+X land where a Mac user
		// expects without a second code path to keep in step.
		const bool bCtrl = (( ev->data3 & ( GKM_CTRL | GKM_META )) != 0 );
		const bool bShift = (( ev->data3 & GKM_SHIFT ) != 0 );

		if ( ev->subtype == EV_GUI_Char )
		{
			// Ctrl+letter arrives here too on some layouts; those are commands, not text.
			if ( !bCtrl )
				return ApplyEdit( zx::InsertChar( g_Search, ev->data1, SB_SEARCH_MAXLEN ));
			return true;
		}

		// KeyRepeat is eaten by M_Responder before it gets here, whatever this menu wants -- the
		// framework does its own repeat handling so that gamepads repeat too.
		if ( ev->subtype != EV_GUI_KeyDown )
			return false;

		const int key = ev->data1;

		if ( bCtrl )
		{
			switch ( key )
			{
			case 'a': case 'A':
				g_Search = zx::SelectAll( g_Search );
				return true;

			case 'c': case 'C':
				if ( zx::HasSelection( g_Search ))
					I_PutInClipboard( zx::SelectedText( g_Search ).c_str( ));
				return true;

			case 'x': case 'X':
				if ( zx::HasSelection( g_Search ))
				{
					I_PutInClipboard( zx::SelectedText( g_Search ).c_str( ));
					return ApplyEdit( zx::DeleteSelection( g_Search ));
				}
				return true;

			case 'v': case 'V':
				{
					const FString pasted = I_GetFromClipboard( false );
					return ApplyEdit( zx::InsertText( g_Search, pasted.GetChars( ), SB_SEARCH_MAXLEN ));
				}

			case GK_LEFT:
			case GK_RIGHT:
				g_Search = zx::MoveWord( g_Search, ( key == GK_RIGHT ), bShift );
				return true;

			case '':
				// Ctrl+Backspace erases the word behind the caret, which is the fastest way to undo a
				// mistyped query without holding the key down.
				if ( !zx::HasSelection( g_Search ))
					g_Search = zx::MoveWord( g_Search, false, true );
				return ApplyEdit( zx::DeleteSelection( g_Search ));

			default:
				return true;		// swallowed: a chord the field does not use is not a menu shortcut
			}
		}

		switch ( key )
		{
		case GK_ESCAPE:
			// Out of the box, not out of the browser. A second escape then closes the menu, which is
			// the ordinary meaning restored as soon as the field stops claiming it.
			SetFocus( zx::BrowserFocus::Tabs );
			g_SearchDragging = false;
			return true;

		case GK_RETURN:
			if ( static_cast<int>( g_SortedServers.Size( )) > 0 )
			{
				SetFocus( zx::BrowserFocus::Rows );
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
			}
			return true;

		case GK_UP:
		case GK_DOWN:
			// Navigation, which the framework would normally have translated for us -- while the field
			// holds the keyboard it has to pass these on itself.
			Navigate(( key == GK_UP ) ? zx::NavKey::Up : zx::NavKey::Down,
				static_cast<int>( g_SortedServers.Size( )));
			return true;

		case '':
			return ApplyEdit( zx::Backspace( g_Search ));

		case GK_DEL:
			return ApplyEdit( zx::DeleteForward( g_Search ));

		case GK_HOME:
			g_Search = zx::CaretHome( g_Search, bShift );
			return true;

		case GK_END:
			g_Search = zx::CaretEnd( g_Search, bShift );
			return true;

		case GK_LEFT:
		case GK_RIGHT:
			// [rc4l] ALWAYS the caret, never navigation. Moving through what you have typed is the
			// thing these keys mean inside a field, and a box that jumped to the next control instead
			// would be one you could not edit. Up, down, escape and enter are how it is left.
			g_Search = zx::MoveCaret( g_Search, ( key == GK_LEFT ) ? -1 : 1, bShift );
			return true;

		default:
			// Everything else is swallowed while the field has the keyboard. A letter is a letter, not
			// a menu shortcut, and the alternative is 'y' answering a question that is not on screen.
			return true;
		}
	}

	// [rc4l] Whether shift was down on the event being handled.
	//
	// MouseEvent is handed only a type and a position, so the modifiers are captured in Responder --
	// which sees the same event first -- rather than queried from the OS. Shift+click extending the
	// selection is the one thing that needs it.
	bool bShiftHeld( )
	{
		return (( g_LastModifiers & GKM_SHIFT ) != 0 );
	}

	// [rc4l] Which character of the query a screen x lands on, for click and drag.
	//
	// Walks the drawn text measuring as it goes rather than dividing by an average width, because
	// SmallFont is not monospace -- dividing would put the caret a character or two off in a query
	// with any 'i' or 'm' in it, which is exactly where a click has to be exact.
	size_t SearchCharAt( int px )
	{
		const int textX = SB_SEARCH_LEFT + SB_SEARCH_PAD;
		const FString all = g_Search.text.c_str( );
		const int first = ( g_SearchFirstChar < static_cast<int>( all.Len( ))) ? g_SearchFirstChar : 0;

		for ( int i = first; i < static_cast<int>( all.Len( )); ++i )
		{
			const FString upTo = all.Mid( first, i - first );
			const int glyphW = SmallFont->StringWidth( all.Mid( i, 1 ));
			const int leftEdge = textX + SmallFont->StringWidth( upTo );

			// The half-way point, so clicking the left of a character puts the caret before it and the
			// right of it puts the caret after -- which is where the eye says it should go.
			if ( px < serverbrowser_ToScreenX( leftEdge + glyphW / 2 ))
				return static_cast<size_t>( i );
		}

		return all.Len( );
	}

	// Applies an edit and rebuilds the list if the text actually changed. Always returns true: the key
	// was the field's whether or not it altered anything.
	bool ApplyEdit( const zx::TextInput &next )
	{
		const bool bChanged = ( next.text != g_Search.text );
		g_Search = next;

		if ( bChanged )
			OnSearchChanged( );

		return true;
	}

	//*************************************************************************
	//
	// [rc4l] The query changed, so the list it filters is a different list.
	//
	// The selection is an INDEX into that list, and the row it pointed at may not be in it any more --
	// or may now be a different server at the same index, which is the one that would actually hurt:
	// pressing JOIN would connect to something the player never picked. Rebuild, then let the clamp
	// put the selection somewhere that exists, and put the view back at the top because the list under
	// it is new.
	void OnSearchChanged( )
	{
		serverbrowser_RebuildList( );
		g_ScrollFirst = 0;
		g_RevealSelection = true;
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

		const zx::NavResult nav = zx::ComputeNav( g_Focus, key, total > 0,
			g_Tab == BrowserTab::Private );
		const zx::BrowserFocus was = g_Focus;
		SetFocus( nav.focus );

		if ( nav.tabStep != 0 )
		{
			// A step along the row, not a flip: ComputeNav has already refused to step off either end,
			// so a non-zero step always has somewhere to land.
			SelectTab(( nav.tabStep > 0 ) ? BrowserTab::Private : BrowserTab::Public );
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
			if (( g_Focus == zx::BrowserFocus::Tabs ) || ( g_Focus == zx::BrowserFocus::Search ))
			{
				if ( total > 0 )
				{
					SetFocus( zx::BrowserFocus::Rows );
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
					return true;
				}

				// [rc4l] Nothing to enter, but a transfer may still be running -- the server it belongs
				// to can die and take the whole list with it. Enter then means the only thing left on
				// screen that does anything: stop the download. Without this the keyboard has no route
				// to a button the mouse can reach.
				if ( !serverbrowser_DownloadRunning( ))
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
