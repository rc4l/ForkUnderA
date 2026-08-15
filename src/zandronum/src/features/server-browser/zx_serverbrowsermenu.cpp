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
// [rc4l] level.MapName and gamestate: a hosted server starts on the map the host is standing on.
#include "doomstat.h"
#include "g_level.h"
#include "v_text.h"
#include "gi.h"
#include "i_system.h"
#include "d_event.h"
#include "d_gui.h"
#include "d_main.h"		// D_AddFile, for the have/have-not colouring
#include "textures/textures.h"
#include "m_png.h"		// [rc4l] M_VerifyPNG / PNGTexture_CreateFromFile, for the catalogue art
#include "r_data/r_translate.h"
#include "templates.h"

#include "features/server-browser/browser.h"
#include "i_input.h"		// [rc4l] I_PutInClipboard / I_GetFromClipboard, for the search box
#include "cl_main.h"		// [rc4l] cl_password, handed to the join from the password prompt
#include "features/server-browser/computation/browserchrome_compute.h"
#include "features/server-browser/computation/choicerow_compute.h"
#include "features/server-browser/computation/scrollbar_compute.h"
#include "features/server-browser/computation/scrollview_compute.h"
#include "features/server-browser/computation/pointerdrag_compute.h"
#include "features/server-browser/computation/dialog_compute.h"
#include "features/server-browser/computation/glowtravel_compute.h"
#include "features/server-browser/computation/serversearch_compute.h"
#include "features/server-browser/computation/textinput_compute.h"
#include "features/server-browser/computation/tooltip_compute.h"
#include "features/server-browser/computation/browserfocus_compute.h"
#include "features/server-browser/computation/pillflow_compute.h"
#include "features/server-browser/computation/flagset_compute.h"
#include "features/server-browser/computation/flaghelp_compute.h"
#include "features/server-browser/computation/maplist_compute.h"
#include "features/server-browser/computation/customsave_compute.h"
#include "features/server-browser/zx_mapscan.h"
#include "features/server-browser/zx_customstore.h"
#include "features/server-browser/computation/servervar_compute.h"
#include "features/server-browser/zx_flagtable.h"
#include "gamemode.h"		// [rc4l] GAMEMODE_GetFlags, so the GAMEPLAY box shows what the mode uses
#include "features/server-browser/computation/openingtab_compute.h"
#include "features/server-browser/computation/refreshgate_compute.h"
#include "features/global-header/zx_globalheader.h" // [rc4l] the bar above owns the arrows sometimes
#include "features/menu-focus/zx_focusglow.h"       // [rc4l] the focus orb, shared with that bar
#include "features/server-browser/computation/timeago_compute.h"
#include "features/server-browser/computation/liverow_compute.h"
#include "features/server-hosting/zx_hosting.h" // [rc4l] the HOST tab runs a server from in here
#include "features/addon-catalogue/zx_catalogue.h"
#include "features/wad-library/zx_wadlibrary.h"
#include "features/wad-library/computation/loadorder_compute.h"
#include "features/addon-catalogue/computation/hostplan_compute.h"
#include "features/wad-download/computation/iwadallow_compute.h"
#include "features/addon-catalogue/computation/iwadpick_compute.h"
#include "features/addon-catalogue/computation/livespick_compute.h"
#include "features/addon-catalogue/computation/maprotation_compute.h"
#include "features/addon-catalogue/computation/menuart_compute.h"
#include "features/addon-catalogue/computation/teamspick_compute.h"
#include "features/addon-catalogue/computation/weaponspick_compute.h"
#include "features/addon-catalogue/computation/remixpick_compute.h"
#include "features/addon-catalogue/computation/variantpick_compute.h"
#include "features/addon-catalogue/computation/hostlist_compute.h"
#include "features/wad-download/zx_wadsearch.h"
#include "features/wadreload/zx_wadreload.h"
#include "features/server-hosting/zx_reachprobe.h" // [rc4l] and says whether the internet can reach it
#include "features/server-hosting/computation/hoststatus_compute.h"
#include "features/server-hosting/computation/hostport_compute.h" // [rc4l] which port the check asks about
#include "features/server-hosting/computation/hostfocus_compute.h"
#include "features/port-mapping/zx_portmap.h" // [rc4l] and may ask the router to open the port
#include "features/server-browser/computation/bytesize_compute.h"
#include "features/server-browser/computation/browserhit_compute.h"
#include "features/server-browser/computation/colortext_compute.h"
#include "features/server-browser/computation/serverbrowser_compute.h"
#include "features/server-browser/computation/joinintent_compute.h"
#include "features/server-browser/computation/ownjoin_compute.h"
#include "features/server-browser/computation/pillgrid_compute.h"
#include "features/server-browser/computation/wadlist_compute.h"
#include "features/server-browser/computation/replyrouting_compute.h"
#include "features/server-browser/computation/scrollbar_compute.h"
#include "features/server-browser/computation/scrollview_compute.h"
#include "features/server-browser/computation/serversort_compute.h"
#include "features/server-browser/zx_joinserver.h"
#include "features/updater/computation/promptpanel_compute.h"
#include "features/wad-download/zx_waddownload.h"
#include "features/wad-download/zx_resolvejob.h"
#include "features/wad-download/computation/jobstate_compute.h"

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
// [rc4l] The space the browser's layout is WRITTEN in. Not the space it is drawn in: see below.
#define SB_LAYOUT_W			640
#define SB_LAYOUT_H			400

//*****************************************************************************
//
// [rc4l] The space the browser is DRAWN in, sized so this screen's mapping comes out UNIFORM, plus
// where the layout sits inside it.
//
// The browser used to be drawn in a fixed 640x400 through VirtualToRealCoords. Virtual scaling fits
// a space to the window by stretching each axis into whatever is left of it, so on a wide window the
// pills and rows came out squat while the stock menus beside them kept their shape. The menus do not
// stretch because V_CalcCleanFacs forces CleanXfac and CleanYfac equal and everything they draw goes
// through that one factor. This is the same idea by a different route: ask for a virtual space whose
// ASPECT matches the screen, and the scale is then identical on both axes by construction.
//
// A matching aspect fixes the shape but not the position. The layout is 640x400 and the space it now
// lives in is bigger on one axis, so it has to be placed: centred across, and below the global header
// down the screen. That is what the origin is, and it is applied ONCE, to the handful of layout
// constants that carry an absolute position. Everything else here is written as an offset from one
// of those, so it follows for free, and the mouse follows too because the hit tests read the same
// constants the drawing does.
//
// The header's height is taken out of the available room BEFORE the scale is chosen, so the browser
// shrinks to fit under the bar rather than being pushed off the bottom by it.
static int serverbrowser_VirtW( void );
static int serverbrowser_VirtH( void );
static int serverbrowser_OriginX( void );
static int serverbrowser_OriginY( void );

#define SB_VIRT_W			serverbrowser_VirtW( )
#define SB_VIRT_H			serverbrowser_VirtH( )

// [rc4l] The origin goes on the ABSOLUTE positions only, of which there are ten across the whole
// layout. A size must never carry it, and a width written as the difference of two positions cancels
// it on its own, which is why almost everything below is left exactly as it was.
#define SB_X( v )			( serverbrowser_OriginX( ) + ( v ))
#define SB_Y( v )			( serverbrowser_OriginY( ) + ( v ))

#define SB_PANEL_LEFT		SB_X( 36 )
#define SB_PANEL_RIGHT		SB_X( 604 )
// [rc4l] Derived from the rule below the tabs rather than hardcoded, so nothing ever sits on it.
#define SB_HEADER_Y			( SB_TAB_SEP_Y + 8 )
// [rc4l] 103 rather than 92: the sub-tab row and its two rules cost 11 virtual pixels. Everything
// else on this screen derives from this number through SB_CONTENT_TOP, so moving it is what keeps the
// margins even, and 103 is the value that lands them at 29 top and bottom with all 14 rows intact.
#define SB_FIRST_ROW_Y		SB_Y( 103 )

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
#define SB_DETAIL_LEFT		SB_X( 418 )
#define SB_DETAIL_RIGHT		SB_X( 596 )
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

// [rc4l] The modal. Centred, sized to hold a title, a wrapped message, an optional field and a row
// of buttons -- generous rather than snug, because a question that looks cramped reads as an error.
#define SB_DLG_W			280
#define SB_DLG_LEFT			(( SB_VIRT_W - SB_DLG_W ) / 2 )
#define SB_DLG_PAD			14
#define SB_DLG_LINE			11
#define SB_DLG_BTN_H		16
// [rc4l] Wide enough for the focus glow to sit in. Every other control in the browser has the glow
// five units off its left edge, and this is the only place two of them stand side by side -- at the
// gap that merely LOOKS right, the glow lands on the previous button and points at the wrong answer.
#define SB_DLG_BTN_GAP		22
#define SB_DLG_FIELD_H		16

// [rc4l] The remix picker, wider than the question dialog and split in two: the names on the left,
// what the highlighted one actually does on the right.
//
// Its own width because the dialog's is sized for a sentence and a row of buttons. A list beside a
// description needs more, and the description is the reason the picker is a panel rather than three
// buttons -- "Survival" tells you nothing you did not already guess, and "three lives each, spend
// them and you watch" is the whole answer.
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

// [rc4l] The player list sits under the WADs and gets whatever is left, up to this. It is capped for
// the same reason the WAD list is -- a full server would otherwise be thirty-two names long -- but
// its cap is the smaller of the two on purpose: the files decide whether you CAN join, and the names
// only decide whether you want to, so when the panel is tight the files keep their room.
#define SB_PLAYERLIST_MAX_H	56

// Its bar shares the WADs' column and width. Two scrollbars on one panel that did not line up would
// read as two different kinds of control.
#define SB_PLRBAR_X			SB_WADBAR_X
#define SB_PLRBAR_W			SB_WADBAR_W

// [rc4l] The hosting panel, which stands where the list and the detail panel would be.
//
// It used to be inset forty units either side, from when this was a centred one-column form with six
// fields and nothing to compare them against. It is a two-column browser now, and eighty units of
// margin were coming straight out of the experience names -- which are the longest text on the
// screen and the thing the whole tab is for reading. Now it takes the panel it stands in, less the
// same small breathing room the server list leaves.
#define SB_HOST_LEFT		( SB_PANEL_LEFT + 8 )
#define SB_HOST_RIGHT		( SB_PANEL_RIGHT - 8 )
// [rc4l] Below the SECOND rule, not the first. Hosting has its own sub-tab row now, so the panel
// starts where the browse tab's content starts and the two tabs no longer disagree about where the
// header ends.
#define SB_HOST_TOP			( SB_TAB_SEP_Y + 14 )
#define SB_HOST_PAD			16
#define SB_HOST_LINE		11

// [rc4l] The band a catalogue picture is drawn in, when an experience shipped one.
//
// Three times the header text it stands in for. Measured rather than chosen: at one text height a
// logo is an unreadable smudge whatever the colour depth, at two the wordmarks read but a full
// picture does not, and at four the widest art costs more than it is worth. The assets are built to
// suit this number, so changing it means regenerating them.
#define SB_HOST_ART_H		( BigFont->GetHeight( ) * 3 )

// Enough air to read the two as a pair rather than as one wide picture.
#define SB_HOST_ART_GAP		8
#define SB_HOST_ROW_H		15		// one field and the space under it
#define SB_HOST_FIELD_H		16
// [rc4l] Wide enough for the longest label, which is PREFERRED PORT. At 92 it fitted every label
// there was and then clipped the moment one grew -- the label is drawn from the left, so a label too
// long for its column runs under the field rather than being visibly cut, which reads as a typo
// ("PREFERRED POR") instead of as a layout problem.
#define SB_HOST_BTN_H		18
// [rc4l] Inset from its column rather than filling it. A button spanning every pixel of its column
// reads as a bar the panel is made of instead of as a thing to press, and the two of them edge to
// edge lost the gutter that says they are separate controls.
#define SB_HOST_BTN_INSET	14
#define SB_HOST_MAXLEN		40

// [rc4l] A row of mutually exclusive choices. Tall enough for the marker to be legible beside the
// label, which is what decides these two numbers rather than the text.
// [rc4l] The scrolling viewport, and the button below it.
//
// The button is pinned to the bottom of the panel rather than following the last setting. It is the
// one thing on this screen you always want to reach, and a control that moves every time a row is
// added is one you have to go looking for. The settings scroll behind it.
// [rc4l] The host panel runs lower than the server list's detail box. That box stops where it does
// to leave room for JOIN underneath it; this panel's button lives INSIDE it, so the same stopping
// point was simply wasted height -- and the settings, which are the tallest thing here, were the
// ones paying for it.
#define SB_HOST_BOTTOM		( SB_DETAIL_BOTTOM + 26 )
#define SB_HOST_BTN_Y		( SB_HOST_BOTTOM - SB_HOST_PAD - SB_HOST_BTN_H )
#define SB_HOST_VIEW_TOP	( SB_HOST_TOP + SB_HOST_PAD + SB_HOST_LINE + 8 )

// [rc4l] Two columns: WHAT to run on the left, how to run it on the right.
//
// [rc4l] The split is placed from the RIGHT: the detail column needs a fixed readable width and the
// list takes whatever is left, so widening the panel widens the list rather than padding both.
// Every unit gained goes to the experience names, which is where it was missing.
//
// The list has its own scroll because it grows with the catalogue while the settings never do.
#define SB_HOST_LIST_LEFT	( SB_HOST_LEFT + SB_HOST_PAD )
#define SB_HOST_LIST_RIGHT	( SB_HOST_RCOL_LEFT - 12 )
#define SB_HOST_RCOL_LEFT	SB_X( 328 )
#define SB_HOST_RCOL_RIGHT	( SB_HOST_RIGHT - SB_HOST_PAD )

// [rc4l] Wide enough for PREFERRED PORT, which is the longest label and the one that decides this.
// At the server list's 418 split the column had ~130px for a label AND a field, so the label was cut
// mid-word and the box was drawn over what was left of it.
#define SB_HOST_RLABEL_W	100

// [rc4l] The right column is ONE region showing one thing at a time: what the selection is, or the
// settings behind a button. Stacking both and giving each a scrollbar meant two thin bars a few
// pixels apart and content bleeding across the boundary between them, and the second bar was
// answering a question ("which of these two am I scrolling") that nobody should have to ask.
#define SB_HOST_RTOGGLE_H	SB_HOST_BTN_H
#define SB_HOST_RTOGGLE_Y	SB_HOST_BTN_Y

// [rc4l] BOTH buttons live at the foot of the right column now: what to do, and the settings beside
// it. They used to be one per column, which put the thing you press furthest from the thing you were
// reading and left the action button hanging under a list it does not act on.
//
// The action takes the left half while the toggle is beside it and the whole row when it is not:
// while a server is running there are no settings to toggle, and half a row with a hole beside it
// reads as a button that failed to draw rather than as the only one there.
#define SB_HOST_FOOT_GAP	10
#define SB_HOST_FOOT_LEFT	( SB_HOST_RCOL_LEFT + SB_HOST_BTN_INSET )
#define SB_HOST_FOOT_RIGHT	( SB_HOST_RCOL_RIGHT - SB_HOST_BTN_INSET )
#define SB_HOST_FOOT_W		( SB_HOST_FOOT_RIGHT - SB_HOST_FOOT_LEFT )
#define SB_HOST_FOOT_HALF	(( SB_HOST_FOOT_W - SB_HOST_FOOT_GAP ) / 2 )
#define SB_HOST_TOGGLE_X	( SB_HOST_FOOT_RIGHT - SB_HOST_FOOT_HALF )

// [rc4l] How many lines of filenames are drawn when something is under them.
//
// Three, and the number is a judgement rather than a measurement: it is enough for the four or five
// files a typical entry loads, and short enough that an entry loading twelve cannot push the
// gameplay settings off the bottom of a column. Past it the list ends in an ellipsis and hovering
// gives the whole thing. An entry with no settings has nothing to protect and gets no cap at all.
#define SB_HOST_WADS_MAXLINES	3

// The gameplay rows: one per way of playing, indented under their heading.
#define SB_HOST_GAME_ROW_H		SB_HOST_LINE
#define SB_HOST_GAME_INDENT		10

// [rc4l] Nearly half the pill's height, so the ends read as round rather than merely softened. Any
// larger and ComputeRoundedInset eats into the text at the widest row.
#define SB_HOST_PILL_RADIUS		4

// The lit dot inside a pill. Odd, so it has a middle pixel and reads as round at this size.
#define SB_HOST_PILL_DOT		5

// Between WRAPPED lines of pills. Small: enough that the rounded ends do not touch, not so much
// that one axis stops reading as one group.
#define SB_HOST_PILL_VGAP		2

#define SB_HOST_RTOP_TOP	SB_HOST_VIEW_TOP
#define SB_HOST_RTOP_BOTTOM	( SB_HOST_RTOGGLE_Y - 6 )
#define SB_HOST_RTOP_H		( SB_HOST_RTOP_BOTTOM - SB_HOST_RTOP_TOP )
#define SB_HOST_RBOT_TOP	SB_HOST_RTOP_TOP
#define SB_HOST_RBOT_BOTTOM	SB_HOST_RTOP_BOTTOM
#define SB_HOST_RBOT_H		SB_HOST_RTOP_H

// [rc4l] While a server is running the right column carries TWO things: what you are looking at, and
// what you are running. Each half scrolls on its own, with the rule at the seam. STOP sits below
// both and is never scrolled away from.
//
// The seam was fixed at the middle, which spent half the column on a status that is usually a few
// lines and left the gameplay settings in a window too short to use -- the very thing you are there
// to change while deciding whether to switch. HostRunSplit gives the status the room it needs and
// the details everything above it, falling back to the old half when the status is long.
#define SB_HOST_RUN_SPLIT	HostRunSplit( )
#define SB_HOST_RUN_TOP_BOT	( SB_HOST_RUN_SPLIT - 5 )
#define SB_HOST_RUN_BOT_TOP	( SB_HOST_RUN_SPLIT + 5 )
#define SB_HOST_RUN_TOP_H	( SB_HOST_RUN_TOP_BOT - SB_HOST_RTOP_TOP )
#define SB_HOST_RUN_BOT_H	( SB_HOST_RTOP_BOTTOM - SB_HOST_RUN_BOT_TOP )
// [rc4l] The SAME line the detail column ends on. It stopped four pixels higher, which cost the
// list a sliver of room and left its scrollbar visibly short of the one beside it -- two bars down
// one panel, ending at different heights, reads as one of them being cut off.
#define SB_HOST_VIEW_BOTTOM	SB_HOST_RTOP_BOTTOM
#define SB_HOST_VIEW_H		( SB_HOST_VIEW_BOTTOM - SB_HOST_VIEW_TOP )
#define SB_HOST_BAR_W		2
// [rc4l] Just inside the right column's backdrop rather than out at the panel's own edge, which is
// where the WAD list's bar sits relative to the detail panel and for the same reason: a bar floating
// outside the thing it scrolls does not read as belonging to it.
#define SB_HOST_BAR_X		( SB_HOST_RCOL_RIGHT + 3 )

// [rc4l] How far the right column's backdrop stands off its content. The same on all four sides, so
// the title is inset from the top by what the text is inset from the sides.
#define SB_HOST_RCOL_INSET	8

// [rc4l] The NEW screen, laid out in the same two columns the preset form uses so the two tabs read
// as one panel rather than two designs. Left is what you have, right is what you picked.
//
// The IWAD list is SHORT by construction: one is chosen, and a tall list of things you can only
// have one of wastes the height the wad list needs. Four rows plus its own scroll is enough to see
// that there is a choice, and the search box under it is what handles somebody with twenty.
#define SB_NEW_LINE			SB_HOST_LINE
#define SB_NEW_ROW_H		12
#define SB_NEW_TOP			( SB_HOST_TOP + SB_HOST_PAD )

// [rc4l] ONE ROW, not a list. You get one IWAD, so a list of them standing open is four rows spent
// saying what a single line already says -- and those rows come out of the wad list, which is the
// thing this screen is actually for. The choosing happens in a modal, where there is room to say
// where IWADs have to live for the game to see them at all.
// The label and the button share a line, so the whole choice is one row rather than a heading over
// a control. Everything under it moves up by a line, which the wad list gets.
#define SB_NEW_IWAD_TOP		SB_NEW_TOP
#define SB_NEW_IWAD_H		12
#define SB_NEW_IWAD_BOTTOM	( SB_NEW_IWAD_TOP + SB_NEW_IWAD_H )
#define SB_NEW_IWAD_GAP		8

// [rc4l] The modal. Wider than the browser's question dialogs because it holds a grid rather than a
// sentence, and because the line telling somebody where to put their files is only useful if it fits
// on one line.
//
// SB_NEW_MODAL_ROWS is what the box can SHOW, not what the list has. How many rows the pills come to
// depends on the player's filenames, so the two are different numbers and the difference is what the
// scrollbar exists for.
#define SB_NEW_MODAL_W		320
#define SB_NEW_MODAL_LEFT	(( SB_VIRT_W - SB_NEW_MODAL_W ) / 2 )
#define SB_NEW_MODAL_PAD	14
#define SB_NEW_MODAL_TOP	54
#define SB_NEW_MODAL_ROWS	6
#define SB_NEW_PILL_H		13
#define SB_NEW_PILL_VGAP	3
#define SB_NEW_PILL_HGAP	5
#define SB_NEW_PILL_PAD		9
#define SB_NEW_PILL_ROW_H	( SB_NEW_PILL_H + SB_NEW_PILL_VGAP )

// Room for the bar down the right of the grid, taken out of the content width so a pill can never
// be drawn underneath it.
#define SB_NEW_MODAL_BAR_W	6

// [rc4l] The wad list takes everything left over, which is what makes it the thing this screen is
// for. The gap is what it is because the YOUR WADS heading is measured BACKWARDS from the search
// box -- a line above it -- so too small a gap here puts that heading under the IWAD button rather
// than under nothing, which is what it did at ten.
#define SB_NEW_SEARCH_TOP	( SB_NEW_IWAD_BOTTOM + 18 )
#define SB_NEW_SEARCH_H		12
// [rc4l] This screen's button sits a little lower than the preset form's, and has its own name for
// it: the two panels are different shapes, and moving SB_HOST_BTN_Y would move the other one too.
#define SB_NEW_BTN_Y		( SB_HOST_BTN_Y + 5 )

#define SB_NEW_WADS_TOP		( SB_NEW_SEARCH_TOP + SB_NEW_SEARCH_H + 6 )

// [rc4l] The three settings buttons, in the strip under the wad list. One row rather than stacked:
// three fit across the column, and stacking would take three rows off the list to save nothing.
#define SB_NEW_TOOL_H		13
#define SB_NEW_TOOL_GAP		5
#define SB_NEW_TOOL_Y		( SB_NEW_BTN_Y - SB_NEW_TOOL_H - 6 )
#define SB_NEW_TOOL_COUNT	3
#define SB_NEW_TOOL_W		(( SB_NEW_WADS_RIGHT - SB_HOST_LIST_LEFT - \
								SB_NEW_TOOL_GAP * ( SB_NEW_TOOL_COUNT - 1 )) / SB_NEW_TOOL_COUNT )

#define SB_NEW_WADS_BOTTOM	( SB_NEW_TOOL_Y - 8 )

// [rc4l] Where a wad ROW stops, which is short of where the list stops: the scrollbar lives in the
// last few units of the column, so a row drawn to the column's edge puts its size under the bar.
//
// Reserved whether or not the bar is showing. Widening the rows when the list happens to fit would
// mean every size on screen shifting sideways the moment a search narrowed the list past the
// overflow, which is a lot of movement to save a few pixels.
#define SB_NEW_WADS_RIGHT	( SB_HOST_LBAR_X - 4 )

// The right column: the load order, with the buttons that act on a row sitting IN the row.
//
// [rc4l] Far enough down that the BACKDROP clears the heading above it. The backdrop is inset by
// SB_HOST_RCOL_INSET on every side, so the first row has to start that much lower again or the
// panel is drawn through the bottom half of the words "LOAD ORDER". Written as the sum it is rather
// than as the number it comes to, so it stays true if the inset changes.
#define SB_NEW_ORDER_TOP	( SB_NEW_TOP + SB_NEW_LINE + 2 + SB_HOST_RCOL_INSET )
#define SB_NEW_ORDER_BOTTOM	( SB_NEW_BTN_Y - 12 )
#define SB_NEW_ORDER_BTN_W	11

// [rc4l] The experience list's own bar, INSIDE the list the way the server list keeps its own, with
// the rows stopping short of it.
//
// It sat in the gap to the right of the list at first, which was the gap the column divider used to
// occupy -- and once the divider went and the right column got a backdrop, a bar out there read as
// part of the panel's margin rather than as something belonging to the list. A list's bar goes down
// the edge of the list.
#define SB_HOST_LBAR_X		( SB_HOST_LIST_RIGHT - 4 )
#define SB_HOST_ROW_RIGHT	( SB_HOST_LBAR_X - 3 )

#define SB_CHOICE_H			15
// Wide enough for the focus glow to sit in the gap rather than on the previous cell -- the same
// number the dialog's buttons needed, and for the same reason: every control in this browser puts
// the glow five units off its left edge, so anywhere two sit side by side the gap has to hold one.
#define SB_CHOICE_GAP		22

#define SB_FOOTER_Y			( SB_ROWS_BOTTOM + 20 )

// [rc4l] The refresh button, bottom left, beneath the list rather than beside it. The footer text is
// centred, so the left corner is the one piece of that line nothing else wants.
#define SB_REFRESH_W		74
#define SB_REFRESH_H		14
#define SB_REFRESH_X		( SB_PANEL_LEFT + 12 )
#define SB_REFRESH_Y		( SB_FOOTER_Y - 3 )

// [rc4l] The registry bars, sitting just right of the refresh button: they are about where the list
// came from, which is what that button goes and fetches.
#define SB_REGBAR_W			3
#define SB_REGBAR_H			10
#define SB_REGBAR_GAP		3
#define SB_REGBAR_X			( SB_REFRESH_X + SB_REFRESH_W + 8 )
#define SB_REGBAR_Y			( SB_REFRESH_Y + 2 )

// [rc4l] The panel's content span, in virtual pixels. ComputePanelRect pads BOTH ends by the corner
// radius, so the visible gap to the screen edge is ( SB_CONTENT_TOP - radius ) above and
// ( SB_VIRT_H - SB_CONTENT_BOTTOM - radius ) below. Deriving the top from the bottom is what forces
// those two to be equal; hardcoding them separately is how the panel ended up 4px from the top edge
// and 28px from the bottom.
#define SB_CONTENT_BOTTOM	( SB_FOOTER_Y + 24 )
// [rc4l] Mirrored inside the LAYOUT's own 400 units, not inside the drawing space. The drawing space
// is now whatever shape the window is, so measuring the top margin from its far edge would make the
// panel grow with the window while its contents stayed put.
#define SB_CONTENT_TOP		( 2 * serverbrowser_OriginY( ) + SB_LAYOUT_H - SB_CONTENT_BOTTOM )
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
#define SB_TAB_LEFT			SB_X( 48 )
// [rc4l] Pills are sized to their own text rather than to one shared width. MULTIPLAYER is nearly
// three times the length of HOST, and a width that fits the longer is mostly empty space around the
// shorter. The pad is the room either side of the label.
#define SB_TAB_PILL_PAD		13
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

// [rc4l] The sub-tab row: which servers BROWSE is showing, and the box that filters them.
//
// Smaller than the tabs above, because it is a choice WITHIN the thing the tabs chose and should not
// compete with them for the eye. The search box sits on this row rather than the one above because it
// filters this list: putting it beside PLAY would have it visible on a page where it filters nothing.
#define SB_SUBTAB_LEFT		SB_TAB_LEFT
#define SB_SUBTAB_PILL_PAD	10
#define SB_SUBTAB_GAP		5
#define SB_SUBTAB_H			12

// [rc4l] The gap between the two rows, and the rule that sits in the middle of it. 8 rather than 4
// so the rows read as two bands with a break between them instead of one crowded block. It costs a
// virtual pixel of margin at each end of the screen, which is the whole price: everything derives
// from SB_FIRST_ROW_Y, and widening the gap by two moves that by two and the margins by one.
// [rc4l] The rule under the TAB ROW, and it is drawn on every tab at the same height, always.
//
// It used to sit at the bottom of the whole header, which meant it stood in one place on BROWSE and
// another on PLAY, and switching tabs slid it up and down the screen. A divider that moves when you
// change what you are looking at reads as the page rebuilding itself underneath you. The fix is not
// to compute it more carefully, it is to have only ONE answer: this rule closes the tab row, the tab
// row is on every tab, so the rule never moves.
#define SB_TAB_ROW_SEP_Y	( SB_TAB_TOP + SB_TAB_H + SB_TAB_PAD )

// [rc4l] The sub-tab row is CENTRED between its two rules by construction: the same pad sits above
// and below it, so the two can never drift apart the way they did when the top was derived from the
// tab row above and the bottom from a separate constant.
#define SB_SUBTAB_PAD		5
#define SB_SUBTAB_TOP		( SB_TAB_ROW_SEP_Y + SB_SUBTAB_PAD )

#define SB_SEARCH_TOP		SB_SUBTAB_TOP
#define SB_SEARCH_H			SB_SUBTAB_H
#define SB_SEARCH_PAD		5

// A query longer than the box can show is a query nobody can read back. 40 is comfortably past any
// server name worth typing a fragment of.
#define SB_SEARCH_MAXLEN	40

// [rc4l] The second rule, under the sub-tab row, and it exists only on BROWSE. Everything that tab
// puts below it (the column header, the list, the detail panel) hangs off this rather than off the
// first rule.
//
// Nothing on PLAY reads it, which is what keeps the two tabs from fighting over one number: PLAY
// starts its panel from the rule that is always there, BROWSE starts its list from the one that only
// it draws, and neither has to know what the other does.
#define SB_TAB_SEP_Y		( SB_SUBTAB_TOP + SB_SUBTAB_H + SB_SUBTAB_PAD )

// Column x positions (left edge of each), virtual pixels.
#define SB_COL_FLAG			SB_X( 48 )
#define SB_COL_NAME			SB_X( 84 )

// [rc4l] Players sits as far right as it can rather than in the middle, and every pixel that buys
// goes to the name: SB_NAME_MAX_WIDTH is measured from here, so moving this moves that.
//
// The counts are what set the floor, MAXPLAYERS being 64, so the widest this column ever draws is
// "64/64" and the header "PLRS" is shorter still.
//
// Ping is right-aligned on SB_COL_PING and grows leftward to at most "999", so the gap left here
// is measured against its "PING" header rather than against the digits.
#define SB_COL_PLAYERS		SB_X( 330 )
#define SB_COL_PING			SB_X( 398 )

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

// When the glow was last advanced, so the next frame knows how much time it owes it.
static	int				g_GlowLastMs = 0;

// [rc4l] Showing "cancel this download?". Drawn and answered by this menu rather than through
// M_StartMessage, so the browser keeps control of the pairing: the hold placed on the join resume
// when this goes up MUST be released on exactly one of the two answers, and a message box that can be
// dismissed by other menu machinery is a way for that to not happen.
// [rc4l] The one modal, shared by every question the browser asks.
//
// It replaces "Y - stop it   N - keep going" painted on a dim -- the only part of this screen that
// did not look like the rest of it, and a second set of rules for the player to learn at the exact
// moment they are being asked something. The decisions live in computation/dialog_compute; this is
// the state they operate on.
//
// TWO SHAPES, ONE DIALOG: a row of choices, and a text field with choices under it. The second is
// why this is general rather than a cancel-specific box.
// [rc4l] What the hosting panel's one action button means right now.
//
// Named as a state rather than worked out at each of the four places that care, because they were
// disagreeing: the label came from one test, the tint from another, and what pressing it did from a
// third. STOP being unclickable was the same class of mistake one step further out.
enum class HostAction
{
	Play,		// nothing is running: start the selected entry and join it
	Cancel,		// fetching what the entry needs: stop the transfer
	Stop,		// the selected entry IS what is running: shut it down
	Switch,		// something else is running: stop that and stand this one up instead
	Back,		// the last attempt failed: dismiss the failure and get the form back
};

enum class DialogAction
{
	None,
	CancelDownload,
	JoinPassword,
	StopHosting,
	StopHostingAndJoin,
	SwitchHosting,

	// [rc4l] Deleting one of the player's own presets, which is a folder on their disk and gone for
	// good. Through the shared dialog rather than a box of its own, so it gets the same rule every
	// destructive question here gets: focus starts on the safe answer and Escape resolves to it.
	DeleteCustom,

	// [rc4l] Throwing away every flag somebody has set, or every map they have taken out of the
	// rotation. Not destructive on disk, but destructive of work -- a hundred and seventy-five
	// switches is an afternoon -- and there is no undo, so it is asked the same way.
	ResetFlags,
	ResetMaps,
	ResetGameplay,
};

struct BrowserDialog
{
	bool open;
	FString title;
	FString message;

	FString labels[3];
	char shortcuts[3];
	int count;
	int focus;
	int cancelIndex;		// what Escape resolves to; -1 means no way out

	bool hasInput;
	FString inputLabel;
	bool masked;			// a password is read over shoulders

	DialogAction action;

	BrowserDialog( ) : open( false ), count( 0 ), focus( 0 ), cancelIndex( -1 ),
		hasInput( false ), masked( false ), action( DialogAction::None )
	{
		shortcuts[0] = shortcuts[1] = shortcuts[2] = 0;
	}
};

static	BrowserDialog	g_Dialog;

// [rc4l] Whether saying yes to this question destroys something the player cannot get back -- their
// preset, their flags, their map list. Only the answer is tinted, not the button that asks; see
// DrawDialog.
//
// Stopping or switching a server is deliberately NOT in here. Those end something that is running
// and can be started again in a press, which is a different weight of mistake from work that is
// simply gone.
static bool DialogIsDestructive( DialogAction action )
{
	return ( action == DialogAction::DeleteCustom ) ||
		( action == DialogAction::ResetFlags ) ||
		( action == DialogAction::ResetMaps ) ||
		( action == DialogAction::ResetGameplay );
}

// What is typed into the dialog's field, edited by the same rules as the search box.
static	zx::TextInput	g_DialogInput;

// Which button the pointer is over, so hover lights it as it does everywhere else.
static	int				g_DialogHot = -1;

// [rc4l] The top row picks WHAT YOU ARE DOING; the row under it picks which servers.
//
// These used to be one row of three: PUBLIC, PRIVATE, HOST. Hosting is not a filter on a list of
// servers, so sitting it beside two things that are made the row mean two jobs at once, and the
// keyboard had to walk past hosting to get from one filter to the other.
//
// MULTIPLAYER FIRST, AND HOST IS CALLED HOST. The row used to read PLAY, MULTIPLAYER, which put the
// less common answer where the eye starts and named it after something the other tab does more of.
// Browsing is what this screen is for; hosting is what you do when the browsing turned nothing up,
// so it reads second and says what it is.
enum class BrowserTab { Browse, Host };
const int kTabCount = 2;

// Which servers MULTIPLAYER is showing. Only meaningful while that tab is selected. Public is the
// default because it is what nearly everyone wants nearly all the time: a private server is one you
// were told about, so you already know it is there.
enum class BrowseKind { Public, Private };
const int kBrowseCount = 2;

// [rc4l] And what HOST is showing. The second row belongs to whichever tab is selected rather than
// to browsing alone.
//
// Three, because "made by us" and "made by you" are not the same shelf and one word cannot mean
// both: PRESETS is the shipped catalogue this tab has always been, CUSTOM is where the ones you
// saved land, and NEW is where you build one. The order is the order you meet them in.
enum class HostKind { Presets, Custom, New };
const int kHostKindCount = 3;

// [rc4l] The row labels, at file scope because three things have to agree about them: what is
// drawn, what is clicked, and the widths both of those are measured from.
const char *const kTabLabels[kTabCount] = { "MULTIPLAYER", "HOST" };
const char *const kSubTabLabels[kBrowseCount] = { "PUBLIC", "PRIVATE" };
const char *const kHostSubTabLabels[kHostKindCount] = { "PRESETS", "CUSTOM", "NEW" };

// [rc4l] Browse, and Init decides again on every visit. This used to be "the tab you left on is the
// tab you come back to", which sounds considerate and is not: the player who ended their last visit
// on HOST because the list was empty gets sent back to HOST on the next visit, when the list may
// well have filled up in the meantime. Where you land is a question about the servers, so it is
// asked of the servers, in openingtab_compute.
static	BrowserTab		g_Tab = BrowserTab::Browse;
static	BrowseKind		g_Browse = BrowseKind::Public;
static	HostKind		g_HostKind = HostKind::Presets;

// [rc4l] Has any refresh run to completion this session? Only ever set, never cleared: the question
// is whether we have EVER had an answer, which is what separates "there are no servers" from "we
// have not looked yet", and only the first of those is a reason to open on HOST.
static	bool			g_ListHasAnswered = false;
static	int				g_TabHot = -1;
static	int				g_SubTabHot = -1;

// [rc4l] Hover state for the refresh button, matching how the tabs carry theirs.
static	bool			g_RefreshHot = false;

// [rc4l] The floor under REFRESH, and whether a press has been turned away since the last one that
// got through. Ten seconds because that is the registry's own limit. Ask sooner and it answers
// SRSC_REQUESTIGNORED, which spends the refresh AND earns a place on its flood queue, so refusing on
// this side is strictly better than being refused on that one.
//
// The flag is what lets the button count down instead of swallowing the press: it stays set until
// the floor runs out, so the player who asked gets an answer that keeps answering. Nobody who has
// not pressed it ever sees a countdown, which is the point of hanging this off a press at all.
#define SB_REFRESH_FLOOR_MS	10000
static	bool			g_RefreshRefused = false;

// [rc4l] The refresh tooltip, composed when it appears rather than every frame. See where it is
// built: it counts in seconds, and a box that resizes under the pointer as the number grows a digit
// is a box you cannot finish reading.
static	FString			g_RefreshTip;
static	bool			g_RefreshTipShown = false;


// [rc4l] What the hosting form was left holding. Archived so a player who hosts the same game every
// evening is not retyping it every evening; the password is deliberately absent, because one saved
// in a config file anybody with the machine can read is a worse promise than no password at all.
CVAR( String, cl_fua_hostname, FUA_DEFAULT_SERVERNAME, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )
CVAR( Int, cl_fua_hostport, 10666, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )
CVAR( Int, cl_fua_hostmaxplayers, 8, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )
CVAR( Bool, cl_fua_hostpublic, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )

// [rc4l] THE HOSTING FORM. What the player is about to run, kept across visits to the tab.
//
// Fields rather than one blob of config because each one is a separate decision with a separate
// failure: a name nobody can read, a port already taken, a password on a server meant to be open.
// The values live here and the editing rules live in textinput_compute, the same split the search
// box uses.
enum HostField
{
	kHostFieldName,
	kHostFieldPort,
	kHostFieldMaxPlayers,
	kHostFieldPassword,
	kHostFieldCount,
};

// [rc4l] PREFERRED, because that is what it is. A busy port does not stop the server -- the engine
// binds the next free one -- so this is what we ask for, not what we are guaranteed. Calling it PORT
// stated something the code could not honour, and the panel already shows the port actually in use
// once the server is up.
static const char *const g_HostFieldLabels[kHostFieldCount] = {
	"SERVER NAME", "PREFERRED PORT", "MAX PLAYERS", "PASSWORD",
};

static const char *const g_HostFieldTips[kHostFieldCount] = {
	"What other players see in their browser",
	"If it's already in use the server takes the next free one",
	"How many can be in at once",
	"Leave empty for a server anyone can join",
};

static	zx::TextInput	g_HostFields[kHostFieldCount];
static	int				g_HostFieldHot = -1;

// [rc4l] WHERE THE KEYBOARD IS ON THE HOST PANEL, as one value.
//
// This was three: an int for the field, a bool for the visibility row, a bool for the button, with an
// unwritten rule that at most one counted. Four places forgot to clear the others, and the symptoms
// were a caret in a box that would not type, two things glowing at once, and DOWN doing nothing.
//
// One value cannot disagree with itself. What each key does is computation/hostfocus_compute, the
// same shape browserfocus_compute gives the rest of the browser.
static	zx::HostFocusPos	g_HostFocus( zx::HostSlot::List, 0 );

// [rc4l] WHAT to run, above the settings that say how. The catalogue answers the first question and
// the fields answer the second, which is the same split as an entry's addon.json against the host's
// own choices: the entry never names the server and the form never picks the files.
//
// [rc4l] Every row is a catalogue entry now. There used to be a "Custom setup" row above them at -1,
// meaning "serve whatever this client is running", which is what the form did before the catalogue
// existed -- it went because it is not an experience. It described no content, its name told a player
// nothing about what they would be hosting, and it sat at the top of a list whose whole job is to
// answer "what do you want to play".
#define SB_HOST_CATALOGUE_FIRST		( 0 )
#define SB_HOST_ENTRY_H				12

static	int				g_HostEntrySel = SB_HOST_CATALOGUE_FIRST;
static	int				g_HostEntryHot = -2;	// -2 is "none"

// [rc4l] Which way of playing the selected entry the player wants, held as the variant's ID rather
// than a row number.
//
// An index would have to be reset every time the selection moved, in each of the several places that
// move it, and a missed one hands them a different game from the one the panel is showing. An id
// cannot be stale in that way: PickVariant answers with the entry's default whenever this names
// something the entry does not have, which covers both switching entries and a catalogue that has
// been updated underneath a remembered choice.
static	FString			g_HostVariantId;

// [rc4l] Whether the cursor is on an opened experience's OWN row rather than on one of the ways of
// playing under it.
//
// The one thing the selection cannot say by itself. Everywhere else the cursor is derived from what
// is chosen, and that works because a choice names exactly one row -- but an open experience's own
// row and its default variant's row are both "this entry, playing the default", so deriving alone
// put the cursor on the variant and left the row above it unreachable. UP off the first variant then
// moved the selection to a row it was already on and looked like a key that did nothing.
static	bool			g_HostOnEntryRow = false;

// [rc4l] Which experiences are opened out to show their ways of playing, entry for entry.
//
// Any number at once. One at a time was the first attempt and it made comparing two packs
// impossible: opening the second shut the first, so the thing you wanted to compare against
// disappeared at the moment you went to look at it. Grown on demand, and anything past the end
// counts as shut, so a fresh session starts with nothing to allocate.
static	std::vector<bool>	g_HostOpenEntries;

static bool HostEntryIsOpen( int entry )
{
	return ( entry >= 0 ) && ( entry < static_cast<int>( g_HostOpenEntries.size( ))) &&
		g_HostOpenEntries[entry];
}

static void HostToggleEntryOpen( int entry )
{
	if ( entry < 0 )
		return;

	if ( entry >= static_cast<int>( g_HostOpenEntries.size( )))
		g_HostOpenEntries.resize( entry + 1, false );

	g_HostOpenEntries[entry] = !g_HostOpenEntries[entry];
}

// [rc4l] Which remix the player wants ON EACH AXIS, held as ids for the reason the variant is: the
// pool is re-read and reordered, and a stored number would eventually point at something else.
//
// One entry per group, keyed by group id. Kept across entries on purpose: choosing Brutal Doom and
// then looking at three other experiences should not quietly forget it, and an entry that does not
// offer that axis simply never reads the key.
static	std::vector<std::pair<std::string, std::string> >	g_HostRemixIds;

// What is wanted on one axis, or empty when nothing has been chosen there yet.
static const std::string &HostRemixWanted( const std::string &group )
{
	static const std::string kNone;

	for ( size_t i = 0; i < g_HostRemixIds.size( ); ++i )
	{
		if ( g_HostRemixIds[i].first == group )
			return g_HostRemixIds[i].second;
	}

	return kNone;
}

static void HostSetRemixWanted( const std::string &group, const std::string &id )
{
	for ( size_t i = 0; i < g_HostRemixIds.size( ); ++i )
	{
		if ( g_HostRemixIds[i].first == group )
		{
			g_HostRemixIds[i].second = id;
			return;
		}
	}

	g_HostRemixIds.push_back( std::make_pair( group, id ));
}

// What the selected way of playing can be played with. The VARIANT decides, not just the entry:
// Skulltag's Invasion takes three lives and its Duel does not.
static std::vector<zx::AddonRemix> HostOfferedRemixes( const zx::AddonEntry &addon )
{
	const zx::VariantPick pick = zx::PickVariant( addon, g_HostVariantId.GetChars( ));

	return zx::OfferedRemixes( addon, pick.index, zx::CatalogueRemixes( ));
}

// [rc4l] How many lives the player has asked for, or -1 for "not yet". Kept across entries like the
// remix ids, and clamped per entry rather than reset: asking for two lives is a preference about how
// you play, not about which pack you were looking at when you said it.
static	int				g_HostLives = -1;

// [rc4l] Weapon speed, kept the same way and for the same reason: it is a preference about how you
// play rather than about which pack you were looking at when you set it.
static	int				g_HostFastWeapons = -1;

// [rc4l] The mode actually in force. Declared here and defined below the picks it needs: both the
// lives control and the teams control ask the same question and must not answer it two ways.
static zx::HostGameMode HostGameModeFor( const zx::AddonEntry &addon );

// The lives control for the CHOSEN way of playing. The variant's gamemode when it declares one,
// otherwise the entry's, because most packs play one way and should say it once.
static zx::LivesControl HostLivesControl( const zx::AddonEntry &addon )
{
	const zx::VariantPick pick = zx::PickVariant( addon, g_HostVariantId.GetChars( ));

	const zx::HostGameMode mode = HostGameModeFor( addon );

	// [rc4l] And the variant's own lives when it states them, because an entry can gather packs that
	// are not alike: six campaign mapsets that can be run as Survival sit beside four co-op packs that
	// cannot, and the entry's single answer put a lives slider on all ten.
	int defaultLives = addon.defaultLives;
	int maxLives = addon.maxLives;

	if (( pick.index >= 0 ) && ( pick.index < static_cast<int>( addon.variants.size( ))))
	{
		const zx::AddonVariant &v = addon.variants[pick.index];

		if ( v.defaultLives >= 0 )
			defaultLives = v.defaultLives;
		if ( v.maxLives >= 0 )
			maxLives = v.maxLives;
	}

	return zx::LivesFor( mode, g_HostLives, defaultLives, maxLives );
}

// [rc4l] Whether the chosen way of playing offers the weapon speed. Either the entry or the variant
// may say yes, the same rule the teams control uses and for the same reason.
static bool HostFastWeaponsOffered( const zx::AddonEntry &addon )
{
	const zx::VariantPick pick = zx::PickVariant( addon, g_HostVariantId.GetChars( ));

	if ( addon.fastWeapons )
		return true;

	if (( pick.index >= 0 ) && ( pick.index < static_cast<int>( addon.variants.size( ))))
		return addon.variants[pick.index].fastWeapons;

	return false;
}

// [rc4l] Which map to open on, as an index into the chosen way of playing's own rotation, or -1 for
// "wherever it would have started". An index rather than a name because the axis IS the rotation:
// remembering MAP07 and finding a pack that has no MAP07 would leave a choice that cannot be met.
static	int				g_HostStartMap = -1;

// The rotation of the last cfg asked about. One entry of cache, which is all this needs: the panel
// asks about the same cfg many times a frame and about a different one only when the selection
// changes.
static	FString			g_HostRotationCfg;
static	std::vector<std::string>	g_HostRotation;

// [rc4l] The maps the chosen way of playing writes into its rotation, read out of its cfg.
//
// The cfg is the SERVER's file and this client has never parsed one. This is the one exception and
// it is narrow: only addmap lines, only to name them. See maprotation_compute.h for why reading it
// beats writing the same thirty-two names into addon.json a second time.
static const std::vector<std::string> &HostRotation( const zx::CatalogueEntry &entry )
{
	const FString path = zx::CatalogueServerCfgPath( entry, g_HostVariantId.GetChars( )).c_str( );

	if ( g_HostRotationCfg.Compare( path ) == 0 )
		return g_HostRotation;

	g_HostRotationCfg = path;
	g_HostRotation.clear( );

	if ( path.IsNotEmpty( ))
	{
		FILE *fp = fopen( path.GetChars( ), "rb" );
		if ( fp != NULL )
		{
			std::string text;
			char buf[4096];
			size_t got;

			while (( got = fread( buf, 1, sizeof( buf ), fp )) > 0 )
				text.append( buf, got );

			fclose( fp );
			g_HostRotation = zx::MapsInRotation( text );
		}
	}

	return g_HostRotation;
}

// The rotation of whatever is selected right now, which is what the picker runs on. Empty when
// nothing is selected, or when the way of playing writes no rotation at all -- Doom Barracks Zone
// leans on its pack's own mapinfo chain and lists nothing.
static const std::vector<std::string> &HostSelectedRotation( )
{
	static const std::vector<std::string> kNone;

	const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( );

	if (( g_HostEntrySel < 0 ) || ( g_HostEntrySel >= static_cast<int>( entries.size( ))))
		return kNone;

	return HostRotation( entries[g_HostEntrySel] );
}

// Which map the picker is on, always a legal index when there is a rotation at all. Nothing chosen
// is the first, which is where the server would have started anyway.
static int HostStartMapIndex( )
{
	const int count = static_cast<int>( HostSelectedRotation( ).size( ));

	if ( count <= 0 )
		return 0;

	return clamp(( g_HostStartMap < 0 ) ? 0 : g_HostStartMap, 0, count - 1 );
}

// [rc4l] The axis whose choices REPLACE the weapons, which is the one the speed slider argues with.
// Named rather than guessed at: it is the group id those remixes carry in the catalogue.
static const char *const kHostMixGroup = "mix";

// Whether the mix axis is sitting on the choice that adds nothing, which is the first one offered.
// Read WITHOUT the lock below, or the answer would be whatever the lock had just forced.
static bool HostMixIsBaseline( const zx::AddonEntry &addon )
{
	const std::vector<zx::RemixGroup> groups = zx::GroupRemixes( HostOfferedRemixes( addon ));

	for ( size_t g = 0; g < groups.size( ); ++g )
	{
		if ( groups[g].id != kHostMixGroup )
			continue;

		return zx::PickRemix( groups[g].choices, HostRemixWanted( groups[g].id )).index <= 0;
	}

	// No mix axis at all, so nothing has replaced the weapons.
	return true;
}

// Which of the weapon speed and the mix has the panel. See weaponspick_compute.h for why only one of
// them can.
static zx::WeaponsPlan HostWeaponsPlan( const zx::AddonEntry &addon )
{
	return zx::PlanWeapons( HostFastWeaponsOffered( addon ), g_HostFastWeapons,
		HostMixIsBaseline( addon ));
}

// What is in force on every axis at once. One pick per group, in the entry's own group order.
//
// [rc4l] With the mix TAKEN BACK to its baseline while the weapon speed is up. Done here rather than
// at the draw, because this is the one place that answers "what is in force": the file list, the
// host plan, the download set and the configuration key all come through it, and a lock only the
// pills knew about would show Vanilla while starting a server on Brutal Doom.
static std::vector<zx::RemixPick> HostRemixPicks( const zx::AddonEntry &addon )
{
	std::vector<std::pair<std::string, std::string> > wanted = g_HostRemixIds;

	if ( HostWeaponsPlan( addon ).forceBaselineMix )
	{
		for ( size_t i = 0; i < wanted.size( ); ++i )
		{
			// Emptied rather than removed, and never written back to g_HostRemixIds: an empty want
			// takes the first offered, which is the baseline by the catalogue's own convention, and
			// the player's real choice is still there when the speed comes back down.
			if ( wanted[i].first == kHostMixGroup )
				wanted[i].second = "";
		}
	}

	return zx::PickRemixes( HostOfferedRemixes( addon ), wanted );
}

// [rc4l] The mode in force: what the entry says, then its variant, then whatever mode mix is lit.
//
// The pill wins over both, because it is the most recent and most specific thing said. An entry
// declares how it plays by default; a variant narrows that; picking DEATHMATCH or CAPTURE THE FLAG
// on the panel is the player changing it now, and the teams and lives controls read the answer.
static zx::HostGameMode HostGameModeFor( const zx::AddonEntry &addon )
{
	const zx::VariantPick pick = zx::PickVariant( addon, g_HostVariantId.GetChars( ));

	zx::HostGameMode stated = addon.gameMode;
	if (( pick.index >= 0 ) && ( pick.index < static_cast<int>( addon.variants.size( ))) &&
		( addon.variants[pick.index].gameMode != zx::HostGameMode::Unknown ))
	{
		stated = addon.variants[pick.index].gameMode;
	}

	return zx::EffectiveGameMode( stated, HostRemixPicks( addon ));
}

// [rc4l] How many teams the player has asked for, or -1 for "not yet". Kept across entries like the
// lives count and for the same reason.
static	int				g_HostTeams = -1;

// The teams control for the CHOSEN way of playing. Read off the VARIANT throughout: the gamemode
// because Skulltag's Deathmatch and its Duel are not the same question, and the say-so because its
// Skulltag variant declares deathmatch and then runs a mode of its own.
static zx::TeamsControl HostTeamsControl( const zx::AddonEntry &addon )
{
	const zx::VariantPick pick = zx::PickVariant( addon, g_HostVariantId.GetChars( ));

	// EITHER may say yes. An entry that plays one way has no variant to say it on -- Brutal Doom is
	// eleven deathmatch maps and nothing else -- and an entry with several says it per variant.
	bool bOffered = addon.teams;

	if (( pick.index >= 0 ) && ( pick.index < static_cast<int>( addon.variants.size( ))))
		bOffered = bOffered || addon.variants[pick.index].teams;

	return zx::TeamsFor( HostGameModeFor( addon ), bOffered, g_HostTeams );
}

//*****************************************************************************
//
// [rc4l] The pictures the catalogue ships, loaded from disk and kept until the selection changes.
//
// These are ordinary files beside an addon.json, NOT lumps, and that is deliberate. Anything added
// to the loaded-file list is advertised to a server and authenticated against it, so putting menu
// decoration there would make a picture a reason a player cannot join. The engine already reads a
// PNG straight off disk for savegame thumbnails; this is the same road.
//
// Held rather than reloaded per frame because a texture is a decode, and the panel redraws sixty
// times a second while the selection changes at the speed of a keypress.
//
struct HostArt
{
	FTexture		*pTex;
	FString			Path;

	HostArt( ) : pTex( NULL ) { }
};

static	HostArt		g_HostArtMain;		// what you are playing
static	HostArt		g_HostArtMix;		// what you are playing it with

//*****************************************************************************
//
// One picture, from a path. NULL for anything that is not there or will not decode, which is the
// ordinary answer: most experiences ship none and the panel draws their name instead.
//
static FTexture *serverbrowser_LoadArt( const char *pszPath )
{
	if (( pszPath == NULL ) || ( pszPath[0] == '\0' ))
		return NULL;

	FILE *pFile = fopen( pszPath, "rb" );
	if ( pFile == NULL )
		return NULL;

	FTexture *pTex = NULL;
	PNGHandle *pPng = M_VerifyPNG( pFile );

	if ( pPng != NULL )
	{
		// The texture keeps the PATH and reopens it when it needs the pixels, so this handle is
		// finished with either way and the file must simply stay where it is.
		pTex = PNGTexture_CreateFromFile( pPng, pszPath );
		delete pPng;

		// What a refusal looks like: a one-pixel texture rather than a null. Drawing it would put a
		// dot where the name should be, which reads as a bug rather than as no art.
		if (( pTex != NULL ) && ( pTex->GetWidth( ) <= 1 ) && ( pTex->GetHeight( ) <= 1 ))
		{
			delete pTex;
			pTex = NULL;
		}
	}

	fclose( pFile );
	return pTex;
}

//*****************************************************************************
//
// Point a slot at a path, reloading only when it actually changed.
//
static void serverbrowser_SetArt( HostArt &art, const FString &path )
{
	if ( art.Path.Compare( path ) == 0 )
		return;

	if ( art.pTex != NULL )
	{
		delete art.pTex;
		art.pTex = NULL;
	}

	art.Path = path;
	art.pTex = serverbrowser_LoadArt( path.GetChars( ));
}

//*****************************************************************************
//
// [rc4l] Let go of every picture. Called when the catalogue is reread and when the browser closes:
// a reload can replace the file a texture is still holding a path to, and nothing else would notice.
//
static void serverbrowser_FreeArt( void )
{
	serverbrowser_SetArt( g_HostArtMain, FString( ));
	serverbrowser_SetArt( g_HostArtMix, FString( ));
}

// [rc4l] What the CHOSEN way of playing loads, remix included. Every question the host tab asks about
// files -- what to list, what to size, what to verify, what to fetch, what to start on -- comes
// through here.
//
// Not addon.files, which used to be the answer and no longer is one. Ghouls vs Humans keeps nothing
// at the entry level and a whole different wad on each way of playing, so reading the entry's own
// list would show and start something no variant plays.
//
// The remix's files go on the end, which is also why the panel needs no work to show them: pick a
// remix that loads something and it appears in the file list with its size, beside everything else
// the server will be started on.
static std::vector<zx::AddonFileRef> HostSelectedFiles( const zx::AddonEntry &addon )
{
	// [rc4l] Assembled by the unit rather than here, because dropping what a mix already contains
	// needs to know which list each file came from. Doing it here is also why it is right: every
	// question about files comes through this one function, so a file a mix replaces leaves the
	// panel and its size total as well as the command line, for free.
	return zx::CombineFiles( zx::PickVariant( addon, g_HostVariantId.GetChars( )).files,
		HostRemixPicks( addon ));
}

// [rc4l] What we told the server to load, kept so the client can match it before joining.
//
// JoinOwnServer connected straight to the address, and the reason it could was that the server was
// always running the files WE were running -- its command line came from ours. A catalogue entry
// breaks that: the server loads the entry, this client is still on whatever it had, and the join is
// refused for protected-lump authentication. So when an entry decided the files, the client reloads
// onto them first.
static	FString			g_HostEntryIwad;
static	TArray<FString>	g_HostEntryPwads;

// [rc4l] The list scrolls independently of the settings: it grows with the catalogue and they never
// do, so sharing one offset would drag the form off screen as entries were added.
static	int				g_HostListScroll = 0;

// The detail region's own offset. An entry with many files scrolls here without moving the form.
static	int				g_HostDetailScroll = 0;

// [rc4l] The running status scrolls independently of the details above it, and its height is measured
// as it is drawn: the text wraps, so the line count is not something the layout can know in advance.
static	int				g_HostStatusScroll = 0;

// Which of the running panel's two bars a held drag belongs to, so sliding off one sideways does not
// hand the grab to the other.
static	bool			g_DraggingHostDetailBar = false;
static	bool			g_DraggingHostStatusBar = false;
static	bool			g_DraggingHostListBar = false;

// [rc4l] The band the status text may draw in, or an empty range for "anywhere". Set around the
// status half only; see HostTextRowVisible for why a rectangle would not have done.
static	int				g_HostTextClipTop = 0;
static	int				g_HostTextClipBottom = 0;
static	int				g_HostStatusH = 0;

// [rc4l] Where the running server's status begins. Measured from the BOTTOM: the status takes the
// room its content needs and the details keep the rest, rather than each taking half whatever they
// hold. Never more than half, so a long status cannot push the details off the panel entirely.
//
// g_HostStatusH is last frame's measurement, which is what every other reader of it uses. It is a
// content height and so does not move when the seam does, and the feedback loop that would
// otherwise imply cannot start.
static int HostRunSplit( )
{
	const int half = SB_HOST_RTOP_TOP + SB_HOST_RTOP_H / 2;
	const int wanted = SB_HOST_RTOP_BOTTOM - g_HostStatusH - 5;

	return ( wanted > half ) ? wanted : half;
}

// [rc4l] The right column shows the description by default. The settings are one click away rather
// than always on screen: almost nobody changes them, and the thing worth reading before pressing
// START is what the selection will actually load.
static	bool			g_HostShowSettings = false;
static	bool			g_HostOnSettingsToggle = false;
// [rc4l] The gameplay rows are drawn inside the scrolled detail region, so where they LAND is only
// known once they have been drawn. Each frame's draw records them here and the pointer tests against
// that, which is how a row that scrolled out of view stops being clickable without a second copy of
// the layout deciding when.
struct HostGameplayRow
{
	int x, w, y, h;			// the clickable extent; a pill is narrower than the column
	std::string group;		// which axis the click sets
	std::string id;
};
static	TArray<HostGameplayRow>	g_HostGameRows;
static	int						g_HostGameHot = -1;


// [rc4l] A SLIDER on the gameplay panel, recorded as it is drawn like the pills above.
//
// Not "the lives track". Lives is the only setting using one today and will not be the last -- a
// starting map or a time limit is the same shape -- so the geometry, the drag, the step buttons and
// the rounding all live here once, keyed by an id, rather than being written again per setting with
// the chance to disagree.
struct HostSliderRect
{
	std::string id;			// which setting this is; the caller maps it back
	int trackX, trackY, trackW;
	int minusX, plusX, stepW;
	int min, max, value;
};

static	TArray<HostSliderRect>	g_HostSliders;
static	FString					g_HostSliderHot;		// id under the pointer, or empty
static	FString					g_HostSliderDragging;	// id being dragged, or empty

// [rc4l] One entry per ROW of the gameplay panel, in the order it draws, so the keyboard has
// something to walk.
//
// Built BY THE DRAW, the same way the hit rects are, and for the same reason: the alternative is a
// second function listing the rows in the order the first one happens to draw them, and two lists
// of one layout is how a keyboard comes to land on a control that is not where it thinks.
//
// Unlike a hit rect it is pushed even when the row is scrolled out of view. A pointer cannot click
// what it cannot see, but the keyboard is entitled to reach it -- RevealHostFocus scrolls it back.
struct HostGameFocusRow
{
	bool		bSlider;	// false: an axis of pills
	std::string	id;			// the slider's id, or the axis's group
	int			y;			// as drawn, so the glow and the reveal agree with the draw
	int			h;
};

static	TArray<HostGameFocusRow>	g_HostGameFocusRows;

// [rc4l] The remix picker, open over the panel. A list rather than a row of buttons, because the
// dialog's three-button ceiling is exactly the wall this would hit: two options today, and the whole
// point of a shared pool is that there will be more.

// [rc4l] Which catalogue row the RUNNING server was started from, so the list can mark it and SWITCH
// knows there is nothing to switch to. -2 is "custom setup", matching g_HostEntrySel's own spelling.
static	int				g_HostingEntry = -2;

// [rc4l] And the whole configuration it was started on, so the action button can tell "looking at
// what is running" from "looking at the same pack set up differently". See HostSelectionKey.
static	FString			g_HostingKey;

// [rc4l] And WHICH way of playing it was started as, so the tint can go on the row the server is
// actually running. The id rather than the row number, for the reason g_HostVariantId is: rows move
// when an experience is opened out, and a stored number would then point at the wrong one.
static	FString			g_HostingVariantId;

// [rc4l] A transfer fetching what an entry needs before it can be hosted.
//
// The catalogue ships an md5 per file precisely so this is possible, and BuildHostPlan has always
// returned `missing` rather than treating an absent file as fatal -- and then the button refused
// anyway, because nobody wired the two together. Hosting an entry you do not have yet said
// "downloading from here is not possible" while the join beside it downloaded the same file happily.
//
// `entry` is the catalogue row the transfer is FOR, so what resumes is the thing the player asked
// for rather than whatever is selected by the time it lands. -1 means nothing of ours is waiting.
//
// The WAITING itself is not ours: zx::SetPendingResume parks this in the same slot a pending join
// uses, so the hold while a prompt is up, the "you have wandered off, so it waits" band, and the
// cancel that keeps the file but drops the intent are the same code for both. See zx_joinserver.h.
static	int				g_HostDownloadEntry = -1;

// [rc4l] Set on the frame the resume fires, and acted on by the Ticker one frame later. The resume
// arrives from inside waddownload::Tick, and starting a server from there would re-enter it.
static	bool			g_HostDownloadResumed = false;
static	bool			g_HostDownloadSucceeded = false;

// [rc4l] The same pair for a CUSTOM preset's fetch. A separate slot rather than a shared one
// because the two resumes do different things afterwards -- one hosts a catalogue entry by index,
// the other reloads a preset from disk -- and one flag serving both would host whichever was last.
static	bool			g_CustomDownloadResumed = false;
static	bool			g_CustomDownloadSucceeded = false;
static	FString			g_CustomDownloading;

static void serverbrowser_CustomDownloadResume( bool allSucceeded )
{
	g_CustomDownloadResumed = true;
	g_CustomDownloadSucceeded = allSucceeded;
}

// Free function because ResumeProc is a plain pointer and this menu is a class.
static void serverbrowser_HostDownloadResume( bool allSucceeded )
{
	g_HostDownloadResumed = true;
	g_HostDownloadSucceeded = allSucceeded;

	// [rc4l] Say so when the player is not here to see it happen.
	//
	// The resume below is driven by the browser's Ticker, so it only fires once they are back in the
	// browser. A player who wandered off while an experience downloaded got no ending at all: the
	// progress band vanished on completion and nothing replaced it. The band already knew how to
	// wait to be come back to; it just had no way to talk about hosting.
	if ( allSucceeded && ( zx::IsServerBrowserOpen( ) == false ))
		zx::NoteHostReady( );
}

// [rc4l] Bumped whenever files may have appeared on disk, which invalidates the have-cache behind the
// file list. Without it a download would land and the panel would go on saying the file was missing,
// because the cache only noticed the SELECTION changing and the selection had not.
static	int				g_HostHaveGeneration = 0;

// Drag-selection in a host field, and when the last click landed -- the two things a field needs to
// tell a double-click from two clicks, and a drag from a press.
static	bool			g_HostFieldDragging = false;
static	int				g_HostClickTime = 0;

// [rc4l] The clip rectangle DimClipped intersects against, in screen pixels. -1 means no clip.
// File scope beside the rest of the browser's state rather than inside the menu class, which is
// where every other global here lives.
static	int				g_ClipTopPx = -1;
static	int				g_ClipBottomPx = -1;

// [rc4l] How far the settings are scrolled, in virtual units. The form outgrew its panel the moment
// a fifth row was imagined, and a form that simply overflows is one whose last setting cannot be
// reached at all.
static	int				g_HostScroll = 0;

static	bool			g_HostButtonHot = false;

// [rc4l] Whether to announce to the registry. Local hosting is the default: it always works, needs
// nothing forwarded, and is what somebody playing with people in the same house wants. Global is a
// deliberate second step -- see the reachability check.
static	bool			g_HostAdvertise = false;

// Which cell of the visibility row the pointer is over. Whether the KEYBOARD is on it is g_HostFocus.
static	int				g_HostVisHot = -1;

// Whether the pointer is on COPY TO NEW, the same split for the same reason.
static	bool			g_HostCopyHot = false;


// Two answers: local, then global. Named so the row and everything that indexes it agree.
// [rc4l] Internet FIRST, and the default. Hosting to be joined is the ordinary reason to host, and
// a form that opens on the narrower answer makes the common case the one you have to correct.
//
// The values carry the order so that nothing else has to know it -- every index in this file goes
// through these names, which is what stops a reorder from silently swapping the two answers.
enum { kHostVisGlobal = 0, kHostVisLocal = 1, kHostVisCount = 2 };

// [rc4l] Which cell the keyboard is POINTING AT, which is not the same as which one is chosen.
//
// Left and right used to set the value as they moved, so there was no way to look along the row
// without changing the answer -- arrowing past INTERNET to read what HOME said had already switched
// you to it. A checkbox row is navigated and then committed, like every other one.
static	int				g_HostVisCursor = kHostVisGlobal;

// True once the form has been filled from the CVARs that remember it, so a visit to the tab does not
// wipe what was typed on the last one.
static	bool			g_HostFormLoaded = false;

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

// ---------------------------------------------------------------------------------------------
//
// [rc4l] NEW: a server built by hand, out of the player's own files.
//
// Four regions and one rule about them: the IWAD is a single choice and the PWADs are an ordered
// list, so they are two different controls rather than one list with a flag on a row. See
// features/wad-library/computation/loadorder_compute.h for why order is the whole point.

// [rc4l] Tools is the row of three buttons under the wad list, and Buttons is SAVE and PLAY NOW.
// Both are focuses of their own because what they open or do is otherwise reachable only by mouse,
// which would make the keyboard a way to do some of the job.
enum class NewFocus { Iwads, Search, Wads, Tools, Order, Buttons };

static	NewFocus			g_NewFocus = NewFocus::Wads;

// Which row is under the cursor in each region, and where each list is scrolled to. Separate
// because they are separate lists: scrolling one must not move the others.
static	int					g_NewIwadSel = 0;
static	int					g_NewIwadScroll = 0;

// Whether the player has picked one themselves. Until they have, the selection follows the IWAD
// this client is running, which is the only one their own join can authenticate against.
static	bool				g_NewIwadChosen = false;

// [rc4l] Which modal is up, if any. One selector rather than a flag per box: every place that has
// to know "is a modal open" -- the pointer, the keys, the draw order -- asks once, and adding a
// fourth box cannot leave one of them behind.
enum class NewModal { None, Iwad, Flags, Maps, Gameplay, Save };

static	NewModal			g_NewModal = NewModal::None;

// [rc4l] The settings the three boxes edit, as name/value pairs handed to the server on the command
// line. HostConfig already carries exactly this for the preset panel's gameplay controls, so a
// server built here needs no cfg of its own to be configured.
static	std::vector<std::pair<std::string, std::string> >	g_NewCvars;

// The flag fields as this screen has them: read from the engine when the tab is first opened, then
// edited here. Kept whole rather than as a diff so the number boxes always have something to show.
static	std::vector<zx::FlagField>	g_NewFlags;
static	bool				g_NewFlagsLoaded = false;
static	int					g_NewFlagsScroll = 0;
static	int					g_NewFlagHot = -1;
static	int					g_NewFlagFieldHot = -1;
static	int					g_NewToolHot = -1;
static	int					g_NewToolSel = 0;		// which of the three the keyboard is on
static	bool				g_NewSaveHot = false;
static	int					g_NewButtonSel = 1;		// 0 SAVE, 1 PLAY NOW; play is the usual one

// [rc4l] The save box: the name being typed, whether the player has been asked about replacing, and
// what the box last worked out to say. See customsave_compute for why the asking is a state.
static	zx::TextInput		g_NewSaveName;
static	bool				g_NewSaveAsked = false;
static	int					g_NewSaveFirstChar = 0;
static	bool				g_NewSaveDragging = false;
static	int					g_NewSaveClickTime = 0;
static	int					g_NewSaveBtnHot = -1;	// 0 Confirm, 1 Cancel
static	int					g_NewSaveBtnSel = 0;

// [rc4l] One text box per field, and which one is being typed in.
//
// The box holds TEXT, not the number: mid-edit it can be empty, or hold something that is not a
// number yet. Rewriting it from the value on every frame would fight the typing -- delete the last
// digit and it would reappear. So the box is the source while it is being edited, and the value is
// the source the rest of the time.
static	std::vector<zx::TextInput>	g_NewFlagInput;
static	int					g_NewFlagEditing = -1;
static	bool				g_NewFlagInputDragging = false;
static	int					g_NewFlagInputClickTime = 0;
static	int					g_NewFlagInputFirstChar = 0;

// Which fields are folded away behind their headings. One entry per field, all true to begin with:
// see NewFieldCollapsed for why a box that opens shut is the kinder one.
static	std::vector<bool>	g_NewFlagCollapsed;

// [rc4l] The three boxes share a model and a drawer, and each keeps its own place in it: where it is
// scrolled to, what the pointer is over, and where the keyboard cursor is.
static	int					g_NewVarsScroll = 0;
static	int					g_NewGameScroll = 0;
static	int					g_NewBoxHot = -1;
static	int					g_NewBoxSel = 0;
static	bool				g_DraggingNewBoxBar = false;

// One text box per setting, kept by name -- see SettingInput. The name being edited, or empty.
static	std::vector<std::pair<std::string, zx::TextInput> >	g_NewSettingInput;
static	FString				g_NewSettingEditing;
static	bool				g_NewSettingDragging = false;
static	int					g_NewSettingClickTime = 0;
static	int					g_NewSettingFirstChar = 0;

// The mode a server built here would run. See NewChosenGameMode.
static	GAMEMODE_e			g_NewGameMode = GAMEMODE_COOPERATIVE;

// [rc4l] The CUSTOM tab's own state: the presets read from disk, the search over them, and where
// the cursor is. Three regions, walked the same way the NEW screen's are.
enum class CustomFocus { Search, List, Buttons };

static	std::vector<zx::CustomEntry>	g_CustomAll;
static	bool				g_CustomLoaded = false;

// Bumped whenever what is on disk has changed. Anything remembering a preset by name has to be
// keyed on this as well: a preset replaced under its own name is a different preset with the same
// key. See CustomForget.
static	int					g_CustomGeneration = 0;

// [rc4l] Where a preset's files were found, once a worker has said. Empty path means "looked, and
// no copy here matches", which is a real answer -- absence from the list is the third state, "not
// asked yet", and is what the rows draw "Loading..." for. See CustomVerifyPump.
//
// File scope rather than inside the menu class: a static member would need a definition of its own,
// and all the other state this screen keeps lives out here.
struct ResolvedFile
{
	std::string key;
	std::string path;

	ResolvedFile() {}
	ResolvedFile(const std::string &k, const std::string &p) : key(k), path(p) {}
};

static	std::vector<ResolvedFile>	g_CustomResolved;

// The claim CUSTOM has on the shared resolver, or -1 when it has nothing outstanding.
static	int					g_CustomVerifyToken = -1;

// The same, for the NEW tab's last-played restore, with the entry it is waiting to apply.
static	zx::CustomEntry		g_NewRestoreEntry;
static	int					g_NewRestoreToken = -1;

// [rc4l] And for EDIT, which used to verify every file inline on the press and froze the menu for
// as long as that took. `wanted` is the press before the worker was free; `token` is the claim once
// it is. Both together are "an edit is happening", which is what the button says while it waits.
static	zx::CustomEntry		g_CustomEditEntry;
static	int					g_CustomEditToken = -1;
static	bool				g_CustomEditWanted = false;

static	zx::TextInput		g_CustomSearch;
static	int					g_CustomSearchFirstChar = 0;
static	bool				g_CustomSearchDragging = false;
static	int					g_CustomSearchClickTime = 0;
static	bool				g_CustomSearchHot = false;
static	CustomFocus			g_CustomFocus = CustomFocus::List;
static	int					g_CustomSel = 0;
static	int					g_CustomScroll = 0;
static	int					g_CustomHot = -1;
static	bool				g_CustomRevealSel = true;
static	int					g_CustomBtnHot = -1;
static	int					g_CustomBtnSel = 0;
static	bool				g_CustomEmptyHot = false;
static	bool				g_DraggingCustomBar = false;

// The detail column's own scroll, separate from the list's: they are two views and moving one must
// not move the other.
static	int					g_CustomDetailScroll = 0;
static	bool				g_DraggingCustomDetailBar = false;

// The read-only map list a saved preset opens. See DrawCustomMapsModal for why it cannot be edited.
static	bool				g_CustomMapsOpen = false;
static	bool				g_CustomMapsHot = false;
static	bool				g_CustomMapsDoneHot = false;
static	int					g_CustomMapsScroll = 0;
static	bool				g_DraggingCustomMapsBar = false;

// Which preset a delete question is about. The dialog answers with a yes or a no and nothing else,
// so what it was about has to be remembered on this side of it.
static	FString				g_CustomDeleting;


// [rc4l] The rotation: every map in the chosen files, in the order the files provide them, and
// whatever the player has since done to that list.
//
// `key` is what it was built from -- the IWAD and the load order -- so changing the files rebuilds
// it and nothing else does. Rebuilding on every look would throw away a rotation somebody had just
// finished arranging.
// [rc4l] A map is in the rotation or out of it, and it stays on the list either way.
//
// Taking one out used to remove the row, which made it a decision you could not undo without
// rebuilding the whole list -- and rebuilding means changing the files, so there was no way back at
// all. A switch costs one bool and makes a mis-click a mis-click rather than an accident.
struct NewMapEntry
{
	std::string name;
	bool bIn;

	NewMapEntry() : bIn(true) {}
	NewMapEntry(const std::string &n) : name(n), bIn(true) {}
};

static	std::vector<NewMapEntry>	g_NewMaps;
static	FString				g_NewMapsKey;
static	int					g_NewMapSel = 0;
static	int					g_NewMapScroll = 0;
static	int					g_NewMapHot = -1;
static	int					g_NewMapBtnHot = -1;
static	int					g_NewMapBtnSel = 0;		// which of the row's buttons the keyboard is on
static	bool				g_NewMapRevealSel = true;
static	bool				g_DraggingNewMapBar = false;

// The same cursor for the load order's rows, which have the same three buttons.
static	int					g_NewOrderBtnSel = 0;

// The chooser's own state: where its cursor is and where it is scrolled to, separate from the
// selection itself, so backing out of it changes nothing.
static	int					g_NewIwadModalSel = 0;
static	int					g_NewIwadModalScroll = 0;
static	int					g_NewIwadModalHot = -1;
static	bool				g_NewIwadRefreshHot = false;
static	bool				g_NewIwadConfirmHot = false;

// The RESET beside it, on the boxes that have one. See NewBoxHasReset.
static	bool				g_NewBoxResetHot = false;
static	bool				g_DraggingIwadBar = false;
static	bool				g_DraggingNewWadBar = false;
static	bool				g_DraggingNewOrderBar = false;

// [rc4l] Whether the load order's view should chase its selection this frame.
//
// Set by anything that MOVES the selection -- an arrow, one of the row's own buttons, a file added
// or removed -- and by nothing that scrolls. Chasing every frame fights the wheel; chasing never
// loses the row you are moving the moment it passes the edge of the view, which is precisely when
// you are watching it.
static	bool				g_NewOrderRevealSel = true;

// Whether the wad list's view should chase its selection this frame. Set when a key moves it, never
// when the wheel or the bar does -- otherwise either would snap straight back.
static	bool				g_NewWadRevealSel = true;

// Whether the view should chase the selection this frame. Set when a key moves it, never when the
// wheel or the bar does -- see the draw for why doing it unconditionally breaks both.
static	bool				g_NewIwadRevealSel = true;

// [rc4l] Bumped to make the IWAD probe look again. The list is cached for a couple of seconds
// because probing the disk per frame is one stat per IWAD per frame; a player who has just dropped
// a file in should not have to wait out a cache they cannot see.
static	int					g_NewIwadEpoch = 0;
static	int					g_NewWadSel = 0;
static	int					g_NewWadScroll = 0;
static	int					g_NewOrderSel = 0;
static	int					g_NewOrderScroll = 0;

// Hover, so the mouse lights what it is over exactly as everywhere else on this screen.
static	int					g_NewIwadHot = -1;
static	int					g_NewWadHot = -1;
static	int					g_NewOrderHot = -1;

// Which of a row's three buttons the pointer is on, as row * 3 + (X, up, down). One number rather
// than a pair, because the draw only ever asks "is it this one".
static	int					g_NewOrderBtnHot = -1;
static	bool				g_NewSearchHot = false;
static	bool				g_NewButtonHot = false;

static	zx::TextInput		g_NewSearch;

// The first character the box is showing, which the shared drawer works out and a click has to use
// to land on the right one. The drag and the double-click clock are its own, because two fields
// sharing one of either would fight.
static	int					g_NewSearchFirstChar = 0;
static	bool				g_NewSearchDragging = false;
static	int					g_NewSearchClickTime = 0;

// The chosen files, in the order they will load. The IWAD is deliberately NOT in here.
static	std::vector<zx::LoadOrderEntry>	g_NewOrder;

// [rc4l] The rows the wad list is showing: filtered, deduplicated and sorted, rebuilt only when the
// query or the scan changes. NEVER per frame -- that is the whole reason this is cached rather than
// derived where it is drawn, and at twenty thousand files it is the difference between a menu and a
// slideshow.
static	std::vector<zx::LibraryRow>		g_NewRows;
static	FString				g_NewRowsKey;
static	size_t				g_NewRowsFiles = 0;
static	bool				g_NewRowsValid = false;

// What the last add did, so a refusal can say why rather than looking like a dead button.
static	FString				g_NewNotice;
static	int					g_NewNoticeMs = 0;
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

// [rc4l] Everything registered so far, forgotten. Called by a modal as it starts drawing.
//
// A modal covers the screen behind it and takes its pointer, but the tooltips of what it covers were
// registered before it drew and stayed hoverable through it -- so resting the pointer over a row of
// flags produced the IWAD row's explanation, from a control the player could not even see. Dropping
// the registry at the modal's first line is the same rule the pointer already follows, applied to
// the one thing that was still reaching past it.
static void serverbrowser_ClearTips( )
{
	g_Tips.Clear( );
}

// [rc4l] Where each tab was left. Two entries, indexed by BrowserTab.
static	int				g_TabScroll[kBrowseCount] = { 0, 0 };
static	int				g_TabSelected[kBrowseCount] = { -1, -1 };

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

// [rc4l] What the cache was built FROM, so it can tell it has gone stale.
//
// The slot index alone cannot: the browser reuses slots, and a slot also fills in stages -- the name
// arrives with the first reply and the file list can land later. Both leave the index unchanged while
// the server behind it is a different one, which put a server's name over another server's WADs.
static	FString			g_DetailAddress;
static	LONG			g_DetailPwadCount = -1;

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

// [rc4l] The player list, scrolled and hit-tested exactly as the WAD list above it. Its own scroll
// rather than a shared one: the two lists are different lengths and a player reading down the names
// has not asked to lose their place in the files.
static	int				g_PlayerScroll = 0;
static	bool			g_DraggingPlayerBar = false;
static	int				g_PlayerListTop = 0;
static	int				g_PlayerListBottom = 0;
static	int				g_PlayerListRows = 0;

// [rc4l] The last position the pointer was reported at, in screen pixels. Wheel events do not carry
// one, and which list a notch belongs to is entirely a question of where the pointer is.
static	int				g_MouseX = -1;
static	int				g_MouseY = -1;

//*****************************************************************************
//	FUNCTIONS

//*****************************************************************************
//
// [rc4l] The drawing space, and where the layout sits in it. See the block by SB_LAYOUT_W.
//
// The aspect is matched to the window so the scale is the same on both axes, which is the whole
// point: a browser that stretches beside a menu that does not is the difference the player sees.
// Whichever axis runs out first decides the scale, and the header's band is taken off the height
// before that choice so the browser shrinks to fit under the bar instead of being pushed off the
// bottom by it.
static void serverbrowser_VirtSize( int &vw, int &vh )
{
	const int sw = screen->GetWidth( );
	const int sh = screen->GetHeight( );
	const int barPx = zx::GlobalHeader_ScreenBottom( );

	// Never let the bar claim the whole window, however odd the window gets.
	int avail = sh - barPx;
	if ( avail < sh / 2 )
		avail = sh / 2;

	if (( sw <= 0 ) || ( sh <= 0 ) || ( avail <= 0 ))
	{
		vw = SB_LAYOUT_W;
		vh = SB_LAYOUT_H;
		return;
	}

	if (( sw * SB_LAYOUT_H ) <= ( avail * SB_LAYOUT_W ))
	{
		// Width runs out first: the layout spans the window and the space is taller than 400.
		vw = SB_LAYOUT_W;
		vh = ( sh * SB_LAYOUT_W ) / sw;
	}
	else
	{
		// Height runs out first, which is every wide window: the space is wider than 640.
		vw = ( sw * SB_LAYOUT_H ) / avail;
		vh = ( sh * SB_LAYOUT_H ) / avail;
	}

	if ( vw < SB_LAYOUT_W ) vw = SB_LAYOUT_W;
	if ( vh < SB_LAYOUT_H ) vh = SB_LAYOUT_H;
}

static int serverbrowser_VirtW( void )
{
	int vw = 0, vh = 0;
	serverbrowser_VirtSize( vw, vh );
	return vw;
}

static int serverbrowser_VirtH( void )
{
	int vw = 0, vh = 0;
	serverbrowser_VirtSize( vw, vh );
	return vh;
}

static int serverbrowser_OriginX( void )
{
	return ( serverbrowser_VirtW( ) - SB_LAYOUT_W ) / 2;
}

static int serverbrowser_OriginY( void )
{
	int vw = 0, vh = 0;
	serverbrowser_VirtSize( vw, vh );

	// The bar's height in this space, so the layout is centred in what is LEFT rather than in the
	// whole window. Centring in the whole window would tuck the top of the browser under the bar by
	// half the bar's height, which is the overlap this replaces.
	const int sh = screen->GetHeight( );
	const int barV = ( sh > 0 ) ? ( zx::GlobalHeader_ScreenBottom( ) * vh ) / sh : 0;

	const int room = vh - barV;
	const int slack = ( room > SB_LAYOUT_H ) ? ( room - SB_LAYOUT_H ) / 2 : 0;
	return barV + slack;
}

//*****************************************************************************
//
// [rc4l] Layout coordinates to real screen pixels, for the things that cannot use DTA_Virtual*.
//
// Dim() and the flag's clip rectangle take screen pixels only, so they have to reproduce whatever
// mapping the renderer used for the text. This is the engine's own arithmetic for the branch
// DTA_KeepRatio takes, repeated rather than approximated, so a panel edge and the text sitting on it
// cannot land a pixel apart. Widths come from the mapped far edge minus the mapped near one, the way
// the engine does it, so truncation cannot open a seam between rectangles that share a boundary.
static void serverbrowser_ToScreen( int vx, int vy, int vw, int vh, int &x, int &y, int &w, int &h )
{
	const int sw = screen->GetWidth( );
	const int sh = screen->GetHeight( );

	int vW = 0, vH = 0;
	serverbrowser_VirtSize( vW, vH );

	x = vx * sw / vW;
	y = vy * sh / vH;
	w = ( vx + vw ) * sw / vW - x;
	h = ( vy + vh ) * sh / vH - y;
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
	// [rc4l] Checked against WHAT THE SERVER IS, not which slot it landed in. The address says it is
	// still the same server; the file count says the reply that carries the files has arrived since we
	// last looked. Both are cheap -- what the cache exists to avoid is the per-WAD work below, not
	// these two comparisons.
	const FString address = BROWSER_GetAddress( lServer ).ToString( );
	const LONG lPwadCount = BROWSER_GetNumPWADs( lServer );

	if (( lServer == g_DetailServer ) && ( address.Compare( g_DetailAddress ) == 0 )
		&& ( lPwadCount == g_DetailPwadCount ))
	{
		return;
	}

	g_DetailServer = lServer;
	g_DetailAddress = address;
	g_DetailPwadCount = lPwadCount;
	g_DetailWads.Clear( );
	g_DetailWadSizes.Clear( );
	g_DetailWadHashes.Clear( );

	// A different server means a different list, so the old scroll position describes nothing.
	g_WadScroll = 0;
	g_DraggingWadBar = false;
	g_PlayerScroll = 0;
	g_DraggingPlayerBar = false;

	const char *pszIwad = BROWSER_GetIWADName( lServer );
	if (( pszIwad != NULL ) && ( pszIwad[0] != 0 ))
	{
		g_DetailWads.Push( pszIwad );
		g_DetailWadSizes.Push( BROWSER_GetIWADSize( lServer ));
		g_DetailWadHashes.Push( BROWSER_GetIWADHash( lServer ));
	}

	for ( LONG i = 0; i < lPwadCount; i++ )
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
// [rc4l] LAN first, then busiest, then alphabetically -- see computation/serversort_compute.h for
// why LAN is a group rather than a bonus, and for the two things the name comparison has to strip
// before it means anything.
//
// Humans only, matching the count the row draws: sorting a server to the top for holding seven bots
// would be ranking it by a number the player can already see is not people.
static int STACK_ARGS serverbrowser_CompareServers( const void *pA, const void *pB )
{
	const int lA = *reinterpret_cast<const int *>( pA );
	const int lB = *reinterpret_cast<const int *>( pB );

	const char *pszNameA = BROWSER_GetHostName( lA );
	const char *pszNameB = BROWSER_GetHostName( lB );

	const int lResult = zx::CompareServersWithVersion(
		BROWSER_IsLAN( lA ), static_cast<int>( BROWSER_GetNumHumanPlayers( lA )),
		( pszNameA != NULL ) ? pszNameA : "", BROWSER_GetVersionRelation( lA ),
		BROWSER_IsLAN( lB ), static_cast<int>( BROWSER_GetNumHumanPlayers( lB )),
		( pszNameB != NULL ) ? pszNameB : "", BROWSER_GetVersionRelation( lB ));

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
		if ( BROWSER_IsListable( ulIdx ))
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
	const bool bWantPrivate = ( g_Browse == BrowseKind::Private );

	// [rc4l] Folded once here rather than once per server: the query does not change while we walk
	// the list, and ServerMatchesSearch is called MAX_BROWSER_SERVERS times.
	const std::string searchKey = zx::SearchKey( g_Search.text );

	for ( ULONG ulIdx = 0; ulIdx < MAX_BROWSER_SERVERS; ulIdx++ )
	{
		if ( BROWSER_IsListable( ulIdx ) == false )
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
			"?", DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
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
			pszCode, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
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
// [rc4l] A server-supplied name as plain text: no colour, whichever way the colour was written.
//
// BOTH STEPS ARE NEEDED and they are not the same step. V_ColorizeString turns the wire form -- the
// literal "\cd" an operator typed -- into the escape byte the renderer understands; without it those
// two characters are simply drawn. StripColorCodes then removes the escapes themselves. Doing only
// the first leaves the browser rendering names in colours it may not have (see
// computation/colortext_compute.h for why that is worse than it sounds); doing only the second
// leaves "\cd" sitting in the middle of the name.
// [rc4l] The escape scan is worth its keep. This runs per drawn row per frame, and the crossing into
// the compute unit costs two more allocations -- an std::string in and an FString back out -- on top
// of the one V_ColorizeString may already have caused. Most names have no colour code at all, so
// checking for one byte buys the common case its way out of both.
static FString serverbrowser_PlainName( const char *pszName )
{
	FString name = pszName;
	V_ColorizeString( name );

	if ( name.IndexOf( zx::kColorEscape ) < 0 )
		return name;

	return FString( zx::StripColorCodes( std::string( name.GetChars( ))).c_str( ));
}

//*****************************************************************************
//
// [rc4l] A server-supplied name, plain, cut to fit `maxWidth`.
//
// The cutting used to need care -- shortening a byte at a time could land between an escape and the
// character it takes, leaving a dangling escape to eat the next glyph. Stripping first removes that
// hazard at the source rather than navigating around it: there are no escapes left to cut through,
// so any offset is a safe offset.
//
// [rc4l] Which also makes the search BINARY, and that is the part that mattered. The old walk tried
// every length from longest down, copying the string and measuring it each time -- O(n) allocations
// and O(n^2) character work for one name, repeated per row per frame. Width only ever grows with
// length, so the longest prefix that fits can be found in about six probes instead of sixty, and the
// one string being shortened is reused rather than recopied.
//
// [rc4l] The font is a parameter because the panel's title is drawn in BigFont, and measuring a
// BigFont line against SmallFont's widths says it fits when it does not -- which is how the title
// came to run out of its column while every SmallFont line beneath it stayed inside one.
static FString serverbrowser_FitName( const char *pszName, int maxWidth, FFont *font = SmallFont )
{
	FString name = serverbrowser_PlainName( pszName );

	// The common case, and the cheap one: it already fits, so nothing is cut, copied or searched.
	if ( font->StringWidth( name ) <= maxWidth )
		return name;

	// Room for the ellipsis BEFORE cutting, so the result including "..." fits.
	const int budget = maxWidth - font->StringWidth( "..." );
	if ( budget <= 0 )
		return FString( "..." );

	long lo = 0;
	long hi = static_cast<long>( name.Len( ));
	long best = 0;

	FString probe;
	while ( lo <= hi )
	{
		const long mid = lo + ( hi - lo ) / 2;

		probe = name;
		probe.Truncate( mid );

		if ( font->StringWidth( probe ) <= budget )
		{
			best = mid;
			lo = mid + 1;
		}
		else
		{
			hi = mid - 1;
		}
	}

	if ( best <= 0 )
		return FString( "..." );

	FString out = name;
	out.Truncate( best );
	out += "...";
	return out;
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

		// Per-list memory is per-VISIT. The rows survive now, but their INDICES do not: the refresh
		// below re-sorts as fresh replies land, so a saved position points at whichever server has
		// since moved into that slot rather than at the one it was saved against.
		//
		// kBrowseCount, because these belong to the LIST and there is one per sub-tab. It read
		// kTabCount while the two happened to be equal, which is the kind of agreement that holds
		// until somebody adds a tab and then silently walks off the end of the array.
		for ( int i = 0; i < kBrowseCount; ++i )
		{
			g_TabScroll[i] = 0;
			g_TabSelected[i] = -1;
		}

		// [rc4l] Where the browser lands, decided from what is known right now and then left alone.
		//
		// Not re-asked while the screen is up, on purpose. The refresh below can turn an empty list
		// into a full one seconds after the player arrives, and a tab that swaps itself out at that
		// moment moves whatever they were already reaching for.
		g_Tab = ( zx::ComputeOpeningTab( static_cast<int>( g_SortedServers.Size( )), g_ListHasAnswered )
			== zx::OpeningTab::Host ) ? BrowserTab::Host : BrowserTab::Browse;

		// [rc4l] The form has to be filled HERE as well as in SelectTab. Landing straight on HOST
		// because the list came up empty means SelectTab never runs, and the fields would be blank
		// on the one tab the player was sent to precisely because it is all they can do.
		if ( g_Tab == BrowserTab::Host )
		{
			LoadHostForm( );
			g_HostFocus = zx::HostFocusPos( zx::HostSlot::List, 0 );
		}

		// [rc4l] THE LIST IS NOT CLEARED. It used to be, and that made every visit start on a spinner
		// -- worst of all the visit that matters most, coming back to join a server whose files you
		// just spent time downloading. The rows were still on screen a moment ago and are still very
		// probably true; they are not KNOWN to be current, which is an argument for checking them,
		// not for hiding them while we do.
		//
		// So the servers stay listed and clickable, and are verified underneath: each is re-queried
		// at its own address and drops out on its own timeout if it has gone. Nothing waits on the
		// registry, which is only asked so that servers we do not know about yet can appear.
		//
		// What this cannot promise is that a row is live at the instant it is clicked -- but nothing
		// ever could, because the list is a snapshot of other people's machines. Joining already
		// re-contacts the server and already fails when it cannot, so a stale row leads to the same
		// place it always did rather than to a new kind of surprise.
		// [rc4l] ONLY THE FIRST TIME THIS SESSION. Opening the browser used to re-check every listed
		// server, which meant the list was being challenged constantly for no reason the player
		// asked for, and every sweep was another chance for a live server to miss a datagram and be
		// treated as gone.
		//
		// The list is not more true for having been asked again ten seconds later. It is a snapshot
		// of other people's machines either way, and joining re-contacts the server regardless. So
		// the sweep happens once, when there is nothing to show yet, and after that it is the
		// REFRESH button's job: the player says when, because only the player knows they have been
		// away from the screen long enough to care.
		if ( g_ListHasAnswered == false )
		{
			BROWSER_RefreshListedServers( );
			BROWSER_QueryServerRegistry( );
		}
	}

	// [rc4l] The browser can be torn down by machinery that never saw the question -- a console
	// command, a restart. Leaving the hold in place would strand a finished download forever, so it
	// is released here as "keep going", which is what a player who never answered "stop" meant.
	void Destroy( )
	{
		if ( g_Dialog.open )
		{
			g_Dialog = BrowserDialog( );
			zx::ReleaseJoinResume( true );
		}

		// [rc4l] The catalogue pictures go with the menu that was showing them. They are held only to
		// avoid decoding one per frame, so nothing wants them once there is no panel to draw.
		//
		// It also settles the reload question by construction: fua_catalogue rereads from disk, and a
		// texture holding a path to a file that has just been replaced would go on drawing the old one
		// for as long as the menu stayed open. Reopening the browser is what a reload means anyway.
		serverbrowser_FreeArt( );

		Super::Destroy( );
	}

	//*************************************************************************
	//
	void Ticker( )
	{
		Super::Ticker( );

		// [rc4l] A HELD ARROW MUST STOP WHEN THE KEYBOARD CHANGES HANDS.
		//
		// M_Responder latches a menu button on the way down and unlatches it on the way up, and
		// M_Ticker repeats whatever is still latched. The unlatch only happens while the key is
		// still being TRANSLATED into an MKEY -- so the moment a text field takes focus mid-press,
		// the release arrives raw, nothing unlatches, and the arrow repeats forever. Tapping Up at
		// the top of a list walked the focus into the search box and then kept going on its own.
		//
		// SetFocus already released for this reason, but it can only see the focus IT owns. This
		// screen has a second focus of its own inside BrowserFocus::Host, so moving from the wad
		// list into the search box changed no focus SetFocus could see while flipping the
		// translation all the same.
		//
		// So the test is the translation itself rather than any one focus variable: whatever moved,
		// whatever owns the keyboard now, a change of hands releases. Every field added after this
		// is covered without having to remember this rule.
		{
			static bool bWasTranslating = true;
			const bool bNow = TranslateKeyboardEvents( );

			if ( bNow != bWasTranslating )
			{
				M_ReleaseMenuButtons( );
				bWasTranslating = bNow;
			}
		}

		// [rc4l] The retry/give-up clock and the per-server timeouts moved to BROWSER_BackgroundTick,
		// which runs whether or not this menu is open. Ticking them here as well would halve every
		// timeout while the browser was on screen.

		// [rc4l] A finished download for an entry we were about to host, acted on here rather than in
		// the callback: that arrives from inside waddownload::Tick, and starting a server from there
		// would re-enter it.
		ResumeHostAfterDownload( );
		ResumeCustomAfterDownload( );

		// [rc4l] Only while the HOST tab is up, and only while nothing is being hosted.
		//
		// The check opens a socket on the port the player is about to host on. Doing that while they
		// browse someone else's server would be taking a port nobody asked us to take -- and doing it
		// while a server of ours is running takes the port from that server, or fails and records
		// "unreachable" about a port our own process is holding.
		if (( g_Tab == BrowserTab::Host ) && ( zx::HostCurrentState( ) == zx::HostState::Idle ))
		{
			zx::ReachProbeRequest( HostConfiguredPort( ));
			zx::ReachProbeTick( );
		}

		serverbrowser_RebuildList( );

		// [rc4l] Our own server has finished coming up, so go and play on it. The player pressed
		// START SERVER, and starting a server you are then left staring at is only half of what that
		// button says -- the tooltip promises "start the server and join it".
		//
		// Taken as an EDGE rather than a state so this fires exactly once. Asking "is it ready" every
		// tic would keep trying to connect to a server we are already on.
		if ( zx::HostTakeReadyEdge( ))
			JoinOwnServer( );
	}

	//*************************************************************************
	//
	// [rc4l] Connect to the server we just started.
	//
	// Not through the browser's own join path: that one resolves WADs and may start downloads, and
	// neither applies to a server whose files are already on this machine by definition. What DOES
	// apply is the reload, because a catalogue entry means the server is running the entry's files
	// and this client is still running whatever it had. computation/ownjoin_compute decides which.
	// [rc4l] Whether the running server was started from a catalogue entry rather than from the form.
	// A custom setup runs the client's own files, so there is nothing for it to reload onto.
	bool HostingCatalogueEntry( )
	{
		const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( );
		return ( g_HostingEntry >= 0 ) && ( g_HostingEntry < static_cast<int>( entries.size( )));
	}

	// [rc4l] What the entry we are HOSTING loads, resolved to paths, rebuilt from the entry rather
	// than remembered. Answers only for a catalogue entry; ask HostingCatalogueEntry first.
	//
	// The pair of statics above is filled in when the server is started and cleared the first time it
	// is used, which makes "did we remember to reload" a question with a wrong answer available. If
	// the ready edge ever fires with them empty -- a second edge, a reload that returned instead of
	// restarting, a switch -- the join used to go ahead with whatever the client happened to have
	// loaded, and land on protected lump authentication failed. The client cannot tell that from a
	// genuinely mismatched server, so it reads as the experience being broken.
	//
	// Derived from g_HostingEntry, which IS what the running server was started from, so it cannot go
	// stale in that way. Returning false here means the files cannot be found NOW, which is a refusal
	// and not a reason to connect anyway -- see computation/ownjoin_compute.
	bool HostedEntryFiles( FString &outIwad, TArray<FString> &outPwads )
	{
		const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( );

		if ( !HostingCatalogueEntry( ))
			return false;

		const zx::AddonEntry &addon = entries[g_HostingEntry].addon;

		// The one place everything asks what an entry loads, so this cannot answer differently from
		// what the server was handed.
		const std::vector<zx::AddonFileRef> loads = HostSelectedFiles( addon );

		const zx::IwadPick pick = zx::PickIwad( addon.iwad, zx::AvailableIwads( addon.iwad ));
		if ( pick.choice == zx::IwadChoice::None )
			return false;

		outIwad = zx::FindIwadInEngineSearchPaths( pick.iwad.c_str( ));
		if ( outIwad.IsEmpty( ))
			outIwad = zx::FindFileInEngineSearchPaths( pick.iwad.c_str( ));
		if ( outIwad.IsEmpty( ))
			return false;

		const std::vector<FString> verified = HostEntryVerifiedPaths( loads );

		outPwads.Clear( );
		for ( size_t i = 0; i < loads.size( ); ++i )
		{
			FString path = ( i < verified.size( )) ? verified[i] : FString( );
			if ( path.IsEmpty( ))
				path = zx::FindFileInEngineSearchPaths( loads[i].name.c_str( ));
			if ( path.IsEmpty( ))
				return false;		// cannot reload onto a file we cannot find; say so rather than guess

			outPwads.Push( path );
		}

		return true;
	}

	void JoinOwnServer( )
	{
		const FString address = zx::HostConnectAddress( );
		if ( address.IsEmpty( ))
			return;

		// [rc4l] Rebuild what the start-time statics were meant to carry, whenever they are empty, and
		// find out whether we are allowed to connect at all. The rule is computation/ownjoin_compute:
		// hosting an entry means the server is NOT running what we are, so a connect that skips the
		// reload cannot authenticate, and guessing is worse than saying so.
		FString rebuiltIwad;
		TArray<FString> rebuiltPwads;

		zx::OwnJoinIn in;
		in.hostingCatalogueEntry = HostingCatalogueEntry( );
		in.haveRememberedFiles = g_HostEntryIwad.IsNotEmpty( );
		in.canRebuildFiles = in.hostingCatalogueEntry && !in.haveRememberedFiles
			&& HostedEntryFiles( rebuiltIwad, rebuiltPwads );

		const zx::OwnJoinOut decision = zx::DecideOwnJoin( in );

		if ( decision.action == zx::OwnJoinAction::Refuse )
		{
			// The server is up and only the join failed, so it stays up: the player can fix the file
			// and press JOIN rather than lose the server to a problem on their own end.
			ShowNotice( "Cannot join your own server", decision.refusal.c_str( ));
			return;
		}

		// [rc4l] RequestReload is what the browser's own join uses, and it is the reason this works
		// where an earlier attempt did not. Queueing `wad_reload` and a `connect` behind it loses the
		// connect, because the reload restarts the game loop and takes the queued command with it.
		// This instead rewrites argv and throws CRestartException, so the connect RIDES the restart
		// rather than waiting for it. It also validates every file before tearing anything down, so a
		// bad PWAD leaves the running game alone.
		if ( decision.action == zx::OwnJoinAction::ReloadThenConnect )
		{
			const FString iwad = decision.useRebuilt ? rebuiltIwad : g_HostEntryIwad;
			TArray<FString> pwads = decision.useRebuilt ? rebuiltPwads : g_HostEntryPwads;

			g_HostEntryIwad = "";
			g_HostEntryPwads.Clear( );

			// [rc4l] The menu is NOT cleared first any more. RequestReload returns instead of throwing
			// when the set will not load -- a file that is present but truncated, or one that went
			// missing between starting the server and joining it -- and clearing the menu ahead of the
			// call meant that refusal landed on an empty screen while the panel behind it went on
			// saying we were hosting. We were: the server was up and we simply could not join it.
			const zx::wadreload::ReloadResult r = zx::wadreload::RequestReload(
				iwad.GetChars( ), pwads, NULL, address.GetChars( ));

			if ( r == zx::wadreload::ReloadResult::InvalidWads )
			{
				// Take the server down with it. Leaving it running would advertise a game to other
				// people that its own host cannot get into.
				zx::HostStop( );
				ShowNotice( "Cannot join your own server",
					"Some of its files will not load on this machine, so the server has been stopped. "
					"The console says which one." );
				return;
			}

			M_ClearMenus( );
			return;
		}

		FString command;
		command.Format( "connect %s", address.GetChars( ));
		C_DoCommand( command.GetChars( ));

		// [rc4l] And get out of the way. Joining a server the player asked for means they are done
		// with the browser -- leaving it open over the game they just joined would make them dismiss
		// a menu to reach the thing the menu just gave them.
		M_ClearMenus( );
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

		// [rc4l] Anything but Loading means the looking is over, which is the one fact the NEXT visit
		// needs to tell an empty world from an unfinished search. Recorded here because this is
		// where the phase is already known, and never unset: the question is whether we have ever
		// had an answer, and a later refresh going quiet does not un-answer the last one.
		if ( phase != zx::BrowserPhase::Loading )
			g_ListHasAnswered = true;

		if (( phase == zx::BrowserPhase::Ready ) && ( g_SortedServers.Size( ) == 0 ))
			return zx::BrowserPhase::Empty;

		return phase;
	}

	unsigned VisibleParts( const zx::BrowserCounts &counts )
	{
		// The hosting tab is a different screen, not a filter, so it does not ask the phase at all --
		// "still looking for servers" has no meaning on the page where you are making one.
		if ( g_Tab == BrowserTab::Host )
			// Ours does not count: the host panel's own button is the CANCEL for it.
			return zx::ComputeHostParts( serverbrowser_DownloadRunning( )
				&& !HostDownloadRunning( ));

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
		if ( parts & zx::kPartHost )
			DrawHostPanel( );
		if ( parts & zx::kPartList )
			DrawRows( );
		if ( parts & zx::kPartPlaceholder )
			DrawPlaceholder( phase );
		if ( parts & zx::kPartDetail )
			DrawDetails( );
		if ( parts & zx::kPartFooter )
			DrawFooter( phase, counts );

		// Last, and over everything: something the player has to deal with before anything else.
		if ( g_Notice.IsNotEmpty( ))
			DrawNotice( );
		else
		{
			// A question is drawn BEFORE the glow so its own anchor is the live one -- the glow then
			// travels from whatever the player was on to the button they are being asked about,
			// rather than being stranded behind the panel on a control they cannot reach.
			if ( g_Dialog.open )
				DrawDialog( );

			DrawFocusTravel( );

			// A tooltip about a control behind the modal is about something not being asked.
			if ( !g_Dialog.open )
				DrawTooltip( );
		}
	}

	// [rc4l] Move the glow to wherever this frame's focused control said it wanted it, and draw it.
	void DrawFocusTravel( )
	{
		if ( !g_FocusGlowValid )
		{
			// Nothing has focus, so there is nothing to travel from next time either.
			g_GlowPlaced = false;
			return;
		}

		const zx::GlowPos want( g_FocusGlowX, g_FocusGlowY );

		// The FIRST placement snaps. There is nowhere for it to have travelled from, and sliding in
		// from a stale position left over from the last visit would be a lie about where the focus
		// had been.
		if ( !g_GlowPlaced )
		{
			g_GlowAt = want;
			g_GlowTravel = zx::BeginGlowTravel( want, want );
			g_GlowLastMs = static_cast<int>( I_MSTime( ));
			g_GlowPlaced = true;
		}
		else
		{
			// [rc4l] Advanced HERE, once per frame, by however many milliseconds actually passed --
			// not once per 35Hz tic.
			//
			// The tic was smooth in the sense of being frame-rate independent and steppy in the sense
			// that mattered: 35 positions a second is a slideshow beside a 144Hz panel. Real elapsed
			// time is both -- as many positions as there are frames, and the same wall-clock duration
			// on every machine.
			const int now = static_cast<int>( I_MSTime( ));
			const int delta = now - g_GlowLastMs;
			g_GlowLastMs = now;

			// The focus can move again mid-flight. Setting out afresh FROM WHERE THE GLOW IS --
			// rather than from where the last journey began -- is what stops it snapping backwards
			// when the player changes their mind halfway through.
			if (( g_GlowTravel.to.x != want.x ) || ( g_GlowTravel.to.y != want.y ))
				g_GlowTravel = zx::BeginGlowTravel( g_GlowAt, want );

			g_GlowTravel = zx::StepGlowTravel( g_GlowTravel, delta );
			g_GlowAt = zx::GlowTravelPoint( g_GlowTravel );
		}

		DrawFocusGlow( g_GlowAt.x, g_GlowAt.y );
	}

	// [rc4l] Whether ANY control on this screen currently has the pointer held.
	//
	// One list rather than a test per caller: this is asked by the tooltip, and every list added
	// since was another bar this forgot to name -- so a tooltip would appear over the very thing
	// being dragged, which is the gesture it claims to be explaining.
	bool BrowserDragging( )
	{
		return g_SearchDragging || g_DraggingScrollbar || g_DraggingWadBar || g_DraggingPlayerBar ||
			g_HostSliderDragging.IsNotEmpty( ) || g_HostFieldDragging ||
			g_DraggingHostDetailBar || g_DraggingHostStatusBar || g_DraggingHostListBar ||
			g_DraggingIwadBar || g_DraggingNewWadBar || g_DraggingNewOrderBar ||
			g_DraggingNewBoxBar || g_NewSearchDragging || g_NewFlagInputDragging ||
			g_NewSettingDragging || g_ButtonPressed;
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

		// [rc4l] Not while the player is DRAGGING something.
		//
		// A tooltip answers "what is this?", which is a question you ask by resting the pointer
		// somewhere. Mid-drag you are not asking it -- you are selecting text, or pulling a scrollbar
		// -- and the box is big enough to land on top of the very thing being manipulated. It showed
		// up over the list while a selection was being dragged out in the search box, which is the
		// tooltip getting in the way of the gesture it is meant to be explaining.
		if ( BrowserDragging( ))
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

		// [rc4l] WRAPPED, at a fixed maximum. It used to break only on newlines the caller had put
		// there, which is exactly what a Win32 tooltip does before anybody sends it
		// TTM_SETMAXTIPWIDTH: one line, no cap, as wide as the string happens to be. The file list
		// tooltip is a whole comma-separated set of names, and it drew a box across the screen.
		//
		// There is no clever sizing rule to copy here -- Windows has a default (unbounded) and every
		// well-behaved app picks a width. A third of the view is ours: wide enough that ordinary one
		// line tips stay on one line, narrow enough that the box never spans the panel it explains.
		FBrokenLines *broken = V_BreakLines( SmallFont, SB_VIRT_W / 3, found->text.GetChars( ));
		if ( broken == NULL )
			return;

		const int lineH = SmallFont->GetHeight( ) + 1;
		const int padX = 4;
		const int padY = 3;

		// V_BreakLines measured each line as it broke it, so the width is the widest it produced --
		// which for a short tip is the text itself, not the cap. The box still shrinks to its
		// content; the cap only stops it growing.
		int contentW = 0;
		int count = 0;
		for ( ; broken[count].Width >= 0; ++count )
			contentW = MAX( contentW, broken[count].Width );

		if ( count == 0 )
		{
			V_FreeBrokenLines( broken );
			return;
		}

		const int boxW = contentW + 2 * padX;
		const int boxH = count * lineH + 2 * padY;

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
		for ( int i = 0; i < count; ++i )
		{
			// First line white, the rest dimmer: the thing hovered, then what is known about it.
			screen->DrawText( SmallFont, ( i == 0 ) ? CR_WHITE : CR_GRAY,
				box.x + padX, y, broken[i].Text,
				DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
			y += lineH;
		}

		V_FreeBrokenLines( broken );
	}

	// [rc4l] How tall the dialog needs to be for what it is carrying, worked out before anything is
	// drawn so the panel is centred on its real height rather than a guess.
	int DialogHeight( )
	{
		int h = SB_DLG_PAD;
		h += SB_DLG_LINE + 4;
		h += DialogWrapCount( g_Dialog.message ) * SB_DLG_LINE;

		if ( g_Dialog.hasInput )
			h += 6 + SB_DLG_LINE + SB_DLG_FIELD_H;

		h += 10 + SB_DLG_BTN_H + SB_DLG_PAD;
		return h;
	}

	// [rc4l] One wrapping pass, shared by the measure and the draw so they cannot disagree about how
	// many lines there are -- which would centre the panel on the wrong height.
	//
	// Taken out of DialogWrap when the remix picker wanted the same thing left-aligned in a column.
	// Two loops would have been two chances to measure one way and draw another.
	int WrapText( const FString &text, int x, int y, int width, int colour, bool bCentre, bool draw )
	{
		int lines = 0;
		FString line;
		long start = 0;

		while ( start <= static_cast<long>( text.Len( )))
		{
			long space = text.IndexOf( " ", start );
			const bool last = ( space < 0 );
			if ( last )
				space = static_cast<long>( text.Len( ));

			const FString word = text.Mid( start, space - start );
			const FString candidate = line.IsEmpty( ) ? word : ( line + " " + word );

			if ( line.IsNotEmpty( ) && ( SmallFont->StringWidth( candidate ) > width ))
			{
				if ( draw )
					screen->DrawText( SmallFont, colour,
						bCentre ? ( x + ( width - SmallFont->StringWidth( line )) / 2 ) : x,
						y + lines * SB_DLG_LINE, line,
						DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
				++lines;
				line = word;
			}
			else
				line = candidate;

			if ( last )
				break;
			start = space + 1;
		}

		if ( line.IsNotEmpty( ))
		{
			if ( draw )
				screen->DrawText( SmallFont, colour,
					bCentre ? ( x + ( width - SmallFont->StringWidth( line )) / 2 ) : x,
					y + lines * SB_DLG_LINE, line,
					DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
			++lines;
		}

		return lines;
	}

	int DialogWrap( const FString &text, int y, bool draw )
	{
		return WrapText( text, SB_DLG_LEFT + SB_DLG_PAD, y, SB_DLG_W - 2 * SB_DLG_PAD,
			CR_GRAY, true, draw );
	}

	int DialogWrapCount( const FString &text )
	{
		return text.IsEmpty( ) ? 0 : DialogWrap( text, 0, false );
	}

	// Where a button sits. One source for the drawing and the hit test, so a button can never be
	// somewhere other than where it is clickable.
	bool DialogButtonRect( int index, int &outX, int &outY, int &outW )
	{
		if (( index < 0 ) || ( index >= g_Dialog.count ))
			return false;

		int widths[3] = { 0, 0, 0 };
		int total = 0;
		for ( int i = 0; i < g_Dialog.count; ++i )
		{
			widths[i] = MAX( 64, SmallFont->StringWidth( g_Dialog.labels[i] ) + 26 );
			total += widths[i];
		}
		total += SB_DLG_BTN_GAP * ( g_Dialog.count - 1 );

		int x = ( SB_VIRT_W / 2 ) - ( total / 2 );
		for ( int i = 0; i < index; ++i )
			x += widths[i] + SB_DLG_BTN_GAP;

		const int h = DialogHeight( );
		outX = x;
		outY = (( SB_VIRT_H - h ) / 2 ) + h - SB_DLG_PAD - SB_DLG_BTN_H;
		outW = widths[index];
		return true;
	}

	//*************************************************************************
	//
	// [rc4l] The remix picker used to be a modal here, opened from a button above PLAY NOW. Both are
	// gone: the choice is a SETTING, so it is drawn in the detail panel beside the thing it changes
	// (DrawHostGameplay) rather than behind a click that hid the browser to ask one question.
	std::vector<zx::AddonRemix> RemixChoices( )
	{
		const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( );

		if (( g_HostEntrySel < 0 ) || ( g_HostEntrySel >= static_cast<int>( entries.size( ))))
			return std::vector<zx::AddonRemix>( );

		return HostOfferedRemixes( entries[g_HostEntrySel].addon );
	}

	//*************************************************************************
	//
	// [rc4l] The modal, drawn like the rest of the browser rather than like a debug print.
	void DrawDialog( )
	{
		// A heavy dim rather than a blur. The engine has no blur primitive, and faking one means
		// re-drawing the frame at a lower resolution and scaling it back up -- real cost for a box
		// that is on screen for two seconds. Darkening until the browser reads as "behind glass" says
		// the same thing (that is not what you are talking to) for one Dim.
		screen->Dim( PalEntry( 0, 0, 0 ), 0.72f, 0, 0, SCREENWIDTH, SCREENHEIGHT );

		const int h = DialogHeight( );
		const int top = ( SB_VIRT_H - h ) / 2;

		const zx::PanelColor topCol = { 30, 32, 46, 246 };
		const zx::PanelColor botCol = { 12, 13, 20, 252 };
		DrawRoundedPanel( SB_DLG_LEFT, top, SB_DLG_W, h, topCol, botCol, 10 );

		int y = top + SB_DLG_PAD;

		screen->DrawText( SmallFont, CR_WHITE,
			( SB_VIRT_W / 2 ) - ( SmallFont->StringWidth( g_Dialog.title ) / 2 ), y, g_Dialog.title,
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
		y += SB_DLG_LINE + 4;

		if ( g_Dialog.message.IsNotEmpty( ))
			y += DialogWrap( g_Dialog.message, y, true ) * SB_DLG_LINE;

		if ( g_Dialog.hasInput )
		{
			y += 6;
			screen->DrawText( SmallFont, CR_DARKGRAY, SB_DLG_LEFT + SB_DLG_PAD, y, g_Dialog.inputLabel,
				DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
			y += SB_DLG_LINE;

			DrawDialogField( y );
		}

		for ( int i = 0; i < g_Dialog.count; ++i )
		{
			int bx, by, bw;
			if ( !DialogButtonRect( i, bx, by, bw ))
				continue;

			const bool bFocused = ( g_Dialog.focus == i );

			// [rc4l] THE WARNING BELONGS ON THE ANSWER, not on the button that asks the question.
			//
			// RESET on the flag box was tinted red itself, which made a button that merely opens a
			// question look like the dangerous act. The dangerous act is confirming it, and index 0
			// is always the affirmative here -- so the red sits on the one press that cannot be
			// taken back, beside a neutral way out.
			const bool bWarn = ( i == 0 ) && DialogIsDestructive( g_Dialog.action );

			DrawRoundedButton( bx, by, bw, SB_DLG_BTN_H, g_Dialog.labels[i],
				bFocused || ( g_DialogHot == i ),
				bWarn ? ButtonTint::Warn : ButtonTint::Neutral );

			if ( bFocused )
				FocusAnchor( zx::BrowserFocus::Dialog, bx - 5, by + SB_DLG_BTN_H / 2 );
		}
	}

	// The field, drawn as the search box is -- sunken, with a caret -- so a field looks like a field
	// wherever it turns up.
	void DrawDialogField( int y )
	{
		const int fx = SB_DLG_LEFT + SB_DLG_PAD;
		const int fw = SB_DLG_W - 2 * SB_DLG_PAD;

		const zx::PanelColor topCol = { 14, 14, 22, 235 };
		const zx::PanelColor botCol = { 34, 34, 48, 220 };
		DrawRoundedPanel( fx, y, fw, SB_DLG_FIELD_H, topCol, botCol, 5 );

		// MASKED when the caller asked for it. A password typed with other people in the room is the
		// one string in this browser that must not be legible over a shoulder.
		FString shown;
		if ( g_Dialog.masked )
		{
			for ( size_t i = 0; i < g_DialogInput.text.size( ); ++i )
				shown += "*";
		}
		else
			shown = g_DialogInput.text.c_str( );

		const int textY = y + ( SB_DLG_FIELD_H - SmallFont->GetHeight( )) / 2 + 1;
		screen->DrawText( SmallFont, CR_WHITE, fx + 5, textY, shown,
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );

		if (( DMenu::MenuTime / 16 ) % 2 == 0 )
		{
			const int caretX = fx + 5 + SmallFont->StringWidth( shown );
			const int cx = serverbrowser_ToScreenX( caretX );
			const int cw = MAX( 1, serverbrowser_ToScreenX( caretX + 1 ) - cx );
			const int cy = serverbrowser_ToScreenY( textY );
			const int ch = serverbrowser_ToScreenY( textY + SmallFont->GetHeight( )) - cy;
			DimClipped( PalEntry( 235, 235, 245 ), 0.85f, cx, cy, cw, ch );
		}
	}

	//*************************************************************************
	//
	// [rc4l] Put a question up. One entry point for both shapes, so a caller cannot invent a third.
	void ShowDialog( DialogAction action, const char *title, const char *message,
		const char *yes, char yesKey, const char *no, char noKey, bool wantsInput = false,
		const char *inputLabel = NULL, bool masked = false )
	{
		g_Dialog = BrowserDialog( );
		g_Dialog.open = true;
		g_Dialog.action = action;
		g_Dialog.title = title;
		g_Dialog.message = ( message != NULL ) ? message : "";

		g_Dialog.labels[0] = yes;
		g_Dialog.shortcuts[0] = yesKey;
		g_Dialog.labels[1] = no;
		g_Dialog.shortcuts[1] = noKey;
		g_Dialog.count = 2;

		// Focus starts on the SAFE choice and Escape resolves to it. A question you opened by accident
		// should take two deliberate acts to answer destructively, not one stray Enter.
		g_Dialog.cancelIndex = 1;
		g_Dialog.focus = 1;

		g_Dialog.hasInput = wantsInput;
		g_Dialog.inputLabel = ( inputLabel != NULL ) ? inputLabel : "";
		g_Dialog.masked = masked;

		g_DialogInput = zx::ClearInput( );
		g_DialogHot = -1;
		SetFocus( zx::BrowserFocus::Dialog );

		S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
	}

	// [rc4l] The same dialog with nothing to decide: something went wrong and the player has to be
	// told, not asked. One button, and Escape resolves to it.
	//
	// Worth having rather than printing to the console. A refusal only the console mentions is a
	// button that made a noise and did nothing as far as anyone looking at the screen can tell, which
	// is how hosting an entry with missing files felt.
	void ShowNotice( const char *title, const char *message )
	{
		g_Dialog = BrowserDialog( );
		g_Dialog.open = true;
		g_Dialog.action = DialogAction::None;
		g_Dialog.title = title;
		g_Dialog.message = ( message != NULL ) ? message : "";

		g_Dialog.labels[0] = "OK";
		g_Dialog.shortcuts[0] = 'o';
		g_Dialog.count = 1;
		g_Dialog.cancelIndex = 0;
		g_Dialog.focus = 0;

		g_DialogInput = zx::ClearInput( );
		g_DialogHot = -1;
		SetFocus( zx::BrowserFocus::Dialog );

		S_Sound( CHAN_VOICE | CHAN_UI, "menu/invalid", snd_menuvolume, ATTN_NONE );
	}

	// [rc4l] What a dialog's answer MEANS, in one place, so the three input routes cannot come to
	// disagree about it. Every route -- letter, Enter, click -- ends here with an index.
	void AnswerDialog( int index )
	{
		if ( !g_Dialog.open || ( index < 0 ) || ( index >= g_Dialog.count ))
			return;

		const DialogAction action = g_Dialog.action;
		const bool bAffirmative = ( index == 0 );
		const FString typed = g_DialogInput.text.c_str( );

		// Closed BEFORE the action runs. Joining tears the engine down for a WAD reload and never
		// returns, so anything left until afterwards is never done at all.
		CloseDialog( );

		switch ( action )
		{
		case DialogAction::DeleteCustom:
			if ( bAffirmative && g_CustomDeleting.IsNotEmpty( ))
			{
				if ( zx::CustomDelete( g_CustomDeleting.GetChars( )))
					CustomForget( );
				else
					ShowNotice( "Could not delete", "That preset's folder would not go away." );
			}

			g_CustomDeleting = "";
			break;

		// [rc4l] The box stays OPEN either way. The question was asked from inside it and the answer
		// belongs there: closing on "yes" would hide the very thing the player asked to look at
		// afresh, and closing on "no" would punish them for changing their mind.
		case DialogAction::ResetFlags:
			if ( bAffirmative )
			{
				NewResetFlags( );
				NewSay( "Flags reset" );
			}
			break;

		case DialogAction::ResetMaps:
			if ( bAffirmative )
			{
				NewResetMaps( );
				NewSay( "Map list reset" );
			}
			break;

		// [rc4l] Re-applying the MODE is the reset: NewSetGameMode writes this mode's skill, its
		// limits, its clock and its teams, which is the whole of what this box shows. Doing it that
		// way rather than listing the cvars again means the defaults live in one place and the
		// button cannot fall behind them.
		case DialogAction::ResetGameplay:
			if ( bAffirmative )
			{
				// FORCED: this is the defaults being asked for outright, so it takes the settings a
				// mode change would have left alone. See NewSetGameMode.
				NewSetGameMode( NewChosenGameMode( ), true );
				NewSay( "Gameplay settings reset" );
			}
			break;

		case DialogAction::CancelDownload:
			// The hold placed when the question went up must be released on exactly one of the two
			// answers -- that pairing is the whole reason this menu owns the question rather than
			// handing it to M_StartMessage.
			if ( bAffirmative )
				zx::waddownload::Cancel( );
			zx::ReleaseJoinResume( !bAffirmative );
			break;

		case DialogAction::StopHosting:
			if ( bAffirmative )
				zx::HostStop( );
			break;

		case DialogAction::SwitchHosting:
			if ( bAffirmative )
				DoHostSwitch( );
			break;

		// [rc4l] Stop first, then go. The order is the point: joining reloads the engine and never
		// comes back here, so a HostStop left until afterwards would never run and the server would be
		// orphaned rather than closed.
		case DialogAction::StopHostingAndJoin:
			if ( bAffirmative )
			{
				zx::HostStop( );
				DoJoinSelected( );
			}
			break;

		case DialogAction::JoinPassword:
			if ( bAffirmative )
			{
				// The password goes in before the join, because the connect reads it on its way out.
				cl_password = typed.GetChars( );
				DoJoinSelected( );
			}
			break;

		case DialogAction::None:
			break;
		}
	}

	// [rc4l] Clicking a dialog. Hover lights a button and a release presses it -- the same
	// press-then-release pairing the JOIN button uses, so a drag off a button cancels it there too.
	bool DialogMouseEvent( int type, int x, int y )
	{
		g_DialogHot = -1;

		for ( int i = 0; i < g_Dialog.count; ++i )
		{
			int bx, by, bw;
			if ( !DialogButtonRect( i, bx, by, bw ))
				continue;

			if (( x < serverbrowser_ToScreenX( bx )) || ( x >= serverbrowser_ToScreenX( bx + bw )) ||
				( y < serverbrowser_ToScreenY( by )) || ( y >= serverbrowser_ToScreenY( by + SB_DLG_BTN_H )))
			{
				continue;
			}

			g_DialogHot = i;

			// Moving the pointer over a button also moves the FOCUS to it, so the glow follows the
			// mouse and a player who switches from one to the other is never answering a different
			// question from the one they were looking at.
			g_Dialog.focus = i;

			if ( type == MOUSE_Release )
				AnswerDialog( i );
			return true;
		}

		// Swallowed: the dialog is modal, so a click on the browser behind it does nothing at all
		// rather than quietly operating a control the player cannot see the point of.
		return true;
	}

	void CloseDialog( )
	{
		g_Dialog = BrowserDialog( );
		g_DialogInput = zx::ClearInput( );
		g_DialogHot = -1;
		SetFocus( zx::BrowserFocus::Rows );
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
	void DrawRows( void )
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
		screen->DrawText( SmallFont, CR_DARKGRAY, SB_COL_NAME, SB_HEADER_Y, "SERVER", DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
		screen->DrawText( SmallFont, CR_DARKGRAY, SB_COL_PLAYERS, SB_HEADER_Y, "PLRS", DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
		DrawRightAligned( SmallFont, CR_DARKGRAY, SB_COL_PING, SB_HEADER_Y, "PING" );

		for ( int i = 0; i < window.count; i++ )
		{
			const int row = window.first + i;
			const int lServer = g_SortedServers[row];
			const int y = SB_FIRST_ROW_Y + i * SB_ROW_HEIGHT;
			const bool bSelected = ( row == g_Selected );

			// [rc4l] The same rule the HOSTING CATALOGUE uses for the experience it is serving, from
			// the same unit, so being on a server and running one are drawn alike. See 3e08af3.
			const zx::RowPaint paint = zx::PaintListRow( bSelected, RowIsTheServerWeAreOn( lServer ),
				row == g_HoverRow );

			// Hover is a HINT, not a selection. It used to move the selection outright, which meant
			// sweeping the pointer across the list repainted every row it crossed and rewrote the
			// whole detail panel each time -- names flicking between red and white, the panel
			// churning through servers nobody asked about. A faint band says "this is what you would
			// be clicking" without claiming anything happened.
			if ( paint.band != zx::RowBand::None )
			{
				PalEntry bar( 150, 170, 215 );
				float alpha = 0.06f;

				if ( paint.band == zx::RowBand::Live )
				{
					bar = PalEntry( 40, 96, 52 );
					alpha = bSelected ? 0.40f : 0.28f;
				}
				else if ( paint.band == zx::RowBand::Selection )
				{
					bar = PalEntry( 120, 150, 220 );
					alpha = 0.28f;
				}

				DimRow( y, bar, alpha );
			}

			// [rc4l] The glow, only when the ARROW KEYS are on the list. The highlight marks what is
			// selected and stays put while you click around with the mouse; this marks where the
			// keyboard is, which is a different question and used to have no answer.
			if ( bSelected )
				FocusAnchor( zx::BrowserFocus::Rows, SB_PANEL_LEFT + 9, serverbrowser_RowTextY( y, 0 ) + 1 );

			serverbrowser_DrawCountry( lServer, SB_COL_FLAG, y );

			const int ty = serverbrowser_RowTextY( y, SmallFont->GetHeight( ));

			// [rc4l] White whether selected or not. CR_UNTRANSLATED is the font's own colour, which for
			// SmallFont is Doom red -- so every unselected server read as a warning about itself, and
			// the highlight had to carry the selection on its own anyway.
			//
			// GREEN for the server you are actually on, the same green the hosting catalogue uses for
			// the experience it is running. One colour, one meaning across the whole browser: this is
			// the live one. The band above keeps saying it once the selection moves onto this row.
			// [rc4l] A row we cannot join is drawn grey throughout, so it reads as unavailable at a
			// glance rather than requiring the version column to be compared against your own build.
			const bool bUnjoinable = !zx::VersionRelationCanJoin( BROWSER_GetVersionRelation( lServer ));

			const FString name = serverbrowser_FitName( BROWSER_GetHostName( lServer ), SB_NAME_MAX_WIDTH );
			screen->DrawText( SmallFont, bUnjoinable ? CR_DARKGRAY
					: (( paint.label == zx::RowLabel::Live ) ? CR_GREEN : CR_WHITE),
				SB_COL_NAME, ty, name, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );

			// Humans only -- a row reading 8/8 for seven bots and one person is a lie the player
			// only discovers after joining.
			const int humans = static_cast<int>( BROWSER_GetNumHumanPlayers( lServer ));
			const int slots = static_cast<int>( BROWSER_GetMaxClients( lServer ));

			FString players;
			players.Format( "%d/%d", humans, slots );

			// [rc4l] Colour only where it means something, the same way ping does: full is the one
			// state that changes what you can do about the row, so it is the only one worth marking.
			const EColorRange playersColor = bUnjoinable ? CR_DARKGRAY
				: ((( slots > 0 ) && ( humans >= slots )) ? CR_RED : CR_WHITE);
			screen->DrawText( SmallFont, playersColor, SB_COL_PLAYERS, ty, players, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );

			const int ping = static_cast<int>( BROWSER_GetPing( lServer ));
			FString pingText;
			pingText.Format( "%d", ping );
			DrawRightAligned( SmallFont, bUnjoinable ? CR_DARKGRAY : serverbrowser_PingColor( ping ),
				SB_COL_PING, ty, pingText );
		}

		// [rc4l] There used to be a "querying N more" line under the list here. It went because it was
		// the third thing on the footer saying the same thing: the button already reads CHECKING while
		// a sweep is out, and the count it added was of servers the player has never heard of and
		// cannot act on. A row that has not answered is simply not on the list yet, which the list
		// already shows by not having it.
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
			( SB_VIRT_W / 2 ) - ( SmallFont->StringWidth( text ) / 2 ), y, text, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
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
	// [rc4l] A rounded, gradient-filled panel in virtual coordinates.
	//
	// Extracted rather than copied. The browser panel, the detail panel and now the dialog all draw
	// the same shape, and three copies of a gradient loop is three places to adjust when the look
	// changes and two places to forget.
	//*************************************************************************
	//
	// [rc4l] A clip rectangle for screen->Dim, which takes no DTA tags of its own.
	//
	// DrawText can be clipped with DTA_ClipTop and DTA_ClipBottom, but every panel, field and marker
	// in this browser is a Dim -- a raw rectangle with no tag list to attach a clip to. A scrolling
	// area whose text stopped at the boundary while its backgrounds carried on would not be a mask,
	// it would be a smear.
	//
	// So the rectangle is intersected here instead, once, and everything that draws inside a viewport
	// goes through DimClipped rather than Dim.
	void PushClip( int topPx, int bottomPx )
	{
		g_ClipTopPx = topPx;
		g_ClipBottomPx = bottomPx;
	}

	void PopClip( )
	{
		g_ClipTopPx = -1;
		g_ClipBottomPx = -1;
	}

	void DimClipped( PalEntry colour, float alpha, int x, int y, int w, int h )
	{
		if ( g_ClipTopPx >= 0 )
		{
			const int top = MAX( y, g_ClipTopPx );
			const int bottom = MIN( y + h, g_ClipBottomPx );
			if ( bottom <= top )
				return;

			y = top;
			h = bottom - top;
		}

		if (( w <= 0 ) || ( h <= 0 ))
			return;

		screen->Dim( colour, alpha, x, y, w, h );
	}

	void DrawRoundedPanel( int vx, int vy, int vw, int vh, const zx::PanelColor &topCol,
		const zx::PanelColor &botCol, int vradius )
	{
		const int left = serverbrowser_ToScreenX( vx );
		const int right = serverbrowser_ToScreenX( vx + vw );
		const int top = serverbrowser_ToScreenY( vy );
		const int bottom = serverbrowser_ToScreenY( vy + vh );
		const int radius = serverbrowser_ToScreenY( vradius ) - serverbrowser_ToScreenY( 0 );

		const int w = right - left;
		const int h = bottom - top;
		if (( w <= 0 ) || ( h <= 0 ))
			return;

		for ( int row = 0; row < h; ++row )
		{
			const int inset = zx::ComputeRoundedInset( row, h, radius );
			const int rowW = w - 2 * inset;
			if ( rowW <= 0 )
				continue;

			const zx::PanelColor c = zx::ComputePanelGradient( row, h, topCol, botCol );
			DimClipped( PalEntry( c.r, c.g, c.b ), c.a / 255.f, left + inset, top + row, rowW, 1 );
		}
	}

	// [rc4l] The browser's button, wherever it appears. Same shape as JOIN because it IS the JOIN
	// drawing -- a dialog whose buttons merely resembled the browser's would drift the first time
	// either was touched.
	// [rc4l] `warn` tints the button the way CANCEL is tinted while a download runs: warmer, with the
	// label in orange. Reserved for buttons that TAKE SOMETHING AWAY, so the one that ends a running
	// thing looks the same wherever it appears rather than only on the download.
	// [rc4l] What a button is FOR, in its colour. Neutral is every ordinary one; Warn is the button
	// that stops a running server; Cool invites rather than warns, and is for the one button on an
	// otherwise empty screen.
	//
	// A named tint rather than a second bool beside `warn`: two bools at a call site say nothing
	// about which combinations mean anything, and three of the four here mean nothing at all.
	enum class ButtonTint { Neutral, Warn, Cool };

	void DrawRoundedButton( int vx, int vy, int vw, int vh, const char *label, bool lit,
		ButtonTint tint = ButtonTint::Neutral )
	{
		const int base = lit ? 70 : 45;

		const bool warn = ( tint == ButtonTint::Warn );
		const bool cool = ( tint == ButtonTint::Cool );

		// Warm pushes red, cool pushes blue, and the untinted one stays grey.
		const int warmTop = warn ? base + 30 : base;
		const int warmBot = warn ? base + 15 : base / 2;
		const int coolTop = cool ? base + 40 : base;
		const int coolBot = cool ? base + 25 : base / 2;

		const zx::PanelColor topCol = { static_cast<BYTE>( warmTop ), static_cast<BYTE>( base ),
			static_cast<BYTE>( coolTop ), 220 };
		const zx::PanelColor botCol = { static_cast<BYTE>( warmBot ), static_cast<BYTE>( base / 2 ),
			static_cast<BYTE>( coolBot ), 235 };

		DrawRoundedPanel( vx, vy, vw, vh, topCol, botCol, 4 );

		const EColorRange textCol = warn ? CR_ORANGE : ( lit ? CR_WHITE : CR_GRAY );

		const int textY = vy + ( vh - SmallFont->GetHeight( )) / 2 + 1;
		screen->DrawText( SmallFont, textCol,
			vx + ( vw / 2 ) - ( SmallFont->StringWidth( label ) / 2 ), textY, label,
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
	}

	// [rc4l] The sunken black backdrop a column of detail sits on, given its corners.
	//
	// The server list's detail panel had this written out inline; the HOST tab's right column now wants
	// the same one, and two hand-rolled gradients would have drifted apart the first time either was
	// touched. DrawRoundedPanel above already does the work -- this only names the colours, which are
	// what "the detail panel's background" actually means.
	void DrawDetailBackdrop( int vLeft, int vTop, int vRight, int vBottom )
	{
		const zx::PanelColor topCol = { 0, 0, 0, 170 };
		const zx::PanelColor botCol = { 0, 0, 0, 205 };

		DrawRoundedPanel( vLeft, vTop, vRight - vLeft, vBottom - vTop, topCol, botCol, 8 );
	}

	void DrawDetailPanel( )
	{
		DrawDetailBackdrop( SB_DETAIL_LEFT, SB_DETAIL_TOP, SB_DETAIL_RIGHT, SB_DETAIL_BOTTOM );
	}

	//*************************************************************************
	//
	// [rc4l] Where each pill sits, asked by BOTH the drawing and the hit testing.
	//
	// Those two used to each carry their own copy of `left + i * ( width + gap )`, which was fine
	// only while every pill was the same width. Measured pills make that formula wrong, and two
	// copies of a wrong formula is a click that lands one button away from the one under the pointer.
	int PillW( const char *label, int pad )
	{
		return SmallFont->StringWidth( label ) + 2 * pad;
	}

	int TabW( int i )		{ return PillW( kTabLabels[i], SB_TAB_PILL_PAD ); }

	// [rc4l] The second row belongs to whichever tab is selected, so everything about it -- how many
	// pills, what they say, which one is lit -- is asked of the tab rather than assumed to be the
	// browse filters. Three things have to agree about this row (what is drawn, what is clicked, and
	// the widths both measure from), so they agree by asking the same three functions.
	int SubTabCount( )
	{
		return ( g_Tab == BrowserTab::Browse ) ? kBrowseCount : kHostKindCount;
	}

	const char *SubTabLabel( int i )
	{
		return ( g_Tab == BrowserTab::Browse ) ? kSubTabLabels[i] : kHostSubTabLabels[i];
	}

	int SubTabIndex( )
	{
		return ( g_Tab == BrowserTab::Browse )
			? static_cast<int>( g_Browse ) : static_cast<int>( g_HostKind );
	}

	int SubTabW( int i )	{ return PillW( SubTabLabel( i ), SB_SUBTAB_PILL_PAD ); }

	int TabLeft( int i )
	{
		int x = SB_TAB_LEFT;
		for ( int k = 0; k < i; ++k )
			x += TabW( k ) + SB_TAB_GAP;
		return x;
	}

	int SubTabLeft( int i )
	{
		int x = SB_SUBTAB_LEFT;
		for ( int k = 0; k < i; ++k )
			x += SubTabW( k ) + SB_SUBTAB_GAP;
		return x;
	}

	//*************************************************************************
	//
	// [rc4l] One oval button on one of the two header rows.
	//
	// Shared because there are two rows of these now, and a gradient written out twice is a colour
	// that gets changed in one of them.
	void DrawPill( int vLeft, int vTop, int vW, int vH, const char *label, bool bSelected, bool bHot )
	{
		const int left = serverbrowser_ToScreenX( vLeft );
		const int right = serverbrowser_ToScreenX( vLeft + vW );
		const int top = serverbrowser_ToScreenY( vTop );
		const int bottom = serverbrowser_ToScreenY( vTop + vH );

		const int w = right - left;
		const int h = bottom - top;
		if (( w <= 0 ) || ( h <= 0 ))
			return;

		// Fully oval: the radius is half the height, so the ends are semicircles rather than the
		// softened corners the panels use. Different shape, different job, these switch what you are
		// looking at, they are not another surface to put things on.
		const int radius = h / 2;

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

		const int textY = vTop + ( vH - SmallFont->GetHeight( )) / 2 + 1;
		screen->DrawText( SmallFont, bSelected ? CR_WHITE : CR_DARKGRAY,
			vLeft + ( vW / 2 ) - ( SmallFont->StringWidth( label ) / 2 ), textY,
			label, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
	}

	//*************************************************************************
	//
	// [rc4l] The second header row: which servers BROWSE is showing, and the box that filters them.
	//
	// Drawn only under BROWSE. On PLAY there is no list to filter, so a row of filters would be two
	// controls that do nothing sitting where the eye expects the thing it just chose.
	void DrawSubTabs( )
	{
		static const char *const browseTips[] = {
			"Servers anyone can join",
			"These servers are password-protected",
		};

		// In row order, same as the labels they describe.
		static const char *const hostTips[] = {
			"Ready-made experiences to host",
			"The ones you have put together and saved",
			"Build a server from your own files",
		};

		const char *const *tips = ( g_Tab == BrowserTab::Browse ) ? browseTips : hostTips;

		for ( int i = 0; i < SubTabCount( ); ++i )
		{
			const int vLeft = SubTabLeft( i );
			const int vW = SubTabW( i );
			const bool bSelected = ( SubTabIndex( ) == i );

			// Hover only. Keyboard focus gets a RING instead, because a brighter fill is already what
			// selected looks like and one picture cannot mean both.
			DrawPill( vLeft, SB_SUBTAB_TOP, vW, SB_SUBTAB_H, SubTabLabel( i ), bSelected,
				( g_SubTabHot == i ));

			if ( bSelected )
				FocusAnchor( zx::BrowserFocus::SubTabs, vLeft - 5, SB_SUBTAB_TOP + SB_SUBTAB_H / 2 );

			serverbrowser_Tip( vLeft, SB_SUBTAB_TOP, vW, SB_SUBTAB_H, tips[i] );
		}

		// The search box filters a list, and hosting has none. It stays with the row it belongs to.
		if ( g_Tab == BrowserTab::Browse )
			DrawSearchBox( );
	}

	//*************************************************************************
	//
	// [rc4l] The tab row: what you are doing here, and the rule that closes the header band.
	void DrawTabs( )
	{
		// In tab order, and it has to stay that way: these are indexed by the same i that picks the
		// label, so a tab moved on the row without moving its tip here describes the wrong one.
		static const char *const tips[] = {
			"Find a server to join",
			"Run a server on this machine\nOthers join it while you play",
		};

		for ( int i = 0; i < kTabCount; ++i )
		{
			const int vLeft = TabLeft( i );
			const int vW = TabW( i );
			const bool bSelected = ( static_cast<int>( g_Tab ) == i );

			DrawPill( vLeft, SB_TAB_TOP, vW, SB_TAB_H, kTabLabels[i], bSelected, ( g_TabHot == i ));

			if ( bSelected )
				FocusAnchor( zx::BrowserFocus::Tabs, vLeft - 5, SB_TAB_TOP + SB_TAB_H / 2 );

			serverbrowser_Tip( vLeft, SB_TAB_TOP, vW, SB_TAB_H, tips[i] );
		}

		// Always, and always here. This one closes the tab row, which every tab has, so it is the one
		// thing in the header that must not move when the tab changes.
		DrawSeparatorSpan( SB_TAB_ROW_SEP_Y, SB_PANEL_LEFT + 12, SB_DETAIL_RIGHT );

		// [rc4l] The second row now belongs to BOTH tabs -- browse filters its list, host picks how
		// the server is put together -- so the header is the same shape whichever tab is selected
		// and nothing below it moves when you change tab.
		DrawSubTabs( );
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
	//*************************************************************************
	//
	// [rc4l] The hosting form. Same panel, same field, same button as everywhere else in the browser,
	// because a screen that looked different would be teaching a second set of rules to somebody who
	// has just learned the first.
	//*************************************************************************
	//
	// [rc4l] Fill the form from what was used last time.
	//
	// Retyping a server name and a port on every visit is the kind of small friction that stops
	// people hosting at all, and none of these values is a secret worth forgetting -- except the
	// password, which is deliberately NOT remembered: a password saved in a config file that anyone
	// with the machine can read is a worse promise than no password.
	void LoadHostForm( )
	{
		if ( g_HostFormLoaded )
			return;

		g_HostFormLoaded = true;

		// GetGenericRep rather than assigning the CVAR straight across: MSVC accepts the implicit
		// conversion and GCC does not, so the direct form builds on one platform and fails on two.
		FString name = cl_fua_hostname.GetGenericRep( CVAR_String ).String;
		if ( name.IsEmpty( ))
			name = FUA_DEFAULT_SERVERNAME;

		g_HostFields[kHostFieldName] = zx::TextInput( name.GetChars( ), name.Len( ));

		FString port;
		port.Format( "%d", static_cast<int>( cl_fua_hostport ));
		g_HostFields[kHostFieldPort] = zx::TextInput( port.GetChars( ), port.Len( ));

		FString players;
		players.Format( "%d", static_cast<int>( cl_fua_hostmaxplayers ));
		g_HostFields[kHostFieldMaxPlayers] = zx::TextInput( players.GetChars( ), players.Len( ));

		g_HostFields[kHostFieldPassword] = zx::ClearInput( );
		g_HostAdvertise = ( cl_fua_hostpublic != 0 );
	}

	void SaveHostForm( )
	{
		cl_fua_hostname = g_HostFields[kHostFieldName].text.c_str( );
		cl_fua_hostport = atoi( g_HostFields[kHostFieldPort].text.c_str( ));
		cl_fua_hostmaxplayers = atoi( g_HostFields[kHostFieldMaxPlayers].text.c_str( ));
		cl_fua_hostpublic = g_HostAdvertise ? 1 : 0;
	}

	//*************************************************************************
	//
	// [rc4l] Start the server the form describes, and go to it.
	//
	// The IWAD and the loaded PWADs are taken from what THIS client is running rather than asked for:
	// a host who is already playing something has already answered that question, and a file picker
	// here would be a second way to get it wrong. Whatever we are running is what our server runs, so
	// the host can never fail to have the files for their own game.
	// [rc4l] Down, then up on the selected entry. Stopping FIRST because the new server wants the same
	// port, and the old one is still holding it: starting into an occupied port is what made the
	// number climb, start after start, before ReachProbeRelease existed for the same reason.
	void DoHostSwitch( )
	{
		zx::HostStop( );
		StartHosting( );
	}

	void StartHosting( )
	{
		// Remembered before anything can fail: the list marks THIS row as the one being served, and
		// SWITCH compares against it.
		//
		// [rc4l] The way of playing goes with it, RESOLVED rather than copied from the choice: an
		// empty choice means "the default", and the tint has to name a row rather than a preference.
		g_HostingEntry = g_HostEntrySel;
		g_HostingVariantId = "";
		g_HostingKey = HostSelectionKey( );

		{
			const std::vector<zx::CatalogueEntry> &all = zx::CatalogueLoad( );
			if (( g_HostEntrySel >= 0 ) && ( g_HostEntrySel < static_cast<int>( all.size( ))))
			{
				const zx::AddonEntry &addon = all[g_HostEntrySel].addon;
				const zx::VariantPick chosen = zx::PickVariant( addon, g_HostVariantId.GetChars( ));

				if (( chosen.index >= 0 ) && ( chosen.index < static_cast<int>( addon.variants.size( ))))
					g_HostingVariantId = addon.variants[chosen.index].id.c_str( );
			}
		}

		zx::HostConfig config;

		config.hostName = g_HostFields[kHostFieldName].text;
		config.password = g_HostFields[kHostFieldPassword].text;
		config.port = atoi( g_HostFields[kHostFieldPort].text.c_str( ));
		config.maxPlayers = atoi( g_HostFields[kHostFieldMaxPlayers].text.c_str( ));
		config.advertise = g_HostAdvertise;
		config.serveWads = true;

		// [rc4l] Start on the map we are standing on, for the same reason the WADs are taken from what
		// we are running: the host has already answered this question by playing. "map01" is a poor
		// guess for anything but stock Doom -- MM8BDM's maps are MM3HAR and friends, and a mod whose
		// map01 does not exist would start the server on nothing.
		//
		// Falls back when there is no map to read: hosting straight from the title screen is normal,
		// and MapName is empty there.
		config.map = "map01";

		if (( gamestate == GS_LEVEL ) && level.MapName.IsNotEmpty( ))
			config.map = level.MapName.GetChars( );

		if ( config.maxPlayers < 2 )
			config.maxPlayers = 2;
		if ( config.maxPlayers > MAXPLAYERS )
			config.maxPlayers = MAXPLAYERS;

		// [rc4l] A catalogue entry decides the content; the fields above decided the identity. The
		// two never overlap, which is why an entry has no server name and the form has no file list.
		const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( );

		if (( g_HostEntrySel >= 0 ) && ( g_HostEntrySel < static_cast<int>( entries.size( ))))
		{
			const zx::CatalogueEntry &chosen = entries[g_HostEntrySel];

			// [rc4l] By CONTENT, not by name. The panel asks by name because it must stay free, but
			// having a file called eonweapons.pk3 is not the same as having the one this entry means,
			// and pressing the button is where that difference starts to matter: the resolved path
			// goes to the spawned server as well as to our own reload, so both sides load the same
			// wrong file, authentication compares them and passes, and the server quietly is not the
			// experience it advertises. A mismatch is treated as missing, which routes it into the
			// downloader that was already sitting here for the absent case.
			// [rc4l] The variant reaches the server three times, and has to: as the cfg that decides
			// how it plays, as the files it loads, and in the name, because a joiner reading a server
			// list cannot see either of the first two. "Skulltag" alone does not tell them whether
			// they are about to join an invasion or a duel, and finding out by joining is the cost
			// this avoids.
			const zx::VariantPick pick = zx::PickVariant( chosen.addon, g_HostVariantId.GetChars( ));

			// The remix goes through the same one place the panel asks, so what gets verified,
			// fetched and started is exactly what was on screen.
			const std::vector<zx::AddonFileRef> loads = HostSelectedFiles( chosen.addon );

			// Every axis, in group order, so what gets exec'd matches what the panel showed.
			const std::vector<zx::RemixPick> remixes = HostRemixPicks( chosen.addon );
			std::vector<std::string> remixCfgs;
			for ( size_t i = 0; i < remixes.size( ); ++i )
				remixCfgs.push_back( zx::CatalogueRemixCfgPath( remixes[i].id ));

			const std::vector<FString> verified = HostEntryVerifiedPaths( loads );

			std::vector<std::string> have;
			for ( size_t i = 0; i < loads.size( ); ++i )
			{
				if (( i < verified.size( )) && verified[i].IsNotEmpty( ))
					have.push_back( loads[i].name );
			}


			zx::HostChoices choices;
			choices.serverName = config.hostName;
			choices.maxPlayers = config.maxPlayers;
			choices.port = config.port;
			choices.advertise = config.advertise;

			if ( !pick.name.empty( ))
			{
				choices.serverName = zx::ComposeServerName( config.hostName.c_str( ),
					pick.name, std::string( ));
			}

			const zx::HostPlan plan = zx::BuildHostPlan( chosen.addon, loads,
				zx::PickIwad( chosen.addon.iwad, zx::AvailableIwads( chosen.addon.iwad )),
				zx::CatalogueServerCfgPath( chosen, g_HostVariantId.GetChars( )),
				remixCfgs, pick.map, choices, have,
				zx::IsFreeIwadName( chosen.addon.iwad ));

			// [rc4l] Missing files are a DOWNLOAD, which is what the catalogue's per-file md5 was
			// shipped for and what BuildHostPlan has always meant by returning `missing` rather than
			// calling it fatal. The button refused anyway until now, so hosting an entry said
			// downloading was impossible while the JOIN beside it fetched the same file happily.
			//
			// A blocker is different and still refuses: no IWAD to run on cannot be downloaded.
			if ( plan.blocker.empty( ) && !plan.ready && BeginHostDownload( chosen, loads, plan ))
				return;

			if ( !plan.blocker.empty( ) || !plan.ready )
			{
				// [rc4l] NAMES the files. "Missing files" alone leaves the player to work out which
				// of a dozen it meant, and the entry beside them already marks each with a + or a -.
				FString why = FString( plan.blocker.c_str( ));

				if ( plan.blocker.empty( ))
				{
					// One sentence rather than a list on its own lines: the dialog wraps on spaces
					// and has no idea what a newline is, so a list would come out as one run-on row.
					why = "You do not have ";

					for ( size_t i = 0; i < plan.missing.size( ); ++i )
					{
						if ( i > 0 )
							why += ( i + 1 == plan.missing.size( )) ? " or " : ", ";
						why += plan.missing[i].c_str( );
					}

					// We only reach this having ALREADY tried to fetch them and been refused, so the
					// reason is downloading's rather than hosting's. Start prints which one on the
					// console; there is no point guessing at it a second time here.
					why += ", and they could not be downloaded. The console says why.";
				}

				// On the PANEL, not just in the console. Refusing silently is what made this look
				// like it had started: the server never came up, the console said so, and the only
				// thing on screen was a button that had made a noise.
				FString title;
				title.Format( "Cannot host %s", chosen.addon.name.c_str( ));
				ShowNotice( title.GetChars( ), why.GetChars( ));

				Printf( TEXTCOLOR_ORANGE "Cannot host %s: %s\n" TEXTCOLOR_NORMAL,
					chosen.addon.name.c_str( ), why.GetChars( ));
				return;
			}

			config.execCfg = plan.execCfg;
			config.execRemixCfgs = plan.execRemixCfgs;

			// [rc4l] Set directly rather than exec'd, and after everything, because what lives mean
			// depends on the gamemode: in Cooperative asking for any is a switch to Survival, and in
			// Invasion a zero is genuinely unlimited. No shared cfg can be right in both.
			config.extraCvars = zx::LivesCvars( HostLivesControl( chosen.addon ));

			// [rc4l] Weapon speed rides the same list, and brings the infinite ammo with it. Both come
			// out of one unit because they are one decision: see weaponspick_compute.h.
			{
				const std::vector<std::pair<std::string, std::string> > weapons =
					zx::FastWeaponsCvars( HostFastWeaponsOffered( chosen.addon ), g_HostFastWeapons );

				config.extraCvars.insert( config.extraCvars.end( ), weapons.begin( ), weapons.end( ));
			}

			// [rc4l] Teams ride the same list, and have to: the axis NAMES a gamemode, so an exec
			// running after it would put the old mode straight back.
			{
				const std::vector<std::pair<std::string, std::string> > teams =
					zx::TeamsCvars( HostTeamsControl( chosen.addon ));

				config.extraCvars.insert( config.extraCvars.end( ), teams.begin( ), teams.end( ));
			}

			// [rc4l] RESOLVED to full paths, not left as bare names. These are what the CLIENT
			// reloads onto in order to join, and RequestReload's loadability check opens exactly
			// what it is handed -- so a name is tested against the working directory, and a file
			// living anywhere else on the search path comes back "not found" with the file sitting
			// on disk. The spawned server may be given names because its own -file handling
			// searches; this side has no such step. The join path has always resolved first (see
			// zx_joinserver.cpp) and hosting never did.
			g_HostEntryIwad = zx::FindFileInEngineSearchPaths( plan.iwad.c_str( ));
			if ( g_HostEntryIwad.IsEmpty( ))
				g_HostEntryIwad = zx::FindIwadInEngineSearchPaths( plan.iwad.c_str( ));
			if ( g_HostEntryIwad.IsEmpty( ))
				g_HostEntryIwad = plan.iwad.c_str( );	// nothing found; let the reload say so

			g_HostEntryPwads.Clear( );
			for ( size_t i = 0; i < plan.pwads.size( ); ++i )
			{
				// The copy whose md5 matched, if we checked this one above. Re-resolving by name here
				// would undo the check: the verified file can sit later on the search path than an
				// impostor of the same name, and the first hit is what a name search returns.
				FString path = VerifiedPathFor( loads, verified, plan.pwads[i].c_str( ));
				if ( path.IsEmpty( ))
					path = zx::FindFileInEngineSearchPaths( plan.pwads[i].c_str( ));

				g_HostEntryPwads.Push( path.IsNotEmpty( ) ? path
					: FString( plan.pwads[i].c_str( )));
			}

			// [rc4l] The SERVER gets the same resolved paths, and for the same reason it gets them
			// rather than names: it searches its OWN config, which is not the one this client just
			// registered a download folder in. Handing it a name meant a file we had just fetched was
			// invisible to the server we fetched it for -- it started without the pk3, and the client
			// that joined was told its lumps did not match. One resolution, used by both.
			config.iwad = g_HostEntryIwad.GetChars( );
			config.pwads.clear( );
			for ( unsigned i = 0; i < g_HostEntryPwads.Size( ); ++i )
				config.pwads.push_back( g_HostEntryPwads[i].GetChars( ));

			// [rc4l] The entry's own opening map, which is NOT the first of its rotation: Duel 40
			// opens on START, a welcome map deliberately left out of the rotation. Falling back to
			// the cfg means the rotation decides, and to map01 only when there is neither.
			config.map = plan.map;
			if ( config.map.empty( ) && plan.execCfg.empty( ))
				config.map = "map01";

			// [rc4l] Unless a map was PICKED, which beats all of it. Only when the picker was drawn:
			// an entry whose rotation has one map or none has nothing to have chosen, and writing a
			// map there would override the welcome map that is the whole reason plan.map exists.
			{
				const std::vector<std::string> &maps = HostSelectedRotation( );
				if ( maps.size( ) > 1 )
					config.map = maps[HostStartMapIndex( )];
			}

			SaveHostForm( );
			zx::ReachProbeRelease( );

			if ( zx::HostStart( config ) == false )
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/invalid", snd_menuvolume, ATTN_NONE );

			return;
		}

		// [rc4l] Nothing selected, which the list no longer allows: every row is an entry and one is
		// always current. Reached only if the catalogue is empty, and then there is nothing to host.
		S_Sound( CHAN_VOICE | CHAN_UI, "menu/invalid", snd_menuvolume, ATTN_NONE );
	}

	// [rc4l] How many people other than us are on the server we are connected to.
	//
	// Counted from OUR OWN player table rather than asked of the server, because we are a client of
	// it and the table is what the server has already told us. Asking would mean a round trip, and a
	// question about whether to disconnect cannot wait on the connection it is about.
	int CountOtherPlayersHere( )
	{
		if ( NETWORK_GetState( ) != NETSTATE_CLIENT )
			return 0;

		int count = 0;
		for ( int i = 0; i < MAXPLAYERS; ++i )
		{
			if ( playeringame[i] && ( i != consoleplayer ))
				++count;
		}

		return count;
	}

	//*************************************************************************
	//
	// [rc4l] Fetch what `plan` says is missing, so the entry can be hosted once it lands. True when a
	// transfer started and the caller should stand down.
	//
	// The catalogue's md5 per file goes with each request, so the transfer is CHECKED rather than
	// trusted: a mirror handing us a different build of the same filename is caught here rather than
	// discovered by the server refusing to start on it.
	bool BeginHostDownload( const zx::CatalogueEntry &chosen,
		const std::vector<zx::AddonFileRef> &files, const zx::HostPlan &plan )
	{
		if ( plan.missing.empty( ) || !zx::waddownload::IsAvailable( ))
			return false;

		std::vector<zx::waddownload::WantedFile> wanted;
		for ( size_t i = 0; i < plan.missing.size( ); ++i )
		{
			std::string md5;
			for ( size_t j = 0; j < files.size( ); ++j )
			{
				if ( files[j].name == plan.missing[i] )
				{
					md5 = files[j].md5;
					break;
				}
			}

			// [rc4l] Say when one of these IS the game. The downloader checks an iwad's SHA-256
			// against the shipped list before keeping it, and it can only do that if it is told
			// which file to check -- an entry's own `files` are all mods, so the flag can only come
			// from the plan.
			//
			// It carries no md5, because the catalogue never hashes a game it does not ship. The
			// allowlist's own hash is the check that matters for one of these.
			const bool bIsIwad = plan.missingIwad && ( plan.missing[i] == plan.iwad );
			wanted.push_back( zx::waddownload::WantedFile( plan.missing[i], bIsIwad, md5 ));
		}

		// No extra sites and no last resorts: there is no server here yet, so the shipped mirror
		// list is all there is, and it is where a catalogue entry's files are published anyway.
		// [rc4l] zx::NoteDownloadFinished, not our own resume: that is the callback carrying the
		// truth table. Handing waddownload our resume directly would fire it the instant the bytes
		// landed, wherever the player happened to be, which is the bug the shared path exists to
		// stop.
		if ( !zx::waddownload::Start( std::vector<std::string>( ), std::vector<std::string>( ),
			wanted, zx::NoteDownloadFinished ))
		{
			return false;
		}

		g_HostDownloadEntry = g_HostEntrySel;
		g_HostDownloadResumed = false;

		// Parked in the same slot a downloading JOIN uses, which is the whole point: while this runs
		// the player can close the menu and carry on playing, and everything that makes that safe --
		// the hold while a prompt is up, the waiting band if they have wandered off, the cancel that
		// keeps the file and drops the intent -- is code they already share.
		zx::SetPendingResume( serverbrowser_HostDownloadResume, chosen.addon.name.c_str( ));

		// The panel stays put rather than putting up a modal, the same as the join: the transfer runs
		// for minutes and a box you cannot dismiss without cancelling is a worse way to spend them
		// than a progress line under a list you can still read.
		S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
		return true;
	}

	// Files may have appeared on disk; look again next time anything asks.
	void HostFilesChanged( )
	{
		++g_HostHaveGeneration;
	}

	// [rc4l] Whether a transfer OF OURS is in flight -- ours meaning one this panel started. A join's
	// download running behind us must not turn the host button into a cancel for it.
	bool HostDownloadRunning( )
	{
		return ( g_HostDownloadEntry >= 0 ) && zx::waddownload::IsRunning( );
	}

	// One frame after the resume fires, because it arrives from inside waddownload::Tick and starting
	// a server from there would re-enter it.
	void ResumeHostAfterDownload( )
	{
		if ( !g_HostDownloadResumed )
			return;

		g_HostDownloadResumed = false;

		const int entry = g_HostDownloadEntry;
		const bool bOk = g_HostDownloadSucceeded;
		g_HostDownloadEntry = -1;

		if ( entry < 0 )
			return;

		// A failure has already been reported by the shared path, on the browser's own panel. Saying
		// it again here would be the same news twice.
		if ( !bOk )
			return;

		// [rc4l] Straight back into the same button. StartHosting rebuilds the plan from scratch, so
		// the files that just landed are found by the ordinary lookup and the whole thing proceeds as
		// though they had been there all along -- there is no second, download-shaped path to keep
		// working.
		g_HostEntrySel = entry;
		HostFilesChanged( );
		StartHosting( );
	}

	//*************************************************************************
	//
	// [rc4l] How to name loaded wad `i` to the server: the path it was loaded from when that path can
	// be put on a command line, and its bare name when it cannot.
	//
	// The path is what stops the server searching for a file we already have open. The fallback
	// matters because a name that fails IsSafeFilePath is DROPPED from the argv, and a server missing
	// a file is worse than a server that has to go and look for it.
	std::string HostServeName( int i )
	{
		const char *const pszFull = Wads.GetWadFullName( i );
		if (( pszFull != NULL ) && ( pszFull[0] != 0 ) && zx::IsSafeFilePath( pszFull ))
			return pszFull;

		const char *const pszName = Wads.GetWadName( i );
		return ( pszName != NULL ) ? pszName : "";
	}

	// [rc4l] Whatever the button under the pointer means right now.
	void PressHostButton( )
	{
		const zx::HostState state = zx::HostCurrentState( );

		if ( state == zx::HostState::Failed )
		{
			// BACK: clear the failure so the form returns, with what was typed still in it.
			zx::HostForget( );
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/backup", snd_menuvolume, ATTN_NONE );
			return;
		}

		if ( zx::HostIsActive( ))
		{
			// [rc4l] Ask, because the cost is not ours. Stopping a server with people on it ends
			// THEIR game, and the host is the one person on the machine who cannot see how many that
			// is by looking at their own screen. A count in the question is the whole difference
			// between an informed decision and a stray click.
			//
			// Asked only when it would actually cost somebody something: an empty server is one
			// nobody is playing on, and a confirmation for that is a dialog that trains people to
			// dismiss dialogs.
			const int others = CountOtherPlayersHere( );
			if ( others > 0 )
			{
				FString message;
				message.Format( "%d %s playing on it. Stopping ends their game as well as yours.",
					others, ( others == 1 ) ? "person is" : "people are" );

				ShowDialog( DialogAction::StopHosting, "Stop the server?", message.GetChars( ),
					"STOP IT", 's', "KEEP IT UP", 'k' );
				return;
			}

			zx::HostStop( );
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/backup", snd_menuvolume, ATTN_NONE );
			return;
		}

		StartHosting( );
	}

	// [rc4l] SWITCH: stop what is running and stand the selected entry up in its place.
	//
	// Confirmed on the same terms as STOP, because it IS a stop as far as anyone playing is
	// concerned. Offering it without the question would let a stray click on the list column end
	// other people's game more quietly than the button that says it does.
	void PressHostSwitchButton( )
	{
		if ( zx::HostIsActive( ) == false )
			return;

		const int others = CountOtherPlayersHere( );
		if ( others > 0 )
		{
			FString message;
			message.Format( "%d %s playing on it. Switching ends their game as well as yours.",
				others, ( others == 1 ) ? "person is" : "people are" );

			ShowDialog( DialogAction::SwitchHosting, "Switch to this?", message.GetChars( ),
				"SWITCH", 's', "KEEP IT UP", 'k' );
			return;
		}

		DoHostSwitch( );
	}

	// Down the form, then onto the button, and no further. The button is the end because it is the
	// thing the form exists to reach.
	//*************************************************************************
	//
	// [rc4l] The focus, asked as the questions the rest of this file wants answered.
	//
	// Reads look the way they did when there were three variables; what changed is that there is one
	// now, so no caller can leave two of them disagreeing.
	// How many rows the gameplay panel drew last frame, which is what the keyboard walks. Zero while
	// the form is open, while a server is running, and for an experience with nothing to decide.
	int HostGameplayRowCount( )		{ return static_cast<int>( g_HostGameFocusRows.Size( )); }

	bool HostOnGameplay( )		{ return g_HostFocus.slot == zx::HostSlot::Gameplay; }

	// Which row the keyboard is on, or -1. Bounds-checked against what actually drew, because the
	// panel can shrink underneath a focus that was legitimate when it was set.
	int HostGameplayFocus( )
	{
		if ( !HostOnGameplay( ) || ( g_HostFocus.field < 0 ) ||
			( g_HostFocus.field >= HostGameplayRowCount( )))
		{
			return -1;
		}

		return g_HostFocus.field;
	}

	bool HostOnList( )			{ return g_HostFocus.slot == zx::HostSlot::List; }
	bool HostOnVisibility( )	{ return g_HostFocus.slot == zx::HostSlot::Visibility; }
	bool HostOnButton( )		{ return g_HostFocus.slot == zx::HostSlot::Action; }
	bool HostOnToggle( )		{ return g_HostFocus.slot == zx::HostSlot::Toggle; }

	// Which field, or -1 when the keyboard is somewhere else. Every caller already range-checks,
	// so -1 is the answer they were all written to handle.
	int HostFieldFocus( )
	{
		return ( g_HostFocus.slot == zx::HostSlot::Field ) ? g_HostFocus.field : -1;
	}

	// Whether a text box can be typed into at all -- which is the fields, and nothing else.
	bool HostInAField( )		{ return HostFieldFocus( ) >= 0; }

	// [rc4l] What the form currently OFFERS, handed to the compute unit so focus can never land on
	// something that is not drawn. The settings take the fields and the visibility row with them, and
	// a running server takes the toggle.
	bool HostHasFields( )
	{
		const zx::HostState state = zx::HostCurrentState( );
		const bool bForm = ( zx::HostIsActive( ) == false ) && ( state != zx::HostState::Failed );
		return bForm && g_HostShowSettings;
	}

	// Corrects a focus that named something no longer on screen. Called before anything reads it,
	// because the panel can change underneath a position that was legitimate when it was set.
	void ClampHostFocus( )
	{
		g_HostFocus = zx::ClampHostFocus( g_HostFocus, kHostFieldCount,
			HostHasFields( ), HostFootHasToggle( ), HostGameplayRowCount( ), HostCopyOffered( ));
	}

	// [rc4l] One key, answered by computation/hostfocus_compute and applied here.
	//
	// This used to be the whole rule, written out as a chain of ifs over three variables: which field,
	// on the row, on the button. It could not be tested, it did not know whether the settings were
	// even open -- so DOWN walked five boxes that were not on screen -- and every other place that
	// moved focus had to reproduce its clearing by hand.
	void NavigateHostFocus( zx::HostNavKey key )
	{
		ClampHostFocus( );

		// [rc4l] An axis of pills that has wrapped is a GRID, and up and down belong to it before
		// they belong to the panel. Asked first, and only falls through when the axis has no line
		// that way -- see computation/pillgrid_compute.
		if ( HostOnGameplay( ) &&
			(( key == zx::HostNavKey::Up ) || ( key == zx::HostNavKey::Down )))
		{
			if ( StepPillGridVertically( HostGameplayFocus( ),
				( key == zx::HostNavKey::Up ) ? -1 : 1 ))
			{
				return;
			}
		}

		// [rc4l] What the keyboard OWNS before the key is answered, so the release below can tell a
		// move that changes it from one that does not.
		const bool bWasInField = HostInAField( );

		const zx::HostNavResult r = zx::ComputeHostNav( g_HostFocus, key, kHostFieldCount,
			HostHasFields( ), HostFootHasToggle( ), HostGameplayRowCount( ), HostCopyOffered( ));

		// [rc4l] The list moves its SELECTION rather than focus, so it is applied here and focus is
		// left alone -- the movement/traversal split the unit reports separately.
		if ( r.rowStep != 0 )
		{
			// [rc4l] Walks the ROWS, which now include the open experience's ways of playing, and then
			// says what the row it landed on means. The cursor is derived from the choice rather than
			// stored, so moving it IS choosing: there is no third state where the highlight is on one
			// thing and the button would start another.
			const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( );
			const std::vector<zx::HostListRow> rows = HostListRows( );

			if ( !rows.empty( ))
			{
				const int here = HostSelectedRow( rows );
				const int next = (( here >= 0 ) ? here : 0 ) + r.rowStep;

				// Up off the first row is the one edge that leaves, back to the tabs. Down off the
				// last simply stops, the same as the server list.
				if ( next < 0 )
				{
					SetFocus( zx::BrowserFocus::Tabs );
					return;
				}

				if ( next < static_cast<int>( rows.size( )))
				{
					g_HostEntrySel = rows[next].entry;
					HostSelectionChanged( );
					g_HostOnEntryRow = ( rows[next].variant < 0 );

					if ( rows[next].variant >= 0 )
						g_HostVariantId = entries[rows[next].entry].addon.variants[rows[next].variant].id.c_str( );

					RevealHostCatalogueRow( next );
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
				}
			}
			return;
		}

		// [rc4l] The visibility row's left and right move the CURSOR along it. They do not answer the
		// question -- enter does. Moving and choosing are different acts, and a row that decided as
		// you passed over it could not be read without being changed.
		if (( r.choiceStep != 0 ) && HostOnGameplay( ))
		{
			// [rc4l] LEFT off the first option leaves the axis for the list. An axis is the leftmost
			// thing in the right column, so there is nothing else that way, and going back to what
			// the panel is describing is more use than doing nothing.
			if (( r.choiceStep < 0 ) && HostGameplayRowAtFirstChoice( HostGameplayFocus( )))
			{
				g_HostFocus = zx::HostLeftOfTheForm( );
				RevealHostFocus( );
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
				return;
			}

			// [rc4l] The row decides what a step MEANS -- a stop on a slider, the next option on an
			// axis of pills -- so the unit reports the direction and this applies it. Same split the
			// visibility row makes just below.
			//
			// RIGHT off the last option does nothing. It is the end of the row and there is nothing
			// beyond it: wrapping to the first would undo the choice the player just walked to.
			StepHostGameplayRow( HostGameplayFocus( ), r.choiceStep );
			return;
		}

		if ( r.choiceStep != 0 )
		{
			const int at = zx::ChoiceStep( g_HostVisCursor, kHostVisCount, r.choiceStep );

			if ( at != g_HostVisCursor )
			{
				g_HostVisCursor = at;
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
			}
			return;
		}

		// In a field, left and right are the caret's. The field itself handles them.
		if ( r.caret )
			return;

		if ( r.pos.slot == zx::HostSlot::Away )
		{
			SetFocus( zx::BrowserFocus::Tabs );
			return;
		}

		const bool bMoved = ( r.pos.slot != g_HostFocus.slot ) || ( r.pos.field != g_HostFocus.field );

		// [rc4l] LET GO OF THE HELD KEY ONLY WHEN RAW-KEY OWNERSHIP CHANGES.
		//
		// This released on every arrow, which killed auto-repeat dead: M_Ticker repeats whatever is
		// still latched, and unlatching on each press meant one step per press and no more. Holding
		// down through the experience list moved one row.
		//
		// The release is still needed where it was aimed -- crossing into or out of a text field
		// turns TranslateKeyboardEvents over mid-press, and the key-up then arrives untranslated and
		// unlatches nothing, so the button would repeat forever. That is a change of ownership, not
		// a change of position, and only the first has to stop the repeat.
		//
		// The rule for any list added later: MOVING WITHIN a region keeps the key, LEAVING it does
		// not. SetFocus does the leaving half for regions; this does it for the panel's own halves.
		// Compared against where the key LANDS, not where it started -- g_HostFocus has not moved yet.
		if ((( r.pos.slot == zx::HostSlot::Field ) ? true : false ) != bWasInField )
			M_ReleaseMenuButtons( );

		// Arriving on the row starts the cursor on the answer that is already given, so the first
		// thing the player sees marked is what they currently have.
		if (( r.pos.slot == zx::HostSlot::Visibility ) && ( g_HostFocus.slot != zx::HostSlot::Visibility ))
			g_HostVisCursor = g_HostAdvertise ? kHostVisGlobal : kHostVisLocal;

		g_HostFocus = r.pos;
		RevealHostFocus( );

		if ( bMoved )
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
	}

	// Kept for the two callers that think in steps rather than keys.
	void MoveHostFocus( int step )
	{
		NavigateHostFocus( ( step < 0 ) ? zx::HostNavKey::Up : zx::HostNavKey::Down );
	}

	//*************************************************************************
	//
	// [rc4l] The hosting panel's share of a click. Returns true when it took it.
	//
	// Hover moves the keyboard focus with the pointer, the same as everywhere else in the browser --
	// so picking a field up with the mouse and then reaching for the arrows carries on from what you
	// are looking at rather than from wherever the keyboard was left.
	// [rc4l] Is there anything on the hosting panel under this point?
	//
	// One answer, read by the click-away rule and matching what the handlers below actually claim --
	// two functions disagreeing about where a control is would give a field that refuses to let go
	// over exactly the strip the other one thought was empty.
	bool HostControlAt( int x, int y )
	{
		const zx::HostState state = zx::HostCurrentState( );
		const bool bForm = ( zx::HostIsActive( ) == false ) && ( state != zx::HostState::Failed );

		// The whole foot row: the action, and the settings toggle when there is one. Taken as one
		// band because the two are edge to edge apart from the gutter between them, and a click in
		// that gutter is still a click on the panel rather than on nothing.
		if (( y >= serverbrowser_ToScreenY( SB_HOST_RTOGGLE_Y )) &&
			( y < serverbrowser_ToScreenY( SB_HOST_RTOGGLE_Y + SB_HOST_RTOGGLE_H )) &&
			( x >= serverbrowser_ToScreenX( SB_HOST_FOOT_LEFT )) &&
			( x < serverbrowser_ToScreenX( SB_HOST_FOOT_RIGHT )))
		{
			return true;
		}

		// The catalogue rows, using the same y helper the drawing uses, bounded to the left column.
		for ( int row = SB_HOST_CATALOGUE_FIRST; row < HostCatalogueRowCount( ); ++row )
		{
			const int rowY = HostCatalogueRowY( row );
			if ( HostRowVisible( rowY, SB_HOST_ENTRY_H ) &&
				( y >= serverbrowser_ToScreenY( rowY )) &&
				( y < serverbrowser_ToScreenY( rowY + SB_HOST_ENTRY_H )) &&
				( x >= serverbrowser_ToScreenX( SB_HOST_LIST_LEFT )) &&
				( x < serverbrowser_ToScreenX( SB_HOST_LIST_RIGHT )))
			{
				return true;
			}
		}

		// The settings only exist while they are showing AND while the form is the thing on screen:
		// g_HostShowSettings survives across a start, so without the bForm test the fields would go
		// on claiming clicks from behind the running panel.
		if ( !g_HostShowSettings || !bForm )
			return false;

		int fieldY = HostFirstFieldY( );
		for ( int i = 0; i < kHostFieldCount; ++i )
		{
			if ( HostRowVisible( fieldY, SB_HOST_FIELD_H ) &&
				( y >= serverbrowser_ToScreenY( fieldY )) &&
				( y < serverbrowser_ToScreenY( fieldY + SB_HOST_FIELD_H )) &&
				( x >= serverbrowser_ToScreenX( SB_HOST_RCOL_LEFT )) &&
				( x < serverbrowser_ToScreenX( SB_HOST_RCOL_RIGHT )))
			{
				return true;
			}

			fieldY += HostRowPitch( );
		}

		const int visY = HostVisibilityY( );
		if ( HostRowVisible( visY, SB_CHOICE_H ) &&
			( y >= serverbrowser_ToScreenY( visY )) &&
			( y < serverbrowser_ToScreenY( visY + SB_CHOICE_H )))
		{
			const int rowX = SB_HOST_RCOL_LEFT + SB_HOST_RLABEL_W;
			const int rowW = SB_HOST_RIGHT - SB_HOST_PAD - rowX;

			for ( int i = 0; i < kHostVisCount; ++i )
			{
				const zx::ChoiceCell cell = zx::ChoiceCellAt( i, kHostVisCount, rowX, rowW,
					SB_CHOICE_GAP );
				if ( !cell.valid )
					continue;

				if (( x >= serverbrowser_ToScreenX( cell.x )) &&
					( x < serverbrowser_ToScreenX( cell.x + cell.width )))
				{
					return true;
				}
			}
		}

		return false;
	}

	// [rc4l] Whether this row is an axis of pills sitting on its FIRST option.
	//
	// Which is where LEFT stops being about the axis: there is nothing to its left on the row, and
	// the thing to its left on the panel is the experience list. The same answer the foot's action
	// button gives for the same key, so the right column has one way back rather than two.
	//
	// Sliders are deliberately not included. Left is how their value comes DOWN, and a slider that
	// threw the keyboard across the panel when the value reached its floor would fight the key that
	// was being held to get it there.
	bool HostGameplayRowAtFirstChoice( int row )
	{
		if (( row < 0 ) || ( row >= HostGameplayRowCount( )))
			return false;

		const HostGameFocusRow &at = g_HostGameFocusRows[row];
		if ( at.bSlider )
			return false;

		const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( );
		if (( g_HostEntrySel < 0 ) || ( g_HostEntrySel >= static_cast<int>( entries.size( ))))
			return false;

		const zx::AddonEntry &addon = entries[g_HostEntrySel].addon;
		const std::vector<zx::RemixGroup> groups = zx::GroupRemixes( HostOfferedRemixes( addon ));

		for ( size_t g = 0; g < groups.size( ); ++g )
		{
			if ( groups[g].id != at.id )
				continue;

			return zx::PickRemix( groups[g].choices, HostRemixWanted( groups[g].id )).index <= 0;
		}

		return false;
	}

	// [rc4l] UP or DOWN inside an axis of pills, when the axis has wrapped onto more than one line.
	//
	// True when the key was spent inside the axis. False means there is no line that way, and the
	// caller lets it fall through to the ordinary navigation -- which is how up off the first line
	// still reaches the control above and down off the last still reaches the one below.
	//
	// The grid it walks is computed by the same function the draw uses, so the marker cannot land
	// where a pill is not.
	bool StepPillGridVertically( int row, int dir )
	{
		if (( row < 0 ) || ( row >= HostGameplayRowCount( )))
			return false;

		const HostGameFocusRow &at = g_HostGameFocusRows[row];
		if ( at.bSlider )
			return false;

		const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( );
		if (( g_HostEntrySel < 0 ) || ( g_HostEntrySel >= static_cast<int>( entries.size( ))))
			return false;

		const zx::AddonEntry &addon = entries[g_HostEntrySel].addon;
		const std::vector<zx::RemixGroup> groups = zx::GroupRemixes( HostOfferedRemixes( addon ));

		for ( size_t g = 0; g < groups.size( ); ++g )
		{
			if ( groups[g].id != at.id )
				continue;

			const std::vector<zx::AddonRemix> &choices = groups[g].choices;
			if ( choices.size( ) <= 1 )
				return false;

			// A locked axis does not move, by any key. The mouse cannot touch it either.
			if (( groups[g].id == kHostMixGroup ) && HostWeaponsPlan( addon ).mixLocked )
				return false;

			const HostPillGeom geom = HostPillGeometry( SB_HOST_RCOL_LEFT, groups[g] );
			const zx::RemixPick pick = zx::PickRemix( choices, HostRemixWanted( groups[g].id ));

			const zx::PillMove to = zx::MovePillVertically( geom.layout, geom.widths, geom.gap,
				( pick.index >= 0 ) ? pick.index : 0, dir );

			if ( to.leaves )
				return false;

			if ( to.index != pick.index )
			{
				HostSetRemixWanted( groups[g].id, choices[to.index].id );
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
			}

			return true;
		}

		return false;
	}

	// [rc4l] Put the keyboard on the gameplay row with this id, so a click moves the marker to what
	// was clicked.
	//
	// Without it the next arrow key sets off from wherever the keyboard happened to be left, which is
	// never where the player is looking: their cursor is the thing they just pressed.
	void FocusHostGameplayRow( bool bSlider, const std::string &id )
	{
		for ( unsigned i = 0; i < g_HostGameFocusRows.Size( ); ++i )
		{
			if (( g_HostGameFocusRows[i].bSlider != bSlider ) || ( g_HostGameFocusRows[i].id != id ))
				continue;

			SetFocus( zx::BrowserFocus::Host );
			g_HostFocus = zx::HostFocusPos( zx::HostSlot::Gameplay, static_cast<int>( i ));
			return;
		}
	}

	// [rc4l] A step on whichever gameplay row the keyboard is on.
	//
	// The row registry says what the row IS; this says what a step does to each kind. Both kinds go
	// through the very same call the mouse makes -- HostSliderSet for a track, HostSetRemixWanted for
	// an axis -- so a key and a click cannot come to mean different things.
	void StepHostGameplayRow( int row, int step )
	{
		if (( row < 0 ) || ( row >= HostGameplayRowCount( )) || ( step == 0 ))
			return;

		const HostGameFocusRow &at = g_HostGameFocusRows[row];

		if ( at.bSlider )
		{
			// The slider's own recorded geometry, so the ends and the clamp are the ones the mouse
			// is using rather than a second opinion about the range.
			for ( unsigned i = 0; i < g_HostSliders.Size( ); ++i )
			{
				if ( g_HostSliders[i].id != at.id )
					continue;

				const HostSliderRect &s = g_HostSliders[i];
				const int now = clamp( s.value + step, s.min, s.max );

				if ( now != s.value )
				{
					HostSliderSet( s.id, now );
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
				}
				return;
			}

			return;
		}

		// An axis of pills. Stepping moves along the OFFERED order, which is the order they are
		// drawn in, so left and right go the way the eye expects on a wrapped row too.
		const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( );
		if (( g_HostEntrySel < 0 ) || ( g_HostEntrySel >= static_cast<int>( entries.size( ))))
			return;

		const zx::AddonEntry &addon = entries[g_HostEntrySel].addon;
		const std::vector<zx::RemixGroup> groups = zx::GroupRemixes( HostOfferedRemixes( addon ));

		for ( size_t g = 0; g < groups.size( ); ++g )
		{
			if ( groups[g].id != at.id )
				continue;

			const std::vector<zx::AddonRemix> &choices = groups[g].choices;
			if ( choices.size( ) <= 1 )
				return;

			// [rc4l] A locked axis does not move. It draws inert and registers no hit rect, so the
			// mouse cannot touch it; the keyboard must not be the one way round that.
			if (( groups[g].id == kHostMixGroup ) && HostWeaponsPlan( addon ).mixLocked )
				return;

			const zx::RemixPick pick = zx::PickRemix( choices, HostRemixWanted( groups[g].id ));

			const int count = static_cast<int>( choices.size( ));
			const int from = ( pick.index >= 0 ) ? pick.index : 0;
			const int to = clamp( from + step, 0, count - 1 );

			if ( to != from )
			{
				HostSetRemixWanted( groups[g].id, choices[to].id );
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
			}
			return;
		}
	}

	// [rc4l] What a slider's id means, which is the ONE place a setting is tied to its control. The
	// slider itself knows nothing about lives.
	void HostSliderSet( const std::string &id, int value )
	{
		// [rc4l] A slider inside one of the NEW screen's settings boxes, which names the cvar it
		// moves. Namespaced so it cannot be confused with this panel's own four; see SettingSliderId.
		if ( id.compare( 0, 4, "box:" ) == 0 )
		{
			// [rc4l] Through SettingApplyNumber, NOT NewSetCvar. Some of these settings write a
			// companion cvar -- sv_maxplayers has to carry sv_maxclients with it -- and that used to
			// live in SettingSanitise, which only runs when TYPING stops. Making Players a slider
			// with the raw setter here would have moved the players and left the seats behind,
			// which is the silent half-full server the one-row comment warns about.
			SettingApplyNumber( id.substr( 4 ), value );
			return;
		}

		if ( id == "lives" )
			g_HostLives = value;
		else if ( id == "fastweapons" )
			g_HostFastWeapons = value;
		else if ( id == "startmap" )
			g_HostStartMap = value;
		else if ( id == "teams" )
		{
			// The one axis whose slider moves an INDEX rather than the value: the stops are 0, 2, 3
			// and 4, and a slider that ran on the count would offer a team of one between them.
			g_HostTeams = zx::TeamsCountAtStop( value );
		}
	}

	// Every slider on the panel, from the rects the last draw recorded. Steps first: they sit at the
	// track's ends and a generous track hitbox would otherwise swallow them, which is exactly what
	// makes a button look decorative.
	bool HostSliderMouseEvent( int type, int x, int y )
	{
		// A drag cannot outlive the control it is dragging: a release off the panel, a changed
		// selection, or the row scrolling away all leave the id set with nothing to move.
		bool bStillDrawn = false;
		for ( unsigned i = 0; i < g_HostSliders.Size( ); ++i )
		{
			if ( g_HostSliderDragging.Compare( g_HostSliders[i].id.c_str( )) == 0 )
				bStillDrawn = true;
		}
		if ( !bStillDrawn )
			g_HostSliderDragging = "";

		g_HostSliderHot = "";

		for ( unsigned i = 0; i < g_HostSliders.Size( ); ++i )
		{
			const HostSliderRect &s = g_HostSliders[i];

			const bool bOnRow = ( y >= serverbrowser_ToScreenY( s.trackY - 2 )) &&
				( y < serverbrowser_ToScreenY( s.trackY + SB_HOST_GAME_ROW_H - 1 ));

			if ( bOnRow && ( type == MOUSE_Click ))
			{
				const bool bMinus = ( x >= serverbrowser_ToScreenX( s.minusX )) &&
					( x < serverbrowser_ToScreenX( s.minusX + s.stepW ));
				const bool bPlus = ( x >= serverbrowser_ToScreenX( s.plusX )) &&
					( x < serverbrowser_ToScreenX( s.plusX + s.stepW ));

				if ( bMinus || bPlus )
				{
					const int was = s.value;
					const int now = clamp( was + ( bPlus ? 1 : -1 ), s.min, s.max );

					FocusHostGameplayRow( true, s.id );
					HostSliderSet( s.id, now );

					// Silent at the ends, so a press that changed nothing does not sound like one
					// that did.
					if ( now != was )
						S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );

					return true;
				}
			}

			const bool bOnTrack = bOnRow &&
				( x >= serverbrowser_ToScreenX( s.trackX - 4 )) &&
				( x < serverbrowser_ToScreenX( s.trackX + s.trackW + 4 ));

			if (( type == MOUSE_Click ) && bOnTrack )
			{
				g_HostSliderDragging = s.id.c_str( );
				FocusHostGameplayRow( true, s.id );
			}

			const bool bMine = ( g_HostSliderDragging.Compare( s.id.c_str( )) == 0 );
			if ( bOnTrack || bMine )
				g_HostSliderHot = s.id.c_str( );

			if ( !bMine )
				continue;

			// Tracked once the button is down even after the pointer leaves the row, which is what
			// makes a drag feel like one. Clamped to the track's ends first, so dragging past either
			// end pins the value rather than letting the arithmetic run away.
			const int span = MAX( 1, s.max - s.min );
			const int vx = clamp( serverbrowser_ToVirtualX( x ) - s.trackX, 0, s.trackW );
			const int steps = (( vx * span ) + ( s.trackW / 2 )) / MAX( 1, s.trackW );
			const int now = clamp( s.min + steps, s.min, s.max );

			if ( now != s.value )
			{
				HostSliderSet( s.id, now );
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
			}

			if ( type == MOUSE_Release )
				g_HostSliderDragging = "";

			return true;
		}

		return false;
	}

	bool HostMouseEvent( int type, int x, int y )
	{
		g_HostFieldHot = -1;
		g_HostButtonHot = false;
		g_HostVisHot = -1;
		g_HostCopyHot = false;

		if ( g_HostKind == HostKind::New )
			return NewMouseEvent( type, x, y );

		if ( g_HostKind == HostKind::Custom )
			return CustomMouseEvent( type, x, y );

		if ( g_HostKind != HostKind::Presets )
			return false;

		// [rc4l] FIRST, and before the click-away rule below. A bar lives over the same column the
		// details and the status draw in, and a drag along it must not read as "clicked on nothing"
		// and drop the keyboard focus every frame it moves.
		if ( HostListBarMouseEvent( type, x, y ))
			return true;

		if ( HostRegionBarsMouseEvent( type, x, y ))
			return true;

		// The rule lives in computation/pointerdrag_compute -- above all, that a PRESS always ends the
		// previous gesture and is never consumed by it. Inline, that rule had a hole in it and ate a
		// click; see the unit's header for what that looked like.
		const zx::DragOutcome drag = zx::StepDrag( g_HostFieldDragging, PointerEventOf( type ));
		g_HostFieldDragging = drag.dragging;

		// [rc4l] A CLICK THAT LANDS ON NOTHING LETS THE FIELD GO.
		//
		// The search box has had this from the start and the hosting form never got it, which is the
		// whole bug: click into a box, click away, and the caret is still blinking in it. Worse than
		// cosmetic -- while a field holds focus TranslateKeyboardEvents hands it the raw keys, so the
		// arrows stop navigating too and the entire form feels dead.
		//
		// Clicking away from a field is how every interface says "I am done with that", and a caret
		// left in a box you have visibly left is a lie about where the next keystroke will land.
		//
		// Decided HERE, before the handlers below, so it applies to clicks that land on nothing at
		// all and are otherwise ignored -- which is exactly the case that felt broken.
		const bool bReleasingFocus = ( type == MOUSE_Click )
			&& ( g_Focus == zx::BrowserFocus::Host ) && ( HostControlAt( x, y ) == false );

		if ( bReleasingFocus )
		{
			SetFocus( zx::BrowserFocus::Tabs );
			g_HostFieldDragging = false;
		}

		if ( drag.consumed && HostInAField( ))
		{
			// Tracked even once the pointer leaves the box, because that is what dragging means
			// everywhere else -- a selection that stopped the moment you overshot the last character
			// is one you could never make in a single gesture.
			g_HostFields[HostFieldFocus( )] = zx::SetCaret( g_HostFields[HostFieldFocus( )],
				HostFieldCharAt( HostFieldFocus( ), x ), true );
			return true;
		}

		const zx::HostState state = zx::HostCurrentState( );
		const bool bForm = ( zx::HostIsActive( ) == false ) && ( state != zx::HostState::Failed );

		// [rc4l] The ACTION button, at the foot of the right column beside the settings toggle. Its
		// rectangle comes from the same helpers the drawing uses, so the two cannot come to disagree
		// about where it is -- which is exactly how STOP SERVER ended up unclickable.
		const int actW = HostActionW( );

		if (( x >= serverbrowser_ToScreenX( SB_HOST_FOOT_LEFT )) &&
			( x < serverbrowser_ToScreenX( SB_HOST_FOOT_LEFT + actW )) &&
			( y >= serverbrowser_ToScreenY( SB_HOST_RTOGGLE_Y )) &&
			( y < serverbrowser_ToScreenY( SB_HOST_RTOGGLE_Y + SB_HOST_RTOGGLE_H )))
		{
			g_HostButtonHot = true;

			if ( type == MOUSE_Release )
			{
				SetFocus( zx::BrowserFocus::Host );
				g_HostFocus = zx::HostFocusPos( zx::HostSlot::Action, 0 );
				PressHostAction( );
			}
			return true;
		}

		// The catalogue, before the fields: it is above them on screen and it decides what the rest
		// of the panel is about.
		g_HostEntryHot = -2;
		{
			const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( );
			const std::vector<zx::HostListRow> rows = HostListRows( );

			for ( int row = SB_HOST_CATALOGUE_FIRST; row < static_cast<int>( rows.size( )); ++row )
			{
				const int rowY = HostCatalogueRowY( row );
				if ( !HostRowVisible( rowY, SB_HOST_ENTRY_H ) ||
					( y < serverbrowser_ToScreenY( rowY )) ||
					( y >= serverbrowser_ToScreenY( rowY + SB_HOST_ENTRY_H )) ||
					( x < serverbrowser_ToScreenX( SB_HOST_LIST_LEFT )) ||
					( x >= serverbrowser_ToScreenX( SB_HOST_LIST_RIGHT )))
				{
					continue;
				}

				g_HostEntryHot = row;

				if ( type == MOUSE_Click )
				{
					const zx::HostListRow &r = rows[row];

					SetFocus( zx::BrowserFocus::Host );
					g_HostFocus = zx::HostFocusPos( zx::HostSlot::List, 0 );
					g_HostEntrySel = r.entry;
					HostSelectionChanged( );
					g_HostOnEntryRow = ( r.variant < 0 );

					if ( r.variant >= 0 )
					{
						// A way of playing chooses itself and leaves the list open, because comparing
						// two of them means going back and forth between them.
						g_HostVariantId = entries[r.entry].addon.variants[r.variant].id.c_str( );
					}
					else if ( !entries[r.entry].addon.variants.empty( ))
					{
						// The experience's own row opens and shuts it. Clicking the row anywhere does
						// it, not just the caret: a caret is a small target and the row already means
						// "this one", which is the same thing opening it says.
						HostToggleEntryOpen( r.entry );

						// [rc4l] Opening it puts the cursor on the way of playing that would ACTUALLY
						// be hosted, rather than leaving it on the heading.
						//
						// Selecting the heading and selecting its default start the same server, so
						// leaving the highlight up there said nothing the row below did not, while
						// looking like a fourth thing you could pick. Derived rather than set to row
						// zero, so an entry whose default is not its first still highlights the one
						// the button would start.
						g_HostOnEntryRow = !HostEntryIsOpen( r.entry );
					}

					S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
				}
				return true;
			}
		}

		// [rc4l] The sliders, whichever settings they belong to. One handler over the rects recorded
		// as they were drawn, so adding a second slider needs nothing here.
		if ( HostSliderMouseEvent( type, x, y ))
			return true;

		// [rc4l] The gameplay rows, inside the scrolled detail region. Tested against what the last
		// frame actually DREW rather than against a computed row height: these scroll, so a rule
		// saying "the nth row is at this y" is wrong the moment the region moves under it.
		g_HostGameHot = -1;


		for ( unsigned i = 0; i < g_HostGameRows.Size( ); ++i )
		{
			const HostGameplayRow &row = g_HostGameRows[i];

			if (( y < serverbrowser_ToScreenY( row.y - 1 )) ||
				( y >= serverbrowser_ToScreenY( row.y + row.h - 1 )) ||
				( x < serverbrowser_ToScreenX( row.x )) ||
				( x >= serverbrowser_ToScreenX( row.x + row.w )))
			{
				continue;
			}

			g_HostGameHot = static_cast<int>( i );


			if ( type == MOUSE_Click )
			{
				// Sets that AXIS only. Every other axis keeps whatever it had, which is the whole
				// reason they are separate.
				HostSetRemixWanted( row.group, row.id );
				FocusHostGameplayRow( false, row.group );
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
			}

			return true;
		}

		// The toggle, in the other half of the foot row. Only when it is DRAWN: while a server is
		// running the action button spans the whole row, and a toggle still taking clicks from under
		// it would be the invisible-but-clickable bug in its purest form.
		g_HostOnSettingsToggle = false;
		if ( HostFootHasToggle( ) &&
			( y >= serverbrowser_ToScreenY( SB_HOST_RTOGGLE_Y )) &&
			( y < serverbrowser_ToScreenY( SB_HOST_RTOGGLE_Y + SB_HOST_RTOGGLE_H )) &&
			( x >= serverbrowser_ToScreenX( SB_HOST_TOGGLE_X )) &&
			( x < serverbrowser_ToScreenX( SB_HOST_FOOT_RIGHT )))
		{
			g_HostOnSettingsToggle = true;

			if ( type == MOUSE_Click )
			{
				SetFocus( zx::BrowserFocus::Host );
				g_HostFocus = zx::HostFocusPos( zx::HostSlot::Toggle, 0 );

				// The same function the keyboard presses, so the two cannot come to mean different
				// things -- which is how the mouse ended up being the only way to open the settings.
				PressHostSettingsToggle( );
			}
			return true;
		}

		// [rc4l] bForm as well as the toggle: g_HostShowSettings survives a start, so without this the
		// fields would keep taking clicks from behind the running panel.
		if ( !g_HostShowSettings || !bForm )
			return false;

		// The fields.
		int fieldY = HostFirstFieldY( );
		for ( int i = 0; i < kHostFieldCount; ++i )
		{
			const int rowTop = serverbrowser_ToScreenY( fieldY );
			const int rowBottom = serverbrowser_ToScreenY( fieldY + SB_HOST_FIELD_H );

			if ( HostRowVisible( fieldY, SB_HOST_FIELD_H ) &&
				( y >= rowTop ) && ( y < rowBottom ) &&
				( x >= serverbrowser_ToScreenX( SB_HOST_RCOL_LEFT )) &&
				( x < serverbrowser_ToScreenX( SB_HOST_RCOL_RIGHT )))
			{
				g_HostFieldHot = i;

				if ( type == MOUSE_Click )
				{
					SetFocus( zx::BrowserFocus::Host );

					g_HostFocus = zx::HostFocusPos( zx::HostSlot::Field, i );

					const int now = static_cast<int>( DMenu::MenuTime );
					const bool bDouble = (( now - g_HostClickTime ) < 15 );
					g_HostClickTime = now;

					if ( bDouble )
					{
						// The word under the pointer, or everything when there is no word there. No
						// drag afterwards: a second press that started selecting again would undo
						// what the player just asked for before they let go.
						g_HostFields[i] = zx::SelectWordOrAll( g_HostFields[i],
							HostFieldCharAt( i, x ));
						g_HostFieldDragging = false;
					}
					else
					{
						// Press puts the caret and arms a drag; the drag turns it into a selection.
						g_HostFieldDragging = zx::BeginDrag( );
						g_HostFields[i] = zx::SetCaret( g_HostFields[i], HostFieldCharAt( i, x ),
							bShiftHeld( ));
					}
				}
				return true;
			}

			fieldY += HostRowPitch( );
		}

		// The visibility row. Which CELL the pointer is in decides the answer -- the gaps belong to
		// nobody, so a click between the two is not evidence for either.
		const int visY = HostVisibilityY( );
		if ( HostRowVisible( visY, SB_CHOICE_H ) &&
			( y >= serverbrowser_ToScreenY( visY )) &&
			( y < serverbrowser_ToScreenY( visY + SB_CHOICE_H )))
		{
			const int rowX = SB_HOST_RCOL_LEFT + SB_HOST_RLABEL_W;
			const int rowW = SB_HOST_RIGHT - SB_HOST_PAD - rowX;

			// Back into virtual units, because that is what the layout is expressed in.
			int at = -1;
			for ( int i = 0; i < kHostVisCount; ++i )
			{
				const zx::ChoiceCell cell = zx::ChoiceCellAt( i, kHostVisCount, rowX, rowW,
					SB_CHOICE_GAP );
				if ( !cell.valid )
					continue;

				if (( x >= serverbrowser_ToScreenX( cell.x )) &&
					( x < serverbrowser_ToScreenX( cell.x + cell.width )))
				{
					at = i;
					break;
				}
			}

			g_HostVisHot = at;

			if (( at >= 0 ) && ( type == MOUSE_Release ))
			{
				SetFocus( zx::BrowserFocus::Host );
				g_HostFocus = zx::HostFocusPos( zx::HostSlot::Visibility, 0 );

				// A click is not navigation -- it names a cell and answers in one act, so it does
				// both. The cursor follows so the keyboard carries on from where the pointer left.
				g_HostVisCursor = at;
				PressHostVisibility( );
			}

			return ( at >= 0 );
		}

		// COPY TO NEW, under it. Guarded by HostRowVisible for the reason every control in this
		// scrolling column is: one scrolled out of the viewport must not still be clickable.
		if ( HostCopyOffered( ))
		{
			const int copyY = HostCopyY( );

			if ( HostRowVisible( copyY, HostCopyH( )) &&
				( y >= serverbrowser_ToScreenY( copyY )) &&
				( y < serverbrowser_ToScreenY( copyY + HostCopyH( ))) &&
				( x >= serverbrowser_ToScreenX( SB_HOST_RCOL_LEFT )) &&
				( x < serverbrowser_ToScreenX( SB_HOST_RCOL_RIGHT )))
			{
				g_HostCopyHot = true;

				// On the PRESS, which is what every other control on this panel answers. Written as
				// a release first, and it never fired: the panel is gone by the time one arrives.
				if ( type == MOUSE_Click )
				{
					SetFocus( zx::BrowserFocus::Host );
					g_HostFocus = zx::HostFocusPos( zx::HostSlot::Copy, 0 );
					HostPressCopy( );
				}

				return true;
			}
		}

		return false;
	}

	// [rc4l] The browser's three mouse types in the drag unit's terms.
	zx::PointerEvent PointerEventOf( int type )
	{
		if ( type == MOUSE_Click )
			return zx::PointerEvent::Press;
		if ( type == MOUSE_Release )
			return zx::PointerEvent::Release;
		return zx::PointerEvent::Move;
	}

	// [rc4l] Which character of a host field the pointer is over. The same half-way rule the search
	// box uses -- clicking the left of a glyph puts the caret before it and the right of it after,
	// which is where the eye says it should go.
	size_t HostFieldCharAt( int index, int px )
	{
		if (( index < 0 ) || ( index >= kHostFieldCount ))
			return 0;

		const int textX = SB_HOST_RCOL_LEFT + SB_HOST_RLABEL_W + 5;

		// A masked field is measured on what is DRAWN, not on what is stored: the asterisks are a
		// different width from the characters behind them, and hit-testing the real text would put
		// the caret somewhere the player is not pointing.
		FString shown;
		if ( index == kHostFieldPassword )
		{
			for ( size_t i = 0; i < g_HostFields[index].text.size( ); ++i )
				shown += "*";
		}
		else
			shown = g_HostFields[index].text.c_str( );

		for ( unsigned i = 0; i < shown.Len( ); ++i )
		{
			const int glyphW = SmallFont->StringWidth( shown.Mid( i, 1 ));
			const int leftEdge = textX + SmallFont->StringWidth( shown.Left( i ));

			if ( px < serverbrowser_ToScreenX( leftEdge + glyphW / 2 ))
				return static_cast<size_t>( i );
		}

		return shown.Len( );
	}

	// [rc4l] Where the form's rows land. Worked out once and read by both the drawing and the hit
	// test, because a field that is somewhere other than where it is clickable is the bug that this
	// browser already avoids everywhere else by sharing its geometry.
	//*************************************************************************
	//
	// [rc4l] Keep the scroll inside what there is to scroll. The rule lives in scrollview_compute;
	// this only supplies the measurement, which is the part that moves.
	//
	// Called on every frame rather than only when the scroll changes, because what it is measured
	// against changes underneath it: the form is one row shorter while a server is running.
	void ClampHostScroll( )
	{
		g_HostScroll = zx::ClampScroll( g_HostScroll, HostMaxScroll( ));
	}

	// The same, for the list beside them. It shortens and lengthens as experiences are opened out.
	void ClampHostListScroll( )
	{
		g_HostListScroll = zx::ClampScroll( g_HostListScroll, HostListMaxScroll( ));
	}

	// [rc4l] Bring a row into view, for the keyboard.
	//
	// Arrowing onto a field that is scrolled out of sight would move a focus the player cannot see --
	// the glow would travel to somewhere off the panel and the caret would be somewhere they are not
	// looking. So the view follows the focus, by the least it can.
	void RevealHostRow( int vy, int vh )
	{
		g_HostScroll = zx::ScrollToReveal( g_HostScroll, vy, vh, SB_HOST_VIEW_TOP,
			SB_HOST_VIEW_BOTTOM, HostMaxScroll( ));
	}

	// Whichever row the keyboard is on, brought into view.
	// [rc4l] Bring a catalogue row into view. The LIST has its own scroll, separate from the
	// settings' -- they are two columns that scroll independently -- so it needs its own reveal.
	// Same ScrollToReveal underneath, so both move by the least they can.
	void RevealHostCatalogueRow( int row )
	{
		g_HostListScroll = zx::ScrollToReveal( g_HostListScroll,
			HostCatalogueRowY( row ), SB_HOST_ENTRY_H,
			SB_HOST_VIEW_TOP, SB_HOST_VIEW_BOTTOM, HostListMaxScroll( ));
	}

	void RevealHostFocus( )
	{
		if ( HostOnButton( ) || HostOnToggle( ))
			return;					// pinned to the panel's foot; always visible

		if ( HostOnList( ))
		{
			// The ROW, which is not the entry index any more: the open experience's ways of playing
			// sit between the entries, so scrolling to the entry number would reveal the wrong line.
			const int row = HostSelectedRow( HostListRows( ));
			if ( row >= 0 )
				RevealHostCatalogueRow( row );
			return;
		}

		if ( HostOnVisibility( ))
		{
			RevealHostRow( HostVisibilityY( ), SB_CHOICE_H );
			return;
		}

		// [rc4l] The gameplay panel scrolls in the DETAIL column, not the settings one, so it has its
		// own reveal. The row's y is where it was last drawn, offset included, which is why this can
		// nudge the offset by the shortfall and be right next frame.
		{
			const int row = HostGameplayFocus( );
			if ( row >= 0 )
			{
				const HostGameFocusRow &at = g_HostGameFocusRows[row];

				if ( at.y < SB_HOST_RTOP_TOP )
					g_HostDetailScroll -= ( SB_HOST_RTOP_TOP - at.y );
				else if (( at.y + at.h ) > HostDetailViewBottom( ))
					g_HostDetailScroll += (( at.y + at.h ) - HostDetailViewBottom( ));

				g_HostDetailScroll = zx::ClampScroll( g_HostDetailScroll, HostDetailMaxScroll( ));
				return;
			}
		}

		if ( HostInAField( ))
		{
			RevealHostRow( HostFirstFieldY( ) + HostFieldFocus( ) * HostRowPitch( ),
				SB_HOST_FIELD_H );
		}
	}

	//*************************************************************************
	//
	// [rc4l] The settings' own scrollbar. Drawn only when there is something to scroll, because a
	// full-height thumb on a list that fits is a control that looks live and does nothing.
	// [rc4l] The one control that swaps the right column between what the selection is and how to
	// run it. Drawn as a row rather than a pill so it reads as part of the column instead of
	// competing with START SERVER below it.
	// [rc4l] The line between the two columns: DrawSeparatorSpan turned on its side.
	//
	// Same colour, same peak alpha, same ComputeSeparatorAlpha fade, so it is the horizontal rule
	// rotated rather than a second divider style that would read as a different KIND of boundary.
	// The fade also does the work the old flat line could not: it ends the rule without an edge, so
	// nothing has to decide where a hard stop looks deliberate.
	//
	// Stops short of the buttons at its foot. Running it down to them would box each into its own
	// cell, and they are a pair sitting under the panel rather than two things in two cells.
	// [rc4l] UNUSED since the right column got its own backdrop, which separates the two columns by
	// being a different surface -- a rule down the middle as well said the same thing twice, and its x
	// landed exactly on the backdrop's left edge, so the line appeared to be touching the panel.
	//
	// Kept rather than deleted because the divider is the fallback if the backdrop is ever dropped.
	void DrawHostColumnDivider( )
	{
		const int vx = ( SB_HOST_LIST_RIGHT + SB_HOST_RCOL_LEFT ) / 2;

		const int x = serverbrowser_ToScreenX( vx );
		const int top = serverbrowser_ToScreenY( SB_HOST_VIEW_TOP - 4 );
		const int bottom = serverbrowser_ToScreenY( SB_HOST_BTN_Y - 10 );

		// At least one physical pixel, for the same reason the horizontal rule insists on it: one
		// virtual unit rounds to zero on a small window.
		const int w = MAX( 1, serverbrowser_ToScreenX( vx + 1 ) - x );
		const int h = bottom - top;

		for ( int i = 0; i < h; i++ )
		{
			const int a = zx::ComputeSeparatorAlpha( i, h, 130 );
			if ( a <= 0 )
				continue;

			screen->Dim( PalEntry( 150, 170, 215 ), a / 255.f, x, top + i, w, 1 );
		}
	}

	// [rc4l] Everything the panel would START, as one string to compare against.
	//
	// Written out rather than compared field by field so that adding a setting cannot forget to
	// teach the button about it: a control missing from here is a control you can change while the
	// button goes on saying STOP SERVER.
	//
	// RESOLVED values rather than the raw preferences. A stored -1 means the entry's own default,
	// and two entries with different defaults would otherwise read as the same request.
	FString HostSelectionKey( )
	{
		const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( );

		FString key;
		key.Format( "e%d", g_HostEntrySel );

		if (( g_HostEntrySel < 0 ) || ( g_HostEntrySel >= static_cast<int>( entries.size( ))))
			return key;

		const zx::AddonEntry &addon = entries[g_HostEntrySel].addon;
		const zx::VariantPick chosen = zx::PickVariant( addon, g_HostVariantId.GetChars( ));

		if (( chosen.index >= 0 ) && ( chosen.index < static_cast<int>( addon.variants.size( ))))
			key.AppendFormat( "|v%s", addon.variants[chosen.index].id.c_str( ));

		const std::vector<zx::RemixPick> picks = HostRemixPicks( addon );
		for ( size_t i = 0; i < picks.size( ); ++i )
			key.AppendFormat( "|m%s", picks[i].id.c_str( ));

		const zx::LivesControl lives = HostLivesControl( addon );
		if ( lives.applies )
			key.AppendFormat( "|l%d", lives.value );

		if ( HostFastWeaponsOffered( addon ))
			key.AppendFormat( "|w%d", zx::FastWeaponsValue( g_HostFastWeapons ));

		const zx::TeamsControl teams = HostTeamsControl( addon );
		if ( teams.applies )
			key.AppendFormat( "|t%d", teams.count );

		if ( HostSelectedRotation( ).size( ) > 1 )
			key.AppendFormat( "|s%d", HostStartMapIndex( ));

		return key;
	}

	// [rc4l] Whether what is on screen is what is already being served, so SWITCH does not offer to
	// restart a server onto exactly what it is running.
	//
	// The whole configuration, not just the row. It compared the entry alone, so picking a different
	// way of playing, a different mix, or any of the gameplay settings left the button saying STOP
	// SERVER: there was no way to ask for the thing you had just chosen.
	bool HostSelectionIsWhatIsRunning( )
	{
		if ( g_HostEntrySel != g_HostingEntry )
			return false;

		return HostSelectionKey( ).Compare( g_HostingKey ) == 0;
	}

	// [rc4l] Which of the four the action button is, worked out ONCE.
	//
	// Everything else about that button -- its label, its tooltip, its tint, and what pressing it
	// does -- reads this, so the four can never describe four different buttons. They did: the label
	// said STOP while the press handler was still asking its own question about the same state.
	HostAction HostActionNow( )
	{
		if ( zx::HostCurrentState( ) == zx::HostState::Failed )
			return HostAction::Back;

		// Before the idle test: nothing is running yet, and will not be until the files land, but the
		// button must not go on offering to start something that is already on its way.
		if ( HostDownloadRunning( ))
			return HostAction::Cancel;

		if ( zx::HostIsActive( ) == false )
			return HostAction::Play;

		return HostSelectionIsWhatIsRunning( ) ? HostAction::Stop : HostAction::Switch;
	}

	// The settings toggle is beside the action only while there are settings to reach. While a server
	// is running the right column is showing what it is doing, and the form behind that button is not
	// the thing to offer.
	bool HostFootHasToggle( )
	{
		const HostAction action = HostActionNow( );
		return ( action == HostAction::Play );
	}

	int HostActionW( )
	{
		return HostFootHasToggle( ) ? SB_HOST_FOOT_HALF : SB_HOST_FOOT_W;
	}

	const char *HostActionLabel( )
	{
		switch ( HostActionNow( ))
		{
		case HostAction::Cancel:	return "CANCEL";
		case HostAction::Stop:		return "STOP SERVER";
		case HostAction::Switch:	return "SWITCH TO THIS";
		case HostAction::Back:		return "BACK";
		default:					return "PLAY NOW!";
		}
	}

	const char *HostActionTip( )
	{
		switch ( HostActionNow( ))
		{
		case HostAction::Cancel:
			return "Stop downloading what this needs\nYou will be asked to confirm";
		case HostAction::Stop:
			return "Shut the server down\nAnyone playing on it is disconnected";
		case HostAction::Switch:
			return "Stop this server and start the selected one instead\n"
				"Anyone playing is disconnected";
		case HostAction::Back:
			return "Go back to the form";
		default:
			return "Start the server and join it\nAnything missing is downloaded first";
		}
	}

	// [rc4l] The visibility row, answered. The arrows moved the cursor; this is what takes it.
	void PressHostVisibility( )
	{
		const bool bWanted = ( g_HostVisCursor == kHostVisGlobal );

		if ( bWanted != g_HostAdvertise )
		{
			g_HostAdvertise = bWanted;
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
		}
	}

	// [rc4l] The settings toggle, pressed. Its own function because the keyboard can reach it now,
	// so opening the settings is no longer something only the mouse path knows how to do.
	void PressHostSettingsToggle( )
	{
		g_HostShowSettings = !g_HostShowSettings;
		ClampHostFocus( );
		S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
	}

	// [rc4l] Whatever the action button means right now, done.
	void PressHostAction( )
	{
		const HostAction action = HostActionNow( );

		if ( action == HostAction::Cancel )
		{
			// [rc4l] The join's question, word for word, because it is the same question. HELD while
			// it is up: a transfer that finished one frame into this prompt would otherwise start the
			// server underneath it, and the answer would land on something already decided.
			zx::HoldJoinResume( );
			ShowDialog( DialogAction::CancelDownload, "Cancel?",
				"The download stops. What has arrived so far is kept.",
				"YES", 'y', "NO", 'n' );
			return;
		}

		if ( action == HostAction::Switch )
		{
			PressHostSwitchButton( );
			return;
		}

		// Play, Stop and Back are all PressHostButton's business, and it asks the same question of
		// the same state to tell them apart.
		PressHostButton( );
	}

	// [rc4l] The panel's foot: the action, and the settings toggle beside it when there is one.
	//
	// ONE function draws both, from the same constants the hit test reads. The last time these were
	// worked out separately STOP ended up drawn in one place and clickable in another, which is to
	// say not clickable at all.
	void DrawHostFootButtons( )
	{
		const HostAction action = HostActionNow( );
		const int actW = HostActionW( );

		// Tinted like CANCEL whenever it ENDS something that is running -- which for the CANCEL face
		// is not a resemblance, it IS that button. BACK dismisses a failure and PLAY NOW! starts
		// something, so neither is.
		const bool bWarn = ( action == HostAction::Stop ) || ( action == HostAction::Switch )
			|| ( action == HostAction::Cancel );

		DrawRoundedButton( SB_HOST_FOOT_LEFT, SB_HOST_RTOGGLE_Y, actW, SB_HOST_RTOGGLE_H,
			HostActionLabel( ), HostOnButton( ) || g_HostButtonHot,
			bWarn ? ButtonTint::Warn : ButtonTint::Neutral );

		if (( g_Focus == zx::BrowserFocus::Host ) && HostOnButton( ))
		{
			FocusAnchor( zx::BrowserFocus::Host, SB_HOST_FOOT_LEFT - 5,
				SB_HOST_RTOGGLE_Y + SB_HOST_RTOGGLE_H / 2 );
		}

		serverbrowser_Tip( SB_HOST_FOOT_LEFT, SB_HOST_RTOGGLE_Y, actW, SB_HOST_RTOGGLE_H,
			HostActionTip( ));

		if ( !HostFootHasToggle( ))
			return;

		// [rc4l] DrawRoundedButton, which IS the JOIN and START drawing. A button that merely
		// resembled them would drift apart from them the first time either was touched.
		DrawRoundedButton( SB_HOST_TOGGLE_X, SB_HOST_RTOGGLE_Y, SB_HOST_FOOT_HALF, SB_HOST_RTOGGLE_H,
			g_HostShowSettings ? "BACK" : "SETTINGS", HostOnToggle( ) || g_HostOnSettingsToggle );

		// [rc4l] And the glow, which it never had -- it had no keyboard to mark until the toggle
		// became a focus slot, so arrowing onto it lit nothing and the marker stayed on the button
		// beside it.
		if ( HostOnToggle( ))
		{
			FocusAnchor( zx::BrowserFocus::Host, SB_HOST_TOGGLE_X - 5,
				SB_HOST_RTOGGLE_Y + SB_HOST_RTOGGLE_H / 2 );
		}

		serverbrowser_Tip( SB_HOST_TOGGLE_X, SB_HOST_RTOGGLE_Y, SB_HOST_FOOT_HALF, SB_HOST_RTOGGLE_H,
			g_HostShowSettings
				? "Back to what the selected experience is"
				: "Name, port, player limit and who can see it" );
	}

	void DrawHostScrollBar( )
	{
		if ( !g_HostShowSettings )
			return;
		if ( HostMaxScroll( ) <= 0 )
			return;

		const int trackTop = serverbrowser_ToScreenY( SB_HOST_RBOT_TOP );
		const int trackBottom = serverbrowser_ToScreenY( SB_HOST_RBOT_BOTTOM );
		const int trackH = trackBottom - trackTop;
		if ( trackH <= 0 )
			return;

		const int x = serverbrowser_ToScreenX( SB_HOST_BAR_X );
		const int w = MAX( 1, serverbrowser_ToScreenX( SB_HOST_BAR_X + SB_HOST_BAR_W ) - x );

		// Same arithmetic as the server list's bar -- one unit, so the two behave identically.
		const int thumbH = zx::ComputeThumbHeight( trackH, SB_HOST_RBOT_H, HostContentH( ), 8 );
		const int thumbTop = zx::ComputeThumbTop( trackH, thumbH, g_HostScroll, HostMaxScroll( ));

		screen->Dim( PalEntry( 40, 42, 58 ), 0.55f, x, trackTop, w, trackH );
		screen->Dim( PalEntry( 150, 155, 180 ), 0.9f, x, trackTop + thumbTop, w, thumbH );
	}
	void DrawHostRegionScrollBar( int viewTop, int viewBottom, int contentH, int scroll,
		int barX = SB_HOST_BAR_X )
	{
		const int viewH = viewBottom - viewTop;
		if (( viewH <= 0 ) || ( contentH <= viewH ))
			return;

		const int trackTop = serverbrowser_ToScreenY( viewTop );
		const int trackH = serverbrowser_ToScreenY( viewBottom ) - trackTop;
		if ( trackH <= 0 )
			return;

		const int x = serverbrowser_ToScreenX( barX );
		const int w = MAX( 1, serverbrowser_ToScreenX( barX + SB_HOST_BAR_W ) - x );

		const int thumbH = zx::ComputeThumbHeight( trackH, viewH, contentH, 8 );
		const int thumbTop = zx::ComputeThumbTop( trackH, thumbH, scroll, contentH - viewH );

		screen->Dim( PalEntry( 40, 42, 58 ), 0.55f, x, trackTop, w, trackH );
		screen->Dim( PalEntry( 150, 155, 180 ), 0.9f, x, trackTop + thumbTop, w, thumbH );
	}

	// [rc4l] A click or drag on one of those bars, mapped to a scroll position.
	//
	// The DRAWING of these two shared the compute helpers from the start; the INTERACTION was never
	// written at all, so the thumbs looked exactly like the server list's and did nothing when
	// grabbed. Only the wheel moved them.
	//
	// Every number here comes from the same expressions DrawHostRegionScrollBar uses, for the reason
	// scrollbar_compute exists: a bar whose hit test works out its own thumb height disagrees with
	// the one on screen, and the error grows with the thumb.
	//
	// The compute unit talks about rows and this scrolls by pixels, which costs nothing -- the
	// mapping is linear and unit-agnostic, so "first row" is "pixels down" with no conversion.
	bool HostRegionBarDrag( int viewTop, int viewBottom, int contentH, int maxScroll,
		int x, int y, int &scroll, int barX = SB_HOST_BAR_X )
	{
		const int viewH = viewBottom - viewTop;
		if (( viewH <= 0 ) || ( contentH <= viewH ) || ( maxScroll <= 0 ))
			return false;

		const int trackTop = serverbrowser_ToScreenY( viewTop );
		const int trackH = serverbrowser_ToScreenY( viewBottom ) - trackTop;
		if ( trackH <= 0 )
			return false;

		// Wider than the bar is drawn, by the same few pixels the WAD list's bar allows. A two-pixel
		// target is one nobody hits on the first try.
		if (( x < serverbrowser_ToScreenX( barX - 3 )) ||
			( x >= serverbrowser_ToScreenX( barX + SB_HOST_BAR_W + 3 )))
		{
			return false;
		}

		if (( y < trackTop ) || ( y >= trackTop + trackH ))
			return false;

		const int thumbH = zx::ComputeThumbHeight( trackH, viewH, contentH, 8 );
		scroll = zx::ComputeFirstFromPointer( y - trackTop, trackH, thumbH, maxScroll );
		return true;
	}

	// [rc4l] A bar's WHOLE mouse life in one function: the press that grabs it, the moves that drag
	// it, the release that lets go.
	//
	// Only the press was shared before this. Each list then wrote its own thirty lines for the other
	// two, so every new list started with a thumb that drew correctly and did nothing when pulled --
	// three times running, the flags box being the third. The state that made it copyable is the
	// `dragging` bool, so that is passed in rather than being a global each caller tests for itself.
	//
	// Scroll units are the caller's own: the mapping from pointer to scroll is linear, so a list that
	// counts rows and one that counts pixels both hand over their own maxScroll and get it back in
	// the same currency. `contentH` and the view are in virtual pixels because that is what decides
	// the size of the thumb, and the thumb has to be the one on screen -- a hit test that works out
	// its own disagrees with the drawing, by more the bigger the thumb gets.
	bool RegionBarMouse( int type, int x, int y, int viewTop, int viewBottom, int contentH,
		int maxScroll, int &scroll, bool &dragging, int barX = SB_HOST_BAR_X )
	{
		if ( type == MOUSE_Click )
		{
			if ( HostRegionBarDrag( viewTop, viewBottom, contentH, maxScroll, x, y, scroll, barX ))
			{
				dragging = true;
				return true;
			}

			return false;
		}

		if ( !dragging )
			return false;

		if ( type == MOUSE_Release )
		{
			dragging = false;
			return true;
		}

		// The grab is kept without re-testing the pointer: sliding sideways off a six-pixel bar
		// mid-drag is how everybody uses one, and letting go there would be the bar's fault.
		const int viewH = viewBottom - viewTop;
		const int trackTop = serverbrowser_ToScreenY( viewTop );
		const int trackH = serverbrowser_ToScreenY( viewBottom ) - trackTop;

		if (( trackH > 0 ) && ( maxScroll > 0 ) && ( contentH > viewH ))
		{
			const int thumbH = zx::ComputeThumbHeight( trackH, viewH, contentH, 8 );
			scroll = zx::ComputeFirstFromPointer( y - trackTop, trackH, thumbH, maxScroll );
		}

		return true;
	}

	// [rc4l] The experience list's bar, which unlike the two below is there whether a server is
	// running or not -- the list is drawn in both faces of the panel.
	//
	// Answered before the rows, because the bar sits in the gap beside them and a click that scrolled
	// the list and also picked whatever row happened to be under it would be picking at random.
	bool HostListBarMouseEvent( int type, int x, int y )
	{
		return RegionBarMouse( type, x, y, SB_HOST_VIEW_TOP, SB_HOST_VIEW_BOTTOM, HostCatalogueH( ),
			HostListMaxScroll( ), g_HostListScroll, g_DraggingHostListBar, SB_HOST_LBAR_X );
	}

	// [rc4l] The two running-panel bars, answered in the order they are drawn. Returns true when one
	// of them took the event, so the panel underneath does not also act on it.
	bool HostRegionBarsMouseEvent( int type, int x, int y )
	{
		// [rc4l] The detail column has a bar whether or not a server is running. This used to bail
		// outright unless one was, so the bar drawn beside the experience panel could be looked at
		// and not grabbed.
		if ( !zx::HostIsActive( ) && !g_HostShowSettings )
		{
			return RegionBarMouse( type, x, y, SB_HOST_RTOP_TOP, HostDetailViewBottom( ),
				HostDetailH( ), HostDetailMaxScroll( ), g_HostDetailScroll, g_DraggingHostDetailBar );
		}

		// The detail bar is asked first because it is drawn first. A held drag stays with whichever
		// one took the press, which is the shared helper's own rule rather than one written here.
		if ( RegionBarMouse( type, x, y, SB_HOST_RTOP_TOP, SB_HOST_RUN_TOP_BOT, HostDetailH( ),
			HostDetailMaxScroll( ), g_HostDetailScroll, g_DraggingHostDetailBar ))
		{
			return true;
		}

		return RegionBarMouse( type, x, y, SB_HOST_RUN_BOT_TOP, SB_HOST_RTOP_BOTTOM, g_HostStatusH,
			HostStatusMaxScroll( ), g_HostStatusScroll, g_DraggingHostStatusBar );
	}

	// [rc4l] The port as typed, falling back to the default. Read by the reachability check before
	// anything is running, which is a question about one specific port and must follow the field as
	// it is edited. Once a server exists, PortToCheck prefers the port it actually holds.
	int HostConfiguredPort( )
	{
		const int typed = atoi( g_HostFields[kHostFieldPort].text.c_str( ));
		return (( typed > 0 ) && ( typed <= 65535 )) ? typed : 10666;
	}

	// One row's pitch, so the count below and the loops above cannot drift apart.
	int HostRowPitch( )
	{
		return SB_HOST_ROW_H + SB_HOST_FIELD_H - 4;
	}

	// How tall the settings are, viewport or no viewport. What decides whether they scroll.
	int HostContentH( )
	{
		int h = kHostFieldCount * HostRowPitch( ) + 4 + SB_HOST_LINE + SB_CHOICE_H;

		// [rc4l] COPY TO NEW counts toward the scroll only when it is there. A height that always
		// allowed for it would leave the column scrolling past its own bottom on every experience
		// that is missing a file, which is the sort of empty gap nobody can explain later.
		if ( HostCopyOffered( ))
			h += SB_HOST_LINE + 8 + HostCopyH( );

		return h;
	}

	int HostMaxScroll( )
	{
		const int over = HostContentH( ) - ( HostRightBottom( ) - SB_HOST_RBOT_TOP );
		return ( over > 0 ) ? over : 0;
	}

	int HostDetailMaxScroll( )
	{
		const int over = HostDetailH( ) - HostDetailViewH( );
		return ( over > 0 ) ? over : 0;
	}

	// [rc4l] The status half. Its height is measured while drawing rather than derived, because the
	// text is wrapped and how many lines it becomes depends on the wrap width and on what the server
	// is currently doing. Last frame's measurement is what the scrollbar and the clamp read.
	int HostStatusMaxScroll( )
	{
		const int over = g_HostStatusH - SB_HOST_RUN_BOT_H;
		return ( over > 0 ) ? over : 0;
	}

	// [rc4l] A different experience is a different panel, so it starts at the top.
	//
	// The offset used to survive the selection changing, which is wrong twice over: you are reading
	// something you did not scroll, and on an entry with a shorter panel the whole thing sits above
	// the viewport with nothing drawn at all. Nothing drawn means nothing RECORDED either, so the
	// gameplay pills were not merely invisible, they were unclickable -- which is how this surfaced.
	void HostSelectionChanged( )
	{
		g_HostDetailScroll = 0;
	}

	void ClampHostDetailScroll( )
	{
		const int maxScroll = HostDetailMaxScroll( );
		if ( g_HostDetailScroll > maxScroll )
			g_HostDetailScroll = maxScroll;
		if ( g_HostDetailScroll < 0 )
			g_HostDetailScroll = 0;
	}

	void ClampHostStatusScroll( )
	{
		const int maxScroll = HostStatusMaxScroll( );
		if ( g_HostStatusScroll > maxScroll )
			g_HostStatusScroll = maxScroll;
		if ( g_HostStatusScroll < 0 )
			g_HostStatusScroll = 0;
	}

	// [rc4l] Where the rows land ON SCREEN -- content position less the scroll. Both the drawing and
	// the hit test read these, so a row can never be somewhere other than where it is clickable, and
	// scrolling cannot separate the two.
	// [rc4l] The catalogue list, and everything below it. One anchor, so a row drawn somewhere other
	// than where it is clickable stays impossible -- the same rule the fields already follow.
	// [rc4l] The list as ROWS, which is no longer one per entry: the open entry's ways of playing hang
	// under it. Rebuilt each time it is asked for rather than cached, because the catalogue can be
	// re-read and a stale row list is one that describes one experience while the button starts
	// another.
	std::vector<zx::HostListRow> HostListRows( )
	{
		const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( );

		std::vector<int> counts;
		counts.reserve( entries.size( ));
		for ( size_t i = 0; i < entries.size( ); ++i )
			counts.push_back( static_cast<int>( entries[i].addon.variants.size( )));

		return zx::BuildHostListRows( counts, g_HostOpenEntries );
	}

	int HostCatalogueRowCount( )
	{
		return static_cast<int>( HostListRows( ).size( ));
	}

	// Where the cursor is, derived from what is CHOSEN rather than stored beside it. A stored row
	// index would have to be corrected every time the list changed shape -- opening an entry, the
	// catalogue being re-read -- and the correction that gets missed is the one that starts the wrong
	// experience.
	//
	// The one thing the choice cannot say is whether the cursor is on an OPEN experience's own row or
	// on the way of playing it defaults to, because both are the same choice. That bit is kept beside
	// it; see g_HostOnEntryRow.
	int HostSelectedRow( const std::vector<zx::HostListRow> &rows )
	{
		const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( );

		int variant = -1;
		if ( !g_HostOnEntryRow &&
			( g_HostEntrySel >= 0 ) && ( g_HostEntrySel < static_cast<int>( entries.size( ))))
		{
			const zx::VariantPick pick = zx::PickVariant( entries[g_HostEntrySel].addon,
				g_HostVariantId.GetChars( ));
			variant = pick.index;
		}

		return zx::FindHostListRow( rows, g_HostEntrySel, variant );
	}

	int HostCatalogueY( )
	{
		return SB_HOST_VIEW_TOP - g_HostListScroll;
	}

	int HostCatalogueH( )
	{
		return HostCatalogueRowCount( ) * SB_HOST_ENTRY_H + 8;
	}

	// How far the experience list can scroll. Zero when every row already fits, which is the usual
	// case with a handful of entries.
	int HostListMaxScroll( )
	{
		const int over = HostCatalogueH( ) - ( SB_HOST_VIEW_BOTTOM - SB_HOST_VIEW_TOP );
		return ( over > 0 ) ? over : 0;
	}

	// The y of one catalogue row. The selection value and the row index are the same number, so
	// the two cannot drift apart.
	int HostCatalogueRowY( int row )
	{
		// [rc4l] No heading offset: EXPERIENCES is gone and the rows begin where it was. A label over
		// three obvious rows was a line spent saying what the rows already said.
		return HostCatalogueY( ) + row * SB_HOST_ENTRY_H;
	}

	// The settings live in the right column now, so they start at the top of the viewport rather than
	// below the list. They keep their own scroll for the case where the panel is short.
	int HostFirstFieldY( )
	{
		return SB_HOST_RBOT_TOP - g_HostScroll;
	}

	int HostVisibilityY( )
	{
		return HostFirstFieldY( ) + kHostFieldCount * HostRowPitch( ) + 4 + SB_HOST_LINE;
	}

	// [rc4l] COPY TO NEW, on its own line under the visibility row. A gap of a row above it, because
	// it is not another server setting -- it leaves this screen.
	int HostCopyY( )	{ return HostVisibilityY( ) + SB_CHOICE_H + SB_HOST_LINE + 8; }
	int HostCopyH( )	{ return SB_HOST_RTOGGLE_H; }

	// [rc4l] Whether every file the chosen way of playing loads is already on this machine.
	//
	// By NAME, through the sizes the panel has already measured, so this costs nothing per frame --
	// see HostEntryFileSizes for why that cache exists and what "by name" does and does not promise.
	// A copy is not a launch: it fills in the NEW screen, whose own list is names on disk, so a name
	// is the right question here. The stricter by-hash check stays where it belongs, on the button
	// that actually starts a server.
	bool HostFilesAllPresent( const zx::AddonEntry &addon )
	{
		const std::vector<zx::AddonFileRef> loads = HostSelectedFiles( addon );
		if ( loads.empty( ))
			return false;

		const std::vector<unsigned long long> &sizes = HostEntryFileSizes( g_HostEntrySel,
			g_HostVariantId.GetChars( ), loads );

		if ( sizes.size( ) != loads.size( ))
			return false;

		for ( size_t i = 0; i < sizes.size( ); ++i )
		{
			if ( sizes[i] == 0 )
				return false;
		}

		// And something to run them on, RESOLVED THE WAY HOSTING WOULD RESOLVE IT.
		//
		// Not "AvailableIwads is not empty", which was the first version of this line and was wrong:
		// that list carries SUBSTITUTES, so it is non-empty whenever the machine has any IWAD at all.
		// Mega Man 8-bit Deathmatch wants megagame.wad; with that file gone the list still came back
		// full, the button was still offered, and the copy landed on the NEW screen with doom2.wad
		// selected and no word said. Found by hiding the file and pressing it.
		return HostCopyIwad( addon ).empty( ) == false;
	}

	// What COPY would put in the IWAD box: the entry's own, or the substitute hosting would fall back
	// to, or "" when the NEW screen cannot select either. Asked by the offer and by the press, so the
	// button cannot promise an IWAD the copy then fails to select.
	//
	// [rc4l] AND IT MUST BE ONE THAT SCREEN ACTUALLY OFFERS, which is a shorter list than "IWADs that
	// exist": NewIwads is built from KnownIwadNames, a fixed set of the IWADs a player might own.
	// Mega Man 8-bit Deathmatch runs on megagame.wad and falls back to fuamega.wad, and neither is in
	// it -- so the box could not be moved to either, the copy silently kept whatever was selected
	// before, and a Mega Man setup arrived on the NEW tab reading doom2.wad. Found by hiding
	// megagame.wad and watching the copy land on the wrong game.
	//
	// Not offering the button is the right answer rather than copying anyway. The NEW screen builds
	// servers out of the files this machine has, and an experience it cannot express is one it cannot
	// build.
	std::string HostCopyIwad( const zx::AddonEntry &addon )
	{
		const zx::IwadPick pick = zx::PickIwad( addon.iwad, zx::AvailableIwads( addon.iwad ));
		if ( pick.iwad.empty( ))
			return std::string( );

		const std::vector<std::string> &offered = NewIwads( );
		for ( size_t i = 0; i < offered.size( ); ++i )
		{
			if ( stricmp( offered[i].c_str( ), pick.iwad.c_str( )) == 0 )
				return pick.iwad;
		}

		return std::string( );
	}

	// Whether the button is on screen at all: the settings face, no server running, an experience
	// chosen, and every file for it already here.
	bool HostCopyOffered( )
	{
		if ( !g_HostShowSettings || zx::HostIsActive( ))
			return false;

		const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( );
		if (( g_HostEntrySel < 0 ) || ( g_HostEntrySel >= static_cast<int>( entries.size( ))))
			return false;

		return HostFilesAllPresent( entries[g_HostEntrySel].addon );
	}

	// [rc4l] Both buttons now sit at the foot of the RIGHT column, in one row, drawn and hit-tested
	// from the same SB_HOST_FOOT_* constants. See DrawHostFootButtons.

	// Whether a row at `vy` is inside the viewport at all. A control scrolled out of sight must not
	// be clickable -- that is the invisible-but-clickable bug this browser avoids everywhere else.
	bool HostRowVisible( int vy, int vh )
	{
		return zx::RowIntersectsView( vy, vh, SB_HOST_VIEW_TOP, SB_HOST_VIEW_BOTTOM );
	}

	// [rc4l] Whether a row is ENTIRELY inside the viewport, which is what decides whether its text is
	// drawn at all. See scrollview_compute.h for why that is a different question from the above.
	bool HostRowFullyVisible( int vy, int vh )
	{
		return zx::RowFullyInView( vy, vh, SB_HOST_VIEW_TOP, SB_HOST_VIEW_BOTTOM );
	}

	// [rc4l] Whether the selected entry has any gameplay setting to show, which is what decides
	// whether the panel exists and so whether the file list is capped.
	//
	// Most entries have none: a pack bringing its own weapons and classes has nowhere to put someone
	// else's, and a heading over an empty group on every one of those is the wasted space this is
	// supposed to save. Those entries get the whole column for their files instead.
	// [rc4l] The label column the gameplay panel lines every control up in, measured ONCE.
	//
	// Two callers need the identical number -- the drawing and the height that gives the panel its
	// scrollbar -- and they had a copy each. The copies had already drifted: only LIVES was measured,
	// so WEAPONS, which is wider, drew its label straight through its own minus button.
	int HostGameplayLabelW( const std::vector<zx::RemixGroup> &groups )
	{
		// WEAPON SPEED is deliberately absent: it draws its label above its track, so sizing this
		// column to it would push the other controls across the panel to line up with nothing.
		static const char *const kBuiltIn[3] = { "LIVES", "TEAMS", "FIRST MAP" };

		int labelW = 0;
		for ( int i = 0; i < 3; ++i )
			labelW = MAX( labelW, SmallFont->StringWidth( kBuiltIn[i] ));

		for ( size_t g = 0; g < groups.size( ); ++g )
		{
			if (( groups[g].choices.size( ) <= 1 ) || groups[g].id.empty( ))
				continue;

			FString label = groups[g].id.c_str( );
			label.ToUpper( );
			labelW = MAX( labelW, SmallFont->StringWidth( label ));
		}

		return labelW + SmallFont->StringWidth( "  " );
	}

	// [rc4l] Where an axis of PILLS starts, which is deliberately not the shared label column.
	//
	// The sliders line up under each other because they are one control repeated and a ragged left
	// edge on three of those reads as a mistake. Pills are not that: an axis of them wraps over as
	// many rows as it needs, and every pixel the shared column reserves is taken off ALL of them.
	// Mix already runs to four rows and the catalogue keeps gaining mods; indenting it to clear the
	// word FIRST MAP costs a row or two for an alignment nothing lines up with anyway.
	//
	// So pills sit against their own label and the sliders keep the column. Rule broken once, where
	// it pays, and the reason written down so the next axis does not copy it blindly.
	int HostPillLeft( int x, const std::string &groupId )
	{
		if ( groupId.empty( ))
			return x;

		FString label = groupId.c_str( );
		label.ToUpper( );

		return x + SmallFont->StringWidth( label ) + SmallFont->StringWidth( "  " );
	}

	// [rc4l] Where an axis's pills sit, worked out ONCE.
	//
	// Three callers need the identical answer -- the draw, the height that gives the panel its
	// scrollbar, and the keyboard that walks the grid -- and each had its own copy of the widths and
	// the wrap. Three arithmetics for one layout is how a marker lands where a pill is not.
	struct HostPillGeom
	{
		std::vector<int>	widths;
		zx::WadListLayout	layout;
		int					gap;
		int					left;
	};

	HostPillGeom HostPillGeometry( int x, const zx::RemixGroup &group )
	{
		HostPillGeom out;

		// Room for the dot and the gaps either side of it, plus the trailing gap after the label.
		const int pad = SB_HOST_PILL_DOT * 2 + 3 + SmallFont->StringWidth( " " );

		out.gap = 4;
		out.left = HostPillLeft( x, group.id );

		out.widths.reserve( group.choices.size( ));
		for ( size_t i = 0; i < group.choices.size( ); ++i )
			out.widths.push_back( SmallFont->StringWidth( group.choices[i].name.c_str( )) + pad );

		out.layout = zx::LayoutWadList( out.widths, out.gap, 0,
			SB_HOST_RCOL_RIGHT - out.left, 0 );

		return out;
	}

	bool HostHasGameplayRow( )
	{
		const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( );

		if (( g_HostEntrySel < 0 ) || ( g_HostEntrySel >= static_cast<int>( entries.size( ))))
			return false;

		// [rc4l] Any AXIS with something to decide, not any remix at all. An entry offering one mod
		// and nothing else has a row that cannot change, which is not a setting and must not cost the
		// file list three lines to display.
		if ( HostLivesControl( entries[g_HostEntrySel].addon ).adjustable ||
			HostFastWeaponsOffered( entries[g_HostEntrySel].addon ) ||
			HostTeamsControl( entries[g_HostEntrySel].addon ).adjustable ||
			( HostSelectedRotation( ).size( ) > 1 ))
		{
			return true;
		}

		const std::vector<zx::RemixGroup> groups =
			zx::GroupRemixes( HostOfferedRemixes( entries[g_HostEntrySel].addon ));

		for ( size_t g = 0; g < groups.size( ); ++g )
		{
			if ( groups[g].choices.size( ) > 1 )
				return true;
		}

		return false;
	}

	// Where the RIGHT column's content has to stop. The gameplay settings scroll WITH the details now
	// rather than sitting in a fixed row beneath them, so the region runs the full height.
	int HostRightBottom( )
	{
		return SB_HOST_VIEW_BOTTOM;
	}

	// [rc4l] Where the detail region stops, which is not a constant: while a server is running it
	// gives up its lower half to the status, so the details scroll inside what is left rather than
	// running underneath it.
	int HostDetailViewBottom( )
	{
		return zx::HostIsActive( ) ? SB_HOST_RUN_TOP_BOT : HostRightBottom( );
	}

	int HostDetailViewH( )
	{
		return HostDetailViewBottom( ) - SB_HOST_RTOP_TOP;
	}

	// The detail region clips to its own box, so a file list longer than the box stops at the rule
	// instead of running into the form below it.
	bool HostDetailRowVisible( int vy, int vh )
	{
		return zx::RowFullyInView( vy, vh, SB_HOST_RTOP_TOP, HostDetailViewBottom( ));
	}

	//*************************************************************************
	//
	// [rc4l] A row of mutually exclusive choices, drawn the way the rest of the browser draws things.
	//
	// Reusable on purpose: two options here, and a game mode picker is three or four with nothing
	// changed but the array. The geometry and the hit test both come from choicerow_compute, so a
	// cell can never be somewhere other than where it is clickable.
	//
	// Each option is a rounded cell -- the same DrawRoundedPanel every other surface uses -- with a
	// filled dot on the chosen one. The dot rather than colour alone, because a row where the only
	// difference is brightness is a row somebody with a dim screen cannot read.
	// [rc4l] `cellColors` overrides the label colour per cell, or NULL for the usual chosen/unchosen
	// pair. It exists so a cell can say something about ITSELF -- that this option is not currently
	// available -- without that meaning having to be smuggled into the label text.
	// [rc4l] `hot` is the cell under the POINTER; `cursor` is the cell the KEYBOARD is on, or -1 when
	// the keyboard is elsewhere. Both light a cell, because both mean "this is the one you are about
	// to act on" and the row cannot tell which hand the player is using.
	void DrawChoiceRow( int vx, int vy, int vw, int count, const char *const *labels, int selected,
		int hot, int cursor, const EColorRange *cellColors = NULL )
	{
		selected = zx::ChoiceNormalise( selected, count );

		for ( int i = 0; i < count; ++i )
		{
			const zx::ChoiceCell cell = zx::ChoiceCellAt( i, count, vx, vw, SB_CHOICE_GAP );
			if ( !cell.valid )
				continue;

			const bool bChosen = ( i == selected );

			// [rc4l] The CURSOR lights, not the chosen cell.
			//
			// This was `bChosen && bFocused`, which was the same thing back when the arrows changed
			// the answer as they moved -- there was no cursor to be anywhere else. Now that they do
			// not, arrowing onto the cell you have NOT chosen lit nothing at all: the glow moved and
			// the cell under it stayed dark, so the pointer got a highlight the keyboard never did.
			const bool bLit = ( i == hot ) || ( i == cursor );

			// The chosen one sits higher than the rest, the same lift the tabs and buttons use for
			// the same reason: what is true here should be answerable by looking.
			const int base = bChosen ? ( bLit ? 62 : 48 ) : ( bLit ? 30 : 20 );
			const zx::PanelColor topCol = { static_cast<BYTE>( base ), static_cast<BYTE>( base ),
				static_cast<BYTE>( base + 12 ), 225 };
			const zx::PanelColor botCol = { static_cast<BYTE>( base / 2 ), static_cast<BYTE>( base / 2 ),
				static_cast<BYTE>( base / 2 + 8 ), 235 };

			DrawRoundedPanel( cell.x, vy, cell.width, SB_CHOICE_H, topCol, botCol, 5 );

			// The marker: a filled ring when chosen, an empty one when not. Drawn at a size measured
			// over several units rather than one, because a radius derived from a single virtual
			// pixel rounds to 1 or 2 depending on the window and the dot changes size as you resize.
			const int dotCx = cell.x + 9;
			const int dotCy = vy + SB_CHOICE_H / 2;
			const int span = serverbrowser_ToScreenX( 100 ) - serverbrowser_ToScreenX( 0 );
			const int outer = MAX( 3, ( span * 4 ) / 100 );
			const int inner = MAX( 1, ( span * 2 ) / 100 );

			const int sx = serverbrowser_ToScreenX( dotCx );
			const int sy = serverbrowser_ToScreenY( dotCy );

			DimClipped( PalEntry( 150, 155, 175 ), 0.9f, sx - outer, sy - outer, outer * 2, outer * 2 );
			if ( bChosen )
				DimClipped( PalEntry( 235, 235, 245 ), 1.0f, sx - inner, sy - inner, inner * 2, inner * 2 );
			else
				DimClipped( PalEntry( 18, 19, 27 ), 1.0f, sx - inner, sy - inner, inner * 2, inner * 2 );

			const int textX = cell.x + 18;
			const int textY = vy + ( SB_CHOICE_H - SmallFont->GetHeight( )) / 2 + 1;

			if ( HostRowFullyVisible( vy, SB_CHOICE_H ))
			{
				const EColorRange color = ( cellColors != NULL ) ? cellColors[i]
					: ( bChosen ? CR_WHITE : CR_GRAY );

				screen->DrawText( SmallFont, color, textX, textY, labels[i],
					DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
			}
		}
	}

	// [rc4l] ONE gameplay pill: the green ones on the MIX row, at a rectangle the caller worked out.
	//
	// Split out of DrawHostRemixAxes so the IWAD chooser can be made of the same thing rather than
	// something that merely resembles it. The colours, the halo and the dot live here, so the two
	// cannot drift apart -- which they already had, silently: the chooser was first built out of
	// DrawChoiceRow's cell, another row of pills entirely, and came out grey.
	//
	// `bLocked` is the held state the axis uses when a choice is unavailable. The chooser never
	// passes it; it is here because leaving it behind would have left half the control in one place
	// and half in another.
	void DrawGameplayPill( int px, int py, int pw, int ph, const char *label, bool bOn, bool bHot,
		bool bLocked )
	{
		zx::PanelColor top, bot;
		if ( bLocked )
		{
			// [rc4l] Nearly the panel's own colour, and flat. An unpressable thing has to differ in
			// KIND from a pressable one, not in brightness, or it just looks like the one you have
			// not hovered yet.
			top.r = 26; top.g = 27; top.b = 34; top.a = 120;
			bot.r = 22; bot.g = 23; bot.b = 30; bot.a = 120;
		}
		else if ( bOn )
		{
			top.r = 52; top.g = 118; top.b = 66; top.a = 235;
			bot.r = 34; bot.g = 82;  bot.b = 46; bot.a = 235;
		}
		else
		{
			const int lift = bHot ? 28 : 0;
			top.r = 58 + lift; top.g = 62 + lift; top.b = 82 + lift; top.a = 210;
			bot.r = 40 + lift; bot.g = 44 + lift; bot.b = 60 + lift; bot.a = 210;
		}

		DrawRoundedPanel( px, py, pw, ph, top, bot, SB_HOST_PILL_RADIUS );

		// [rc4l] The dot, which is what actually says which pill is on.
		//
		// The fill alone was doing that job and doing it poorly: a filled pill and a hovered pill
		// are both "brighter than the others", so at a glance the pointer looked like the selection.
		// A lit dot is a different KIND of mark, so hover can never impersonate it.
		const int dotY = py + ( ph - SB_HOST_PILL_DOT ) / 2;
		const int dotX = px + SB_HOST_PILL_DOT;

		// No halo while the axis is locked. The glow is what says "this is live", and a locked axis
		// is precisely what is not.
		if ( bOn && !bLocked )
		{
			// A soft ring under it, so the lit state reads as a glow rather than as a slightly
			// different grey. Drawn first and larger, then the dot on top.
			zx::PanelColor halo;
			halo.r = 90; halo.g = 235; halo.b = 120; halo.a = 60;
			DrawRoundedPanel( dotX - 2, dotY - 2, SB_HOST_PILL_DOT + 4, SB_HOST_PILL_DOT + 4,
				halo, halo, ( SB_HOST_PILL_DOT + 4 ) / 2 );
		}

		// [rc4l] Locked keeps the green so the choice is still legible, at a quarter of the light.
		// Held, not lost.
		zx::PanelColor dot;
		if ( bOn && bLocked )	{ dot.r = 54;  dot.g = 96;  dot.b = 64;  dot.a = 200; }
		else if ( bOn )			{ dot.r = 120; dot.g = 255; dot.b = 150; dot.a = 255; }
		else if ( bLocked )		{ dot.r = 46;  dot.g = 48;  dot.b = 58;  dot.a = 200; }
		else					{ dot.r = 96;  dot.g = 102; dot.b = 124; dot.a = 220; }

		DrawRoundedPanel( dotX, dotY, SB_HOST_PILL_DOT, SB_HOST_PILL_DOT, dot, dot,
			SB_HOST_PILL_DOT / 2 );

		const int textX = dotX + SB_HOST_PILL_DOT + 3;

		screen->DrawText( SmallFont, bLocked ? CR_DARKGRAY : ( bOn ? CR_WHITE : CR_GRAY ),
			textX, py + ( ph - SmallFont->GetHeight( )) / 2,
			serverbrowser_FitName( label, ( px + pw ) - textX - 2 ),
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
	}

	// The room one needs: the dot's lane either side, the label, and the gap after it. The same
	// expression HostPillGeometry measures the gameplay row with, so both come out the same size.
	int GameplayPillW( const char *label )
	{
		return SB_HOST_PILL_DOT * 2 + 3 + SmallFont->StringWidth( label ) + 6;
	}

	// =============================================================================================
	//
	// [rc4l] NEW: building a server out of your own files.
	//
	// =============================================================================================

	// The IWADs on this machine, cached for the frame. AvailableIwads probes the disk, so calling it
	// per row would be one stat per IWAD per frame.
	// The IWAD this client is running, by its bare filename.
	FString NewRunningIwad( )
	{
		const char *const path = Wads.GetWadName( FWadCollection::IWAD_FILENUM );
		if ( path == NULL )
			return FString( );

		FString name = path;
		FixPathSeperator( name );

		const long slash = name.LastIndexOf( '/' );
		return ( slash >= 0 ) ? name.Mid( slash + 1 ) : name;
	}

	const std::vector<std::string> &NewIwads( )
	{
		static std::vector<std::string> cached;
		static int lastMs = -100000;
		static int lastEpoch = -1;

		const int now = static_cast<int>( I_MSTime( ));
		if (( now - lastMs > 2000 ) || ( lastEpoch != g_NewIwadEpoch ))
		{
			lastEpoch = g_NewIwadEpoch;

			// [rc4l] Every IWAD on the machine, NOT zx::AvailableIwads.
			//
			// That one probes the SUBSTITUTE table -- the free stand-ins -- plus whatever name a
			// catalogue entry asked for, which is right for "the entry wants doom2, what can it run
			// on" and wrong for "what has this player got". Asked with no preference it can only
			// ever answer Freedoom, so this screen offered three IWADs to somebody running a fourth,
			// and hosting on one of them produced a server its own client could not authenticate
			// against.
			cached.clear( );

			const std::vector<std::string> &known = zx::KnownIwadNames( );
			for ( size_t i = 0; i < known.size( ); ++i )
			{
				if ( zx::FindIwadInEngineSearchPaths( known[i].c_str( )).IsNotEmpty( ) ||
					zx::FindFileInEngineSearchPaths( known[i].c_str( )).IsNotEmpty( ))
				{
					cached.push_back( known[i] );
				}
			}

			// [rc4l] And whatever we are RUNNING, which the table above cannot be relied on to name:
			// a game shipping its own iwad under its own filename is exactly the case the table
			// cannot enumerate, and it is loaded right now, so there is no question that it exists.
			{
				const FString running = NewRunningIwad( );
				if ( running.IsNotEmpty( ))
				{
					bool bHave = false;
					for ( size_t i = 0; i < cached.size( ); ++i )
					{
						if ( running.CompareNoCase( cached[i].c_str( )) == 0 )
						{
							bHave = true;
							break;
						}
					}

					if ( !bHave )
						cached.push_back( running.GetChars( ));
				}
			}

			lastMs = now;

			// [rc4l] The one already loaded is the one selected, until the player says otherwise.
			//
			// Not the first row. Hosting is followed by joining your own server, and a server on a
			// different IWAD from the client that started it fails level authentication the moment
			// it connects -- so the alphabetical default was a working server nobody could join,
			// which reads as the feature being broken rather than as a choice made for you.
			if ( !g_NewIwadChosen )
			{
				const FString running = NewRunningIwad( );
				for ( size_t i = 0; i < cached.size( ); ++i )
				{
					if ( running.CompareNoCase( cached[i].c_str( )) == 0 )
					{
						g_NewIwadSel = static_cast<int>( i );
						break;
					}
				}
			}
		}

		return cached;
	}

	// [rc4l] The wad rows, rebuilt only when something they depend on has changed.
	//
	// Two inputs: what has been typed, and how many files the scan has found. Both are cheap to
	// compare and neither can change without the rows needing to. Everything else on this screen
	// reads the result, so the filter runs once per change rather than once per region per frame.
	const std::vector<zx::LibraryRow> &NewRows( )
	{
		const std::vector<zx::LibraryFile> &files = zx::wadlibrary::Files( );

		const FString key = g_NewSearch.text.c_str( );

		if ( g_NewRowsValid && ( g_NewRowsFiles == files.size( )) && ( g_NewRowsKey.Compare( key ) == 0 ))
			return g_NewRows;

		g_NewRows = zx::BuildLibraryRows( files, zx::SearchFold( g_NewSearch.text ));
		g_NewRowsKey = key;
		g_NewRowsFiles = files.size( );
		g_NewRowsValid = true;

		// A narrower search can leave the cursor past the end of what is left.
		g_NewWadSel = zx::ComputeClampedSelection( g_NewWadSel, static_cast<int>( g_NewRows.size( )));

		return g_NewRows;
	}

	int NewWadRowsVisible( )
	{
		return ( SB_NEW_WADS_BOTTOM - SB_NEW_WADS_TOP ) / SB_NEW_ROW_H;
	}

	// How far the wad list can scroll, in rows. Zero when it fits, which is what turns the bar and
	// the wheel off together rather than one of them at a time.
	int NewWadMaxScroll( )
	{
		return MAX( 0, static_cast<int>( NewRows( ).size( )) - NewWadRowsVisible( ));
	}

	// The wad list's bar, which is the shared one with this list's numbers in it.
	bool NewWadBarMouse( int type, int x, int y )
	{
		return RegionBarMouse( type, x, y, SB_NEW_WADS_TOP, SB_NEW_WADS_BOTTOM,
			static_cast<int>( NewRows( ).size( )) * SB_NEW_ROW_H, NewWadMaxScroll( ),
			g_NewWadScroll, g_DraggingNewWadBar, SB_HOST_LBAR_X );
	}

	int NewOrderRowsVisible( )
	{
		return ( SB_NEW_ORDER_BOTTOM - SB_NEW_ORDER_TOP ) / SB_NEW_ROW_H;
	}

	int NewOrderMaxScroll( )
	{
		return MAX( 0, static_cast<int>( g_NewOrder.size( )) - NewOrderRowsVisible( ));
	}

	// The load order's bar, likewise.
	bool NewOrderBarMouse( int type, int x, int y )
	{
		return RegionBarMouse( type, x, y, SB_NEW_ORDER_TOP, SB_NEW_ORDER_BOTTOM,
			static_cast<int>( g_NewOrder.size( )) * SB_NEW_ROW_H, NewOrderMaxScroll( ),
			g_NewOrderScroll, g_DraggingNewOrderBar, SB_HOST_BAR_X );
	}

	// Keep a selection on screen by moving the VIEW, never the selection. See the server list, which
	// has had this rule since it had a scrollbar.
	void NewClampScroll( int sel, int count, int visible, int &scroll )
	{
		if ( count <= visible )
		{
			scroll = 0;
			return;
		}

		if ( sel < scroll )
			scroll = sel;
		else if ( sel >= scroll + visible )
			scroll = sel - visible + 1;

		if ( scroll > count - visible )
			scroll = count - visible;
		if ( scroll < 0 )
			scroll = 0;
	}

	void NewSay( const char *text )
	{
		g_NewNotice = text;
		g_NewNoticeMs = static_cast<int>( I_MSTime( ));
	}

	// [rc4l] Add the selected file to the load order, and say what happened.
	//
	// This is the ONE place a file is hashed, and it happens here rather than during the scan for
	// the reason the library header gives at length: one hash is milliseconds and twenty thousand
	// is tens of gigabytes.
	void NewAddSelected( )
	{
		const std::vector<zx::LibraryRow> &rows = NewRows( );
		if (( g_NewWadSel < 0 ) || ( g_NewWadSel >= static_cast<int>( rows.size( ))))
			return;

		const std::vector<zx::LibraryFile> &files = zx::wadlibrary::Files( );
		const zx::LibraryFile &file = files[rows[g_NewWadSel].index];

		zx::LoadOrderEntry entry( file.path, file.name, file.size );
		entry.md5 = zx::wadlibrary::HashOf( file );

		const zx::AddResult result = zx::AddToLoadOrder( g_NewOrder, entry );

		switch ( result.verdict )
		{
		case zx::AddVerdict::Added:
			g_NewOrderSel = static_cast<int>( result.index );
			g_NewOrderRevealSel = true;
			NewSay( "Added" );
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
			break;

		case zx::AddVerdict::AlreadyThere:
			g_NewOrderSel = static_cast<int>( result.index );
			NewSay( "That one is already in the list" );
			break;

		case zx::AddVerdict::NameTaken:
			// [rc4l] Said in these words on purpose. The player is looking at two rows they can SEE
			// are different files, and the reason they cannot have both is about the name a joining
			// client is given, which is not visible from here at all.
			g_NewOrderSel = static_cast<int>( result.index );
			NewSay( "A different file of that name is already in the list" );
			break;

		case zx::AddVerdict::Empty:
			break;
		}
	}

	// [rc4l] Start a server out of what has been built here.
	//
	// The IWAD and the files go over as RESOLVED PATHS, never names, for the reason the preset path
	// gives where it does the same: the spawned server searches its own config, not this one, so a
	// name is a second search that can find a different file. Here they came out of a scan that
	// already knows exactly where each one is, so there is nothing to look up twice.
	// ---------------------------------------------------------------------------------------------
	//
	// [rc4l] THE MAP LIST, which is the rotation.
	//
	// Every map in the chosen files, in the order the files give them, and every one of them in by
	// default: a server built out of a map pack is meant to play the map pack. Taking one out is a
	// decision, and putting them all in is not.
	//
	// The files are read where they lie -- see zx_mapscan -- because this client has not loaded any
	// of them, and it is configuring a server rather than playing one.

	// What the list was built from. Rebuilt when this changes and at no other time, so a rotation
	// somebody has just finished arranging survives being looked at again.
	FString NewMapsKey( )
	{
		FString key = NewIwadPath( );

		for ( size_t i = 0; i < g_NewOrder.size( ); ++i )
		{
			key += "|";
			key += g_NewOrder[i].path.c_str( );
		}

		return key;
	}

	void NewRebuildMaps( bool bForce )
	{
		const FString key = NewMapsKey( );

		if ( !bForce && ( g_NewMapsKey.Compare( key ) == 0 ))
			return;

		std::vector<std::string> maps;

		// The IWAD first, then the files in load order: a rotation should read the way the load
		// order does. MergeMaps keeps each name once, so a pwad replacing MAP01 does not add a
		// second visit to it.
		zx::MergeMaps( maps, zx::MapsInPath( NewIwadPath( ).GetChars( )));

		for ( size_t i = 0; i < g_NewOrder.size( ); ++i )
			zx::MergeMaps( maps, zx::MapsInPath( g_NewOrder[i].path ));

		// Everything in, which is what "the rotation of these files" means before anybody says
		// otherwise.
		g_NewMaps.clear( );
		g_NewMaps.reserve( maps.size( ));
		for ( size_t i = 0; i < maps.size( ); ++i )
			g_NewMaps.push_back( NewMapEntry( maps[i] ));

		g_NewMapsKey = key;
		g_NewMapSel = 0;
		g_NewMapScroll = 0;
		g_NewMapBtnSel = 0;
		g_NewMapRevealSel = true;
	}

	// Where the chosen IWAD actually is. Asked by hosting and by the map scan, which must agree
	// about which file they are talking about.
	FString NewIwadPath( )
	{
		const std::vector<std::string> &iwads = NewIwads( );
		if ( iwads.empty( ))
			return "";

		const std::string name = iwads[zx::ComputeClampedSelection( g_NewIwadSel,
			static_cast<int>( iwads.size( )))];

		FString path = zx::FindFileInEngineSearchPaths( name.c_str( ));
		if ( path.IsEmpty( ))
			path = zx::FindIwadInEngineSearchPaths( name.c_str( ));
		if ( path.IsEmpty( ))
			path = name.c_str( );

		return path;
	}

	void NewStartHosting( )
	{
		const std::vector<std::string> &iwads = NewIwads( );
		if ( iwads.empty( ))
		{
			NewSay( "No IWAD to run on" );
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/invalid", snd_menuvolume, ATTN_NONE );
			return;
		}

		const std::string iwadName = iwads[zx::ComputeClampedSelection( g_NewIwadSel,
			static_cast<int>( iwads.size( )))];

		const FString iwadPath = NewIwadPath( );

		zx::HostConfig config;
		config.hostName = ( std::string( "Fua: " ) +
			( g_NewOrder.empty( ) ? iwadName : g_NewOrder[0].name ));
		config.iwad = iwadPath.GetChars( );

		for ( size_t i = 0; i < g_NewOrder.size( ); ++i )
			config.pwads.push_back( g_NewOrder[i].path );

		// [rc4l] WHAT THE CLIENT HAS TO RELOAD ONTO, written down before the server is started.
		//
		// Only the catalogue path used to do this, on the reasoning that a server built any other
		// way was running the client's own files -- true of the hosting FORM, whose server inherited
		// what this client had loaded, and false the moment this screen existed. A server built here
		// runs files chosen from the library; the client goes on running whatever it booted with.
		// Handing the server two PWADs and then connecting without them is a join Zandronum refuses
		// with PROTECTED LUMP AUTHENTICATION FAILED, naming files the player had just picked.
		//
		// The SAME resolved paths the server was given, not the names: see the catalogue path for
		// why a bare name is tested against the working directory and comes back missing.
		g_HostEntryIwad = iwadPath;

		g_HostEntryPwads.Clear( );
		for ( size_t i = 0; i < g_NewOrder.size( ); ++i )
			g_HostEntryPwads.Push( g_NewOrder[i].path.c_str( ));

		config.maxPlayers = 8;
		config.port = 0;
		config.advertise = false;
		config.serveWads = true;

		// [rc4l] Everything the three settings boxes decided, applied after any exec so it wins --
		// which for a hand-built server is everything, there being no cfg to disagree with.
		config.extraCvars = g_NewCvars;

		// [rc4l] The rotation, and the map it starts on, which is the first of it.
		//
		// One decision rather than two: a starting map chosen separately from the list can name
		// something that is not in the rotation, and the server then leaves it after one round
		// never to return. Empty falls back to map01, which is what a wad without a readable map
		// list would have got anyway.
		NewRebuildMaps( false );

		config.mapRotation = NewRotation( );
		config.map = config.mapRotation.empty( ) ? "map01" : config.mapRotation[0];

		// [rc4l] Written down before the server starts, because starting one makes this client
		// reload its files to match -- and anything held only in memory across that is a bet. It
		// survives the game being closed as well, which is what somebody expects from a screen they
		// spent ten minutes filling in.
		zx::CustomSaveLast( NewAsCustomEntry( "" ));

		zx::ReachProbeRelease( );

		if ( zx::HostStart( config ) == false )
		{
			NewSay( "Could not start the server" );
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/invalid", snd_menuvolume, ATTN_NONE );
		}
	}

	void NewMoveSelected( int step )
	{
		if ( g_NewOrder.empty( ))
			return;

		g_NewOrderSel = static_cast<int>(
			zx::MoveInLoadOrder( g_NewOrder, static_cast<size_t>( g_NewOrderSel ), step ));

		// The row being moved must stay visible: it is the one thing the player is watching, and it
		// crosses the edge of the view exactly when they are moving it there.
		g_NewOrderRevealSel = true;
		S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
	}

	void NewRemoveSelected( )
	{
		if ( g_NewOrder.empty( ))
			return;

		g_NewOrderSel = static_cast<int>(
			zx::RemoveFromLoadOrder( g_NewOrder, static_cast<size_t>( g_NewOrderSel )));
		g_NewOrderRevealSel = true;
		S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
	}

	// One row of either list. Returns the row's top edge, so the hit test and the draw agree by
	// construction rather than by both being written from the same numbers.
	int NewRowY( int top, int row, int scroll )
	{
		return top + ( row - scroll ) * SB_NEW_ROW_H;
	}

	// [rc4l] The baseline every row on this screen shares. The +1 is the same nudge the search box
	// and the choice cells use: the font's reported height runs a pixel above where the glyphs
	// actually sit, so centring on it alone leaves every row looking a touch high in its band.
	int NewRowTextY( int rowY )
	{
		return rowY + ( SB_NEW_ROW_H - SmallFont->GetHeight( )) / 2 + 1;
	}

	void DrawNewRowText( int x, int rowY, EColorRange col, const char *text )
	{
		screen->DrawText( SmallFont, col, x, NewRowTextY( rowY ), text,
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true,
			TAG_DONE );
	}

	// Whether a file is already in the load order, by the same path the list would add.
	bool NewIsAdded( const std::string &path )
	{
		for ( size_t i = 0; i < g_NewOrder.size( ); ++i )
		{
			if ( g_NewOrder[i].path == path )
				return true;
		}

		return false;
	}

	void DrawNewRowHighlight( int left, int right, int rowY, bool bSel, bool bHot )
	{
		if ( !bSel && !bHot )
			return;

		const int a = bSel ? 70 : 34;
		screen->Dim( bSel ? 0x5C7CFF : 0xFFFFFF, a / 255.0f,
			serverbrowser_ToScreenX( left ), serverbrowser_ToScreenY( rowY ),
			serverbrowser_ToScreenX( right ) - serverbrowser_ToScreenX( left ),
			serverbrowser_ToScreenY( rowY + SB_NEW_ROW_H ) - serverbrowser_ToScreenY( rowY ));
	}

	// The IWAD chosen, or "" when there are none.
	std::string NewChosenIwad( )
	{
		const std::vector<std::string> &iwads = NewIwads( );
		if ( iwads.empty( ))
			return std::string( );

		return iwads[zx::ComputeClampedSelection( g_NewIwadSel, static_cast<int>( iwads.size( )))];
	}

	// [rc4l] Where a file has to be for the game to find it, said as a PATH the player can go to.
	//
	// The program directory, out of IWADSearch.Directories, resolved rather than printed as
	// $PROGDIR: the point of the line is that somebody can put a file there, and a variable name is
	// not somewhere you can put a file.
	FString NewIwadDropPath( )
	{
		FString dir = progdir;
		FixPathSeperator( dir );

		while (( dir.Len( ) > 0 ) && ( dir[dir.Len( ) - 1] == '/' ))
			dir.Truncate( dir.Len( ) - 1 );

		return dir;
	}

	// Where the button starts: after the label it shares the row with, measured rather than fixed so
	// the two cannot overlap if either changes.
	int NewIwadButtonLeft( )
	{
		return SB_HOST_LIST_LEFT + SmallFont->StringWidth( "IWAD" ) + SB_NEW_IWAD_GAP;
	}

	void DrawNewIwads( )
	{
		const std::vector<std::string> &iwads = NewIwads( );

		// Centred against the button beside it rather than sat at the row's top edge.
		screen->DrawText( SmallFont, CR_GOLD, SB_HOST_LIST_LEFT,
			SB_NEW_IWAD_TOP + ( SB_NEW_IWAD_H - SmallFont->GetHeight( )) / 2, "IWAD",
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );

		const bool bFocused = ( g_NewFocus == NewFocus::Iwads ) &&
			( g_Focus == zx::BrowserFocus::Host );

		const int bx = NewIwadButtonLeft( );

		DrawRoundedButton( bx, SB_NEW_IWAD_TOP, SB_HOST_LIST_RIGHT - bx, SB_NEW_IWAD_H,
			iwads.empty( ) ? "No IWADs found" : NewChosenIwad( ).c_str( ),
			bFocused || ( g_NewIwadHot == 0 ));

		if ( bFocused )
			FocusAnchor( zx::BrowserFocus::Host, bx - 5, SB_NEW_IWAD_TOP + SB_NEW_IWAD_H / 2 );

		serverbrowser_Tip( bx, SB_NEW_IWAD_TOP, SB_HOST_LIST_RIGHT - bx, SB_NEW_IWAD_H,
			"The game this server runs\nOne only, and it loads first" );
	}

	// =============================================================================================
	//
	// [rc4l] The three settings boxes: FLAGS, VARIABLES, GAMEPLAY.
	//
	// They edit one store, g_NewCvars, which is handed to the server as name/value pairs -- the same
	// road the preset panel's gameplay controls already take through HostConfig::extraCvars. So a
	// server built here needs no cfg of its own, and "save this as a preset" later has the whole
	// setting list in one place to write out.
	//
	// =============================================================================================

	const char *NewToolLabel( int i )
	{
		static const char *const kLabels[SB_NEW_TOOL_COUNT] = { "FLAGS", "MAPS", "GAMEPLAY" };
		return kLabels[i];
	}

	int NewToolLeft( int i )
	{
		return SB_HOST_LIST_LEFT + i * ( SB_NEW_TOOL_W + SB_NEW_TOOL_GAP );
	}

	// [rc4l] What this screen would set the cvar to: what has been decided here, or failing that what
	// the engine has now.
	//
	// The fallback is the point. A server started from this screen begins at the same defaults this
	// client has, so a box that showed nothing until somebody typed in it would be showing something
	// untrue about what the server will do. Same reasoning as NewLoadFlags.
	std::string NewCvarValue( const std::string &name )
	{
		for ( size_t i = 0; i < g_NewCvars.size( ); ++i )
		{
			if ( g_NewCvars[i].first == name )
				return g_NewCvars[i].second;
		}

		FBaseCVar *const cvar = FindCVar( name.c_str( ), NULL );
		if ( cvar == NULL )
			return std::string( );

		UCVarValue val = cvar->GetGenericRep( CVAR_String );
		return ( val.String != NULL ) ? std::string( val.String ) : std::string( );
	}

	void NewSetCvar( const std::string &name, const std::string &value )
	{
		for ( size_t i = 0; i < g_NewCvars.size( ); ++i )
		{
			if ( g_NewCvars[i].first == name )
			{
				g_NewCvars[i].second = value;
				return;
			}
		}

		g_NewCvars.push_back( std::make_pair( name, value ));
	}

	// [rc4l] The flag fields as this screen has them, read from the engine the first time and edited
	// here after. Read rather than assumed: a fresh server starts from the same defaults this client
	// has, so starting the boxes anywhere else would be showing something untrue.
	// [rc4l] Every flag field back to what a fresh configuration starts with.
	//
	// The WHOLE table is rebuilt, not just the four fields that carry an explicit default: a reset
	// that left compatflags holding yesterday's number would be a reset that did not.
	//
	// The collapsed state is deliberately left alone. Refolding every field would move the thing
	// somebody is looking at out from under them, and how the list is folded is not a setting.
	void NewResetFlags( )
	{
		g_NewFlags = zx::FlagTable( );
		NewApplyFlagDefaults( );

		g_NewFlagInput.clear( );
		g_NewFlagInput.resize( g_NewFlags.size( ));

		for ( size_t i = 0; i < g_NewFlags.size( ); ++i )
		{
			NewSetCvar( g_NewFlags[i].name, zx::FormatFlagNumber( g_NewFlags[i].value ));
			g_NewFlagInput[i] = zx::ClearInput( );
			g_NewFlagInput[i].text = zx::FormatFlagNumber( g_NewFlags[i].value );
		}

		g_NewFlagEditing = -1;
	}

	// Every map back in, in the order the files give. Forced, because the cached rotation is keyed on
	// the load order and that has not changed -- the whole point is to discard what was done to it.
	void NewResetMaps( )
	{
		NewRebuildMaps( true );

		g_NewMapSel = 0;
		g_NewMapScroll = 0;
	}

	// [rc4l] The foot of a big box: DONE, and RESET beside it where there is one. ONE function draws
	// both, from the same geometry the hit tests read, for the reason DrawHostFootButtons gives --
	// the last time a pair like this was worked out twice, one of them ended up drawn in one place
	// and clickable in another.
	void DrawBoxFootButtons( )
	{
		DrawRoundedButton( NewBigDoneLeft( ), NewBigButtonTop( ), NewBigBtnW( ), SB_DLG_BTN_H,
			"DONE", g_NewIwadConfirmHot );

		if ( !NewBoxHasReset( ))
			return;

		// Neutral: this one only ASKS. The red belongs on the answer, and the confirmation's own
		// affirmative wears it -- see DialogIsDestructive.
		DrawRoundedButton( NewBigResetLeft( ), NewBigButtonTop( ), NewBigBtnW( ), SB_DLG_BTN_H,
			"RESET", g_NewBoxResetHot );

		// [rc4l] One line per box. This was a two-way choice between maps and flags, which left
		// GAMEPLAY -- added later -- being told about flags it does not show.
		const char *tip = "Put every flag back to what a new setup starts with  (Backspace)";

		if ( g_NewModal == NewModal::Maps )
			tip = "Put every map back, in the order the files give  (Backspace)";
		else if ( g_NewModal == NewModal::Gameplay )
			tip = "Put these settings back to what this mode starts with  (Backspace)";

		serverbrowser_Tip( NewBigResetLeft( ), NewBigButtonTop( ), NewBigBtnW( ), SB_DLG_BTN_H, tip );
	}

	// The RESET button's own hit test, shared by both boxes for the same reason the drawing is.
	// Returns true when the click belonged to it.
	bool BoxResetMouse( int type, int x, int y )
	{
		if ( !NewBoxHasReset( ))
			return false;

		const int bx = NewBigResetLeft( );
		const int by = NewBigButtonTop( );

		if (( x < serverbrowser_ToScreenX( bx )) ||
			( x >= serverbrowser_ToScreenX( bx + NewBigBtnW( ))) ||
			( y < serverbrowser_ToScreenY( by )) ||
			( y >= serverbrowser_ToScreenY( by + SB_DLG_BTN_H )))
		{
			return false;
		}

		g_NewBoxResetHot = true;

		if ( type == MOUSE_Release )
			NewAskReset( );

		return true;
	}

	// [rc4l] The question, asked the same way from the mouse and the keyboard so the two cannot come
	// to mean different things. Says WHAT goes rather than "are you sure": a player who has just
	// spent a while in here deserves to be told which afternoon they are about to lose.
	void NewAskReset( )
	{
		if ( g_NewModal == NewModal::Flags )
		{
			ShowDialog( DialogAction::ResetFlags, "Reset every flag?",
				"All of them go back to what a new setup starts with.",
				"Reset", 'r', "Keep", 'k' );
		}
		else if ( g_NewModal == NewModal::Maps )
		{
			ShowDialog( DialogAction::ResetMaps, "Reset the map list?",
				"Every map goes back in, in the order the files give.",
				"Reset", 'r', "Keep", 'k' );
		}
		else if ( g_NewModal == NewModal::Gameplay )
		{
			// [rc4l] "Everything here" rather than a list of what is on the box. A list has to be
			// rewritten every time a setting is added or a mode shows a different set, and the one
			// that is not rewritten is a dialog quietly lying about what the button does.
			//
			// The mode IS named, because it is the one thing on this box a reset could plausibly be
			// expected to take and does not.
			ShowDialog( DialogAction::ResetGameplay, "Reset these settings?",
				"Everything here goes back to what this mode starts with. The mode itself is kept.",
				"Reset", 'r', "Keep", 'k' );
		}
	}

	void NewLoadFlags( )
	{
		if ( g_NewFlagsLoaded )
			return;

		g_NewFlags = zx::FlagTable( );
		g_NewFlagsLoaded = true;

		g_NewFlagInput.clear( );
		g_NewFlagInput.resize( g_NewFlags.size( ));

		// Every field folded to start with. See NewFieldCollapsed.
		g_NewFlagCollapsed.assign( g_NewFlags.size( ), true );

		// And the mode this screen starts on, so its cvars and its skill are set before anything is
		// touched rather than only once somebody picks a different one.
		NewSetGameMode( g_NewGameMode );

		// The numbers a configuration built here starts from. See NewApplyFlagDefaults.
		NewApplyFlagDefaults( );

		for ( size_t i = 0; i < g_NewFlags.size( ); ++i )
		{
			NewSetCvar( g_NewFlags[i].name, zx::FormatFlagNumber( g_NewFlags[i].value ));
			g_NewFlagInput[i] = zx::ClearInput( );
			g_NewFlagInput[i].text = zx::FormatFlagNumber( g_NewFlags[i].value );
		}
	}

	// [rc4l] What the flag fields start at for a configuration built here.
	//
	// Whole numbers rather than a list of switches, because that is how they were given and how
	// anybody hosting quotes them -- and the box shows both views of the same value, so a number
	// set here arrives as lit pills either way. Decoded by the engine's own walk, these are:
	//
	//   dmflags 2621444             weapons stay, freelook on, no deathmatch-only weapons in co-op
	//   dmflags2 64                 doubled ammo
	//   zadmflags 268435524         no co-op HUD info, shared keys, no enemy icons
	//   sv_forbidvoteflags 3066     no votes on the limits, the flags, forcespec, changemap
	//                               or the secret exit
	//
	// They REPLACE what this client happens to have set rather than adding to it: a starting point
	// for a new configuration is a known state, not this machine's leftovers. Every one of them is
	// a pill in the FLAGS box and can be turned off.
	void NewApplyFlagDefaults( )
	{
		struct FieldDefault { const char *field; unsigned int value; };

		static const FieldDefault kDefaults[] =
		{
			{ "dmflags",			2621444u },
			{ "dmflags2",			64u },
			{ "zadmflags",			268435524u },
			{ "sv_forbidvoteflags",	3066u },
		};

		for ( size_t d = 0; d < countof( kDefaults ); ++d )
		{
			for ( size_t f = 0; f < g_NewFlags.size( ); ++f )
			{
				if ( g_NewFlags[f].name != kDefaults[d].field )
					continue;

				g_NewFlags[f].value = kDefaults[d].value;
				NewFlagValueChanged( static_cast<int>( f ));
				break;
			}
		}
	}

	// A field's value changed by a switch: the number box and the pending setting both follow.
	void NewFlagValueChanged( int field )
	{
		if (( field < 0 ) || ( field >= static_cast<int>( g_NewFlags.size( ))))
			return;

		const std::string text = zx::FormatFlagNumber( g_NewFlags[field].value );

		NewSetCvar( g_NewFlags[field].name, text );

		// Not while it is being typed in: rewriting the box under the caret is how a field fights
		// the person using it.
		if ( g_NewFlagEditing != field )
		{
			g_NewFlagInput[field] = zx::ClearInput( );
			g_NewFlagInput[field].text = text;
		}
	}

	// And the other way: a number typed or pasted re-derives every switch of that field.
	void NewFlagTextChanged( int field )
	{
		if (( field < 0 ) || ( field >= static_cast<int>( g_NewFlags.size( ))))
			return;

		unsigned int value = 0;
		if ( !zx::ParseFlagNumber( g_NewFlagInput[field].text, value ))
			return;			// left as it was until the text is a number again

		g_NewFlags[field].value = value;
		NewSetCvar( g_NewFlags[field].name, zx::FormatFlagNumber( value ));
	}

	// [rc4l] SAVE takes the corner and PLAY NOW keeps the rest. Both are here rather than inline so
	// the draw and the hit test cannot disagree about where a button is.
	int NewSaveLeft( )		{ return SB_HOST_RCOL_LEFT; }
	int NewSaveWidth( )		{ return 54; }
	int NewPlayLeft( )		{ return SB_HOST_RCOL_LEFT + NewSaveWidth( ) + 5; }
	int NewPlayWidth( )		{ return SB_HOST_RCOL_RIGHT - NewPlayLeft( ); }

	// [rc4l] What this screen currently is, as a preset.
	//
	// Built in one place because three things want it: SAVE writes it, PLAY NOW keeps it as the
	// last-played, and a preset loaded from CUSTOM has to come back through the same shape or the
	// round trip would lose something silently.
	zx::CustomEntry NewAsCustomEntry( const std::string &name )
	{
		zx::CustomEntry entry;

		entry.name = name;

		{
			const std::vector<std::string> &iwads = NewIwads( );
			if ( !iwads.empty( ))
			{
				entry.iwad = iwads[zx::ComputeClampedSelection( g_NewIwadSel,
					static_cast<int>( iwads.size( )))];
			}
		}

		// [rc4l] The hash comes with each file, because it is what lets a missing one be FETCHED on
		// another machine rather than merely reported. HashOf caches per path, so a preset saved
		// twice in a session hashes nothing twice.
		for ( size_t i = 0; i < g_NewOrder.size( ); ++i )
		{
			zx::LibraryFile file;
			file.path = g_NewOrder[i].path;
			file.name = g_NewOrder[i].name;

			entry.files.push_back( zx::CustomFile( g_NewOrder[i].name,
				zx::wadlibrary::HashOf( file )));
		}

		// [rc4l] The rotation is READ OUT OF THE FILES here if nobody has opened the MAPS box, the
		// same call PLAY NOW makes. Without it, a preset saved by somebody who never looked at the
		// map list was saved with no rotation at all, and the server it started fell back to map01
		// -- which is a preset quietly not playing the pack it names.
		NewRebuildMaps( false );

		entry.maps = NewRotation( );
		entry.gameMode = NewGameModeCvar( g_NewGameMode );

		const ULONG flags = GAMEMODE_GetFlags( g_NewGameMode );
		entry.bPvP = (( flags & ( GMF_DEATHMATCH | GMF_TEAMGAME )) != 0 );

		// Everything the boxes decided, less the sixteen mode switches: the mode is written as its
		// own line, and repeating the other fifteen as false is noise in a file meant to be read.
		for ( size_t i = 0; i < g_NewCvars.size( ); ++i )
		{
			bool bMode = false;

			for ( int m = 0; m < NUM_GAMEMODES; ++m )
			{
				if ( g_NewCvars[i].first == NewGameModeCvar( static_cast<GAMEMODE_e>( m )))
				{
					bMode = true;
					break;
				}
			}

			if ( !bMode )
				entry.cvars.push_back( g_NewCvars[i] );
		}

		return entry;
	}

	// [rc4l] A preset PUT BACK onto this screen: the IWAD, the load order, the settings, the mode
	// and the rotation.
	//
	// Written once because three things need it and they must agree: the last-played configuration
	// restored after a wad reload, EDIT on a saved preset, and COPY on a shipped one. Three separate
	// versions of "fill in the screen" would drift, and the drift would look like a preset losing a
	// setting for no reason anybody could reproduce.
	//
	// Returns what it could NOT find, so the caller can say so rather than starting a server that
	// is quietly missing a file.
	// `resolved` is name -> path for files somebody has ALREADY looked up, or NULL to look them up
	// here. An empty path in that list means "looked, and no copy on this disk matches", which is a
	// real answer and not the same as being absent from it.
	std::vector<std::string> NewApplyEntry( const zx::CustomEntry &entry,
		const std::vector<std::pair<std::string, std::string> > *resolved = NULL )
	{
		std::vector<std::string> missing;

		// The IWAD, by name, if this machine has it.
		{
			const std::vector<std::string> &iwads = NewIwads( );

			for ( size_t i = 0; i < iwads.size( ); ++i )
			{
				if ( stricmp( iwads[i].c_str( ), entry.iwad.c_str( )) != 0 )
					continue;

				g_NewIwadSel = static_cast<int>( i );
				g_NewIwadChosen = true;
				break;
			}
		}

		// The load order, resolved through the SAME search hosting uses -- by hash first, so a file
		// with the right name and the wrong contents is not quietly accepted.
		g_NewOrder.clear( );

		for ( size_t i = 0; i < entry.files.size( ); ++i )
		{
			// [rc4l] `resolved` is the answer a worker already found, when there is one. Passing it
			// in rather than looking again is what lets the last-played restore happen off the main
			// thread: the hashing is the expensive part and it has already been paid for.
			//
			// Looked up by name here rather than by index, so a caller that resolved a different
			// number of files than the entry has cannot silently pair the wrong path with a name.
			std::string found;
			bool bHaveAnswer = false;

			if ( resolved != NULL )
			{
				for ( size_t r = 0; r < resolved->size( ); ++r )
				{
					if ( (*resolved)[r].first != entry.files[i].name )
						continue;

					found = (*resolved)[r].second;
					bHaveAnswer = true;
					break;
				}
			}

			if ( !bHaveAnswer )
			{
				found = zx::waddownload::FindVerifiedCopy( entry.files[i].name.c_str( ),
					entry.files[i].md5.empty( ) ? NULL : entry.files[i].md5.c_str( )).GetChars( );
			}

			if ( found.empty( ))
			{
				missing.push_back( entry.files[i].name );
				continue;
			}

			zx::LoadOrderEntry row;
			row.name = entry.files[i].name;
			row.path = found;
			g_NewOrder.push_back( row );
		}

		// The settings. Loaded before the flags are read back out of them, so the numbers win.
		NewLoadFlags( );

		g_NewCvars = entry.cvars;

		for ( size_t f = 0; f < g_NewFlags.size( ); ++f )
		{
			for ( size_t c = 0; c < entry.cvars.size( ); ++c )
			{
				if ( entry.cvars[c].first != g_NewFlags[f].name )
					continue;

				unsigned int value = 0;
				if ( zx::ParseFlagNumber( entry.cvars[c].second, value ))
				{
					g_NewFlags[f].value = value;
					NewFlagValueChanged( static_cast<int>( f ));
				}
			}
		}

		// The mode, by its cvar name.
		for ( int m = 0; m < NUM_GAMEMODES; ++m )
		{
			if ( entry.gameMode == NewGameModeCvar( static_cast<GAMEMODE_e>( m )))
			{
				g_NewGameMode = static_cast<GAMEMODE_e>( m );
				break;
			}
		}

		// [rc4l] The rotation as SAVED, rather than rescanned. A preset that deliberately leaves a
		// map out would get it back the moment it was opened if this rebuilt from the files.
		g_NewMaps.clear( );
		for ( size_t i = 0; i < entry.maps.size( ); ++i )
			g_NewMaps.push_back( NewMapEntry( entry.maps[i] ));

		g_NewMapsKey = NewMapsKey( );
		g_NewMapSel = 0;
		g_NewMapScroll = 0;

		g_NewOrderSel = 0;
		g_NewOrderScroll = 0;
		g_NewRowsValid = false;

		return missing;
	}

	// [rc4l] One of the three boxes, opened. The mouse and the keyboard both come through here, so
	// there is one answer to what "opening FLAGS" means.
	void NewOpenTool( int i )
	{
		NewLoadFlags( );

		if ( i == 0 )
			g_NewModal = NewModal::Flags;
		else if ( i == 1 )
		{
			g_NewModal = NewModal::Maps;

			// [rc4l] The files are read HERE, on the press, and only when they have changed since
			// the last read. Not at startup, not while the tab is drawn, and not per frame: this
			// opens files off disk, and the one moment somebody is willing to wait for that is the
			// moment they asked to see what is in them.
			NewRebuildMaps( false );
		}
		else
			g_NewModal = NewModal::Gameplay;

		// The cursor starts at the top of whichever box this is, and the view with it.
		g_NewBoxSel = 0;
		BoxRevealSel( );

		g_NewFlagEditing = -1;
		g_NewSettingEditing = "";
		S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
	}

	void DrawNewTools( )
	{
		const bool bFocused = ( g_NewFocus == NewFocus::Tools ) &&
			( g_Focus == zx::BrowserFocus::Host ) && ( g_NewModal == NewModal::None );

		for ( int i = 0; i < SB_NEW_TOOL_COUNT; ++i )
		{
			const bool bOn = ( g_NewToolHot == i ) || ( bFocused && ( g_NewToolSel == i ));

			DrawRoundedButton( NewToolLeft( i ), SB_NEW_TOOL_Y, SB_NEW_TOOL_W, SB_NEW_TOOL_H,
				NewToolLabel( i ), bOn );

			// The orb marks where the keyboard is, the same as it does on every other row here.
			if ( bFocused && ( g_NewToolSel == i ))
			{
				FocusAnchor( zx::BrowserFocus::Host, NewToolLeft( i ) - 5,
					SB_NEW_TOOL_Y + SB_NEW_TOOL_H / 2 );
			}
		}

		serverbrowser_Tip( NewToolLeft( 0 ), SB_NEW_TOOL_Y, SB_NEW_TOOL_W, SB_NEW_TOOL_H,
			"Every dmflag and compat flag, by name\nAnd the numbers, to paste in or copy out" );
		serverbrowser_Tip( NewToolLeft( 1 ), SB_NEW_TOOL_Y, SB_NEW_TOOL_W, SB_NEW_TOOL_H,
			"Server settings that are not flags" );
		serverbrowser_Tip( NewToolLeft( 2 ), SB_NEW_TOOL_Y, SB_NEW_TOOL_W, SB_NEW_TOOL_H,
			"The mode, the limits, and who plays" );
	}

	// ---------------------------------------------------------------------------------------------
	//
	// [rc4l] FLAGS. Every named bit of every server bitfield, and the numbers they add up to.
	//
	// The pills and the number box are two views of ONE value, not two settings kept in step: a
	// click re-derives the number, a paste re-derives the pills. flagset_compute owns that, and owns
	// the rule that bits this build has no name for survive being edited -- see its header.

	// ---------------------------------------------------------------------------------------------
	//
	// [rc4l] VARIABLES and GAMEPLAY: rows of one setting each.
	//
	// Two boxes, one row. They differ in WHICH settings they show -- one a fixed table, the other
	// whatever the chosen gamemode actually uses -- and not at all in how a row looks or behaves.

	// A row the two boxes share: a label, and a switch, a slider, or a box to type in.
	//
	// [rc4l] `slider` is the menu's business rather than servervar_compute's: what a setting MEANS is
	// the table's, but whether it is worth dragging is a question about this screen. A limit with a
	// handful of sensible stops is; a connection count is not.
	struct SettingRow
	{
		std::string name;			// the cvar
		std::string label;
		zx::VarKind kind;
		bool slider;
		int min;
		int max;					// the cap the slider offers, not a cap on the setting
		std::string zeroText;		// what 0 means, when it means something other than zero

		// [rc4l] What each stop is CALLED, when the number is not the useful part. Skill 4 is
		// "Nightmare" to everyone who plays; a slider showing 4 is a slider you have to look up.
		// Empty for the settings whose value really is a count.
		std::vector<std::string> names;

		SettingRow() : kind(zx::VarKind::Toggle), slider(false), min(0), max(0) {}
	};

	// [rc4l] One row, FULLY specified, rather than one row mutated between pushes.
	//
	// The list used to be built by setting fields on a single SettingRow and pushing it repeatedly,
	// so each row silently inherited whatever the one above it left behind -- a zeroText, a cap, the
	// slider flag. That was survivable while one row in three was a slider. Now that they all are,
	// with different bounds and two of them naming their stops, it is a trap, so each row is built
	// from nothing.
	SettingRow NumberRow( const char *name, const char *label )
	{
		SettingRow row;
		row.name = name;
		row.label = label;
		row.kind = zx::VarKind::Number;
		return row;
	}

	SettingRow SliderRow( const char *name, const char *label, int min, int max,
		const char *zeroText = NULL )
	{
		SettingRow row = NumberRow( name, label );
		row.slider = true;
		row.min = min;
		row.max = max;

		if ( zeroText != NULL )
			row.zeroText = zeroText;

		return row;
	}

	// The value a slider row currently holds.
	int SettingNumber( const SettingRow &row )
	{
		return atoi( NewCvarValue( row.name ).c_str( ));
	}

	// [rc4l] The cap the slider offers, stretched to fit a value already past it.
	//
	// The caps are what somebody hosting usually wants -- five wins, twenty minutes, five lives --
	// and not a limit on the setting. A cfg that asks for a hundred-minute game keeps it: the slider
	// grows to reach it rather than quietly cutting it down the first time the box is opened.
	int SettingSliderMax( const SettingRow &row )
	{
		return MAX( row.max, SettingNumber( row ));
	}

	// [rc4l] One text box per setting, kept BY NAME rather than by position.
	//
	// The row list changes under it -- choosing a different mode is exactly that -- so an index would
	// hand the caret and half-typed text to whatever setting happened to take that row next.
	zx::TextInput &SettingInput( const SettingRow &row )
	{
		for ( size_t i = 0; i < g_NewSettingInput.size( ); ++i )
		{
			if ( g_NewSettingInput[i].first == row.name )
				return g_NewSettingInput[i].second;
		}

		zx::TextInput fresh = zx::ClearInput( );
		fresh.text = NewCvarValue( row.name );

		g_NewSettingInput.push_back( std::make_pair( row.name, fresh ));
		return g_NewSettingInput.back( ).second;
	}

	// [rc4l] A slider's id, namespaced so the hosting panel's own sliders cannot collide with these.
	// HostSliderSet reads the prefix back off; see there.
	std::string SettingSliderId( const SettingRow &row )
	{
		return std::string( "box:" ) + row.name;
	}

	// Where a slider's track starts. Half the width, so the labels down the left keep their column
	// and every track on the box lines up with the others.
	int SettingSliderX( )
	{
		return ( NewBigContentLeft( ) + NewBigContentRight( )) / 2 - 20;
	}

	std::string SettingValueText( const SettingRow &row )
	{
		const int value = SettingNumber( row );

		if (( value == 0 ) && !row.zeroText.empty( ))
			return row.zeroText;

		// A named stop, when this setting has names. Range-checked rather than trusted: a cfg can
		// hold a skill this build has no word for, and the number is a better answer than a crash.
		if (( value >= 0 ) && ( value < static_cast<int>( row.names.size( ))))
			return row.names[value];

		char buf[32];
		mysnprintf( buf, countof( buf ), "%d", value );
		return buf;
	}

	// [rc4l] The bounds a typed room size is held to.
	//
	// Applied when the typing STOPS rather than per keystroke: "1" on the way to "16" is not a
	// server for two, and a box that corrected it mid-word could never be typed into at all.
	//
	// A server for one is not a server, so anything at or under one becomes two. Above 64 is past
	// what the protocol carries, so it becomes 64. Anything that is not a number at all is somebody
	// who deleted the field or pasted a word, and 32 is the answer they would have got by leaving it
	// alone.
	// [rc4l] Write a numeric setting AND whatever has to travel with it. The one place that knows
	// about companions, so the slider and the typed box cannot disagree about them.
	void SettingApplyNumber( const std::string &name, int value )
	{
		char buf[32];
		mysnprintf( buf, countof( buf ), "%d", value );

		NewSetCvar( name, buf );

		// sv_maxclients is the seats and sv_maxplayers the players in the game; a server told to
		// seat fewer than it plays quietly loses the difference.
		if ( name == "sv_maxplayers" )
			NewSetCvar( "sv_maxclients", buf );
	}

	void SettingSanitise( const SettingRow &row )
	{
		if ( row.name != "sv_maxplayers" )
			return;

		const std::string text = NewCvarValue( row.name );

		bool digits = !text.empty( );
		for ( size_t i = 0; i < text.size( ); ++i )
		{
			if (( text[i] < '0' ) || ( text[i] > '9' ))
				digits = false;
		}

		int value = digits ? atoi( text.c_str( )) : 32;

		if ( value <= 1 )
			value = 2;
		if ( value > 64 )
			value = 64;

		// [rc4l] And the OTHER cvar, which is the whole reason this is one row -- through the same
		// setter the slider uses, so there is one answer to what "Players" writes.
		SettingApplyNumber( row.name, value );

		// The typed box keeps its own text, which has to agree with what was just written.
		char buf[32];
		mysnprintf( buf, countof( buf ), "%d", value );
		SettingSet( row, buf );
	}

	bool SettingIsOn( const SettingRow &row )
	{
		const std::string value = NewCvarValue( row.name );
		return ( value == "1" ) || ( value == "true" );
	}

	void SettingSet( const SettingRow &row, const std::string &value )
	{
		NewSetCvar( row.name, value );

		// The box follows unless it is the thing being typed in, the same rule the flag numbers
		// follow: rewriting a field under the caret is how it fights the person using it.
		if ( g_NewSettingEditing.Compare( row.name.c_str( )) != 0 )
		{
			zx::TextInput &input = SettingInput( row );
			input = zx::ClearInput( );
			input.text = value;
		}
	}

	// The big box: nearly the whole screen, because 175 switches is what there is to show.
	int NewBigModalTop( )		{ return SB_CONTENT_TOP + 10; }
	int NewBigModalBottom( )	{ return SB_CONTENT_BOTTOM - 10; }
	int NewBigModalLeft( )		{ return SB_PANEL_LEFT + 20; }
	int NewBigModalRight( )		{ return SB_DETAIL_RIGHT - 20; }
	int NewBigContentTop( )		{ return NewBigModalTop( ) + SB_NEW_MODAL_PAD + SB_NEW_LINE + 6; }
	int NewBigContentLeft( )	{ return NewBigModalLeft( ) + SB_NEW_MODAL_PAD; }
	int NewBigContentRight( )	{ return NewBigModalRight( ) - SB_NEW_MODAL_PAD - SB_NEW_MODAL_BAR_W; }
	int NewBigButtonTop( )		{ return NewBigModalBottom( ) - SB_NEW_MODAL_PAD - SB_DLG_BTN_H; }
	int NewBigBarX( )			{ return NewBigContentRight( ) + 2; }
	int NewBigButtonLeft( )		{ return ( NewBigModalLeft( ) + NewBigModalRight( )) / 2 - 40; }

	// [rc4l] RESET beside DONE, on the three boxes that hold settings somebody can get into a state
	// they cannot undo: the flag fields, the map rotation and the gameplay numbers.
	//
	// GAMEPLAY has a way back already -- picking the mode again re-applies its defaults -- but that
	// is a reset you have to KNOW about, and one that costs you the mode pill you were on if the
	// mode is what you wanted to keep. A button that says what it does beats a trick.
	//
	// Not on the IWAD box or the CUSTOM tab's read-only map list, neither of which holds anything to
	// reset.
	bool NewBoxHasReset( )
	{
		return ( g_NewModal == NewModal::Maps ) || ( g_NewModal == NewModal::Flags ) ||
			( g_NewModal == NewModal::Gameplay );
	}

	// The pair is centred TOGETHER when there are two, so DONE does not sit off to one side on the
	// boxes that have a neighbour and dead centre on the ones that do not.
	int NewBigDoneLeft( )	{ return NewBigButtonLeft( ) - ( NewBoxHasReset( ) ? 44 : 0 ); }
	int NewBigResetLeft( )	{ return NewBigButtonLeft( ) + 44; }
	int NewBigBtnW( )		{ return 80; }

	// [rc4l] A box to type a number into, and what separates two of them in the footer.
	//
	// Wide enough for the widest number a flag field can hold. It was 74, which fits eight digits,
	// and zadmflags at 268435524 is nine -- so the leading 2 was scrolled out of sight and the box
	// read 68435524, a different number entirely.
	int NewNumberBoxW( )		{ return 96; }
	int NewFootGap( )			{ return 12; }

	// A setting's own control, right-aligned so a column of them lines up whatever the labels say.
	int SettingControlW( )		{ return 74; }
	int SettingControlX( )		{ return NewBigContentRight( ) - SettingControlW( ); }

	// [rc4l] WHICH FIELDS BELONG TO WHICH BOX.
	//
	// lmsallowedweapons and lmsspectatorsettings are settings of a GAMEMODE rather than of a server:
	// outside Last Man Standing and its team form they do nothing whatever. So FLAGS does not list
	// them and GAMEPLAY does, when the chosen mode is one of the two that reads them.
	bool NewFlagFieldIsLms( const std::string &name )
	{
		return ( name == "lmsspectatorsettings" ) || ( name == "lmsallowedweapons" );
	}

	std::vector<int> NewFlagFields( bool bLms )
	{
		std::vector<int> out;

		for ( size_t i = 0; i < g_NewFlags.size( ); ++i )
		{
			if ( NewFlagFieldIsLms( g_NewFlags[i].name ) == bLms )
				out.push_back( static_cast<int>( i ));
		}

		return out;
	}

	// [rc4l] The mode this screen would start the server in.
	//
	// Held here rather than read from the engine's own current mode: this client is not in a game, and
	// what a hosted server plays is a decision being made on this screen rather than one already
	// taken. The cvar it becomes is the mode's own name -- `survival true`, `teamgame true` -- which
	// is what every catalogue cfg sets, and therefore what a server already knows how to be told.
	GAMEMODE_e NewChosenGameMode( )
	{
		return g_NewGameMode;
	}

	std::string NewGameModeCvar( GAMEMODE_e mode )
	{
		// "GAMEMODE_TEAMLMS" without its prefix, lowercased, is the cvar: teamlms.
		std::string name = GetStringGAMEMODE_e( mode ) + strlen( "GAMEMODE_" );

		for ( size_t i = 0; i < name.size( ); ++i )
			name[i] = static_cast<char>( tolower( static_cast<unsigned char>( name[i] )));

		return name;
	}

	// [rc4l] What a server built here starts at, with the ammo doubled either way.
	//
	// THE NUMBERS ARE 0-BASED, which is the trap in this setting: the `skill` cvar is an index into
	// the skill list, and g_game.cpp defaults it to 2 -- Hurt Me Plenty, the third. So Ultra-Violence
	// is 3 and Nightmare is 4, and the command line's own -skill is the OTHER convention, 1 to 5.
	// Anything written here in the wrong one lands a whole skill out.
	//
	// CO-OP GETS ULTRA-VIOLENCE. Nightmare respawns every monster, which against a team working
	// through a map is not a harder version of the same game -- it is a different one, where nothing
	// stays cleared and progress is a treadmill. PvP never meets that, so it keeps Nightmare for the
	// pace: fast projectiles and items that come back.
	//
	// A default rather than a rule: the GAMEPLAY box shows the skill and it can be moved.
	int NewSkillDefault( GAMEMODE_e mode )
	{
		return (( GAMEMODE_GetFlags( mode ) & GMF_COOPERATIVE ) != 0 ) ? 3 : 4;
	}

	// [rc4l] `bForceDefaults` is the difference between CHOOSING a mode and RESETTING it.
	//
	// Choosing one keeps the few settings that are a preference rather than a property of the mode:
	// a lives count you picked survives a switch to another mode that also uses lives, and the room
	// size survives everything. Resetting is being asked for the defaults outright, so it takes them
	// all -- which is what "I changed the lives, pressed reset, nothing happened" was: the reset was
	// running the mode-change rules and politely keeping the very number being reset.
	void NewSetGameMode( GAMEMODE_e mode, bool bForceDefaults = false )
	{
		g_NewGameMode = mode;

		{
			char buf[16];
			mysnprintf( buf, countof( buf ), "%d", NewSkillDefault( mode ));

			SettingRow row;
			row.name = "skill";
			row.kind = zx::VarKind::Number;
			SettingSet( row, buf );
		}

		// [rc4l] The limits this mode is usually played to, set when the mode is chosen.
		//
		// Only the ones the mode ACTUALLY HAS, read off its own declared capabilities the same way
		// the rows are -- setting a frag limit on a mode that earns points would be a number the
		// server ignores and the box does not show, left behind for whoever reads the cfg later.
		//
		// Set on every change, like the skill above: picking a mode is asking for that mode's
		// defaults, and a frag limit carried over from the deathmatch you were looking at a moment
		// ago is not something anybody chose.
		{
			const ULONG flags = GAMEMODE_GetFlags( mode );

			const zx::ModeLimits limits = zx::LimitsForMode(
				( flags & GMF_PLAYERSEARNFRAGS ) != 0,
				( flags & GMF_PLAYERSEARNPOINTS ) != 0,
				( flags & GMF_PLAYERSEARNWINS ) != 0,
				( flags & GMF_USEMAXLIVES ) != 0,
				( flags & GMF_PLAYERSONTEAMS ) != 0 );

			// Fifty frags or fifty points is the length of a match people actually play, and ten
			// minutes is the clock beside any of them.
			//
			// A DUEL IS SHORTER. Twenty-five is what duels are played to: two people, every frag
			// contested, and fifty is a long enough round that it stops being a duel and starts
			// being an endurance test.
			if ( limits.fraglimit )
				SettingApplyNumber( "fraglimit", ( mode == GAMEMODE_DUEL ) ? 25 : 50 );

			if ( limits.pointlimit )
				SettingApplyNumber( "pointlimit", 50 );

			if ( limits.winlimit )
				SettingApplyNumber( "winlimit", 5 );

			// [rc4l] Off zero, because zero is unlimited and this mode is about them running out.
			//
			// On a mode CHANGE only when it is still zero: a lives count somebody set is theirs to
			// keep, and picking survival twice should not walk it back to the floor. On a RESET it
			// goes back regardless -- that is what was asked for.
			if ( limits.lives &&
				( bForceDefaults || ( atoi( NewCvarValue( "sv_maxlives" ).c_str( )) < 1 )))
			{
				SettingApplyNumber( "sv_maxlives", 1 );
			}

			// The room size is a preference rather than anything the mode decides, so it survives a
			// mode change and goes back only when the defaults are asked for outright.
			if ( bForceDefaults )
				SettingApplyNumber( "sv_maxplayers", 32 );

			// No clock in co-op, survival or invasion -- see NewGameplayRows for why.
			if (( flags & GMF_COOPERATIVE ) == 0 )
				SettingApplyNumber( "timelimit", 10 );

			if ( limits.teams )
				SettingApplyNumber( "sv_maxteams", 2 );
		}

		// EVERY mode named, not only the chosen one. They are switches on the server, and one left
		// true by an earlier click would still be true when it starts.
		for ( int i = 0; i < NUM_GAMEMODES; ++i )
		{
			NewSetCvar( NewGameModeCvar( static_cast<GAMEMODE_e>( i )),
				( i == static_cast<int>( mode )) ? "true" : "false" );
		}
	}

	// [rc4l] Whether a field's switches are folded away behind its heading.
	//
	// Folded to begin with, all of them. 175 switches is a wall to look at and a far worse one to
	// walk with the arrow keys -- reaching compatflags2 meant holding Down past a hundred pills. With
	// the fields shut, the box opens on seven headings and you open the one you came for.
	bool NewFieldCollapsed( int field )
	{
		if (( field < 0 ) || ( field >= static_cast<int>( g_NewFlagCollapsed.size( ))))
			return false;

		return g_NewFlagCollapsed[field];
	}

	void NewToggleField( int field )
	{
		if (( field < 0 ) || ( field >= static_cast<int>( g_NewFlagCollapsed.size( ))))
			return;

		g_NewFlagCollapsed[field] = !g_NewFlagCollapsed[field];
		S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
	}

	// The LMS fields, and only when the mode uses them.
	std::vector<int> NewGameplayFlagFields( )
	{
		const GAMEMODE_e mode = NewChosenGameMode( );

		if (( mode != GAMEMODE_LASTMANSTANDING ) && ( mode != GAMEMODE_TEAMLMS ))
			return std::vector<int>( );

		return NewFlagFields( true );
	}

	// ---------------------------------------------------------------------------------------------
	//
	// [rc4l] THE STICKY FOOTER: the numbers, at the bottom, where they stay.
	//
	// They used to be a row inside each field's own block, which put the one thing somebody opens this
	// box to paste into behind however much scrolling that field's switches happened to need. The
	// number and the switches are two views of ONE value, and watching the number move while clicking
	// a switch is the whole point -- which only works if the number is on screen the whole time.

	struct FootPlace
	{
		int field;			// index into g_NewFlags
		int x;				// from the content's left edge
		int row;
		int labelW;

		FootPlace() : field(-1), x(0), row(0), labelW(0) {}
	};

	std::vector<FootPlace> NewFootPlan( const std::vector<int> &fields, int &rows )
	{
		std::vector<int> widths;
		widths.reserve( fields.size( ));

		for ( size_t i = 0; i < fields.size( ); ++i )
		{
			widths.push_back( SmallFont->StringWidth( g_NewFlags[fields[i]].name.c_str( )) + 6 +
				NewNumberBoxW( ));
		}

		// Flowed by the same unit the pill grids use, so a footer too wide for the box wraps instead
		// of running off the end of it.
		const std::vector<zx::PillPlace> flow = zx::FlowPills( widths,
			NewBigContentRight( ) - NewBigContentLeft( ), NewFootGap( ));

		std::vector<FootPlace> out;
		out.reserve( flow.size( ));

		for ( size_t i = 0; i < flow.size( ); ++i )
		{
			FootPlace place;
			place.field = fields[i];
			place.x = flow[i].x;
			place.row = flow[i].row;
			place.labelW = SmallFont->StringWidth( g_NewFlags[fields[i]].name.c_str( ));
			out.push_back( place );
		}

		rows = zx::PillFlowRowCount( flow );
		return out;
	}

	int NewFootH( const std::vector<int> &fields )
	{
		if ( fields.empty( ))
			return 0;

		int rows = 0;
		NewFootPlan( fields, rows );
		return rows * SB_NEW_PILL_ROW_H + 8;
	}

	// The content ends where the footer starts, so a box with no numbers to show gets those rows back.
	int NewBigContentBottom( const std::vector<int> &fields )
	{
		return NewBigButtonTop( ) - 8 - NewFootH( fields );
	}

	int NewBigVisibleRows( const std::vector<int> &fields )
	{
		return MAX( 1, ( NewBigContentBottom( fields ) - NewBigContentTop( )) / SB_NEW_PILL_ROW_H );
	}

	int NewFootTop( const std::vector<int> &fields )
	{
		return NewBigContentBottom( fields ) + 8;
	}

	void DrawFlagFooter( const std::vector<int> &fields )
	{
		if ( fields.empty( ))
			return;

		int rows = 0;
		const std::vector<FootPlace> plan = NewFootPlan( fields, rows );

		const int top = NewFootTop( fields );
		const int left = NewBigContentLeft( );

		// A rule above it, so it reads as the box's footer rather than as content that stopped
		// scrolling for no reason anyone can see.
		const int ruleX = serverbrowser_ToScreenX( left );
		const int ruleY = serverbrowser_ToScreenY( top - 5 );
		screen->Dim( PalEntry( 70, 74, 96 ), 0.7f, ruleX, ruleY,
			MAX( 1, serverbrowser_ToScreenX( NewBigContentRight( )) - ruleX ),
			MAX( 1, serverbrowser_ToScreenY( top - 4 ) - ruleY ));

		for ( size_t i = 0; i < plan.size( ); ++i )
		{
			const int x = left + plan[i].x;
			const int y = top + plan[i].row * SB_NEW_PILL_ROW_H;

			screen->DrawText( SmallFont, CR_DARKGRAY, x, y + 2,
				g_NewFlags[plan[i].field].name.c_str( ),
				DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true,
				TAG_DONE );

			int firstChar = 0;
			DrawTextField( x + plan[i].labelW + 6, y, NewNumberBoxW( ), SB_NEW_PILL_H,
				g_NewFlagInput[plan[i].field], ( g_NewFlagEditing == plan[i].field ),
				( g_NewFlagFieldHot == plan[i].field ), "0", false, firstChar );
		}
	}

	bool FlagFooterMouse( int type, int x, int y, const std::vector<int> &fields )
	{
		if ( fields.empty( ))
			return false;

		int rows = 0;
		const std::vector<FootPlace> plan = NewFootPlan( fields, rows );

		const int top = NewFootTop( fields );
		const int left = NewBigContentLeft( );

		for ( size_t i = 0; i < plan.size( ); ++i )
		{
			const int bx = left + plan[i].x + plan[i].labelW + 6;
			const int by = top + plan[i].row * SB_NEW_PILL_ROW_H;

			if (( x < serverbrowser_ToScreenX( bx )) ||
				( x >= serverbrowser_ToScreenX( bx + NewNumberBoxW( ))) ||
				( y < serverbrowser_ToScreenY( by )) ||
				( y >= serverbrowser_ToScreenY( by + SB_NEW_PILL_H )))
			{
				continue;
			}

			g_NewFlagFieldHot = plan[i].field;

			if ( type == MOUSE_Click )
			{
				EndSettingEdit( );
				g_NewFlagEditing = plan[i].field;
			}

			if ( FieldMouse( type, x, y, bx, by, NewNumberBoxW( ), SB_NEW_PILL_H,
				g_NewFlagInput[plan[i].field], g_NewFlagInputFirstChar, g_NewFlagInputDragging,
				g_NewFlagInputClickTime ))
			{
				return true;
			}
		}

		return false;
	}

	// ---------------------------------------------------------------------------------------------
	//
	// [rc4l] ONE MODEL FOR ALL THREE BOXES.
	//
	// A box is a list of items, each sitting on a row: a heading, a switch, a mode to choose, or a
	// setting with something to set it with. FLAGS, VARIABLES and GAMEPLAY differ only in WHICH items
	// they hold -- so the layout, the drawing, the hit test, the wheel and the scrollbar are written
	// once here and each box hands over a list.
	//
	// Written this way because the alternative was tried: the flags box got its own layout and its own
	// hit test, and with them its own scrollbar that drew correctly and could not be dragged. A third
	// copy of that would have been a third bar with the same fault.
	struct BoxItem
	{
		enum Kind { Heading, Flag, Mode, Setting };

		Kind kind;
		int row;
		int x;					// Flag/Mode: from the content's left edge
		int width;				// Flag/Mode
		int field;				// Flag: index into g_NewFlags
		int bit;				// Flag: index into that field's bits.  Mode: the GAMEMODE_e
		FString text;			// Heading: the line.  Setting: the label
		SettingRow setting;

		BoxItem() : kind(Heading), row(0), x(0), width(0), field(-1), bit(-1) {}
	};

	// `field` is set when the heading is a field's own, which is what makes it a control: clicking it
	// or pressing Enter on it folds that field away.
	void BoxAddHeading( std::vector<BoxItem> &items, int &row, const char *text, int field = -1 )
	{
		BoxItem item;
		item.kind = BoxItem::Heading;
		item.row = row;
		item.text = text;
		item.field = field;
		items.push_back( item );

		row += 1;
	}

	// A field's switches, wrapped over as many rows as they need -- or nothing but its heading, when
	// the field is folded.
	void BoxAddFlagField( std::vector<BoxItem> &items, int &row, int field )
	{
		const zx::FlagField &f = g_NewFlags[field];

		// The heading carries the count of bits this build has no name for, because those survive
		// being edited and somebody should be told they are there.
		const int unknown = zx::CountBits( zx::UnknownBits( f.value, f.bits ));

		FString head = f.name.c_str( );
		if ( unknown > 0 )
			head.AppendFormat( "   (+%d bit%s this build has no name for)", unknown,
				( unknown == 1 ) ? "" : "s" );

		BoxAddHeading( items, row, head, field );

		if ( NewFieldCollapsed( field ))
		{
			row += 1;			// the blank row that separates it from the next heading
			return;
		}

		std::vector<int> widths;
		widths.reserve( f.bits.size( ));
		for ( size_t b = 0; b < f.bits.size( ); ++b )
			widths.push_back( GameplayPillW( f.bits[b].name.c_str( )));

		const std::vector<zx::PillPlace> flow = zx::FlowPills( widths,
			NewBigContentRight( ) - NewBigContentLeft( ), SB_NEW_PILL_HGAP );

		for ( size_t b = 0; b < flow.size( ); ++b )
		{
			BoxItem item;
			item.kind = BoxItem::Flag;
			item.row = row + flow[b].row;
			item.x = flow[b].x;
			item.width = flow[b].width;
			item.field = field;
			item.bit = static_cast<int>( b );
			items.push_back( item );
		}

		row += zx::PillFlowRowCount( flow ) + 1;		// and a blank row before whatever is next
	}

	void BoxAddSettings( std::vector<BoxItem> &items, int &row, const std::vector<SettingRow> &rows )
	{
		for ( size_t i = 0; i < rows.size( ); ++i )
		{
			BoxItem item;
			item.kind = BoxItem::Setting;
			item.row = row;
			item.text = rows[i].label.c_str( );
			item.setting = rows[i];
			items.push_back( item );

			row += 1;
		}

		row += 1;
	}

	// Every mode the engine has, as pills. Read from the engine rather than listed, so a mode added
	// to gamemode.txt appears here without this file being touched.
	void BoxAddModes( std::vector<BoxItem> &items, int &row )
	{
		std::vector<int> widths;
		widths.reserve( NUM_GAMEMODES );

		for ( int i = 0; i < NUM_GAMEMODES; ++i )
			widths.push_back( GameplayPillW( GAMEMODE_GetName( static_cast<GAMEMODE_e>( i ))));

		const std::vector<zx::PillPlace> flow = zx::FlowPills( widths,
			NewBigContentRight( ) - NewBigContentLeft( ), SB_NEW_PILL_HGAP );

		for ( size_t i = 0; i < flow.size( ); ++i )
		{
			BoxItem item;
			item.kind = BoxItem::Mode;
			item.row = row + flow[i].row;
			item.x = flow[i].x;
			item.width = flow[i].width;
			item.bit = static_cast<int>( i );
			items.push_back( item );
		}

		row += zx::PillFlowRowCount( flow ) + 1;
	}

	// --- what each box holds ---------------------------------------------------------------------

	std::vector<int> BoxFooterFields( NewModal which )
	{
		if ( which == NewModal::Flags )
			return NewFlagFields( false );
		if ( which == NewModal::Gameplay )
			return NewGameplayFlagFields( );

		return std::vector<int>( );
	}

	std::vector<BoxItem> BuildBox( NewModal which, int &totalRows )
	{
		std::vector<BoxItem> items;
		int row = 0;

		if ( which == NewModal::Flags )
		{
			const std::vector<int> fields = NewFlagFields( false );
			for ( size_t i = 0; i < fields.size( ); ++i )
				BoxAddFlagField( items, row, fields[i] );
		}
		else if ( which == NewModal::Gameplay )
		{
			BoxAddHeading( items, row, "MODE" );
			BoxAddModes( items, row );

			BoxAddHeading( items, row, "SETTINGS" );
			BoxAddSettings( items, row, NewGameplayRows( ));

			// The mode's own flags, when it has any. They are the same switches the FLAGS box draws,
			// through the same items, so a bit set here and a bit set there are one thing.
			const std::vector<int> lms = NewGameplayFlagFields( );
			for ( size_t i = 0; i < lms.size( ); ++i )
				BoxAddFlagField( items, row, lms[i] );
		}

		totalRows = row;
		return items;
	}

	const char *BoxTitle( NewModal which )
	{
		if ( which == NewModal::Gameplay )
			return "GAMEPLAY";

		return "FLAGS";
	}

	// Each box keeps its own place, so closing one and opening it again lands where it was left.
	int &BoxScroll( NewModal which )
	{
		if ( which == NewModal::Gameplay )
			return g_NewGameScroll;

		return g_NewFlagsScroll;
	}

	int BoxMaxScroll( NewModal which )
	{
		int total = 0;
		BuildBox( which, total );

		return MAX( 0, total - NewBigVisibleRows( BoxFooterFields( which )));
	}

	// The row being typed in, found BY NAME in whatever box is up. Empty when there is none.
	SettingRow SettingBeingEdited( )
	{
		if ( g_NewSettingEditing.IsEmpty( ))
			return SettingRow( );

		int rows = 0;
		const std::vector<BoxItem> items = BuildBox( g_NewModal, rows );

		for ( size_t i = 0; i < items.size( ); ++i )
		{
			if (( items[i].kind == BoxItem::Setting ) &&
				( g_NewSettingEditing.Compare( items[i].setting.name.c_str( )) == 0 ))
			{
				return items[i].setting;
			}
		}

		return SettingRow( );
	}

	// [rc4l] Typing into a setting has FINISHED: the value is held to its bounds and the caret let
	// go. Every way out of a box goes through here -- Enter, Escape, an arrow, a click on something
	// else, closing the panel -- because a rule applied by only some of them is a rule the player
	// finds out about at random.
	void EndSettingEdit( )
	{
		if ( g_NewSettingEditing.IsEmpty( ))
			return;

		const SettingRow row = SettingBeingEdited( );

		g_NewSettingEditing = "";

		if ( !row.name.empty( ))
			SettingSanitise( row );
	}

	// --- the keyboard ----------------------------------------------------------------------------
	//
	// [rc4l] Items are built in reading order -- a heading, then its switches row by row -- so left
	// and right are simply the neighbours. Up and down look for the nearest row in that direction
	// that HAS an item, and on it the one nearest the column being left: a grid of pills is not a
	// list, and a cursor that jumped to the start of every row would be no better than scrolling.
	int BoxMove( const std::vector<BoxItem> &items, int sel, int mkey )
	{
		if ( items.empty( ))
			return 0;

		sel = zx::ComputeClampedSelection( sel, static_cast<int>( items.size( )));

		if ( mkey == MKEY_Left )
			return MAX( 0, sel - 1 );
		if ( mkey == MKEY_Right )
			return MIN( static_cast<int>( items.size( )) - 1, sel + 1 );

		const int dir = ( mkey == MKEY_Up ) ? -1 : 1;
		const int fromRow = items[sel].row;
		const int fromX = items[sel].x;

		int best = -1;
		int bestRow = 0;

		for ( size_t i = 0; i < items.size( ); ++i )
		{
			const int row = items[i].row;

			if (( dir < 0 ) ? ( row >= fromRow ) : ( row <= fromRow ))
				continue;

			// A row further away than the one already found is not the next row.
			if (( best >= 0 ) &&
				((( dir < 0 ) && ( row < bestRow )) || (( dir > 0 ) && ( row > bestRow ))))
			{
				continue;
			}

			if (( best >= 0 ) && ( row == bestRow ) &&
				( abs( items[i].x - fromX ) >= abs( items[best].x - fromX )))
			{
				continue;
			}

			best = static_cast<int>( i );
			bestRow = row;
		}

		return ( best >= 0 ) ? best : sel;
	}

	// The view follows the cursor. Without this, walking down the box moves a cursor nobody can see
	// -- which is the same fault the load order had, and is fixed the same way.
	void BoxRevealSel( )
	{
		int totalRows = 0;
		const std::vector<BoxItem> items = BuildBox( g_NewModal, totalRows );
		if ( items.empty( ))
			return;

		const int sel = zx::ComputeClampedSelection( g_NewBoxSel, static_cast<int>( items.size( )));
		const int visible = NewBigVisibleRows( BoxFooterFields( g_NewModal ));

		int &scroll = BoxScroll( g_NewModal );
		const int row = items[sel].row;

		if ( row < scroll )
			scroll = row;
		else if ( row >= scroll + visible )
			scroll = row - visible + 1;

		scroll = zx::ComputeClampedSelection( scroll, BoxMaxScroll( g_NewModal ) + 1 );
	}

	bool BoxMenuKey( int mkey )
	{
		int totalRows = 0;
		const std::vector<BoxItem> items = BuildBox( g_NewModal, totalRows );

		if ( items.empty( ))
			return true;

		g_NewBoxSel = zx::ComputeClampedSelection( g_NewBoxSel, static_cast<int>( items.size( )));

		if ( mkey == MKEY_Enter )
		{
			BoxActivate( items[g_NewBoxSel] );
			BoxRevealSel( );
			return true;
		}

		if (( mkey != MKEY_Up ) && ( mkey != MKEY_Down ) && ( mkey != MKEY_Left ) &&
			( mkey != MKEY_Right ))
		{
			return false;
		}

		// [rc4l] Left and right MOVE a slider rather than moving off it, which is what those keys do
		// on every slider anybody has used. It owns its row, so nothing is lost by taking them.
		{
			const BoxItem &at = items[g_NewBoxSel];

			if (( at.kind == BoxItem::Setting ) && at.setting.slider &&
				(( mkey == MKEY_Left ) || ( mkey == MKEY_Right )))
			{
				const int was = SettingNumber( at.setting );
				const int now = clamp( was + (( mkey == MKEY_Right ) ? 1 : -1 ),
					at.setting.min, SettingSliderMax( at.setting ));

				if ( now != was )
				{
					HostSliderSet( SettingSliderId( at.setting ), now );
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
				}

				return true;
			}
		}

		const int moved = BoxMove( items, g_NewBoxSel, mkey );

		if ( moved != g_NewBoxSel )
		{
			g_NewBoxSel = moved;
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
		}

		BoxRevealSel( );
		return true;
	}

	// [rc4l] A field's heading, which opens and shuts it.
	//
	// NOT the switch pill, though it was that first and looked well enough. The green dot on those
	// means ONE thing on this screen -- a setting that is on -- and a heading wearing it says the
	// field is switched on rather than opened, which is a different claim about a control that does
	// not make it. So this is its own shape: a flat bar with a caret at the left, "v" while it is
	// open and ">" while it is shut, and the same "^"/"v" alphabet the load order's buttons already
	// use rather than a glyph invented for it.
	void DrawFieldHeader( int x, int y, int w, int h, const char *label, bool bOpen, bool bHot )
	{
		const int lift = bHot ? 24 : 0;

		zx::PanelColor top, bot;
		top.r = 40 + lift; top.g = 43 + lift; top.b = 58 + lift; top.a = bOpen ? 235 : 200;
		bot.r = 28 + lift; bot.g = 30 + lift; bot.b = 42 + lift; bot.a = bOpen ? 235 : 200;

		DrawRoundedPanel( x, y, w, h, top, bot, SB_HOST_PILL_RADIUS );

		const char *const caret = bOpen ? "v" : ">";
		const int caretW = SmallFont->StringWidth( ">" );
		const int textY = y + ( h - SmallFont->GetHeight( )) / 2;

		screen->DrawText( SmallFont, bHot ? CR_WHITE : CR_GRAY, x + 6, textY, caret,
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );

		const int labelX = x + 6 + caretW + 5;

		screen->DrawText( SmallFont, CR_WHITE, labelX, textY,
			serverbrowser_FitName( label, ( x + w ) - labelX - 4 ),
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
	}

	// ---------------------------------------------------------------------------------------------
	//
	// [rc4l] THE SAVE BOX: a name, a line that says what will happen, and two buttons.
	//
	// Small on purpose. It asks one question, and the status line under the box is where every
	// answer to it goes -- including the one that matters, which is that the name is taken.

	// [rc4l] Wide enough for the longest thing the status line says, which is the replace warning.
	// It was 260 and that line ran out of the box and across the panel behind it.
	int NewSaveBoxW( )		{ return 330; }
	int NewSaveBoxLeft( )	{ return ( SB_VIRT_W - NewSaveBoxW( )) / 2; }
	int NewSaveBoxTop( )	{ return SB_CONTENT_TOP + 60; }
	// Tall enough for a status line that has WRAPPED to two, which the longest of them does.
	int NewSaveBoxH( )		{ return SB_NEW_MODAL_PAD * 2 + SB_NEW_LINE * 5 + SB_DLG_BTN_H + 14; }
	int NewSaveFieldTop( )	{ return NewSaveBoxTop( ) + SB_NEW_MODAL_PAD + SB_NEW_LINE + 4; }
	int NewSaveStatusTop( )	{ return NewSaveFieldTop( ) + SB_NEW_SEARCH_H + 6; }
	int NewSaveBtnTop( )	{ return NewSaveBoxTop( ) + NewSaveBoxH( ) - SB_NEW_MODAL_PAD - SB_DLG_BTN_H; }
	int NewSaveBtnW( )		{ return 74; }
	int NewSaveConfirmLeft( ) { return NewSaveBoxLeft( ) + NewSaveBoxW( ) / 2 - NewSaveBtnW( ) - 4; }
	int NewSaveCancelLeft( )  { return NewSaveBoxLeft( ) + NewSaveBoxW( ) / 2 + 4; }

	// [rc4l] A MODAL IS MODAL: while one of the NEW screen's boxes is up it has the keyboard, whatever
	// the browser's focus happens to say.
	//
	// This is here because the two halves of the routing disagreed. TranslateKeyboardEvents hands the
	// save box its raw keys with no focus test at all -- it is a name being typed -- while the guard
	// that DELIVERS them demanded g_Focus == Host. Open the box from a click, which did not take the
	// focus, and enter arrived untranslated at a Responder that would not pass it on, fell through to
	// the browser's own handling, and shut the box instead of asking to replace. Both halves ask this
	// now, so they cannot drift apart again.
	bool NewOwnsKeyboard( )
	{
		return ( g_Tab == BrowserTab::Host ) && ( g_HostKind == HostKind::New ) &&
			( g_NewModal != NewModal::None );
	}

	// What the box would do if Confirm were pressed now. Asked by the drawing and by the pressing,
	// so what it says and what it does cannot differ.
	zx::SaveState NewSaveStateNow( )
	{
		return zx::NextSaveState( g_NewSaveName.text, zx::CustomNames( ), g_NewSaveAsked,
			g_NewOrder.size( ));
	}

	void NewOpenSaveModal( )
	{
		g_NewModal = NewModal::Save;

		g_NewSaveName = zx::ClearInput( );
		g_NewSaveAsked = false;
		g_NewSaveFirstChar = 0;
		g_NewSaveBtnSel = 0;
		g_NewSaveBtnHot = -1;

		S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
	}

	// [rc4l] Confirm, which is either the question or the answer. See customsave_compute.
	void NewSaveConfirm( )
	{
		const zx::SaveState state = NewSaveStateNow( );

		if ( state == zx::SaveState::Asking )
		{
			// The first press on a taken name asks. Nothing is written.
			g_NewSaveAsked = true;
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/invalid", snd_menuvolume, ATTN_NONE );
			return;
		}

		if (( state != zx::SaveState::Ready ) && ( state != zx::SaveState::Replace ))
		{
			// Empty, unusable, or nothing to save. The line under the box already says which.
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/invalid", snd_menuvolume, ATTN_NONE );
			return;
		}

		if ( zx::CustomSave( NewAsCustomEntry( g_NewSaveName.text )))
		{
			// [rc4l] The CUSTOM tab reads its list once and keeps it, so a save it does not hear
			// about is a preset written to disk and missing from the screen until something else
			// happens to reload. Found by saving one and looking.
			CustomForget( );

			NewSay( "Saved to CUSTOM" );
			g_NewModal = NewModal::None;
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
		}
		else
		{
			NewSay( "Could not write that preset" );
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/invalid", snd_menuvolume, ATTN_NONE );
		}
	}

	// Escape, Cancel, and clicking away are the same thing: nothing was saved.
	void NewCloseSaveModal( )
	{
		g_NewModal = NewModal::None;
		g_NewSaveAsked = false;
	}

	void DrawNewSaveModal( )
	{
		serverbrowser_ClearTips( );

		screen->Dim( 0x000000, 0.62f, 0, 0, screen->GetWidth( ), screen->GetHeight( ));

		const zx::PanelColor topCol = { 26, 28, 40, 245 };
		const zx::PanelColor botCol = { 12, 13, 20, 250 };
		DrawRoundedPanel( NewSaveBoxLeft( ), NewSaveBoxTop( ), NewSaveBoxW( ), NewSaveBoxH( ),
			topCol, botCol, 8 );

		const int left = NewSaveBoxLeft( ) + SB_NEW_MODAL_PAD;
		const int width = NewSaveBoxW( ) - SB_NEW_MODAL_PAD * 2;

		screen->DrawText( SmallFont, CR_GOLD, left, NewSaveBoxTop( ) + SB_NEW_MODAL_PAD,
			"SAVE THIS SETUP", DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H,
			DTA_KeepRatio, true, TAG_DONE );

		DrawTextField( left, NewSaveFieldTop( ), width, SB_NEW_SEARCH_H, g_NewSaveName, true, true,
			"Name it", false, g_NewSaveFirstChar );

		// [rc4l] The status line, which is the whole reason this box exists rather than a prompt.
		// Red is a refusal or a warning; grey is a remark.
		//
		// WRAPPED TO THE BOX, through V_BreakLines -- the same thing the notice panel and the
		// experience summary use. Drawn as one line it ran out of the box and across the panel
		// behind it, and the answer to that is not a shorter sentence: the next line somebody adds
		// would do it again. A width the text has to fit inside cannot overflow.
		const zx::SaveState state = NewSaveStateNow( );
		const char *const status = zx::SaveStatusText( state );

		if ( status[0] != 0 )
		{
			FBrokenLines *const lines = V_BreakLines( SmallFont, width, status );

			int y = NewSaveStatusTop( );
			for ( int i = 0; lines[i].Width >= 0; ++i )
			{
				screen->DrawText( SmallFont,
					zx::SaveStatusIsWarning( state ) ? CR_RED : CR_DARKGRAY,
					left, y, lines[i].Text,
					DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true,
					TAG_DONE );

				y += SmallFont->GetHeight( ) + 1;
			}

			V_FreeBrokenLines( lines );
		}

		DrawRoundedButton( NewSaveConfirmLeft( ), NewSaveBtnTop( ), NewSaveBtnW( ), SB_DLG_BTN_H,
			( state == zx::SaveState::Replace ) ? "REPLACE" : "CONFIRM",
			( g_NewSaveBtnHot == 0 ) || ( g_NewSaveBtnSel == 0 ));

		DrawRoundedButton( NewSaveCancelLeft( ), NewSaveBtnTop( ), NewSaveBtnW( ), SB_DLG_BTN_H,
			"CANCEL", ( g_NewSaveBtnHot == 1 ) || ( g_NewSaveBtnSel == 1 ));

		FocusAnchor( zx::BrowserFocus::Host,
			(( g_NewSaveBtnSel == 0 ) ? NewSaveConfirmLeft( ) : NewSaveCancelLeft( )) - 5,
			NewSaveBtnTop( ) + SB_DLG_BTN_H / 2 );
	}

	bool NewSaveModalMouse( int type, int x, int y )
	{
		g_NewSaveBtnHot = -1;

		// The name box, through the same field helper every other box on this screen uses.
		if ( FieldMouse( type, x, y, NewSaveBoxLeft( ) + SB_NEW_MODAL_PAD, NewSaveFieldTop( ),
			NewSaveBoxW( ) - SB_NEW_MODAL_PAD * 2, SB_NEW_SEARCH_H, g_NewSaveName,
			g_NewSaveFirstChar, g_NewSaveDragging, g_NewSaveClickTime ))
		{
			return true;
		}

		const int lefts[2] = { NewSaveConfirmLeft( ), NewSaveCancelLeft( ) };

		for ( int i = 0; i < 2; ++i )
		{
			if (( x < serverbrowser_ToScreenX( lefts[i] )) ||
				( x >= serverbrowser_ToScreenX( lefts[i] + NewSaveBtnW( ))) ||
				( y < serverbrowser_ToScreenY( NewSaveBtnTop( ))) ||
				( y >= serverbrowser_ToScreenY( NewSaveBtnTop( ) + SB_DLG_BTN_H )))
			{
				continue;
			}

			g_NewSaveBtnHot = i;

			// [rc4l] HOVERING IS NOT CHOOSING. Moving the selection here meant a pointer left sitting
			// over CANCEL silently reassigned what enter does, so a name typed on the keyboard was
			// cancelled by a mouse nobody touched. The highlight follows the pointer; the selection
			// only moves when a button is actually pressed.
			if (( type == MOUSE_Click ) || ( type == MOUSE_Release ))
				g_NewSaveBtnSel = i;

			if ( type == MOUSE_Release )
			{
				if ( i == 0 )
					NewSaveConfirm( );
				else
					NewCloseSaveModal( );
			}

			return true;
		}

		// Inside swallows; outside is a cancel, which is what closing without answering means.
		if (( x >= serverbrowser_ToScreenX( NewSaveBoxLeft( ))) &&
			( x < serverbrowser_ToScreenX( NewSaveBoxLeft( ) + NewSaveBoxW( ))) &&
			( y >= serverbrowser_ToScreenY( NewSaveBoxTop( ))) &&
			( y < serverbrowser_ToScreenY( NewSaveBoxTop( ) + NewSaveBoxH( ))))
		{
			return true;
		}

		if ( type == MOUSE_Release )
			NewCloseSaveModal( );

		return true;
	}

	// --- the map list, in the same big box --------------------------------------------------------

	// How many will actually be played, and which one is first. Both are asked in two places, and
	// hosting has to agree with what the box says.
	int NewMapsInCount( )
	{
		int count = 0;

		for ( size_t i = 0; i < g_NewMaps.size( ); ++i )
		{
			if ( g_NewMaps[i].bIn )
				count++;
		}

		return count;
	}

	std::string NewFirstMapIn( )
	{
		for ( size_t i = 0; i < g_NewMaps.size( ); ++i )
		{
			if ( g_NewMaps[i].bIn )
				return g_NewMaps[i].name;
		}

		return std::string( );
	}

	std::vector<std::string> NewRotation( )
	{
		std::vector<std::string> out;

		for ( size_t i = 0; i < g_NewMaps.size( ); ++i )
		{
			if ( g_NewMaps[i].bIn )
				out.push_back( g_NewMaps[i].name );
		}

		return out;
	}

	int NewMapRowsVisible( )
	{
		// No footer here: nothing in this box is a number to paste.
		return MAX( 1, ( NewBigContentBottom( std::vector<int>( )) - NewBigContentTop( )) /
			SB_NEW_ROW_H );
	}

	int NewMapMaxScroll( )
	{
		return MAX( 0, static_cast<int>( g_NewMaps.size( )) - NewMapRowsVisible( ));
	}

	void DrawNewMapsModal( )
	{
		serverbrowser_ClearTips( );

		screen->Dim( 0x000000, 0.62f, 0, 0, screen->GetWidth( ), screen->GetHeight( ));

		const zx::PanelColor topCol = { 26, 28, 40, 245 };
		const zx::PanelColor botCol = { 12, 13, 20, 250 };
		DrawRoundedPanel( NewBigModalLeft( ), NewBigModalTop( ),
			NewBigModalRight( ) - NewBigModalLeft( ), NewBigModalBottom( ) - NewBigModalTop( ),
			topCol, botCol, 8 );

		const int left = NewBigContentLeft( );
		const int right = NewBigContentRight( );
		const int top = NewBigContentTop( );
		const int visible = NewMapRowsVisible( );

		// [rc4l] "8 of 32" rather than a count, because the two numbers are different questions:
		// how many will be played, and how many there are to choose from.
		FString heading;
		heading.Format( "MAP ROTATION  (%d of %d)", NewMapsInCount( ),
			static_cast<int>( g_NewMaps.size( )));

		screen->DrawText( SmallFont, CR_GOLD, left, NewBigModalTop( ) + SB_NEW_MODAL_PAD, heading,
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true,
			TAG_DONE );

		if ( g_NewMaps.empty( ))
		{
			// [rc4l] Said plainly, because the two reasons are different problems: a resource pack
			// has no maps and never will, and a file this cannot read is worth knowing about.
			DrawNewRowText( left, top, CR_DARKGRAY,
				"No maps in the IWAD or any of the chosen files" );
		}
		else
		{
			g_NewMapSel = zx::ComputeClampedSelection( g_NewMapSel,
				static_cast<int>( g_NewMaps.size( )));

			if ( g_NewMapRevealSel )
			{
				NewClampScroll( g_NewMapSel, static_cast<int>( g_NewMaps.size( )), visible,
					g_NewMapScroll );
				g_NewMapRevealSel = false;
			}
			else
			{
				g_NewMapScroll = zx::ComputeClampedSelection( g_NewMapScroll,
					NewMapMaxScroll( ) + 1 );
			}

			for ( int row = g_NewMapScroll;
				( row < static_cast<int>( g_NewMaps.size( ))) && ( row < g_NewMapScroll + visible );
				++row )
			{
				const int rowY = NewRowY( top, row, g_NewMapScroll );
				const bool bSel = ( row == g_NewMapSel );

				if ( bSel )
					FocusAnchor( zx::BrowserFocus::Host, left - 9, rowY + SB_NEW_ROW_H / 2 );

				// The pointer wins where it is, and the keyboard's cursor shows on the selected row
				// otherwise -- the same rule the settings boxes follow, so both ways of using this
				// mark the same thing.
				int btnHot = (( g_NewMapBtnHot >= row * 3 ) && ( g_NewMapBtnHot < row * 3 + 3 ))
					? ( g_NewMapBtnHot - row * 3 ) : -1;

				if (( btnHot < 0 ) && bSel && ( g_NewMapHot < 0 ))
					btnHot = g_NewMapBtnSel;

				// The load order's own row, over a different list, with the first button as a
				// SWITCH: a map is in or out, and that is a state rather than an act. See
				// DrawNewOrderToggle for why a glyph was the wrong mark for it.
				DrawOrderRow( left, right, rowY, row, g_NewMaps[row].name.c_str( ), bSel,
					( row == g_NewMapHot ), btnHot, ( row == 0 ),
					( row + 1 == static_cast<int>( g_NewMaps.size( ))), false,
					"X", !g_NewMaps[row].bIn, true, g_NewMaps[row].bIn );
			}

			DrawHostRegionScrollBar( top, top + visible * SB_NEW_ROW_H,
				static_cast<int>( g_NewMaps.size( )) * SB_NEW_ROW_H, g_NewMapScroll * SB_NEW_ROW_H,
				NewBigBarX( ));
		}

		// The first map IN is where the server starts, which is worth saying where it is decided.
		if ( NewMapsInCount( ) > 0 )
		{
			FString foot;
			foot.Format( "Starts on %s", NewFirstMapIn( ).c_str( ));

			screen->DrawText( SmallFont, CR_DARKGRAY, left, NewBigButtonTop( ) + 4, foot,
				DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true,
				TAG_DONE );
		}

		DrawBoxFootButtons( );
	}

	// [rc4l] Switch and move, which is the whole of what this list can be told. Nothing leaves it:
	// see NewMapEntry for why taking a map out has to be undoable.
	void NewMapToggle( int row )
	{
		if (( row < 0 ) || ( row >= static_cast<int>( g_NewMaps.size( ))))
			return;

		g_NewMaps[row].bIn = !g_NewMaps[row].bIn;
		S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
	}

	void NewMapMove( int row, int step )
	{
		const int to = row + step;

		if (( row < 0 ) || ( row >= static_cast<int>( g_NewMaps.size( ))))
			return;
		if (( to < 0 ) || ( to >= static_cast<int>( g_NewMaps.size( ))))
			return;

		const NewMapEntry held = g_NewMaps[row];
		g_NewMaps[row] = g_NewMaps[to];
		g_NewMaps[to] = held;

		g_NewMapSel = to;
		g_NewMapRevealSel = true;
		S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
	}

	bool NewMapsModalMouse( int type, int x, int y )
	{
		g_NewMapHot = -1;
		g_NewMapBtnHot = -1;
		g_NewIwadConfirmHot = false;
		g_NewBoxResetHot = false;

		const int left = NewBigContentLeft( );
		const int right = NewBigContentRight( );
		const int top = NewBigContentTop( );
		const int visible = NewMapRowsVisible( );

		// The bar first, through the shared helper every other list uses.
		if ( RegionBarMouse( type, x, y, top, top + visible * SB_NEW_ROW_H,
			static_cast<int>( g_NewMaps.size( )) * SB_NEW_ROW_H, NewMapMaxScroll( ),
			g_NewMapScroll, g_DraggingNewMapBar, NewBigBarX( )))
		{
			return true;
		}

		if ( BoxResetMouse( type, x, y ))
			return true;

		{
			const int bx = NewBigDoneLeft( );
			const int by = NewBigButtonTop( );

			if (( x >= serverbrowser_ToScreenX( bx )) &&
				( x < serverbrowser_ToScreenX( bx + NewBigBtnW( ))) &&
				( y >= serverbrowser_ToScreenY( by )) &&
				( y < serverbrowser_ToScreenY( by + SB_DLG_BTN_H )))
			{
				g_NewIwadConfirmHot = true;
				if ( type == MOUSE_Release )
				{
					g_NewModal = NewModal::None;
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
				}
				return true;
			}
		}

		for ( int row = g_NewMapScroll;
			( row < static_cast<int>( g_NewMaps.size( ))) && ( row < g_NewMapScroll + visible );
			++row )
		{
			const int rowY = NewRowY( top, row, g_NewMapScroll );

			if (( y < serverbrowser_ToScreenY( rowY )) ||
				( y >= serverbrowser_ToScreenY( rowY + SB_NEW_ROW_H )))
			{
				continue;
			}

			if (( x < serverbrowser_ToScreenX( left - 4 )) ||
				( x >= serverbrowser_ToScreenX( right )))
			{
				continue;
			}

			g_NewMapHot = row;

			const int button = OrderButtonAt( left, right, rowY, x, y );
			if ( button >= 0 )
			{
				g_NewMapBtnHot = row * 3 + button;

				if ( type == MOUSE_Release )
				{
					g_NewMapSel = row;
					g_NewMapBtnSel = button;

					if ( button == 0 )
						NewMapToggle( row );
					else
						NewMapMove( row, ( button == 1 ) ? -1 : 1 );
				}

				return true;
			}

			if ( type == MOUSE_Release )
			{
				g_NewMapSel = row;
				g_NewMapRevealSel = true;
			}

			return true;
		}

		// Inside the box swallows; outside closes, the same as the other boxes.
		if (( x >= serverbrowser_ToScreenX( NewBigModalLeft( ))) &&
			( x < serverbrowser_ToScreenX( NewBigModalRight( ))) &&
			( y >= serverbrowser_ToScreenY( NewBigModalTop( ))) &&
			( y < serverbrowser_ToScreenY( NewBigModalBottom( ))))
		{
			return true;
		}

		if ( type == MOUSE_Release )
			g_NewModal = NewModal::None;

		return true;
	}

	// [rc4l] Up and down walk the rows, LEFT AND RIGHT WALK THE ROW'S BUTTONS, and Enter presses the
	// one under the cursor.
	//
	// Without the second of those, the three buttons on a row were a mouse-only control: the
	// keyboard could reach the row and not the things on it, so switching a map out or moving one
	// meant reaching for the mouse in the middle of arranging a rotation with the arrows.
	bool NewMapsMenuKey( int mkey )
	{
		if ( g_NewMaps.empty( ))
			return true;

		if (( mkey == MKEY_Up ) || ( mkey == MKEY_Down ))
		{
			const int next = g_NewMapSel + (( mkey == MKEY_Up ) ? -1 : 1 );

			if (( next >= 0 ) && ( next < static_cast<int>( g_NewMaps.size( ))))
			{
				g_NewMapSel = next;
				g_NewMapRevealSel = true;
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
			}

			return true;
		}

		if (( mkey == MKEY_Left ) || ( mkey == MKEY_Right ))
		{
			const int next = zx::ComputeClampedSelection(
				g_NewMapBtnSel + (( mkey == MKEY_Left ) ? -1 : 1 ), 3 );

			if ( next != g_NewMapBtnSel )
			{
				g_NewMapBtnSel = next;
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
			}

			return true;
		}

		if ( mkey == MKEY_Enter )
		{
			if ( g_NewMapBtnSel == 0 )
				NewMapToggle( g_NewMapSel );
			else
				NewMapMove( g_NewMapSel, ( g_NewMapBtnSel == 1 ) ? -1 : 1 );

			return true;
		}

		return true;
	}

	// --- drawing and clicking, once for all three -------------------------------------------------

	void DrawSettingsBox( NewModal which )
	{
		int totalRows = 0;
		const std::vector<BoxItem> items = BuildBox( which, totalRows );

		const std::vector<int> footFields = BoxFooterFields( which );
		const int visible = NewBigVisibleRows( footFields );

		int &scroll = BoxScroll( which );
		scroll = zx::ComputeClampedSelection( scroll, BoxMaxScroll( which ) + 1 );

		// Nothing behind this box is hoverable while it is up. See serverbrowser_ClearTips.
		serverbrowser_ClearTips( );

		// The sliders this box draws are recorded fresh, the same as the hosting panel's own: the
		// rects are what the pointer hits, and last frame's are wherever it was scrolled to then.
		g_HostSliders.Clear( );

		screen->Dim( 0x000000, 0.62f, 0, 0, screen->GetWidth( ), screen->GetHeight( ));

		const zx::PanelColor topCol = { 26, 28, 40, 245 };
		const zx::PanelColor botCol = { 12, 13, 20, 250 };
		DrawRoundedPanel( NewBigModalLeft( ), NewBigModalTop( ),
			NewBigModalRight( ) - NewBigModalLeft( ), NewBigModalBottom( ) - NewBigModalTop( ),
			topCol, botCol, 8 );

		const int left = NewBigContentLeft( );
		const int top = NewBigContentTop( );

		screen->DrawText( SmallFont, CR_GOLD, left, NewBigModalTop( ) + SB_NEW_MODAL_PAD,
			BoxTitle( which ), DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H,
			DTA_KeepRatio, true, TAG_DONE );

		// [rc4l] The orb, parked at the box's own edge unless the cursor is somewhere visible below.
		//
		// Set first and overwritten by the selected item, because the screen BEHIND this box has
		// already anchored it this frame -- the last call in a frame wins, so a box that anchored
		// nothing would leave the orb pointing at a wad row underneath it.
		FocusAnchor( zx::BrowserFocus::Host, NewBigContentLeft( ) - 8, NewBigContentTop( ));

		for ( size_t i = 0; i < items.size( ); ++i )
		{
			const BoxItem &item = items[i];

			const int r = item.row - scroll;
			if (( r < 0 ) || ( r >= visible ))
				continue;

			const int y = top + r * SB_NEW_PILL_ROW_H;

			if ( g_NewBoxSel == static_cast<int>( i ))
			{
				const int anchorX = (( item.kind == BoxItem::Flag ) || ( item.kind == BoxItem::Mode ))
					? left + item.x : left;

				FocusAnchor( zx::BrowserFocus::Host, anchorX - 8, y + SB_NEW_PILL_H / 2 );
			}

			// The pointer and the keyboard mark the same way, so the box reads the same whichever
			// one is being used.
			const bool bHot = ( g_NewBoxHot == static_cast<int>( i )) ||
				( g_NewBoxSel == static_cast<int>( i ));

			switch ( item.kind )
			{
			case BoxItem::Heading:
				// A field's heading is a control -- it opens and shuts. A section heading is not,
				// and stays a label.
				if ( item.field >= 0 )
				{
					DrawFieldHeader( left, y, NewBigContentRight( ) - left, SB_NEW_PILL_H,
						item.text, !NewFieldCollapsed( item.field ), bHot );

					// What is inside, so the heading answers "is what I want in here" without
					// having to be opened first.
					serverbrowser_Tip( left, y, NewBigContentRight( ) - left, SB_NEW_PILL_H,
						zx::FlagFieldHelp( g_NewFlags[item.field].name ));
				}
				else
				{
					screen->DrawText( SmallFont, CR_GOLD, left, y + 2, item.text,
						DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio,
						true, TAG_DONE );
				}
				break;

			case BoxItem::Flag:
			{
				const zx::FlagField &field = g_NewFlags[item.field];
				const std::string &flagName = field.bits[item.bit].name;

				DrawGameplayPill( left + item.x, y, item.width, SB_NEW_PILL_H, flagName.c_str( ),
					zx::FlagIsOn( field.value, field.bits[item.bit].bit ), bHot, false );

				// [rc4l] What the switch does, in a line. A cvar name is not an explanation:
				// nobody ticks compat_plasmabump on the strength of being able to read it.
				serverbrowser_Tip( left + item.x, y, item.width, SB_NEW_PILL_H,
					zx::FlagHelp( flagName ));
				break;
			}

			case BoxItem::Mode:
			{
				const GAMEMODE_e mode = static_cast<GAMEMODE_e>( item.bit );
				DrawGameplayPill( left + item.x, y, item.width, SB_NEW_PILL_H,
					GAMEMODE_GetName( mode ), ( mode == NewChosenGameMode( )), bHot, false );
				break;
			}

			case BoxItem::Setting:
			{
				screen->DrawText( SmallFont, bHot ? CR_WHITE : CR_GRAY, left, y + 2, item.text,
					DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true,
					TAG_DONE );

				if ( item.setting.slider )
				{
					// The hosting panel's slider, drawn here. Same control, same code -- see
					// DrawSliderTrack.
					DrawSliderTrack( SettingSliderId( item.setting ).c_str( ), SettingSliderX( ), y,
						NewBigContentRight( ), item.setting.min,
						SettingSliderMax( item.setting ), SettingNumber( item.setting ),
						SettingValueText( item.setting ).c_str( ), NULL );
				}
				else if ( item.setting.kind == zx::VarKind::Toggle )
				{
					const bool bOn = SettingIsOn( item.setting );
					DrawGameplayPill( SettingControlX( ), y, SettingControlW( ), SB_NEW_PILL_H,
						bOn ? "ON" : "OFF", bOn, bHot, false );
				}
				else
				{
					int firstChar = 0;
					DrawTextField( SettingControlX( ), y, SettingControlW( ), SB_NEW_PILL_H,
						SettingInput( item.setting ),
						( g_NewSettingEditing.Compare( item.setting.name.c_str( )) == 0 ), bHot,
						"0", false, firstChar );
				}
				break;
			}
			}
		}

		if ( totalRows > visible )
		{
			DrawHostRegionScrollBar( top, top + visible * SB_NEW_PILL_ROW_H,
				totalRows * SB_NEW_PILL_ROW_H, scroll * SB_NEW_PILL_ROW_H, NewBigBarX( ));
		}

		DrawFlagFooter( footFields );

		DrawBoxFootButtons( );
	}

	// [rc4l] What the GAMEPLAY box shows, which depends on the mode.
	//
	// The mode's own declared capabilities decide it -- GAMEMODE_GetFlags, parsed from gamemode.txt
	// -- so Duel showing a win limit and Domination showing a point limit are facts read out of the
	// engine rather than rules written here. See servervar_compute.
	std::vector<SettingRow> NewGameplayRows( )
	{
		std::vector<SettingRow> out;

		const GAMEMODE_e mode = NewChosenGameMode( );
		const ULONG flags = GAMEMODE_GetFlags( mode );

		const zx::ModeLimits limits = zx::LimitsForMode(
			( flags & GMF_PLAYERSEARNFRAGS ) != 0,
			( flags & GMF_PLAYERSEARNPOINTS ) != 0,
			( flags & GMF_PLAYERSEARNWINS ) != 0,
			( flags & GMF_USEMAXLIVES ) != 0,
			( flags & GMF_PLAYERSONTEAMS ) != 0 );

		// [rc4l] EVERY row on this box is a slider now.
		//
		// They were a mix: two number boxes for the limits, three sliders, then three more boxes.
		// The mix was the problem -- the same kind of question answered two different ways down one
		// column, so half of them wanted a click and a drag and the other half wanted a caret and
		// the keyboard. A slider is also the honest control for all of these: every one is a small
		// bounded number where the useful values are a short range, not free text.
		//
		// Zero is no limit at all on the four limits, which is why they say so rather than "0".
		// A hundred is the reach, not the answer: the default sits at fifty (twenty-five in a duel)
		// and the top of the track is there for the longer games people do run.
		if ( limits.fraglimit )
			out.push_back( SliderRow( "fraglimit", "Frag limit", 0, 100, "Unlimited" ));

		if ( limits.pointlimit )
			out.push_back( SliderRow( "pointlimit", "Point limit", 0, 50, "Unlimited" ));

		if ( limits.winlimit )
			out.push_back( SliderRow( "winlimit", "Win limit", 0, 5, "Unlimited" ));

		// [rc4l] ONE AT THE BOTTOM, not zero.
		//
		// sv_maxlives 0 means unlimited, and a mode that only exists because lives run out cannot
		// offer that: survival with unlimited lives is co-op, and last man standing with unlimited
		// lives never ends. The floor is the smallest number that still makes the mode itself, so
		// there is no "Unlimited" stop to name here.
		//
		// Every mode that uses lives, not just survival: GMF_USEMAXLIVES is exactly the set where
		// running out is the point.
		if ( limits.lives )
			out.push_back( SliderRow( "sv_maxlives", "Lives", 1, 5 ));

		// [rc4l] NOT in co-op, survival or invasion.
		//
		// A clock decides who was ahead when it runs out, and those three have nobody to be ahead
		// of: the map ends when it is finished or when everyone is dead. Zandronum will still cut
		// the map short if a limit is set, which is a way to lose a co-op run to a number nobody
		// meant to set. The three share GMF_COOPERATIVE, so that is the test.
		if (( flags & GMF_COOPERATIVE ) == 0 )
			out.push_back( SliderRow( "timelimit", "Time limit (minutes)", 0, 20, "Unlimited" ));

		// Two to four, the same stops the PRESETS tab's teams slider offers -- see teamspick_compute.
		// One team is not a team game and the engine has colours for four.
		if ( limits.teams )
			out.push_back( SliderRow( "sv_maxteams", "Teams", 2, 4 ));

		// [rc4l] ONE room size, not two.
		//
		// sv_maxplayers and sv_maxclients are separate cvars because a server can hold spectators
		// beyond the players in the game, but two controls set to the same number every time are one
		// question asked twice -- and the pair disagreeing is a server that silently seats fewer
		// people than it advertises. Both are written by SettingApplyNumber, which is where the
		// slider and the typed box meet.
		//
		// The bounds are the ones SettingSanitise held typed text to: under two is not a server, and
		// past 64 is more than the protocol carries.
		out.push_back( SliderRow( "sv_maxplayers", "Players", 2, 64 ));

		// [rc4l] Named stops, because the number is not what anybody means. Zandronum's skills are
		// 0 to 4 and everyone calls them by name.
		{
			SettingRow skill = SliderRow( "skill", "Skill", 0, 4 );

			skill.names.push_back( "I'm too young to die" );
			skill.names.push_back( "Hey, not too rough" );
			skill.names.push_back( "Hurt me plenty" );
			skill.names.push_back( "Ultra-Violence" );
			skill.names.push_back( "Nightmare" );

			out.push_back( skill );
		}

		return out;
	}

	std::vector<SettingRow> NewVarRows( )
	{
		const std::vector<zx::ServerVar> &table = zx::ServerVarTable( );

		std::vector<SettingRow> out;
		out.reserve( table.size( ));

		for ( size_t i = 0; i < table.size( ); ++i )
		{
			SettingRow row;
			row.name = table[i].name;
			row.label = table[i].label;
			row.kind = table[i].kind;
			out.push_back( row );
		}

		return out;
	}

	// [rc4l] The chooser, over everything else.
	//
	// A modal rather than a list standing open, for the reason the geometry comment gives: you get
	// one IWAD. It carries the line about WHERE to put them, which has nowhere else to live -- a
	// player whose IWAD is in the wrong folder sees an empty list and no reason for it, and that is
	// the single most likely thing to go wrong on this screen.
	// ---------------------------------------------------------------------------------------------
	//
	// [rc4l] The modal's geometry, in one place, because five things need to agree about it: the
	// panel's height, where the grid starts, how tall the grid is, where the buttons sit, and where
	// a click lands. Two copies of any of those is a control you can see and cannot press.

	int NewModalContentLeft( )	{ return SB_NEW_MODAL_LEFT + SB_NEW_MODAL_PAD; }
	int NewModalContentRight( )	{ return SB_NEW_MODAL_LEFT + SB_NEW_MODAL_W - SB_NEW_MODAL_PAD; }

	// The grid's own width, less the bar's lane.
	int NewModalGridWidth( )
	{
		return NewModalContentRight( ) - NewModalContentLeft( ) - SB_NEW_MODAL_BAR_W;
	}

	// Where each pill goes. Rebuilt per frame, which is affordable because this list is a couple of
	// dozen entries at most -- unlike the wad list, which is why that one is cached and this is not.
	std::vector<zx::PillPlace> NewIwadPills( )
	{
		const std::vector<std::string> &iwads = NewIwads( );

		// Measured the way the gameplay row measures its own pills, dot lane included, so these are
		// the same shape as the mix pills rather than a near miss.
		std::vector<int> widths;
		widths.reserve( iwads.size( ));
		for ( size_t i = 0; i < iwads.size( ); ++i )
			widths.push_back( GameplayPillW( iwads[i].c_str( )));

		return zx::FlowPills( widths, NewModalGridWidth( ), SB_NEW_PILL_HGAP );
	}

	int NewModalGridTop( )
	{
		return SB_NEW_MODAL_TOP + SB_NEW_MODAL_PAD + SB_NEW_LINE + 8;
	}

	// How many rows are on screen, and how many there are in total. The second is the whole reason
	// the layout is computed rather than drawn: it depends on the player's filenames.
	int NewModalVisibleRows( )
	{
		return SB_NEW_MODAL_ROWS;
	}

	int NewModalGridHeight( )
	{
		const int rows = MIN( PillFlowRowCountOf( ), NewModalVisibleRows( ));
		return MAX( 1, rows ) * SB_NEW_PILL_ROW_H;
	}

	int PillFlowRowCountOf( )
	{
		return MAX( 1, zx::PillFlowRowCount( NewIwadPills( )));
	}

	int NewModalFootTop( )
	{
		return NewModalGridTop( ) + NewModalGridHeight( ) + 10;
	}

	int NewModalHeight( )
	{
		// grid, then the two-line "where to put them" note, then the button row.
		return ( NewModalFootTop( ) - SB_NEW_MODAL_TOP ) + SB_NEW_LINE * 2 + 8 + SB_DLG_BTN_H
			+ SB_NEW_MODAL_PAD;
	}

	int NewModalButtonTop( )
	{
		return NewModalFootTop( ) + SB_NEW_LINE * 2 + 8;
	}

	// CONFIRM's width and left edge, in one place, because the draw and the hit test both need them
	// and a button you can see and cannot press is what two copies produce.
	int NewModalConfirmW( )
	{
		return MAX( 80, GameplayPillW( "CONFIRM" ));
	}

	int NewModalConfirmLeft( )
	{
		return SB_NEW_MODAL_LEFT + ( SB_NEW_MODAL_W - NewModalConfirmW( )) / 2;
	}

	void DrawNewIwadModal( )
	{
		const std::vector<std::string> &iwads = NewIwads( );
		const std::vector<zx::PillPlace> placed = NewIwadPills( );
		const int totalRows = zx::PillFlowRowCount( placed );
		const int visibleRows = NewModalVisibleRows( );

		// Everything behind it goes quiet, which is what makes it modal rather than another panel --
		// its tooltips included. See serverbrowser_ClearTips.
		serverbrowser_ClearTips( );
		screen->Dim( 0x000000, 0.55f, 0, 0, screen->GetWidth( ), screen->GetHeight( ));

		const zx::PanelColor topCol = { 26, 28, 40, 245 };
		const zx::PanelColor botCol = { 12, 13, 20, 250 };
		DrawRoundedPanel( SB_NEW_MODAL_LEFT, SB_NEW_MODAL_TOP, SB_NEW_MODAL_W, NewModalHeight( ),
			topCol, botCol, 8 );

		const int left = NewModalContentLeft( );
		const int right = NewModalContentRight( );

		screen->DrawText( SmallFont, CR_GOLD, left, SB_NEW_MODAL_TOP + SB_NEW_MODAL_PAD,
			"CHOOSE A GAME",
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );

		// REFRESH shares the title's line, where there is room and nothing else wants to be.
		{
			const int w = PillW( "REFRESH", SB_NEW_PILL_PAD );
			DrawRoundedButton( right - w, SB_NEW_MODAL_TOP + SB_NEW_MODAL_PAD - 2, w, SB_NEW_PILL_H,
				"REFRESH", g_NewIwadRefreshHot );

			serverbrowser_Tip( right - w, SB_NEW_MODAL_TOP + SB_NEW_MODAL_PAD - 2, w, SB_NEW_PILL_H,
				"Look again, for a file you have just put there" );
		}

		const int gridTop = NewModalGridTop( );

		if ( iwads.empty( ))
		{
			DrawNewRowText( left, gridTop, CR_DARKGRAY, "None found" );
		}
		else
		{
			g_NewIwadModalSel = zx::ComputeClampedSelection( g_NewIwadModalSel,
				static_cast<int>( iwads.size( )));

			// [rc4l] The view follows the SELECTION only when the selection just moved.
			//
			// Doing it every frame is the obvious way and it breaks both the wheel and the bar: they
			// scroll, and the next frame drags the view straight back to wherever the selected pill
			// is. Same rule the server list follows -- the keyboard drags the view along with it,
			// and scrolling leaves the selection alone.
			if ( g_NewIwadRevealSel )
			{
				NewClampScroll( placed[g_NewIwadModalSel].row, totalRows, visibleRows,
					g_NewIwadModalScroll );
				g_NewIwadRevealSel = false;
			}
			else
			{
				g_NewIwadModalScroll = zx::ComputeClampedSelection( g_NewIwadModalScroll,
					MAX( 1, totalRows - visibleRows + 1 ));
			}

			for ( size_t i = 0; i < placed.size( ); ++i )
			{
				const int row = placed[i].row - g_NewIwadModalScroll;
				if (( row < 0 ) || ( row >= visibleRows ))
					continue;

				const int px = left + placed[i].x;
				const int py = gridTop + row * SB_NEW_PILL_ROW_H;

				// [rc4l] The SAME pill the gameplay panel's MIX row is made of: green when chosen,
				// with the halo and the lit dot. Shared rather than imitated, because two drawings
				// of one control drift apart the first time either is touched -- and this one had
				// already drifted, having first been built out of a different row of pills entirely.
				DrawGameplayPill( px, py, placed[i].width, SB_NEW_PILL_H, iwads[i].c_str( ),
					( static_cast<int>( i ) == g_NewIwadModalSel ),
					( static_cast<int>( i ) == g_NewIwadModalHot ), false );

				// [rc4l] The orb comes INTO the box with the keyboard. The screen behind has already
				// anchored it this frame, so a modal that anchored nothing left it marking a row
				// underneath -- pointing at a control that cannot be reached from here.
				if ( static_cast<int>( i ) == g_NewIwadModalSel )
					FocusAnchor( zx::BrowserFocus::Host, px - 8, py + SB_NEW_PILL_H / 2 );
			}

			// [rc4l] Only when there is something to scroll, and IN THIS BOX.
			//
			// DrawHostRegionScrollBar defaults its x to the host panel's own column, which is
			// somewhere else entirely -- so the bar was drawn, correctly, behind the dim and off
			// the side of the modal. It looked exactly like no bar at all.
			if ( totalRows > visibleRows )
			{
				DrawHostRegionScrollBar( gridTop, gridTop + visibleRows * SB_NEW_PILL_ROW_H,
					totalRows * SB_NEW_PILL_ROW_H, g_NewIwadModalScroll * SB_NEW_PILL_ROW_H,
					right - SB_NEW_MODAL_BAR_W + 2 );
			}
		}

		int y = NewModalFootTop( );

		screen->DrawText( SmallFont, CR_DARKGRAY, left, y, "Put IWADs here to see them listed:",
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
		y += SB_NEW_LINE;

		screen->DrawText( SmallFont, CR_GRAY, left,
			y, serverbrowser_FitName( NewIwadDropPath( ), right - left ),
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );

		// [rc4l] The whole path on hover. It is cut to fit, and a path cut off mid-folder is not
		// somewhere anybody can go -- which makes the line useless for the one thing it is for.
		serverbrowser_Tip( left, y, right - left, SB_NEW_LINE, NewIwadDropPath( ));

		// [rc4l] CONFIRM does exactly what clicking a pill does. It is here because a modal with no
		// way to say yes reads as unfinished even when every click in it already means yes.
		DrawRoundedButton( NewModalConfirmLeft( ), NewModalButtonTop( ), NewModalConfirmW( ),
			SB_DLG_BTN_H, "CONFIRM", g_NewIwadConfirmHot );
	}

	void DrawNewSearch( )
	{
		const bool bFocused = ( g_NewFocus == NewFocus::Search ) &&
			( g_Focus == zx::BrowserFocus::Host );

		if ( bFocused )
		{
			FocusAnchor( zx::BrowserFocus::Host, SB_HOST_LIST_LEFT - 5,
				SB_NEW_SEARCH_TOP + SB_NEW_SEARCH_H / 2 );
		}

		// [rc4l] The same drawer the server search uses, which is how this box gets a caret that
		// blinks, a visible selection, and text that scrolls to follow what you are typing. Drawn by
		// hand it had none of those and looked finished, which is the worst way to be wrong.
		DrawTextField( SB_HOST_LIST_LEFT, SB_NEW_SEARCH_TOP,
			SB_HOST_LIST_RIGHT - SB_HOST_LIST_LEFT, SB_NEW_SEARCH_H, g_NewSearch, bFocused,
			g_NewSearchHot, "SEARCH YOUR WADS", false, g_NewSearchFirstChar );

		serverbrowser_Tip( SB_HOST_LIST_LEFT, SB_NEW_SEARCH_TOP,
			SB_HOST_LIST_RIGHT - SB_HOST_LIST_LEFT, SB_NEW_SEARCH_H,
			"Filter your files by name\nUpper and lower case are the same" );
	}

	void DrawNewWads( )
	{
		const zx::wadlibrary::ScanState state = zx::wadlibrary::State( );
		const std::vector<zx::LibraryFile> &files = zx::wadlibrary::Files( );
		const std::vector<zx::LibraryRow> &rows = NewRows( );

		// The heading carries the count, because on this screen "how many have I got" is the first
		// thing anybody wants to know and there is nowhere else to say it.
		FString heading;
		if ( state == zx::wadlibrary::ScanState::Running )
			heading = "YOUR WADS  (looking...)";
		else if ( state == zx::wadlibrary::ScanState::Failed )
			heading = "YOUR WADS  (nowhere to look)";
		else if ( g_NewSearch.text.empty( ))
			heading.Format( "YOUR WADS  (%d)", static_cast<int>( rows.size( )));
		else
			heading.Format( "YOUR WADS  (%d of %d)", static_cast<int>( rows.size( )),
				static_cast<int>( files.size( )));

		screen->DrawText( SmallFont, CR_WHITE, SB_HOST_LIST_LEFT, SB_NEW_SEARCH_TOP - SB_NEW_LINE - 2,
			heading, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true,
			TAG_DONE );

		if ( rows.empty( ))
		{
			DrawNewRowText( SB_HOST_LIST_LEFT, SB_NEW_WADS_TOP, CR_DARKGRAY,
				( state == zx::wadlibrary::ScanState::Running ) ? "Looking through your folders"
					: "Nothing here matches" );
			return;
		}

		const int visible = NewWadRowsVisible( );

		// [rc4l] The view follows the SELECTION only when the selection just moved. Doing it every
		// frame drags the list back to the cursor the moment the wheel or the bar moves it, which is
		// the same trap the IWAD grid had.
		if ( g_NewWadRevealSel )
		{
			NewClampScroll( g_NewWadSel, static_cast<int>( rows.size( )), visible, g_NewWadScroll );
			g_NewWadRevealSel = false;
		}
		else
		{
			g_NewWadScroll = zx::ComputeClampedSelection( g_NewWadScroll,
				MAX( 1, static_cast<int>( rows.size( )) - visible + 1 ));
		}

		for ( int row = g_NewWadScroll;
			row < static_cast<int>( rows.size( )) && row < g_NewWadScroll + visible; ++row )
		{
			const int rowY = NewRowY( SB_NEW_WADS_TOP, row, g_NewWadScroll );
			const zx::LibraryFile &file = files[rows[row].index];
			const bool bSel = ( row == g_NewWadSel );

			DrawNewRowHighlight( SB_HOST_LIST_LEFT - 4, SB_NEW_WADS_RIGHT, rowY, bSel,
				( row == g_NewWadHot ));

			// [rc4l] The travelling marker, on the selected row, in the same place relative to the
			// text that the catalogue list puts its own. Anchoring rather than drawing is what makes
			// it slide: DrawFocusTravel moves the one marker toward whatever asked for it this
			// frame, so crossing from the search box to a row glides instead of jumping.
			if ( bSel && ( g_NewFocus == NewFocus::Wads ) && ( g_Focus == zx::BrowserFocus::Host ))
				FocusAnchor( zx::BrowserFocus::Host, SB_HOST_LIST_LEFT - 9, rowY + SB_NEW_ROW_H / 2 );

			// [rc4l] Size and folder are not decoration. Two rows of one name differ only by those,
			// and this list is the only place the player can tell them apart before choosing.
			FString right = zx::FormatByteSize( static_cast<unsigned long long>( file.size )).c_str( );
			if ( rows[row].copies > 1 )
				right.AppendFormat( "  x%d", rows[row].copies );

			const int rightW = SmallFont->StringWidth( right );

			const FString name = serverbrowser_FitName( file.name.c_str( ),
				SB_NEW_WADS_RIGHT - SB_HOST_LIST_LEFT - rightW - 10 );

			// [rc4l] Green once it is IN the load order, the same green the browser tints a row it
			// is being served by. It survives the selection moving away, which is the whole point:
			// on a list of twenty thousand the question "have I already taken this one" cannot be
			// answered by looking at the other panel every time.
			const bool bAdded = NewIsAdded( file.path );

			DrawNewRowText( SB_HOST_LIST_LEFT, rowY,
				bAdded ? CR_GREEN : ( bSel ? CR_WHITE : CR_GRAY ), name );

			screen->DrawText( SmallFont, CR_DARKGRAY, SB_NEW_WADS_RIGHT - rightW,
				NewRowTextY( rowY ), right,
				DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
		}

		// [rc4l] The bar, in the gutter the catalogue list's own bar uses, and only when there is
		// something to scroll. On a collection of any size this list is the one thing on the screen
		// that always overflows, so it is also the one that most needed saying how far down it is.
		DrawHostRegionScrollBar( SB_NEW_WADS_TOP, SB_NEW_WADS_BOTTOM,
			static_cast<int>( rows.size( )) * SB_NEW_ROW_H, g_NewWadScroll * SB_NEW_ROW_H,
			SB_HOST_LBAR_X );
	}

	// [rc4l] The three buttons on a load-order row, in one place because the draw and the hit test
	// both need them and a button you can see and cannot press is what two copies produce.
	//
	// Widths are fixed rather than measured: they hold single glyphs, and a column of buttons that
	// changed width with the glyph inside it would not line up down the list.
	// [rc4l] Taking a left and a right rather than reading the NEW screen's own column, because the
	// map list is the same control in a different box. The four wrappers below are the load order's
	// own columns, so every existing caller reads as it did.
	int OrderXLeft( int left )			{ return left; }
	int OrderNameLeft( int left )		{ return left + SB_NEW_ORDER_BTN_W + 4; }
	int OrderUpLeft( int right )		{ return right - SB_NEW_ORDER_BTN_W * 2 - 3; }
	int OrderDownLeft( int right )		{ return right - SB_NEW_ORDER_BTN_W; }

	int NewOrderXLeft( )		{ return OrderXLeft( SB_HOST_RCOL_LEFT ); }
	int NewOrderNameLeft( )		{ return OrderNameLeft( SB_HOST_RCOL_LEFT ); }
	int NewOrderUpLeft( )		{ return OrderUpLeft( SB_HOST_RCOL_RIGHT ); }
	int NewOrderDownLeft( )		{ return OrderDownLeft( SB_HOST_RCOL_RIGHT ); }

	void DrawNewOrderButton( int vx, int rowY, const char *glyph, bool bHot, bool bEnabled = true )
	{
		const int base = !bEnabled ? 16 : ( bHot ? 46 : 28 );
		const zx::PanelColor topCol = { static_cast<BYTE>( base ), static_cast<BYTE>( base ),
			static_cast<BYTE>( base + 10 ), 220 };
		const zx::PanelColor botCol = { static_cast<BYTE>( base / 2 ), static_cast<BYTE>( base / 2 ),
			static_cast<BYTE>( base / 2 + 8 ), 230 };

		DrawRoundedPanel( vx, rowY + 1, SB_NEW_ORDER_BTN_W, SB_NEW_ROW_H - 2, topCol, botCol, 3 );

		const int w = SmallFont->StringWidth( glyph );
		screen->DrawText( SmallFont, bEnabled ? ( bHot ? CR_WHITE : CR_GRAY ) : CR_DARKGRAY,
			vx + ( SB_NEW_ORDER_BTN_W - w ) / 2, NewRowTextY( rowY ), glyph,
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
	}

	// [rc4l] The leftmost button as a TOGGLE rather than a glyph, for a row that is switched rather
	// than emptied.
	//
	// Same footprint as DrawNewOrderButton, and the mark inside is the one the pills use -- a lit
	// dot with a halo. A glyph could only say what PRESSING it would do ("X" to take out, "+" to put
	// back), which means the row tells you its state by describing its opposite, and every row you
	// have switched reads as the inverse of every row you have not. A dot says what IS.
	//
	// No label: this button is one glyph wide by construction, and the pill's own text would not
	// fit. The dot is the part that carries the meaning anyway; see DrawGameplayPill.
	void DrawNewOrderToggle( int vx, int rowY, bool bOn, bool bHot )
	{
		const int base = bHot ? 46 : 28;
		const zx::PanelColor topCol = { static_cast<BYTE>( base ), static_cast<BYTE>( base ),
			static_cast<BYTE>( base + 10 ), 220 };
		const zx::PanelColor botCol = { static_cast<BYTE>( base / 2 ), static_cast<BYTE>( base / 2 ),
			static_cast<BYTE>( base / 2 + 8 ), 230 };

		DrawRoundedPanel( vx, rowY + 1, SB_NEW_ORDER_BTN_W, SB_NEW_ROW_H - 2, topCol, botCol, 3 );

		const int dotX = vx + ( SB_NEW_ORDER_BTN_W - SB_HOST_PILL_DOT ) / 2;
		const int dotY = rowY + 1 + ( SB_NEW_ROW_H - 2 - SB_HOST_PILL_DOT ) / 2;

		if ( bOn )
		{
			zx::PanelColor halo;
			halo.r = 90; halo.g = 235; halo.b = 120; halo.a = 60;
			DrawRoundedPanel( dotX - 2, dotY - 2, SB_HOST_PILL_DOT + 4, SB_HOST_PILL_DOT + 4,
				halo, halo, ( SB_HOST_PILL_DOT + 4 ) / 2 );
		}

		zx::PanelColor dot;
		if ( bOn )	{ dot.r = 120; dot.g = 255; dot.b = 150; dot.a = 255; }
		else		{ dot.r = 96;  dot.g = 102; dot.b = 124; dot.a = 220; }

		DrawRoundedPanel( dotX, dotY, SB_HOST_PILL_DOT, SB_HOST_PILL_DOT, dot, dot,
			SB_HOST_PILL_DOT / 2 );
	}

	// [rc4l] One row of an ordered list: remove, the numbered name, and the two arrows.
	//
	// Written once and used by the load order and by the map list, which are the same control over
	// different things -- a list where the POSITION is the meaning. `btnHot` is 0, 1 or 2 for the
	// button under the pointer, or -1.
	// `firstGlyph` is what the leftmost button says: the load order removes a file, and the map list
	// switches a map in and out, which is a different act and says so.
	// `bToggle` makes the leftmost button a switch showing its state rather than a glyph naming an
	// act -- the map list, where a row is in or out. `firstGlyph` is what it says when it is not.
	void DrawOrderRow( int left, int right, int rowY, int index, const char *label, bool bSel,
		bool bHot, int btnHot, bool bFirst, bool bLast, bool bNumbered,
		const char *firstGlyph = "X", bool bDim = false, bool bToggle = false, bool bOn = false )
	{
		DrawNewRowHighlight( left - 4, right, rowY, bSel, bHot );

		// The switch or the X, then the name, then the two arrows: the order the eye reads them is
		// the order they matter in. The first is the one you reach for; moving is fiddly, so it sits
		// at the far end where it cannot be hit on the way to anything else.
		if ( bToggle )
			DrawNewOrderToggle( OrderXLeft( left ), rowY, bOn, ( btnHot == 0 ));
		else
			DrawNewOrderButton( OrderXLeft( left ), rowY, firstGlyph, ( btnHot == 0 ));

		// [rc4l] Numbered only where the number says something the list does not.
		//
		// The load order keeps it: "3." is how a patch is quoted as going after what it patches,
		// and it is the answer to "which of these wins". A rotation reads top to bottom and that IS
		// the order, so a column of numbers there is a column of noise beside the names.
		FString line;
		if ( bNumbered )
			line.Format( "%d. ", index + 1 );

		line += serverbrowser_FitName( label, OrderUpLeft( right ) - OrderNameLeft( left ) - 6 );

		// Dim says "on the list but not in play". The state is on the ROW rather than on the button,
		// because the button says what pressing it would do and those are different things.
		DrawNewRowText( OrderNameLeft( left ), rowY,
			bDim ? CR_DARKGRAY : ( bSel ? CR_WHITE : CR_GRAY ), line );

		// [rc4l] Drawn dark at the ends where they cannot go anywhere, rather than hidden. A button
		// that vanishes on the first and last row makes the row change shape as the selection passes
		// over it, and the eye reads that as the list moving.
		DrawNewOrderButton( OrderUpLeft( right ), rowY, "^", ( btnHot == 1 ), !bFirst );
		DrawNewOrderButton( OrderDownLeft( right ), rowY, "v", ( btnHot == 2 ), !bLast );
	}

	// Which of a row's three buttons a pointer is over, or -1. The draw and the hit test ask the
	// same function, so a button you can see is a button you can press.
	int OrderButtonAt( int left, int right, int rowY, int x, int y )
	{
		if (( y < serverbrowser_ToScreenY( rowY )) ||
			( y >= serverbrowser_ToScreenY( rowY + SB_NEW_ROW_H )))
		{
			return -1;
		}

		const int lanes[3] = { OrderXLeft( left ), OrderUpLeft( right ), OrderDownLeft( right ) };

		for ( int i = 0; i < 3; ++i )
		{
			if (( x >= serverbrowser_ToScreenX( lanes[i] )) &&
				( x < serverbrowser_ToScreenX( lanes[i] + SB_NEW_ORDER_BTN_W )))
			{
				return i;
			}
		}

		return -1;
	}

	void DrawNewOrder( )
	{
		FString heading;
		heading.Format( "LOAD ORDER  (%d)", static_cast<int>( g_NewOrder.size( )));

		screen->DrawText( SmallFont, CR_GOLD, SB_HOST_RCOL_LEFT, SB_NEW_TOP, heading,
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );

		if ( g_NewOrder.empty( ))
		{
			// [rc4l] "Loading." while the last-played setup is being resolved on a worker, so the
			// screen is not telling somebody to pick files a moment before it fills itself in.
			DrawNewRowText( SB_HOST_RCOL_LEFT, SB_NEW_ORDER_TOP, CR_DARKGRAY,
				NewRestorePending( ) ? LoadingText( ) : "Pick files on the left" );
			return;
		}

		g_NewOrderSel = zx::ComputeClampedSelection( g_NewOrderSel,
			static_cast<int>( g_NewOrder.size( )));

		const int visible = NewOrderRowsVisible( );

		if ( g_NewOrderRevealSel )
		{
			NewClampScroll( g_NewOrderSel, static_cast<int>( g_NewOrder.size( )), visible,
				g_NewOrderScroll );
			g_NewOrderRevealSel = false;
		}
		else
		{
			g_NewOrderScroll = zx::ComputeClampedSelection( g_NewOrderScroll,
				MAX( 1, static_cast<int>( g_NewOrder.size( )) - visible + 1 ));
		}

		for ( int row = g_NewOrderScroll;
			row < static_cast<int>( g_NewOrder.size( )) && row < g_NewOrderScroll + visible; ++row )
		{
			const int rowY = NewRowY( SB_NEW_ORDER_TOP, row, g_NewOrderScroll );
			const bool bSel = ( row == g_NewOrderSel );

			// The same marker on this side, so the keyboard is never in a place that does not say so.
			if ( bSel && ( g_NewFocus == NewFocus::Order ) && ( g_Focus == zx::BrowserFocus::Host ))
				FocusAnchor( zx::BrowserFocus::Host, SB_HOST_RCOL_LEFT - 9, rowY + SB_NEW_ROW_H / 2 );

			int btnHot = (( g_NewOrderBtnHot >= row * 3 ) && ( g_NewOrderBtnHot < row * 3 + 3 ))
				? ( g_NewOrderBtnHot - row * 3 ) : -1;

			// The keyboard's cursor shows where the pointer is not, so the row says which button
			// Enter would press.
			if (( btnHot < 0 ) && bSel && ( g_NewOrderHot < 0 ) &&
				( g_NewFocus == NewFocus::Order ) && ( g_Focus == zx::BrowserFocus::Host ))
			{
				btnHot = g_NewOrderBtnSel;
			}

			// [rc4l] Unnumbered: a list you can drag says its order by BEING in that order, and the
			// numbers were a second copy of that which has to be re-read every time a row moves.
			// The maps list dropped them for the same reason. The read-only list in the CUSTOM tab's
			// maps box keeps its numbers, because there the position is all it has -- nothing can be
			// moved, so nothing demonstrates the order.
			DrawOrderRow( SB_HOST_RCOL_LEFT, SB_HOST_RCOL_RIGHT, rowY, row,
				g_NewOrder[row].name.c_str( ), bSel, ( row == g_NewOrderHot ), btnHot,
				( row == 0 ), ( row + 1 == static_cast<int>( g_NewOrder.size( ))), false );
		}

		// Outside the rows, where the host panel's own right-column bars sit, so the buttons at the
		// end of each row keep their full width.
		DrawHostRegionScrollBar( SB_NEW_ORDER_TOP, SB_NEW_ORDER_BOTTOM,
			static_cast<int>( g_NewOrder.size( )) * SB_NEW_ROW_H, g_NewOrderScroll * SB_NEW_ROW_H,
			SB_HOST_BAR_X );
	}

	// ---------------------------------------------------------------------------------------------
	//
	// [rc4l] THE CUSTOM TAB: the player's own presets.
	//
	// The same list the NEW screen's wad list is: a search box over rows, because a player with
	// forty saved setups is looking for one by name rather than reading the lot. The three buttons
	// below say what can be done with the one selected.
	//
	// A preset whose files are not on this machine is still SHOWN. Hiding it would leave somebody
	// wondering where a preset went; saying what is missing lets them fetch it, which is what PLAY
	// NOW does. Only EDIT is refused, because prefilling the NEW screen with files it cannot resolve
	// would build a load order full of gaps.

	int CustomListTop( )		{ return SB_NEW_SEARCH_TOP + SB_NEW_SEARCH_H + 6; }
	int CustomListBottom( )		{ return SB_NEW_TOOL_Y - 8; }
	int CustomRowsVisible( )	{ return ( CustomListBottom( ) - CustomListTop( )) / SB_NEW_ROW_H; }

	// [rc4l] The detail column, in the place and the shape the PRESETS tab's is: same left and right
	// edges, same backdrop, same wrap width, same rule about which rows are drawn. Two tabs showing
	// what a thing IS should not look like two different screens.
	int CustomDetailTop( )		{ return SB_NEW_TOP + SB_NEW_LINE + 2 + SB_HOST_RCOL_INSET; }
	int CustomDetailBottom( )	{ return SB_NEW_TOOL_Y - 8; }
	int CustomDetailLineH( )	{ return SmallFont->GetHeight( ) + 1; }
	int CustomDetailRows( )		{ return ( CustomDetailBottom( ) - CustomDetailTop( )) / CustomDetailLineH( ); }

	// The buttons across the bottom, in the tool row's own lane.
	int CustomBtnW( )			{ return SB_NEW_TOOL_W; }
	int CustomBtnLeft( int i )	{ return NewToolLeft( i ); }

	// Reloaded from disk when something changes it, not per frame: this reads a folder.
	const std::vector<zx::CustomEntry> &CustomEntries( )
	{
		if ( !g_CustomLoaded )
		{
			g_CustomAll = zx::CustomAll( );
			g_CustomLoaded = true;
		}

		return g_CustomAll;
	}

	// [rc4l] Everything read from the preset folder is stale, INCLUDING what is keyed by name.
	//
	// The counter is the half that was missing. Saving over an existing preset changes what is in
	// it and not what it is called, so a cache keyed on the name alone answered with the old
	// contents forever -- you replaced a preset and the column went on describing the one you had
	// replaced. Bumping this changes the key without anything having to guess what changed.
	void CustomForget( )
	{
		g_CustomLoaded = false;
		++g_CustomGeneration;
	}

	// Which rows the search leaves, as indices into the list. The same folding the wad search uses,
	// so "sunday" finds "Sunday co-op".
	std::vector<int> CustomRows( )
	{
		const std::vector<zx::CustomEntry> &all = CustomEntries( );
		const std::string key = zx::SearchFold( g_CustomSearch.text );

		std::vector<int> out;

		for ( size_t i = 0; i < all.size( ); ++i )
		{
			if ( !key.empty( ) && ( zx::SearchFold( all[i].name ).find( key ) == std::string::npos ))
				continue;

			out.push_back( static_cast<int>( i ));
		}

		return out;
	}

	// [rc4l] Where a file is, ANSWERED ONCE.
	//
	// FindVerifiedCopy is not a lookup: it walks every search path and then MD5s each candidate,
	// because a name is not proof of contents. That is the right answer to ask before hosting and
	// the wrong thing to do while drawing -- and this asked it per file, per visible row, EVERY
	// FRAME, and again for the detail column from the draw, the pointer and the wheel. A preset
	// naming a 127MB wad hashed it several times a frame, which is exactly as slow as it sounds.
	//
	// Kept against a generation rather than forever: files appear while the menu is open, and
	// HostFilesChanged already exists to say so.
	// [rc4l] ANSWERED ON A WORKER, and until it answers the row says so.
	//
	// The memo was the first half of this fix and the cheaper one: it stopped the hashing happening
	// per frame. It could not stop the FIRST answer costing what it costs, and that is the whole of
	// the remaining stall -- one preset naming 157MB measured at 156ms, which is ten frames gone the
	// moment the tab opens. Nothing about drawing a list should ever read 157MB.
	//
	// So the question goes to features/wad-download/zx_resolvejob and the rows draw "Loading..."
	// until it comes back. See that header for why the worker is safe: it is handed plain paths and
	// hands plain paths back, and it never sees a preset, an FString or this cache.
	// ResolvedFile and g_CustomResolved live at file scope with the rest of this screen's state.
	std::string CustomResolveKey( const std::string &name, const std::string &md5 )
	{
		return name + "|" + md5;
	}

	// [rc4l] The one place that notices the answers could have changed, and the only thing that
	// empties the cache.
	//
	// Two counters feed it -- files appearing on disk (g_HostHaveGeneration) and presets being
	// written (g_CustomGeneration) -- and neither is monotonic in a way a job can be stamped with.
	// So this folds both into one number that only ever goes up, which is what a result arriving
	// late is compared against before it is believed.
	// [rc4l] ONE counter for every claim on the resolver, because there is one worker and two screens
	// that want it. A per-screen counter would hand both the same numbers and each would eventually
	// apply the other's answers -- the CUSTOM list's verdicts landing in the NEW tab's restore.
	int NextVerifyToken( )
	{
		static int token = 0;
		return ++token;
	}

	int CustomVerifyEpoch( )
	{
		static int epoch = 0;
		static std::vector<int> last;

		std::vector<int> now;
		now.push_back( g_HostHaveGeneration );
		now.push_back( g_CustomGeneration );

		bool bChanged = false;
		epoch = zx::JobNextEpoch( epoch, last, now, bChanged );

		if ( bChanged )
		{
			last = now;
			g_CustomResolved.clear( );

			// [rc4l] THE CLAIM GOES WITH THE CACHE. A run already in flight is answering the
			// question as it stood a moment ago; leaving the token set would have its result
			// claimed and written into the cache this just emptied, which is the stale answer the
			// clearing was for. Dropping the claim is what makes the result unclaimable.
			g_CustomVerifyToken = -1;

			// And it stops reading files nobody is waiting for.
			zx::resolvejob::Cancel( );
		}

		return epoch;
	}

	enum class ResolveState
	{
		Pending,		// asked, or about to be; no answer yet
		Found,
		Missing,
	};

	ResolveState CustomResolve( const std::string &name, const std::string &md5,
		std::string *outPath = NULL )
	{
		const std::string key = CustomResolveKey( name, md5 );

		for ( size_t i = 0; i < g_CustomResolved.size( ); ++i )
		{
			if ( g_CustomResolved[i].key != key )
				continue;

			if ( outPath != NULL )
				*outPath = g_CustomResolved[i].path;

			return g_CustomResolved[i].path.empty( ) ? ResolveState::Missing : ResolveState::Found;
		}

		return ResolveState::Pending;
	}

	// [rc4l] Drain what came back, then ask about anything still unanswered. Called from the CUSTOM
	// draw, which is the only screen that needs it.
	//
	// EVERY preset's files go in ONE job rather than a job per row. A list of presets is tens of
	// files, the thread is the expensive part, and answering them together means the rows settle at
	// once instead of popping in one at a time while somebody is reading them.
	void CustomVerifyPump( )
	{
		// Bumps the epoch and empties the cache when the answers could have changed. Its return is
		// not used here; what matters is that it runs before anything is read.
		CustomVerifyEpoch( );

		// [rc4l] AN EDIT COMES FIRST. It is a press somebody is waiting on; the row colouring is
		// something they have not asked for and will not miss for another frame. Both want the one
		// worker, so the order here is the whole of that priority.
		if ( g_CustomEditToken >= 0 )
		{
			std::vector<zx::resolvejob::Answer> answers;

			if ( zx::resolvejob::Tick( g_CustomEditToken, answers ))
			{
				g_CustomEditToken = -1;
				CustomEditLanded( answers );
			}

			return;
		}

		if ( g_CustomEditWanted )
		{
			// Keyed by FILENAME, which is what NewApplyEntry looks a resolved path up by.
			std::vector<zx::resolvejob::Want> wants;
			for ( size_t i = 0; i < g_CustomEditEntry.files.size( ); ++i )
			{
				wants.push_back( zx::resolvejob::Want( g_CustomEditEntry.files[i].name,
					g_CustomEditEntry.files[i].name, g_CustomEditEntry.files[i].md5 ));
			}

			const int token = NextVerifyToken( );

			// Begin refuses while the row verification still holds the worker; the press stays
			// wanted and this tries again next frame rather than being dropped.
			if ( zx::resolvejob::Begin( wants, token ))
			{
				g_CustomEditWanted = false;
				g_CustomEditToken = token;
			}

			return;
		}

		if ( g_CustomVerifyToken >= 0 )
		{
			std::vector<zx::resolvejob::Answer> answers;

			if ( zx::resolvejob::Tick( g_CustomVerifyToken, answers ))
			{
				g_CustomVerifyToken = -1;

				for ( size_t i = 0; i < answers.size( ); ++i )
					g_CustomResolved.push_back( ResolvedFile( answers[i].key, answers[i].path ));
			}
		}

		if ( zx::resolvejob::Running( ))
			return;

		std::vector<zx::resolvejob::Want> wants;
		const std::vector<zx::CustomEntry> &all = CustomEntries( );

		for ( size_t e = 0; e < all.size( ); ++e )
		{
			for ( size_t f = 0; f < all[e].files.size( ); ++f )
			{
				const std::string key = CustomResolveKey( all[e].files[f].name,
					all[e].files[f].md5 );

				if ( CustomResolve( all[e].files[f].name, all[e].files[f].md5 ) !=
					ResolveState::Pending )
				{
					continue;
				}

				// The same file named by two presets is one question. Asking twice would hash it
				// twice for an answer that cannot differ.
				bool bAlready = false;
				for ( size_t w = 0; w < wants.size( ); ++w )
				{
					if ( wants[w].key == key )
					{
						bAlready = true;
						break;
					}
				}

				if ( !bAlready )
				{
					wants.push_back( zx::resolvejob::Want( key, all[e].files[f].name,
						all[e].files[f].md5 ));
				}
			}
		}

		const int token = NextVerifyToken( );

		// Only claim it if it actually started. Begin refuses while the NEW tab's restore has the
		// worker, and a token remembered for a run that never began is a claim on somebody else's
		// answers.
		if ( zx::resolvejob::Begin( wants, token ))
			g_CustomVerifyToken = token;
	}

	// [rc4l] What a preset cannot find on this machine. Empty means it is ready to play.
	//
	// THREE THINGS ASK THIS, and they do not want the same answer:
	//
	//   the ROW, to colour itself and say how many are missing;
	//   EDIT, to refuse rather than prefill a load order full of gaps;
	//   PLAY NOW, to decide whether to spend somebody's bandwidth before starting a server.
	//
	// The first two are drawing, and drawing happens sixty times a second: they want a remembered
	// answer. The third is an act, happens once, and is about to commit -- so it asks again from
	// scratch. `bFresh` is that distinction, and having it here rather than at each call site is
	// what stops the cheap answer being used for the expensive decision.
	// [rc4l] `bPending` is the third answer, and leaving it out is how "we have not looked yet" gets
	// drawn as "you are missing everything". It is set when ANY file is still unanswered; `missing`
	// then holds only what is known to be absent, which is nothing worth showing until it is
	// complete.
	std::vector<std::string> CustomMissing( const zx::CustomEntry &entry, bool bFresh = false,
		bool *bPending = NULL )
	{
		std::vector<std::string> missing;

		if ( bPending != NULL )
			*bPending = false;

		for ( size_t i = 0; i < entry.files.size( ); ++i )
		{
			if ( bFresh )
			{
				// [rc4l] STILL SYNCHRONOUS, deliberately. This is PLAY NOW and EDIT: an act, once,
				// about to commit, and the player has already accepted a pause by pressing it. A
				// worker here would mean a button that does nothing for a moment and then acts,
				// which is worse than a button that takes a moment.
				const FString path = zx::waddownload::FindVerifiedCopy( entry.files[i].name.c_str( ),
					entry.files[i].md5.empty( ) ? NULL : entry.files[i].md5.c_str( ));

				if ( path.IsEmpty( ))
					missing.push_back( entry.files[i].name );

				continue;
			}

			switch ( CustomResolve( entry.files[i].name, entry.files[i].md5 ))
			{
			case ResolveState::Missing:
				missing.push_back( entry.files[i].name );
				break;

			case ResolveState::Pending:
				if ( bPending != NULL )
					*bPending = true;
				break;

			default:
				break;
			}
		}

		return missing;
	}

	// The one under the cursor, or NULL when the list is empty or the search has emptied it.
	const zx::CustomEntry *CustomSelected( )
	{
		const std::vector<int> rows = CustomRows( );
		if ( rows.empty( ))
			return NULL;

		const int at = zx::ComputeClampedSelection( g_CustomSel, static_cast<int>( rows.size( )));
		return &CustomEntries( )[rows[at]];
	}

	// [rc4l] The empty state's own geometry, in one place because the draw and the hit test both
	// need it and a button you can see and cannot press is what two copies produce.
	//
	// The BLOCK is centred, not the line: text, a gap, and the button are measured together and
	// placed about the panel's middle, so the pair sits where the eye expects rather than the text
	// sitting on the centre line with the button hanging below it.
	int CustomEmptyBtnW( )
	{
		// Sized to its own label rather than to a number somebody guessed. "CREATE ONE HERE" barely
		// fitted 110 and the next label would not have.
		return SmallFont->StringWidth( "CREATE ONE HERE" ) + 28;
	}

	int CustomEmptyBtnLeft( )	{ return ( SB_HOST_LEFT + SB_HOST_RIGHT ) / 2 - CustomEmptyBtnW( ) / 2; }
	int CustomEmptyBlockH( )	{ return SB_NEW_LINE + 10 + SB_HOST_BTN_H; }
	int CustomEmptyTop( )		{ return ( SB_HOST_TOP + SB_HOST_BOTTOM ) / 2 - CustomEmptyBlockH( ) / 2; }
	int CustomEmptyBtnTop( )	{ return CustomEmptyTop( ) + SB_NEW_LINE + 10; }

	void DrawCustomEmpty( )
	{
		// [rc4l] Said plainly, with the way out under it. An empty list with no explanation reads as
		// a screen that failed to load rather than as one nobody has filled in yet.
		const char *const line = "You have no custom presets";

		screen->DrawText( SmallFont, CR_GRAY,
			( SB_HOST_LEFT + SB_HOST_RIGHT ) / 2 - SmallFont->StringWidth( line ) / 2,
			CustomEmptyTop( ), line,
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true,
			TAG_DONE );

		const bool bFocus = ( g_Focus == zx::BrowserFocus::Host );

		// [rc4l] Blue, because this one INVITES. The warm tint is reserved for the button that ends
		// something; a neutral button here would read as one more grey control on an empty screen
		// rather than as the thing to press.
		DrawRoundedButton( CustomEmptyBtnLeft( ), CustomEmptyBtnTop( ), CustomEmptyBtnW( ),
			SB_HOST_BTN_H, "CREATE ONE HERE", g_CustomEmptyHot || bFocus, ButtonTint::Cool );

		if ( bFocus )
		{
			FocusAnchor( zx::BrowserFocus::Host, CustomEmptyBtnLeft( ) - 5,
				CustomEmptyBtnTop( ) + SB_HOST_BTN_H / 2 );
		}

		serverbrowser_Tip( CustomEmptyBtnLeft( ), CustomEmptyBtnTop( ), CustomEmptyBtnW( ),
			SB_HOST_BTN_H, "Build one on the NEW tab" );
	}

	// [rc4l] What the detail column says about the selected preset, as lines already wrapped to the
	// column's width.
	//
	// Built rather than drawn so the height, the scrollbar and the drawing agree about how much
	// there is -- the same reason the experience summary is measured before it is drawn.
	// [rc4l] A line of the column, by KIND rather than by pre-formatted text.
	//
	// The first version pasted a label and a value into one string and left-aligned the lot, which
	// is why it read as a wall: nothing lined up, and every line had the same weight as every other.
	// Saying what a line IS lets the drawing align the numbers, rule the sections and dim what is
	// secondary, none of which can be done to a string that has already been joined.
	struct DetailLine
	{
		enum Kind { Blank, Title, Subtitle, Heading, Item, Rule };

		Kind kind;
		FString text;			// Item: the label
		FString value;			// Item: right-aligned
		EColorRange colour;

		DetailLine() : kind(Blank), colour(CR_GRAY) {}
	};

	DetailLine CustomDetailMake( DetailLine::Kind kind, const char *text, EColorRange colour )
	{
		DetailLine line;
		line.kind = kind;
		line.text = text;
		line.colour = colour;

		return line;
	}

	void CustomDetailAdd( std::vector<DetailLine> &out, DetailLine::Kind kind, const char *text,
		EColorRange colour )
	{
		// Wrapped the way every other block of text in this browser is, so a long filename becomes
		// two lines rather than running off the panel.
		FBrokenLines *const lines = V_BreakLines( SmallFont, HostDetailWrapWidth( ), text );

		for ( int i = 0; lines[i].Width >= 0; ++i )
			out.push_back( CustomDetailMake( kind, lines[i].Text, colour ));

		V_FreeBrokenLines( lines );
	}

	// A label on the left and its value on the right, which is what makes a column of numbers
	// readable: the eye runs down the right edge rather than hunting along each line.
	void CustomDetailItem( std::vector<DetailLine> &out, const char *label, const char *value,
		EColorRange colour )
	{
		DetailLine line;
		line.kind = DetailLine::Item;
		line.text = serverbrowser_FitName( label, HostDetailWrapWidth( ) -
			SmallFont->StringWidth( value ) - 12 );
		line.value = value;
		line.colour = colour;

		out.push_back( line );
	}

	void CustomDetailSection( std::vector<DetailLine> &out, const char *title )
	{
		out.push_back( DetailLine( ));
		out.push_back( CustomDetailMake( DetailLine::Rule, "", CR_GRAY ));
		out.push_back( CustomDetailMake( DetailLine::Heading, title, CR_GOLD ));
	}

	// [rc4l] The column, built once per preset rather than per look.
	//
	// Even with the file lookups memoised this wraps every line through V_BreakLines, which
	// allocates -- and the draw, the pointer and the wheel each asked for the whole column. Held
	// against the preset's name and the same generation the lookups use, so a file appearing while
	// the menu is open still changes what it says.
	std::vector<DetailLine> CustomDetailCached( const zx::CustomEntry &entry )
	{
		static std::vector<DetailLine> cache;
		static std::string forName;
		static int forFiles = -1;
		static int forPresets = -1;
		static size_t forAnswers = 0;

		// Both generations: one moves when files appear on disk, the other when a preset is written
		// -- and a preset saved over itself changes neither its name nor the files on the machine.
		//
		// [rc4l] And how many answers the resolver has given, which is what makes the "..." lines
		// settle. Without it a column built while the worker was still reading would keep saying
		// "..." about files that had since come back, until something else happened to invalidate
		// it -- the lines are cached, and the answers land after they were drawn.
		if (( forName == entry.name ) && ( forFiles == g_HostHaveGeneration ) &&
			( forPresets == g_CustomGeneration ) && ( forAnswers == g_CustomResolved.size( )))
		{
			return cache;
		}

		cache = CustomDetailLines( entry );
		forName = entry.name;
		forFiles = g_HostHaveGeneration;
		forPresets = g_CustomGeneration;
		forAnswers = g_CustomResolved.size( );

		return cache;
	}

	std::vector<DetailLine> CustomDetailLines( const zx::CustomEntry &entry )
	{
		std::vector<DetailLine> out;

		CustomDetailAdd( out, DetailLine::Title, entry.name.c_str( ), CR_GOLD );

		{
			FString line;
			line.Format( "%s  -  %s", entry.bPvP ? "PvP" : "PvE",
				entry.gameMode.empty( ) ? "default mode" : entry.gameMode.c_str( ));
			CustomDetailAdd( out, DetailLine::Subtitle, line.GetChars( ), CR_DARKGRAY );
		}

		// The files, in load order, with the IWAD first because that is what a server is told first.
		CustomDetailSection( out, "FILES" );

		if ( !entry.iwad.empty( ))
			CustomDetailItem( out, entry.iwad.c_str( ), "IWAD", CR_GRAY );

		for ( size_t i = 0; i < entry.files.size( ); ++i )
		{
			// Three states, not two: saying MISSING about a file nobody has looked at yet is the
			// same lie the rows used to tell, one column over.
			switch ( CustomResolve( entry.files[i].name, entry.files[i].md5 ))
			{
			case ResolveState::Found:
				CustomDetailItem( out, entry.files[i].name.c_str( ), "", CR_GRAY );
				break;

			case ResolveState::Missing:
				CustomDetailItem( out, entry.files[i].name.c_str( ), "MISSING", CR_ORANGE );
				break;

			default:
				CustomDetailItem( out, entry.files[i].name.c_str( ), "...", CR_DARKGRAY );
				break;
			}
		}

		// [rc4l] The flag fields, by name and number, and only the ones that are set. A column of
		// zeroes is a column nobody reads, and the fields that ARE set are the whole question.
		{
			bool bAny = false;

			for ( size_t i = 0; i < entry.cvars.size( ); ++i )
			{
				if ( !zx::IsFlagFieldName( entry.cvars[i].first ))
					continue;
				if (( entry.cvars[i].second == "0" ) || entry.cvars[i].second.empty( ))
					continue;

				if ( !bAny )
				{
					CustomDetailSection( out, "FLAGS" );
					bAny = true;
				}

				CustomDetailItem( out, entry.cvars[i].first.c_str( ),
					entry.cvars[i].second.c_str( ), CR_GRAY );
			}
		}

		return out;
	}

	// [rc4l] The MAPS button sits at the FOOT of the column and does not scroll with the text.
	//
	// A rotation of thirty-two names is a list, not a paragraph, and pasting it into a column meant
	// for labels was what made this read as a wall. It gets a box of its own, and the button that
	// opens it stays where it can be pressed however far down the text somebody has scrolled.
	int CustomMapsBtnH( )		{ return SB_NEW_TOOL_H; }
	int CustomMapsBtnTop( )		{ return CustomDetailBottom( ) - CustomMapsBtnH( ); }
	int CustomTextBottom( )		{ return CustomMapsBtnTop( ) - 6; }
	int CustomDetailRowsShown( ){ return ( CustomTextBottom( ) - CustomDetailTop( )) / CustomDetailLineH( ); }

	void DrawCustomDetail( )
	{
		const zx::CustomEntry *const chosen = CustomSelected( );

		DrawDetailBackdrop( SB_HOST_RCOL_LEFT - SB_HOST_RCOL_INSET,
			CustomDetailTop( ) - SB_HOST_RCOL_INSET,
			SB_HOST_RCOL_RIGHT + SB_HOST_RCOL_INSET,
			CustomDetailBottom( ) + SB_HOST_RCOL_INSET );

		if ( chosen == NULL )
			return;

		const std::vector<DetailLine> lines = CustomDetailCached( *chosen );

		const int lineH = CustomDetailLineH( );
		const int maxScroll = MAX( 0, static_cast<int>( lines.size( )) - CustomDetailRowsShown( ));

		g_CustomDetailScroll = zx::ClampScroll( g_CustomDetailScroll, maxScroll );

		for ( size_t i = 0; i < lines.size( ); ++i )
		{
			const DetailLine &line = lines[i];

			const int y = CustomDetailTop( ) +
				( static_cast<int>( i ) - g_CustomDetailScroll ) * lineH;

			// The presets column's own rule about which rows are drawn, so a line never half
			// appears at an edge.
			if ( !zx::RowFullyInView( y, lineH, CustomDetailTop( ), CustomTextBottom( )))
				continue;

			if ( line.kind == DetailLine::Blank )
				continue;

			// A hairline under a section, the same one the flags box's footer uses. It is what
			// turns a run of lines into blocks somebody can skim.
			if ( line.kind == DetailLine::Rule )
			{
				const int rx = serverbrowser_ToScreenX( SB_HOST_RCOL_LEFT );
				const int ry = serverbrowser_ToScreenY( y + lineH / 2 );

				screen->Dim( PalEntry( 70, 74, 96 ), 0.7f, rx, ry,
					MAX( 1, serverbrowser_ToScreenX( SB_HOST_RCOL_RIGHT ) - rx ),
					MAX( 1, serverbrowser_ToScreenY( y + lineH / 2 + 1 ) - ry ));
				continue;
			}

			screen->DrawText( SmallFont,
				( line.kind == DetailLine::Title ) ? CR_WHITE : line.colour,
				SB_HOST_RCOL_LEFT, y, line.text.GetChars( ),
				DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true,
				TAG_DONE );

			// The value, against the right edge, so a column of numbers lines up.
			if (( line.kind == DetailLine::Item ) && line.value.IsNotEmpty( ))
			{
				screen->DrawText( SmallFont,
					( line.colour == CR_ORANGE ) ? CR_ORANGE : CR_WHITE,
					SB_HOST_RCOL_RIGHT - SmallFont->StringWidth( line.value ), y,
					line.value.GetChars( ),
					DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true,
					TAG_DONE );
			}
		}

		DrawHostRegionScrollBar( CustomDetailTop( ), CustomTextBottom( ),
			static_cast<int>( lines.size( )) * lineH, g_CustomDetailScroll * lineH, SB_HOST_BAR_X );

		FString maps;
		maps.Format( "MAPS  (%d)", static_cast<int>( chosen->maps.size( )));

		DrawRoundedButton( SB_HOST_RCOL_LEFT, CustomMapsBtnTop( ),
			SB_HOST_RCOL_RIGHT - SB_HOST_RCOL_LEFT, CustomMapsBtnH( ), maps.GetChars( ),
			g_CustomMapsHot );

		serverbrowser_Tip( SB_HOST_RCOL_LEFT, CustomMapsBtnTop( ),
			SB_HOST_RCOL_RIGHT - SB_HOST_RCOL_LEFT, CustomMapsBtnH( ),
			"The rotation this preset plays\nEDIT to change it" );
	}

	// ---------------------------------------------------------------------------------------------
	//
	// [rc4l] The maps a saved preset plays, to LOOK AT.
	//
	// Read-only on purpose: EDIT is where a rotation is changed, and it puts the preset back on the
	// NEW screen where the map list already has every control for the job. A second editable copy
	// here would be two places to change one thing, and the one nobody used would rot.

	void DrawCustomMapsModal( )
	{
		const zx::CustomEntry *const chosen = CustomSelected( );
		if ( chosen == NULL )
			return;

		serverbrowser_ClearTips( );

		screen->Dim( 0x000000, 0.62f, 0, 0, screen->GetWidth( ), screen->GetHeight( ));

		const zx::PanelColor topCol = { 26, 28, 40, 245 };
		const zx::PanelColor botCol = { 12, 13, 20, 250 };
		DrawRoundedPanel( NewBigModalLeft( ), NewBigModalTop( ),
			NewBigModalRight( ) - NewBigModalLeft( ), NewBigModalBottom( ) - NewBigModalTop( ),
			topCol, botCol, 8 );

		const int left = NewBigContentLeft( );
		const int top = NewBigContentTop( );
		const int visible = MAX( 1, ( NewBigButtonTop( ) - 8 - top ) / SB_NEW_ROW_H );

		FString heading;
		heading.Format( "%s  -  MAPS  (%d)", chosen->name.c_str( ),
			static_cast<int>( chosen->maps.size( )));

		screen->DrawText( SmallFont, CR_GOLD, left, NewBigModalTop( ) + SB_NEW_MODAL_PAD,
			heading.GetChars( ), DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H,
			DTA_KeepRatio, true, TAG_DONE );

		const int maxScroll = MAX( 0, static_cast<int>( chosen->maps.size( )) - visible );
		g_CustomMapsScroll = zx::ClampScroll( g_CustomMapsScroll, maxScroll );

		if ( chosen->maps.empty( ))
		{
			DrawNewRowText( left, top, CR_DARKGRAY, "This preset names no maps" );
		}
		else
		{
			for ( int row = g_CustomMapsScroll;
				( row < static_cast<int>( chosen->maps.size( ))) &&
				( row < g_CustomMapsScroll + visible ); ++row )
			{
				const int rowY = NewRowY( top, row, g_CustomMapsScroll );

				// [rc4l] The order IS the meaning here and nothing can be moved, so the position is
				// numbered: without a control to drag, the number is all that says "third".
				FString line;
				line.Format( "%d.  %s", row + 1, chosen->maps[row].c_str( ));

				DrawNewRowText( left, rowY, ( row == 0 ) ? CR_WHITE : CR_GRAY, line );
			}

			DrawHostRegionScrollBar( top, top + visible * SB_NEW_ROW_H,
				static_cast<int>( chosen->maps.size( )) * SB_NEW_ROW_H,
				g_CustomMapsScroll * SB_NEW_ROW_H, NewBigBarX( ));
		}

		if ( !chosen->maps.empty( ))
		{
			FString foot;
			foot.Format( "Starts on %s", chosen->maps[0].c_str( ));

			screen->DrawText( SmallFont, CR_DARKGRAY, left, NewBigButtonTop( ) + 4,
				foot.GetChars( ), DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H,
				DTA_KeepRatio, true, TAG_DONE );
		}

		DrawRoundedButton( NewBigButtonLeft( ), NewBigButtonTop( ), 80, SB_DLG_BTN_H, "DONE",
			g_CustomMapsDoneHot );

		// The orb comes into the box with the keyboard, the same as every other modal here: DONE is
		// the only thing in it a key can press.
		FocusAnchor( zx::BrowserFocus::Host, NewBigButtonLeft( ) - 5,
			NewBigButtonTop( ) + SB_DLG_BTN_H / 2 );
	}

	bool CustomMapsModalMouse( int type, int x, int y )
	{
		g_CustomMapsDoneHot = false;

		const zx::CustomEntry *const chosen = CustomSelected( );
		const int lines = ( chosen != NULL ) ? static_cast<int>( chosen->maps.size( )) : 0;

		const int top = NewBigContentTop( );
		const int visible = MAX( 1, ( NewBigButtonTop( ) - 8 - top ) / SB_NEW_ROW_H );

		if ( RegionBarMouse( type, x, y, top, top + visible * SB_NEW_ROW_H,
			lines * SB_NEW_ROW_H, MAX( 0, lines - visible ), g_CustomMapsScroll,
			g_DraggingCustomMapsBar, NewBigBarX( )))
		{
			return true;
		}

		{
			const int bx = NewBigButtonLeft( );
			const int by = NewBigButtonTop( );

			if (( x >= serverbrowser_ToScreenX( bx )) &&
				( x < serverbrowser_ToScreenX( bx + 80 )) &&
				( y >= serverbrowser_ToScreenY( by )) &&
				( y < serverbrowser_ToScreenY( by + SB_DLG_BTN_H )))
			{
				g_CustomMapsDoneHot = true;

				if ( type == MOUSE_Release )
				{
					g_CustomMapsOpen = false;
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
				}

				return true;
			}
		}

		// Inside swallows; outside closes, the same as every other box here.
		if (( x >= serverbrowser_ToScreenX( NewBigModalLeft( ))) &&
			( x < serverbrowser_ToScreenX( NewBigModalRight( ))) &&
			( y >= serverbrowser_ToScreenY( NewBigModalTop( ))) &&
			( y < serverbrowser_ToScreenY( NewBigModalBottom( ))))
		{
			return true;
		}

		if ( type == MOUSE_Release )
			g_CustomMapsOpen = false;

		return true;
	}

	void DrawCustomPanel( )
	{
		const std::vector<int> rows = CustomRows( );

		if ( CustomEntries( ).empty( ))
		{
			DrawCustomEmpty( );
			return;
		}

		// [rc4l] Drain last frame's answers and ask about anything still unanswered. Asked from the
		// draw because this is the screen that wants the answer, and asked every frame because the
		// second ask through is a bool test -- see JobAcceptsBegin.
		CustomVerifyPump( );

		FString heading;
		heading.Format( "YOUR PRESETS  (%d)", static_cast<int>( CustomEntries( ).size( )));

		screen->DrawText( SmallFont, CR_GOLD, SB_HOST_LIST_LEFT, SB_NEW_TOP, heading,
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true,
			TAG_DONE );

		// The search box, in the same place and shape the wad list's is.
		{
			int firstChar = g_CustomSearchFirstChar;
			DrawTextField( SB_HOST_LIST_LEFT, SB_NEW_SEARCH_TOP,
				SB_HOST_LIST_RIGHT - SB_HOST_LIST_LEFT, SB_NEW_SEARCH_H, g_CustomSearch,
				( g_CustomFocus == CustomFocus::Search ) && ( g_Focus == zx::BrowserFocus::Host ),
				g_CustomSearchHot, "Search your presets", false, firstChar );
			g_CustomSearchFirstChar = firstChar;
		}

		const int visible = CustomRowsVisible( );

		g_CustomSel = zx::ComputeClampedSelection( g_CustomSel, static_cast<int>( rows.size( )));

		if ( g_CustomRevealSel )
		{
			NewClampScroll( g_CustomSel, static_cast<int>( rows.size( )), visible, g_CustomScroll );
			g_CustomRevealSel = false;
		}
		else
		{
			g_CustomScroll = zx::ComputeClampedSelection( g_CustomScroll,
				MAX( 1, static_cast<int>( rows.size( )) - visible + 1 ));
		}

		for ( int row = g_CustomScroll;
			( row < static_cast<int>( rows.size( ))) && ( row < g_CustomScroll + visible ); ++row )
		{
			const zx::CustomEntry &entry = CustomEntries( )[rows[row]];

			const int rowY = NewRowY( CustomListTop( ), row, g_CustomScroll );
			const bool bSel = ( row == g_CustomSel );

			DrawNewRowHighlight( SB_HOST_LIST_LEFT - 4, SB_HOST_LIST_RIGHT, rowY, bSel,
				( row == g_CustomHot ));

			if ( bSel && ( g_CustomFocus == CustomFocus::List ) &&
				( g_Focus == zx::BrowserFocus::Host ))
			{
				FocusAnchor( zx::BrowserFocus::Host, SB_HOST_LIST_LEFT - 9,
					rowY + SB_NEW_ROW_H / 2 );
			}

			bool bPending = false;
			const std::vector<std::string> missing = CustomMissing( entry, false, &bPending );

			// [rc4l] Neutral while the answer is still being worked out. Colouring an unanswered row
			// orange would accuse the player of missing files the moment the tab opened, and then
			// take it back a fraction of a second later.
			DrawNewRowText( SB_HOST_LIST_LEFT, rowY,
				( bPending || missing.empty( )) ? ( bSel ? CR_WHITE : CR_GRAY ) : CR_ORANGE,
				serverbrowser_FitName( entry.name.c_str( ), 150 ));

			// What it is, on the right of its own row: the mode, and whether it can be played.
			FString note;
			if ( bPending )
				note = LoadingText( );
			else if ( !missing.empty( ))
				note.Format( "%d missing", static_cast<int>( missing.size( )));
			else
				note.Format( "%d file(s)", static_cast<int>( entry.files.size( )));

			DrawNewRowText( SB_HOST_LIST_RIGHT - SmallFont->StringWidth( note ) - 4, rowY,
				bPending ? CR_DARKGRAY : ( missing.empty( ) ? CR_DARKGRAY : CR_ORANGE ), note );
		}

		DrawHostRegionScrollBar( CustomListTop( ), CustomListBottom( ),
			static_cast<int>( rows.size( )) * SB_NEW_ROW_H, g_CustomScroll * SB_NEW_ROW_H,
			SB_HOST_LBAR_X );

		// What the selected one IS, in the column the PRESETS tab uses for the same question.
		DrawCustomDetail( );

		// [rc4l] The three things that can be done with the selected one. EDIT goes dark when a file
		// is missing rather than vanishing: a button that disappears makes the row change shape as
		// the cursor passes over it, and the eye reads that as the list moving.
		const zx::CustomEntry *const chosen = CustomSelected( );
		const bool bPlayable = ( chosen != NULL );

		// [rc4l] Dark while the answer is still coming, the same as when a file really is missing.
		// Lighting it and then taking it away under the pointer would be a button that changes its
		// mind, and EDIT refuses on a stale answer anyway -- see CustomEdit's fresh check.
		bool bEditPending = false;
		const bool bEditable = ( chosen != NULL ) &&
			CustomMissing( *chosen, false, &bEditPending ).empty( ) && !bEditPending;

		const char *kLabels[3] = { "PLAY NOW!", "EDIT", "DELETE" };

		// [rc4l] EDIT says it is working while the worker checks the files. The press used to freeze
		// the menu instead, which is the same wait with nothing to read.
		if ( CustomEditPending( ))
			kLabels[1] = LoadingText( );

		for ( int i = 0; i < 3; ++i )
		{
			const bool bOn = ( g_CustomBtnHot == i ) ||
				(( g_CustomFocus == CustomFocus::Buttons ) && ( g_CustomBtnSel == i ) &&
				 ( g_Focus == zx::BrowserFocus::Host ));

			DrawRoundedButton( CustomBtnLeft( i ), SB_NEW_TOOL_Y, CustomBtnW( ), SB_NEW_TOOL_H,
				kLabels[i], bOn && (( i != 1 ) || bEditable ));

			if ( bOn && ( g_CustomFocus == CustomFocus::Buttons ))
			{
				FocusAnchor( zx::BrowserFocus::Host, CustomBtnLeft( i ) - 5,
					SB_NEW_TOOL_Y + SB_NEW_TOOL_H / 2 );
			}
		}

		serverbrowser_Tip( CustomBtnLeft( 0 ), SB_NEW_TOOL_Y, CustomBtnW( ), SB_NEW_TOOL_H,
			bPlayable ? "Start a server on this machine, fetching anything missing first"
				: "Nothing selected" );
		serverbrowser_Tip( CustomBtnLeft( 1 ), SB_NEW_TOOL_Y, CustomBtnW( ), SB_NEW_TOOL_H,
			bEditable ? "Open this setup on the NEW tab"
				: "Cannot edit a preset whose files are missing" );
		serverbrowser_Tip( CustomBtnLeft( 2 ), SB_NEW_TOOL_Y, CustomBtnW( ), SB_NEW_TOOL_H,
			"Remove this preset" );

		// [rc4l] LAST, so it is over everything this tab drew rather than among it. Drawn before the
		// three buttons it sat behind them, which is a box you can see through and cannot use.
		if ( g_CustomMapsOpen )
			DrawCustomMapsModal( );
	}

	// --- the pointer and the keyboard on the CUSTOM tab -------------------------------------------

	bool CustomMouseEvent( int type, int x, int y )
	{
		// The box owns the pointer while it is up, which is what modal means.
		if ( g_CustomMapsOpen )
			return CustomMapsModalMouse( type, x, y );

		g_CustomHot = -1;
		g_CustomBtnHot = -1;
		g_CustomSearchHot = false;
		g_CustomEmptyHot = false;
		g_CustomMapsHot = false;

		// Nothing saved: one button, in the middle, and it is the only thing that can be pressed.
		if ( CustomEntries( ).empty( ))
		{
			const int bw = CustomEmptyBtnW( );
			const int bx = CustomEmptyBtnLeft( );
			const int by = CustomEmptyBtnTop( );

			if (( x >= serverbrowser_ToScreenX( bx )) &&
				( x < serverbrowser_ToScreenX( bx + bw )) &&
				( y >= serverbrowser_ToScreenY( by )) &&
				( y < serverbrowser_ToScreenY( by + SB_HOST_BTN_H )))
			{
				g_CustomEmptyHot = true;

				if ( type == MOUSE_Release )
				{
					SelectSubTabIndex( static_cast<int>( HostKind::New ));
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
				}

				return true;
			}

			return false;
		}

		const std::vector<int> rows = CustomRows( );

		// The bars first, through the shared helper, for the reason every other list here does it.
		if ( RegionBarMouse( type, x, y, CustomListTop( ), CustomListBottom( ),
			static_cast<int>( rows.size( )) * SB_NEW_ROW_H,
			MAX( 0, static_cast<int>( rows.size( )) - CustomRowsVisible( )),
			g_CustomScroll, g_DraggingCustomBar, SB_HOST_LBAR_X ))
		{
			return true;
		}

		{
			const zx::CustomEntry *const chosen = CustomSelected( );
			const int detailLines = ( chosen != NULL )
				? static_cast<int>( CustomDetailCached( *chosen ).size( )) : 0;

			if ( RegionBarMouse( type, x, y, CustomDetailTop( ), CustomTextBottom( ),
				detailLines * CustomDetailLineH( ),
				MAX( 0, detailLines - CustomDetailRowsShown( )),
				g_CustomDetailScroll, g_DraggingCustomDetailBar, SB_HOST_BAR_X ))
			{
				return true;
			}

			// The MAPS button, at the foot of that column.
			if (( chosen != NULL ) &&
				( x >= serverbrowser_ToScreenX( SB_HOST_RCOL_LEFT )) &&
				( x < serverbrowser_ToScreenX( SB_HOST_RCOL_RIGHT )) &&
				( y >= serverbrowser_ToScreenY( CustomMapsBtnTop( ))) &&
				( y < serverbrowser_ToScreenY( CustomMapsBtnTop( ) + CustomMapsBtnH( ))))
			{
				g_CustomMapsHot = true;

				if ( type == MOUSE_Release )
				{
					// [rc4l] THE BOX TAKES THE KEYBOARD WITH IT.
					//
					// Without this the browser's focus stayed wherever it was -- usually the sub-tab
					// row -- so the glow went on drawing up there, above a modal that owns every key
					// until it is closed. A marker pointing at something behind a box you cannot
					// interact past is worse than no marker: it says the next press goes somewhere
					// it cannot go. The same gap the save box had; see NewOwnsKeyboard.
					SetFocus( zx::BrowserFocus::Host );
					g_CustomMapsOpen = true;
					g_CustomMapsScroll = 0;
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
				}

				return true;
			}
		}

		// The three buttons.
		if (( y >= serverbrowser_ToScreenY( SB_NEW_TOOL_Y )) &&
			( y < serverbrowser_ToScreenY( SB_NEW_TOOL_Y + SB_NEW_TOOL_H )))
		{
			for ( int i = 0; i < 3; ++i )
			{
				if (( x < serverbrowser_ToScreenX( CustomBtnLeft( i ))) ||
					( x >= serverbrowser_ToScreenX( CustomBtnLeft( i ) + CustomBtnW( ))))
				{
					continue;
				}

				g_CustomBtnHot = i;

				if ( type == MOUSE_Release )
				{
					g_CustomFocus = CustomFocus::Buttons;
					g_CustomBtnSel = i;
					CustomPressButton( i );
				}

				return true;
			}
		}

		// The search box, through the same field rule every other box on this screen follows.
		{
			const bool bOver = ( y >= serverbrowser_ToScreenY( SB_NEW_SEARCH_TOP )) &&
				( y < serverbrowser_ToScreenY( SB_NEW_SEARCH_TOP + SB_NEW_SEARCH_H ));

			if ( bOver && ( type == MOUSE_Click ))
			{
				g_CustomFocus = CustomFocus::Search;
				SetFocus( zx::BrowserFocus::Host );
			}

			g_CustomSearchHot = bOver;

			if ( FieldMouse( type, x, y, SB_HOST_LIST_LEFT, SB_NEW_SEARCH_TOP,
				SB_HOST_LIST_RIGHT - SB_HOST_LIST_LEFT, SB_NEW_SEARCH_H, g_CustomSearch,
				g_CustomSearchFirstChar, g_CustomSearchDragging, g_CustomSearchClickTime ))
			{
				return true;
			}
		}

		// The rows.
		{
			const int row = NewRowAt( y, CustomListTop( ), CustomListBottom( ), g_CustomScroll,
				static_cast<int>( rows.size( )));

			if (( row >= 0 ) &&
				( x >= serverbrowser_ToScreenX( SB_HOST_LIST_LEFT - 4 )) &&
				( x < serverbrowser_ToScreenX( SB_HOST_LIST_RIGHT )))
			{
				g_CustomHot = row;

				if ( type == MOUSE_Release )
				{
					g_CustomFocus = CustomFocus::List;

					// A different preset is a different column of text, read from the top.
					if ( row != g_CustomSel )
						g_CustomDetailScroll = 0;

					g_CustomSel = row;
					g_CustomRevealSel = true;
					SetFocus( zx::BrowserFocus::Host );
				}

				return true;
			}
		}

		return false;
	}

	// [rc4l] Which button, pressed. Shared by the pointer and the keyboard so the two cannot differ
	// about what DELETE means.
	void CustomPressButton( int i )
	{
		if ( i == 0 )
			CustomPlay( );
		else if ( i == 1 )
			CustomEdit( );
		else
			CustomAskDelete( );
	}

	// Up and down walk the rows, left and right walk the buttons, Enter presses what is under the
	// cursor, and Tab moves between the three regions -- the same alphabet the NEW screen uses.
	bool CustomNavigate( zx::NavKey key )
	{
		const std::vector<int> rows = CustomRows( );

		if ( CustomEntries( ).empty( ))
			return true;		// one button, nothing to walk

		switch ( key )
		{
		case zx::NavKey::Up:
		case zx::NavKey::Down:
		{
			const int step = ( key == zx::NavKey::Up ) ? -1 : 1;

			if ( g_CustomFocus == CustomFocus::Search )
			{
				if ( step > 0 )
					g_CustomFocus = CustomFocus::List;
			}
			else if ( g_CustomFocus == CustomFocus::List )
			{
				const int next = g_CustomSel + step;

				if (( next >= 0 ) && ( next < static_cast<int>( rows.size( ))))
				{
					g_CustomSel = next;
					g_CustomRevealSel = true;
					g_CustomDetailScroll = 0;		// a different preset, read from the top
				}
				else if ( step < 0 )
					g_CustomFocus = CustomFocus::Search;
				else
					g_CustomFocus = CustomFocus::Buttons;
			}
			else if ( step < 0 )
			{
				g_CustomFocus = CustomFocus::List;
			}

			S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
			return true;
		}

		case zx::NavKey::Left:
		case zx::NavKey::Right:
			if ( g_CustomFocus == CustomFocus::Buttons )
			{
				const int next = zx::ComputeClampedSelection(
					g_CustomBtnSel + (( key == zx::NavKey::Left ) ? -1 : 1 ), 3 );

				if ( next != g_CustomBtnSel )
				{
					g_CustomBtnSel = next;
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
				}
			}
			return true;

		default:
			break;
		}

		return true;
	}

	// --- what the three buttons do ----------------------------------------------------------------

	// [rc4l] EDIT: the preset onto the NEW screen, and the tab with it.
	//
	// Refused while a file is missing rather than half-done: prefilling a load order with gaps in it
	// would look like the preset had lost them.
	bool CustomEditPending( )	{ return g_CustomEditWanted || ( g_CustomEditToken >= 0 ); }

	// [rc4l] EDIT, ASKED FOR rather than done on the spot.
	//
	// It used to verify every file inline -- hashing each one to be sure the preset can really be
	// opened -- and the menu stopped dead for as long as that took, which for a preset naming a
	// couple of large wads is long enough to look like a hang. The verifying is the same; only where
	// it happens has changed. CustomVerifyPump starts it and CustomEditLanded finishes it.
	//
	// STILL VERIFIED FRESH. Refusing to edit is a refusal, and refusing on a remembered answer is
	// refusing for a reason that may no longer be true -- so this asks again rather than reading the
	// row colours, exactly as before.
	void CustomEdit( )
	{
		const zx::CustomEntry *const chosen = CustomSelected( );
		if ( chosen == NULL )
			return;

		// A second press while the first is still being answered is the same press.
		if ( CustomEditPending( ))
			return;

		g_CustomEditEntry = *chosen;
		g_CustomEditWanted = true;

		// Answered now, so the press is not silent while the worker gets to it.
		S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
	}

	// What the worker found, applied. Empty path means no copy on this disk matches.
	void CustomEditLanded( const std::vector<zx::resolvejob::Answer> &answers )
	{
		std::vector<std::pair<std::string, std::string> > resolved;
		int missing = 0;

		for ( size_t i = 0; i < answers.size( ); ++i )
		{
			resolved.push_back( std::make_pair( answers[i].key, answers[i].path ));

			if ( answers[i].path.empty( ))
				missing++;
		}

		if ( missing > 0 )
		{
			ShowNotice( "Files missing",
				"Play it first to fetch what it needs, then edit it." );
			return;
		}

		NewApplyEntry( g_CustomEditEntry, &resolved );

		SelectSubTabIndex( static_cast<int>( HostKind::New ));
		g_NewFocus = NewFocus::Wads;
		S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
	}

	// [rc4l] The gamemode the panel is showing, as the engine's own enum.
	//
	// The catalogue's vocabulary and the engine's are not quite the same word for the same thing --
	// "teamdeathmatch" in an addon.json is GAMEMODE_TEAMPLAY here -- so the crossing is spelled out
	// once, in the one place that needs it, rather than guessed by lowercasing a name.
	GAMEMODE_e HostModeAsGameMode( zx::HostGameMode mode )
	{
		switch ( mode )
		{
		case zx::HostGameMode::Survival:				return GAMEMODE_SURVIVAL;
		case zx::HostGameMode::Invasion:				return GAMEMODE_INVASION;
		case zx::HostGameMode::Deathmatch:				return GAMEMODE_DEATHMATCH;
		case zx::HostGameMode::TeamDeathmatch:			return GAMEMODE_TEAMPLAY;
		case zx::HostGameMode::Duel:					return GAMEMODE_DUEL;
		case zx::HostGameMode::LastManStanding:			return GAMEMODE_LASTMANSTANDING;
		case zx::HostGameMode::TeamLastManStanding:		return GAMEMODE_TEAMLMS;
		case zx::HostGameMode::Possession:				return GAMEMODE_POSSESSION;
		case zx::HostGameMode::TeamPossession:			return GAMEMODE_TEAMPOSSESSION;
		case zx::HostGameMode::Terminator:				return GAMEMODE_TERMINATOR;
		case zx::HostGameMode::CaptureTheFlag:			return GAMEMODE_CTF;
		case zx::HostGameMode::Skulltag:				return GAMEMODE_SKULLTAG;
		case zx::HostGameMode::Teamgame:				return GAMEMODE_TEAMGAME;

		// Cooperative, and anything an entry declined to say. Co-op is what a server runs when
		// nothing turned another mode on, so it is the honest answer for both.
		default:										return GAMEMODE_COOPERATIVE;
		}
	}

	// [rc4l] The selected experience as an entry the NEW screen can take, which is what COPY hands it.
	//
	// The settings come out of the way of playing's OWN server.cfg, which is the second and last
	// place this client reads one -- HostRotation is the first, and says why. Reading it is what
	// makes a copy a copy: without it the flags would be the NEW screen's defaults and the thing
	// somebody pressed copy on would arrive playing differently.
	//
	// What the panel decided wins over what the cfg says, because the panel is the more recent word:
	// the mix, the lives, the way of playing and the map are all on screen and all changeable there.
	zx::CustomEntry HostAsCustomEntry( const zx::CatalogueEntry &entry )
	{
		const zx::AddonEntry &addon = entry.addon;
		const zx::VariantPick pick = zx::PickVariant( addon, g_HostVariantId.GetChars( ));

		zx::CustomEntry out;

		// A NAME to arrive under, so the save box opens on something rather than empty. The variant
		// is the more useful half when there is one: "Alien Vendetta" beats "Popular Co-op Maps".
		out.name = pick.name.empty( ) ? addon.name : pick.name;

		// The RESOLVED iwad, not the wanted one: the NEW screen matches its box by name and has no
		// substitute table of its own, so handing it a name this machine does not have would leave
		// whatever was selected before quietly in place.
		out.iwad = HostCopyIwad( addon );
		out.bPvP = ( addon.kind == zx::VariantKind::PvP );

		const std::vector<zx::AddonFileRef> loads = HostSelectedFiles( addon );
		for ( size_t i = 0; i < loads.size( ); ++i )
			out.files.push_back( zx::CustomFile( loads[i].name, loads[i].md5 ));

		// The cfg, read here and only here: once, on a press somebody made.
		{
			const FString path = zx::CatalogueServerCfgPath( entry,
				g_HostVariantId.GetChars( )).c_str( );

			FILE *const fp = path.IsNotEmpty( ) ? fopen( path.GetChars( ), "rb" ) : NULL;
			if ( fp != NULL )
			{
				std::string text;
				char buf[4096];
				size_t got;

				while (( got = fread( buf, 1, sizeof( buf ), fp )) > 0 )
					text.append( buf, got );

				fclose( fp );
				zx::ParseCustomCfg( text, out.cvars, out.maps );
			}
		}

		out.gameMode = NewGameModeCvar( HostModeAsGameMode( HostGameModeFor( addon )));

		// [rc4l] The lives the SLIDER is on, which the cfg cannot know: it is the panel's control and
		// half the reason the copy is worth having.
		//
		// Through LivesCvars, which is what the launch path hands the server for the same control --
		// so a copy is set up the way starting it would have been, rather than the way somebody
		// writing this a second time guessed.
		{
			const std::vector<std::pair<std::string, std::string> > lives =
				zx::LivesCvars( HostLivesControl( addon ));

			for ( size_t i = 0; i < lives.size( ); ++i )
			{
				bool bReplaced = false;

				for ( size_t c = 0; c < out.cvars.size( ); ++c )
				{
					if ( out.cvars[c].first != lives[i].first )
						continue;

					out.cvars[c].second = lives[i].second;
					bReplaced = true;
					break;
				}

				if ( !bReplaced )
					out.cvars.push_back( lives[i] );
			}
		}

		return out;
	}

	// [rc4l] COPY: the chosen experience, opened on the NEW screen so it can be taken further.
	//
	// Only offered when every file is already here -- see HostCopyOffered -- so this has nothing to
	// download and nothing to refuse. A press that could fail would need a dialog, and the button
	// not being there at all is the better version of that message.
	void HostPressCopy( )
	{
		const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( );
		if (( g_HostEntrySel < 0 ) || ( g_HostEntrySel >= static_cast<int>( entries.size( ))))
			return;

		const zx::CustomEntry entry = HostAsCustomEntry( entries[g_HostEntrySel] );
		const std::vector<std::string> missing = NewApplyEntry( entry );

		// Should be empty: the button is only drawn when they are all here. Said rather than assumed,
		// because a file can go between the frame that drew the button and the press.
		if ( !missing.empty( ))
		{
			ShowNotice( "Files missing",
				"Some of what it loads is no longer on this machine." );
			return;
		}

		SelectSubTabIndex( static_cast<int>( HostKind::New ));
		g_NewFocus = NewFocus::Wads;
		S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
	}

	void CustomAskDelete( )
	{
		const zx::CustomEntry *const chosen = CustomSelected( );
		if ( chosen == NULL )
			return;

		g_CustomDeleting = chosen->name.c_str( );

		FString message;
		message.Format( "\"%s\" and its folder go for good.", chosen->name.c_str( ));

		ShowDialog( DialogAction::DeleteCustom, "Delete this preset?", message.GetChars( ),
			"Delete", 'd', "Keep", 'k' );
	}

	// [rc4l] PLAY NOW on a preset: fetch what is missing, then host it.
	//
	// The fetch is the SAME transfer a shipped preset uses, with the same checking -- each file goes
	// over with its md5, so a mirror handing back a different build of the same filename is caught
	// here rather than by a server refusing to start on it. A preset saved on another machine is
	// exactly the case this exists for.
	void CustomPlay( )
	{
		const zx::CustomEntry *const chosen = CustomSelected( );
		if ( chosen == NULL )
			return;

		// Asked again from scratch: this is about to fetch files and start a server, and a
		// remembered "missing" would spend somebody's bandwidth on a file that has since arrived.
		const std::vector<std::string> missing = CustomMissing( *chosen, true );

		if ( !missing.empty( ))
		{
			if ( !zx::waddownload::IsAvailable( ))
			{
				ShowNotice( "Files missing",
					"This preset needs files this machine does not have, and downloading is off." );
				return;
			}

			std::vector<zx::waddownload::WantedFile> wanted;

			for ( size_t i = 0; i < missing.size( ); ++i )
			{
				std::string md5;
				for ( size_t j = 0; j < chosen->files.size( ); ++j )
				{
					if ( chosen->files[j].name == missing[i] )
					{
						md5 = chosen->files[j].md5;
						break;
					}
				}

				wanted.push_back( zx::waddownload::WantedFile( missing[i], false, md5 ));
			}

			if ( !zx::waddownload::Start( std::vector<std::string>( ), std::vector<std::string>( ),
				wanted, zx::NoteDownloadFinished ))
			{
				// [rc4l] Gracefully, which here means saying which files and stopping. A preset
				// naming something no mirror carries is a preset that cannot be played on this
				// machine, and pretending otherwise would start a server without them.
				FString message;
				message.Format( "%d file(s) could not be fetched, starting with %s.",
					static_cast<int>( missing.size( )), missing[0].c_str( ));

				ShowNotice( "Could not fetch", message.GetChars( ));
				return;
			}

			g_CustomDownloading = chosen->name.c_str( );
			zx::SetPendingResume( serverbrowser_CustomDownloadResume, chosen->name.c_str( ));

			S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
			return;
		}

		// Everything is here: onto the NEW screen's own hosting, which is the one path that starts
		// a server built out of files rather than out of a catalogue entry.
		NewApplyEntry( *chosen );
		NewStartHosting( );
	}

	// One frame after the transfer says it finished, for the reason the preset path gives: the
	// callback arrives from inside waddownload::Tick and starting a server from there re-enters it.
	void ResumeCustomAfterDownload( )
	{
		if ( !g_CustomDownloadResumed )
			return;

		g_CustomDownloadResumed = false;

		const FString name = g_CustomDownloading;
		const bool bOk = g_CustomDownloadSucceeded;
		g_CustomDownloading = "";

		if ( name.IsEmpty( ) || !bOk )
			return;		// a failure has already been reported on the browser's own panel

		const zx::CustomEntry entry = zx::CustomLoad( name.GetChars( ));
		if ( entry.name.empty( ))
			return;

		const std::vector<std::string> missing = NewApplyEntry( entry );

		if ( !missing.empty( ))
		{
			FString message;
			message.Format( "%d file(s) are still missing after the download.",
				static_cast<int>( missing.size( )));

			ShowNotice( "Still missing", message.GetChars( ));
			return;
		}

		NewStartHosting( );
	}

	// [rc4l] The last configuration played, put back, once per run of the game.
	//
	// Asked for on the first draw of this screen rather than at startup: a player who never opens
	// HOST should not pay for reading a folder, and this is the moment the answer is needed.
	//
	// Only when the screen is EMPTY. Restoring over a load order somebody has already built would
	// be the file remembering better than the person, which is the wrong way round.
	// g_NewRestoreEntry and g_NewRestoreToken are at file scope; this is what the draw asks.
	bool NewRestorePending( )	{ return g_NewRestoreToken >= 0; }

	// [rc4l] The last-played setup, restored WITHOUT the first frame of this tab paying for it.
	//
	// It used to resolve every file inline, which meant hashing them: a setup naming a couple of
	// large wads outside the by-hash store stalled the screen the moment it was opened, before
	// anything had been drawn. Now the entry is read here -- two small files -- and the expensive
	// half is asked of the worker; the load order fills in when it answers.
	//
	// STILL ONCE PER SESSION, and still only over an EMPTY screen. The empty test is repeated at
	// apply time as well as here, because the wait is exactly the window in which somebody can start
	// building an order by hand, and restoring over that would be the file remembering better than
	// the person.
	void NewRestoreLastOnce( )
	{
		static bool bAsked = false;

		if ( NewRestorePending( ))
		{
			std::vector<zx::resolvejob::Answer> answers;
			if ( !zx::resolvejob::Tick( g_NewRestoreToken, answers ))
				return;

			g_NewRestoreToken = -1;

			// Built while we waited. The answers are dropped rather than applied.
			if ( !g_NewOrder.empty( ))
				return;

			std::vector<std::pair<std::string, std::string> > resolved;
			for ( size_t i = 0; i < answers.size( ); ++i )
				resolved.push_back( std::make_pair( answers[i].key, answers[i].path ));

			const std::vector<std::string> missing = NewApplyEntry( g_NewRestoreEntry, &resolved );

			if ( !missing.empty( ))
			{
				FString say;
				say.Format( "%d file(s) from the last setup are missing",
					static_cast<int>( missing.size( )));
				NewSay( say.GetChars( ));
			}

			return;
		}

		if ( bAsked )
			return;

		if ( !g_NewOrder.empty( ))
		{
			bAsked = true;
			return;
		}

		const zx::CustomEntry last = zx::CustomLoadLast( );
		if ( last.files.empty( ))
		{
			bAsked = true;
			return;
		}

		// [rc4l] The KEY IS THE FILENAME here, not name|md5 as the CUSTOM list uses, because this is
		// what NewApplyEntry looks the answer up by. The two consumers never share a result -- the
		// token sees to that -- so they are free to key their own the way each needs.
		std::vector<zx::resolvejob::Want> wants;
		for ( size_t i = 0; i < last.files.size( ); ++i )
		{
			wants.push_back( zx::resolvejob::Want( last.files[i].name, last.files[i].name,
				last.files[i].md5 ));
		}

		const int token = NextVerifyToken( );

		// Not marked asked until it actually starts: the CUSTOM list may hold the worker, and giving
		// up on the restore because it was busy for one frame would lose the setup for the session.
		if ( zx::resolvejob::Begin( wants, token ))
		{
			bAsked = true;
			g_NewRestoreEntry = last;
			g_NewRestoreToken = token;
		}
	}

	void DrawNewPanel( )
	{
		NewRestoreLastOnce( );

		// [rc4l] The scan is asked for HERE rather than at startup, and this is the only place that
		// asks. Begin() is cheap once a scan has run, so calling it while the tab is drawn is what
		// makes "open the tab, get a list" true without costing anything on any other screen.
		zx::wadlibrary::Begin( false );
		zx::wadlibrary::Tick( );

		DrawDetailBackdrop( SB_HOST_RCOL_LEFT - SB_HOST_RCOL_INSET,
			SB_NEW_ORDER_TOP - SB_HOST_RCOL_INSET,
			SB_HOST_RCOL_RIGHT + SB_HOST_RCOL_INSET,
			SB_NEW_ORDER_BOTTOM + SB_HOST_RCOL_INSET );

		NewLoadFlags( );

		DrawNewIwads( );
		DrawNewSearch( );
		DrawNewWads( );
		DrawNewTools( );
		DrawNewOrder( );

		// [rc4l] TWO buttons on the row PRESETS gives to one.
		//
		// They are different acts and only one of them is undoable: playing spends a minute, saving
		// writes a folder somebody will come back to. PLAY NOW keeps the width and the place the eye
		// already goes; SAVE takes the corner.
		//
		// DrawRoundedButton, which IS the JOIN and PLAY NOW drawing on the other screens. A button
		// that merely resembled them would drift apart the first time either was touched.
		const bool bSaveFocus = ( g_NewFocus == NewFocus::Buttons ) && ( g_NewButtonSel == 0 ) &&
			( g_Focus == zx::BrowserFocus::Host ) && ( g_NewModal == NewModal::None );
		const bool bPlayFocus = ( g_NewFocus == NewFocus::Buttons ) && ( g_NewButtonSel == 1 ) &&
			( g_Focus == zx::BrowserFocus::Host ) && ( g_NewModal == NewModal::None );

		DrawRoundedButton( NewSaveLeft( ), SB_NEW_BTN_Y, NewSaveWidth( ), SB_HOST_BTN_H, "SAVE",
			g_NewSaveHot || bSaveFocus );

		DrawRoundedButton( NewPlayLeft( ), SB_NEW_BTN_Y, NewPlayWidth( ), SB_HOST_BTN_H,
			"PLAY NOW!", g_NewButtonHot || bPlayFocus );

		if ( bSaveFocus )
			FocusAnchor( zx::BrowserFocus::Host, NewSaveLeft( ) - 5, SB_NEW_BTN_Y + SB_HOST_BTN_H / 2 );
		if ( bPlayFocus )
			FocusAnchor( zx::BrowserFocus::Host, NewPlayLeft( ) - 5, SB_NEW_BTN_Y + SB_HOST_BTN_H / 2 );

		serverbrowser_Tip( NewSaveLeft( ), SB_NEW_BTN_Y, NewSaveWidth( ), SB_HOST_BTN_H,
			g_NewOrder.empty( ) ? "Add at least one file first"
				: "Keep this setup under a name, in the CUSTOM tab" );

		serverbrowser_Tip( NewPlayLeft( ), SB_NEW_BTN_Y, NewPlayWidth( ), SB_HOST_BTN_H,
			g_NewOrder.empty( ) ? "Add at least one file first"
				: "Start a server on this machine with these files" );

		// [rc4l] This line is the NOTICE and nothing else now.
		//
		// It carried a standing list of what the keys do, which is the kind of thing the eye learns
		// to skip -- and then the one message that matters, "a different file of that name is
		// already in the list", arrives in a place nobody is looking any more.
		if ( g_NewNotice.IsNotEmpty( ) &&
			( static_cast<int>( I_MSTime( )) - g_NewNoticeMs < 2500 ))
		{
			screen->DrawText( SmallFont, CR_GRAY, SB_HOST_LEFT + SB_HOST_PAD,
				SB_NEW_BTN_Y + ( SB_HOST_BTN_H - SmallFont->GetHeight( )) / 2, g_NewNotice,
				DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true,
				TAG_DONE );
		}

		if ( zx::wadlibrary::HitCap( ))
		{
			screen->DrawText( SmallFont, CR_ORANGE, SB_HOST_LIST_LEFT, SB_NEW_WADS_BOTTOM + 2,
				"Stopped counting: there are more files than this list will hold",
				DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
		}

		// Last, so it is over everything this screen drew rather than among it.
		switch ( g_NewModal )
		{
		case NewModal::Iwad:		DrawNewIwadModal( ); break;
		case NewModal::Maps:		DrawNewMapsModal( ); break;
		case NewModal::Save:		DrawNewSaveModal( ); break;
		case NewModal::Flags:
		case NewModal::Gameplay:	DrawSettingsBox( g_NewModal ); break;
		case NewModal::None:		break;
		default:					break;
		}
	}

	// Which row of a list a screen-space y lands on, or -1. One helper for all three, so a row can
	// never be clickable somewhere other than where it was drawn.
	int NewRowAt( int y, int top, int bottom, int scroll, int count )
	{
		if (( y < serverbrowser_ToScreenY( top )) || ( y >= serverbrowser_ToScreenY( bottom )))
			return -1;

		for ( int row = scroll; row < count; ++row )
		{
			const int rowY = NewRowY( top, row, scroll );
			if ( rowY >= bottom )
				break;

			if (( y >= serverbrowser_ToScreenY( rowY )) &&
				( y < serverbrowser_ToScreenY( rowY + SB_NEW_ROW_H )))
			{
				return row;
			}
		}

		return -1;
	}

	void NewOpenIwadModal( )
	{
		g_NewModal = NewModal::Iwad;
		g_NewIwadModalSel = g_NewIwadSel;
		g_NewIwadModalScroll = 0;

		// Opening it should show what is already chosen, wherever in the grid that has ended up.
		g_NewIwadRevealSel = true;
		g_DraggingIwadBar = false;
		S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
	}

	// `bTake` is whether the cursor in the modal becomes the choice. Escape and a click outside say
	// no, which is what makes opening it to look harmless.
	void NewCloseIwadModal( bool bTake )
	{
		if ( bTake && !NewIwads( ).empty( ))
		{
			g_NewIwadSel = g_NewIwadModalSel;
			g_NewIwadChosen = true;
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
		}

		g_NewModal = NewModal::None;
		g_NewIwadModalHot = -1;
	}

	// How far the grid can be scrolled, in rows. Zero when it fits, which is what turns the bar and
	// the wheel off together rather than one of them at a time.
	int NewModalMaxScroll( )
	{
		return MAX( 0, zx::PillFlowRowCount( NewIwadPills( )) - NewModalVisibleRows( ));
	}

	// The IWAD grid's bar. Same helper, its own lane -- the bar sits inside the modal rather than in
	// the host panel's gutter, which is the only thing that differs.
	bool NewIwadBarMouse( int type, int x, int y )
	{
		const int gridTop = NewModalGridTop( );
		const int gridBottom = gridTop + NewModalVisibleRows( ) * SB_NEW_PILL_ROW_H;

		return RegionBarMouse( type, x, y, gridTop, gridBottom,
			zx::PillFlowRowCount( NewIwadPills( )) * SB_NEW_PILL_ROW_H, NewModalMaxScroll( ),
			g_NewIwadModalScroll, g_DraggingIwadBar,
			NewModalContentRight( ) - SB_NEW_MODAL_BAR_W + 2 );
	}

	bool NewIwadModalMouse( int type, int x, int y )
	{
		g_NewIwadModalHot = -1;
		g_NewIwadRefreshHot = false;
		g_NewIwadConfirmHot = false;
		g_NewBoxResetHot = false;

		// The bar first: it lies beside the grid, and a click that scrolled AND picked whatever pill
		// happened to be under it would be picking at random.
		if ( NewIwadBarMouse( type, x, y ))
			return true;

		const int left = NewModalContentLeft( );
		const int right = NewModalContentRight( );

		// REFRESH, on the title's line.
		{
			const int w = PillW( "REFRESH", SB_NEW_PILL_PAD );
			const int by = SB_NEW_MODAL_TOP + SB_NEW_MODAL_PAD - 2;

			if (( x >= serverbrowser_ToScreenX( right - w )) &&
				( x < serverbrowser_ToScreenX( right )) &&
				( y >= serverbrowser_ToScreenY( by )) &&
				( y < serverbrowser_ToScreenY( by + SB_NEW_PILL_H )))
			{
				g_NewIwadRefreshHot = true;
				if ( type == MOUSE_Release )
				{
					++g_NewIwadEpoch;
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
				}
				return true;
			}
		}

		// CONFIRM.
		{
			const int bx = NewModalConfirmLeft( );
			const int w = NewModalConfirmW( );
			const int by = NewModalButtonTop( );

			if (( x >= serverbrowser_ToScreenX( bx )) &&
				( x < serverbrowser_ToScreenX( bx + w )) &&
				( y >= serverbrowser_ToScreenY( by )) &&
				( y < serverbrowser_ToScreenY( by + SB_DLG_BTN_H )))
			{
				g_NewIwadConfirmHot = true;
				if ( type == MOUSE_Release )
					NewCloseIwadModal( true );
				return true;
			}
		}

		// The pills, through the same placement the draw used.
		{
			const std::vector<zx::PillPlace> placed = NewIwadPills( );
			const int gridTop = NewModalGridTop( );
			const int gridBottom = gridTop + NewModalVisibleRows( ) * SB_NEW_PILL_ROW_H;

			if (( y >= serverbrowser_ToScreenY( gridTop )) &&
				( y < serverbrowser_ToScreenY( gridBottom )))
			{
				// Back into the grid's own space -- offset from its left edge, and scrolled -- which
				// is what the hit test was written against.
				const int vx = serverbrowser_ToVirtualX( x ) - left;
				const int vy = serverbrowser_ToVirtualY( y ) - gridTop
					+ g_NewIwadModalScroll * SB_NEW_PILL_ROW_H;

				const int hit = zx::PillFlowHitTest( placed, SB_NEW_PILL_ROW_H, vx, vy );
				if ( hit >= 0 )
				{
					g_NewIwadModalHot = hit;
					if ( type == MOUSE_Release )
					{
						g_NewIwadModalSel = hit;
						NewCloseIwadModal( true );
					}
					return true;
				}

				// Inside the grid but between pills: swallowed, so a near miss does not close the
				// box the player is aiming inside of.
				return true;
			}
		}

		// [rc4l] Anywhere else closes it WITHOUT choosing. A modal that swallows every click and
		// gives no way out but a key is a trap for anybody using the mouse.
		if ( type == MOUSE_Release )
			NewCloseIwadModal( false );

		return true;
	}

	// [rc4l] What a click on an item does. Shared with the keyboard, which is why it is a function
	// rather than the body of the mouse loop: Enter on a switch and a click on a switch have to be
	// the same act, and the only way to be sure of that is for there to be one of them.
	void BoxActivate( const BoxItem &item )
	{
		switch ( item.kind )
		{
		case BoxItem::Heading:
			NewToggleField( item.field );		// a field's heading folds it; any other does nothing
			break;

		case BoxItem::Flag:
		{
			zx::FlagField &field = g_NewFlags[item.field];
			const unsigned int bit = field.bits[item.bit].bit;

			field.value = zx::FlagSet( field.value, bit, !zx::FlagIsOn( field.value, bit ));

			g_NewFlagEditing = -1;
			NewFlagValueChanged( item.field );
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
			break;
		}

		case BoxItem::Mode:
			NewSetGameMode( static_cast<GAMEMODE_e>( item.bit ));
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
			break;

		case BoxItem::Setting:
			if ( item.setting.slider )
				break;			// the arrows and the steps move it; there is nothing to press

			if ( item.setting.kind == zx::VarKind::Toggle )
			{
				SettingSet( item.setting, SettingIsOn( item.setting ) ? "0" : "1" );
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
			}
			else
			{
				EndSettingEdit( );
				g_NewSettingEditing = item.setting.name.c_str( );
				g_NewFlagEditing = -1;
			}
			break;
		}
	}

	// [rc4l] The pointer, for whichever box is up. One handler for the three of them, for the reason
	// the model exists -- including the scrollbar, which goes through the shared bar helper rather
	// than a copy of it.
	bool SettingsBoxMouse( NewModal which, int type, int x, int y )
	{
		g_NewBoxHot = -1;
		g_NewFlagFieldHot = -1;
		g_NewIwadConfirmHot = false;
		g_NewBoxResetHot = false;

		int totalRows = 0;
		const std::vector<BoxItem> items = BuildBox( which, totalRows );

		const std::vector<int> footFields = BoxFooterFields( which );
		const int visible = NewBigVisibleRows( footFields );

		const int left = NewBigContentLeft( );
		const int top = NewBigContentTop( );

		// The bar first: it sits in its own lane beside the content, and a click that scrolled AND
		// pressed whatever was under it would be pressing at random.
		if ( RegionBarMouse( type, x, y, top, top + visible * SB_NEW_PILL_ROW_H,
			totalRows * SB_NEW_PILL_ROW_H, BoxMaxScroll( which ), BoxScroll( which ),
			g_DraggingNewBoxBar, NewBigBarX( )))
		{
			return true;
		}

		if ( BoxResetMouse( type, x, y ))
			return true;

		// DONE, which sits below the content where nothing else claims a click.
		{
			const int bx = NewBigDoneLeft( );
			const int by = NewBigButtonTop( );

			if (( x >= serverbrowser_ToScreenX( bx )) &&
				( x < serverbrowser_ToScreenX( bx + NewBigBtnW( ))) &&
				( y >= serverbrowser_ToScreenY( by )) &&
				( y < serverbrowser_ToScreenY( by + SB_DLG_BTN_H )))
			{
				g_NewIwadConfirmHot = true;
				if ( type == MOUSE_Release )
				{
					EndSettingEdit( );
					g_NewModal = NewModal::None;
					g_NewFlagEditing = -1;
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
				}
				return true;
			}
		}

		if ( FlagFooterMouse( type, x, y, footFields ))
			return true;

		// The sliders, through the hosting panel's own handler -- the steps, the track and the drag
		// are that control's behaviour rather than this box's.
		if ( HostSliderMouseEvent( type, x, y ))
			return true;

		const int scroll = BoxScroll( which );

		for ( size_t i = 0; i < items.size( ); ++i )
		{
			const BoxItem &item = items[i];

			const int r = item.row - scroll;
			if (( r < 0 ) || ( r >= visible ))
				continue;

			const int iy = top + r * SB_NEW_PILL_ROW_H;

			if (( y < serverbrowser_ToScreenY( iy )) ||
				( y >= serverbrowser_ToScreenY( iy + SB_NEW_PILL_H )))
			{
				continue;
			}

			// A heading and a setting own their whole row; a pill owns only its own width, because
			// its neighbours are on that row too.
			int ix = left;
			int iw = NewBigContentRight( ) - left;

			if (( item.kind == BoxItem::Flag ) || ( item.kind == BoxItem::Mode ))
			{
				ix = left + item.x;
				iw = item.width;
			}

			if (( x < serverbrowser_ToScreenX( ix )) || ( x >= serverbrowser_ToScreenX( ix + iw )))
				continue;

			g_NewBoxHot = static_cast<int>( i );
			g_NewBoxSel = static_cast<int>( i );

			// A setting with a box to type in takes the caret on the PRESS, the same as every other
			// field on this screen: the caret is placed by that press, and a box that took it before
			// it took the keyboard would put it where the next keystroke does not go.
			if (( item.kind == BoxItem::Setting ) && ( item.setting.kind != zx::VarKind::Toggle ))
			{
				if ( type == MOUSE_Click )
				{
					g_NewSettingEditing = item.setting.name.c_str( );
					g_NewFlagEditing = -1;
				}

				if ( FieldMouse( type, x, y, SettingControlX( ), iy, SettingControlW( ),
					SB_NEW_PILL_H, SettingInput( item.setting ), g_NewSettingFirstChar,
					g_NewSettingDragging, g_NewSettingClickTime ))
				{
					return true;
				}

				return true;
			}

			if ( type == MOUSE_Release )
				BoxActivate( item );

			return true;
		}

		// Everything else inside the box is swallowed; outside it closes.
		if (( x >= serverbrowser_ToScreenX( NewBigModalLeft( ))) &&
			( x < serverbrowser_ToScreenX( NewBigModalRight( ))) &&
			( y >= serverbrowser_ToScreenY( NewBigModalTop( ))) &&
			( y < serverbrowser_ToScreenY( NewBigModalBottom( ))))
		{
			return true;
		}

		if ( type == MOUSE_Release )
		{
			EndSettingEdit( );
			g_NewModal = NewModal::None;
			g_NewFlagEditing = -1;
		}

		return true;
	}

	bool NewMouseEvent( int type, int x, int y )
	{
		// The modal owns the pointer while it is up, which is what modal means.
		if ( g_NewModal == NewModal::Iwad )
			return NewIwadModalMouse( type, x, y );
		if ( g_NewModal == NewModal::Maps )
			return NewMapsModalMouse( type, x, y );
		if ( g_NewModal == NewModal::Save )
			return NewSaveModalMouse( type, x, y );
		if (( g_NewModal == NewModal::Flags ) || ( g_NewModal == NewModal::Gameplay ))
			return SettingsBoxMouse( g_NewModal, type, x, y );

		g_NewToolHot = -1;

		// The three settings buttons, under the wad list.
		for ( int i = 0; i < SB_NEW_TOOL_COUNT; ++i )
		{
			const int bx = NewToolLeft( i );

			if (( x < serverbrowser_ToScreenX( bx )) ||
				( x >= serverbrowser_ToScreenX( bx + SB_NEW_TOOL_W )) ||
				( y < serverbrowser_ToScreenY( SB_NEW_TOOL_Y )) ||
				( y >= serverbrowser_ToScreenY( SB_NEW_TOOL_Y + SB_NEW_TOOL_H )))
			{
				continue;
			}

			g_NewToolHot = i;

			if ( type == MOUSE_Release )
			{
				g_NewFocus = NewFocus::Tools;
				g_NewToolSel = i;
				SetFocus( zx::BrowserFocus::Host );
				NewOpenTool( i );
			}

			return true;
		}

		g_NewIwadHot = -1;
		g_NewWadHot = -1;
		g_NewOrderHot = -1;
		g_NewOrderBtnHot = -1;
		g_NewSearchHot = false;
		g_NewButtonHot = false;
		g_NewSaveHot = false;

		// The buttons first: they sit inside the right column, so testing them after the rows would
		// let a row's own test claim a click that landed on one.
		if (( y >= serverbrowser_ToScreenY( SB_NEW_BTN_Y )) &&
			( y < serverbrowser_ToScreenY( SB_NEW_BTN_Y + SB_HOST_BTN_H )))
		{
			if (( x >= serverbrowser_ToScreenX( NewSaveLeft( ))) &&
				( x < serverbrowser_ToScreenX( NewSaveLeft( ) + NewSaveWidth( ))))
			{
				g_NewSaveHot = true;
				if ( type == MOUSE_Release )
				{
					g_NewFocus = NewFocus::Buttons;
					g_NewButtonSel = 0;

					// [rc4l] The browser's focus comes with it, the way the tool buttons take it.
					// Without this the box opened while the keyboard belonged to something else,
					// and every key it wanted went somewhere else instead.
					SetFocus( zx::BrowserFocus::Host );
					NewOpenSaveModal( );
				}
				return true;
			}

			if (( x >= serverbrowser_ToScreenX( NewPlayLeft( ))) &&
				( x < serverbrowser_ToScreenX( SB_HOST_RCOL_RIGHT )))
			{
				g_NewButtonHot = true;
				if ( type == MOUSE_Release )
				{
					g_NewFocus = NewFocus::Buttons;
					g_NewButtonSel = 1;
					NewStartHosting( );
				}
				return true;
			}
		}

		// The bars first: they sit in the gutters beside their rows, and a click that scrolled AND
		// picked whatever row happened to be under it would be picking at random.
		if ( NewWadBarMouse( type, x, y ))
			return true;
		if ( NewOrderBarMouse( type, x, y ))
			return true;

		const bool bLeftColumn = ( x >= serverbrowser_ToScreenX( SB_HOST_LIST_LEFT - 4 )) &&
			( x < serverbrowser_ToScreenX( SB_HOST_LIST_RIGHT ));
		const bool bRightColumn = ( x >= serverbrowser_ToScreenX( SB_HOST_RCOL_LEFT - 4 )) &&
			( x < serverbrowser_ToScreenX( SB_HOST_RCOL_RIGHT ));

		if ( bLeftColumn )
		{
			// The BUTTON's own span, not the whole row: the label shares the line with it, and a
			// label that opens things is a control nobody said was one.
			if (( y >= serverbrowser_ToScreenY( SB_NEW_IWAD_TOP )) &&
				( y < serverbrowser_ToScreenY( SB_NEW_IWAD_BOTTOM )) &&
				( x >= serverbrowser_ToScreenX( NewIwadButtonLeft( ))))
			{
				g_NewIwadHot = 0;
				if ( type == MOUSE_Release )
				{
					g_NewFocus = NewFocus::Iwads;
					SetFocus( zx::BrowserFocus::Host );
					NewOpenIwadModal( );
				}
				return true;
			}

			// [rc4l] The search box, through the SAME press/double-press/drag rule the server search
			// uses. Focus is taken on the press rather than the release, because the caret is placed
			// on that press too and a box that took the caret before it took the keyboard would put
			// it somewhere the next keystroke does not go.
			{
				const bool bOverSearch =
					( y >= serverbrowser_ToScreenY( SB_NEW_SEARCH_TOP )) &&
					( y < serverbrowser_ToScreenY( SB_NEW_SEARCH_TOP + SB_NEW_SEARCH_H ));

				if ( bOverSearch && ( type == MOUSE_Click ))
				{
					g_NewFocus = NewFocus::Search;
					SetFocus( zx::BrowserFocus::Host );
				}

				g_NewSearchHot = bOverSearch;

				if ( FieldMouse( type, x, y, SB_HOST_LIST_LEFT, SB_NEW_SEARCH_TOP,
					SB_HOST_LIST_RIGHT - SB_HOST_LIST_LEFT, SB_NEW_SEARCH_H, g_NewSearch,
					g_NewSearchFirstChar, g_NewSearchDragging, g_NewSearchClickTime ))
				{
					return true;
				}
			}

			const std::vector<zx::LibraryRow> &rows = NewRows( );
			const int wadRow = NewRowAt( y, SB_NEW_WADS_TOP, SB_NEW_WADS_BOTTOM, g_NewWadScroll,
				static_cast<int>( rows.size( )));
			if ( wadRow >= 0 )
			{
				g_NewWadHot = wadRow;
				if ( type == MOUSE_Release )
				{
					// [rc4l] A second click on the row already selected ADDS it. One click to look,
					// one to take -- so a click can never add something the player was only reading,
					// which on a list of twenty thousand is the difference between browsing and
					// fighting the mouse.
					const bool bAgain = ( wadRow == g_NewWadSel ) && ( g_NewFocus == NewFocus::Wads );

					g_NewWadSel = wadRow;
					g_NewFocus = NewFocus::Wads;
					SetFocus( zx::BrowserFocus::Host );

					if ( bAgain )
						NewAddSelected( );
					else
						S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
				}
				return true;
			}
		}

		if ( bRightColumn )
		{
			const int orderRow = NewRowAt( y, SB_NEW_ORDER_TOP, SB_NEW_ORDER_BOTTOM, g_NewOrderScroll,
				static_cast<int>( g_NewOrder.size( )));
			if ( orderRow >= 0 )
			{
				// [rc4l] The buttons first, and the row underneath them. Tested in the same order
				// they are drawn so a click on X cannot also be read as a click on the row it sits
				// in -- which would select the row it is about to remove.
				struct { int left; int action; } buttons[3] = {
					{ NewOrderXLeft( ),    0 },
					{ NewOrderUpLeft( ),   1 },
					{ NewOrderDownLeft( ), 2 },
				};

				for ( int b = 0; b < 3; ++b )
				{
					if (( x < serverbrowser_ToScreenX( buttons[b].left )) ||
						( x >= serverbrowser_ToScreenX( buttons[b].left + SB_NEW_ORDER_BTN_W )))
					{
						continue;
					}

					g_NewOrderBtnHot = orderRow * 3 + buttons[b].action;

					if ( type == MOUSE_Release )
					{
						g_NewOrderSel = orderRow;
						g_NewFocus = NewFocus::Order;
						SetFocus( zx::BrowserFocus::Host );

						if ( buttons[b].action == 0 )
							NewRemoveSelected( );
						else
							NewMoveSelected(( buttons[b].action == 1 ) ? -1 : +1 );
					}

					return true;
				}

				g_NewOrderHot = orderRow;
				if ( type == MOUSE_Release )
				{
					g_NewOrderSel = orderRow;
					g_NewFocus = NewFocus::Order;
					SetFocus( zx::BrowserFocus::Host );
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
				}
				return true;
			}
		}

		return false;
	}

	// [rc4l] The arrows, applied to whichever region has the keyboard.
	//
	// Up and down move within a region; left and right move BETWEEN them, which is the only mapping
	// that works when the regions are laid out in two columns and one of them is a text field.
	bool NewNavigate( zx::NavKey key )
	{
		// [rc4l] The modal takes every arrow while it is up. Letting one through would move the
		// selection on a screen the player cannot see, and they would find it changed on closing.
		if ( ( g_NewModal == NewModal::Iwad ) )
		{
			const int count = static_cast<int>( NewIwads( ).size( ));
			if ( count <= 0 )
				return true;

			// [rc4l] The pills are a GRID, so left and right walk it in order and up and down move
			// by a row. Moving by a row is not moving by a fixed number of pills -- rows hold
			// different numbers of them -- so it is done by looking for the nearest pill on the row
			// above or below, which is what the eye expects and what a fixed step would get wrong
			// every time two rows disagreed about their count.
			const std::vector<zx::PillPlace> placed = NewIwadPills( );
			int next = g_NewIwadModalSel;

			if (( key == zx::NavKey::Left ) || ( key == zx::NavKey::Right ))
			{
				const int step = ( key == zx::NavKey::Left ) ? -1 : 1;
				next = ( g_NewIwadModalSel + step + count ) % count;
			}
			else
			{
				const int step = ( key == zx::NavKey::Up ) ? -1 : 1;
				const int wantRow = placed[g_NewIwadModalSel].row + step;
				const int here = placed[g_NewIwadModalSel].x;

				int best = -1;
				int bestDist = 0;

				for ( int i = 0; i < count; ++i )
				{
					if ( placed[i].row != wantRow )
						continue;

					const int dist = abs( placed[i].x - here );
					if (( best < 0 ) || ( dist < bestDist ))
					{
						best = i;
						bestDist = dist;
					}
				}

				if ( best >= 0 )
					next = best;
			}

			if ( next != g_NewIwadModalSel )
			{
				g_NewIwadModalSel = next;
				g_NewIwadRevealSel = true;
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
			}

			return true;
		}

		switch ( key )
		{
		case zx::NavKey::Up:
		case zx::NavKey::Down:
		{
			// [rc4l] NO WRAPPING WITHIN A REGION. The left column is three controls stacked up, so
			// running off the end of one means the next one, not the other end of the same one.
			// Wrapping would make the IWAD list and the search box unreachable from the wad list,
			// which is where the keyboard lands when the screen opens.
			const int step = ( key == zx::NavKey::Up ) ? -1 : 1;

			// One row, so there is nothing to walk through: down is the box below it, and up is the
			// top of the screen. The choosing happens in the modal.
			if ( g_NewFocus == NewFocus::Iwads )
			{
				if ( step > 0 )
					g_NewFocus = NewFocus::Search;
			}
			else if ( g_NewFocus == NewFocus::Search )
			{
				// A single-line field has nowhere to go vertically, so up and down mean what they
				// mean everywhere else: leave.
				g_NewFocus = ( step < 0 ) ? NewFocus::Iwads : NewFocus::Wads;
			}
			else if ( g_NewFocus == NewFocus::Wads )
			{
				const int count = static_cast<int>( NewRows( ).size( ));
				const int next = g_NewWadSel + step;

				if (( next >= 0 ) && ( next < count ))
				{
					g_NewWadSel = next;
					g_NewWadRevealSel = true;
				}
				else if ( step < 0 )
				{
					g_NewFocus = NewFocus::Search;	// off the top of the wads is the box above
				}
				else
				{
					g_NewFocus = NewFocus::Tools;	// and off the bottom is the row of buttons
				}
			}
			else if ( g_NewFocus == NewFocus::Tools )
			{
				// One row of three, so up leaves it and down has nowhere to go.
				if ( step < 0 )
					g_NewFocus = NewFocus::Wads;
			}
			else if ( g_NewFocus == NewFocus::Buttons )
			{
				// The bottom row of the screen: up is the load order above it.
				if ( step < 0 )
					g_NewFocus = NewFocus::Order;
			}
			else
			{
				const int count = static_cast<int>( g_NewOrder.size( ));
				const int next = g_NewOrderSel + step;

				if (( next >= 0 ) && ( next < count ))
				{
					g_NewOrderSel = next;
					g_NewOrderRevealSel = true;
				}
			}

			S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
			return true;
		}

		case zx::NavKey::Left:
			if ( g_NewFocus == NewFocus::Order )
			{
				// [rc4l] Along the row's own buttons first, and out of the region only from the
				// leftmost one. They were unreachable by keyboard entirely: the cursor could get to
				// a row and not to the three controls sitting on it.
				if ( g_NewOrderBtnSel > 0 )
				{
					g_NewOrderBtnSel--;
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
					return true;
				}

				g_NewFocus = NewFocus::Wads;
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
			}
			else if ( g_NewFocus == NewFocus::Tools )
			{
				// Along the row of three, which is what left and right mean while it has the keyboard.
				if ( g_NewToolSel > 0 )
				{
					g_NewToolSel--;
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
				}
			}
			else if ( g_NewFocus == NewFocus::Buttons )
			{
				if ( g_NewButtonSel > 0 )
				{
					g_NewButtonSel--;
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
				}
			}
			return true;

		case zx::NavKey::Right:
			if ( g_NewFocus == NewFocus::Tools )
			{
				if ( g_NewToolSel < SB_NEW_TOOL_COUNT - 1 )
				{
					g_NewToolSel++;
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
				}
				return true;
			}

			if ( g_NewFocus == NewFocus::Order )
			{
				// The other half of the row walk. See the Left case.
				if ( g_NewOrderBtnSel < 2 )
				{
					g_NewOrderBtnSel++;
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
				}
				return true;
			}

			if ( g_NewFocus == NewFocus::Buttons )
			{
				if ( g_NewButtonSel < 1 )
				{
					g_NewButtonSel++;
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
				}
				return true;
			}
			// [rc4l] Deliberately nothing, for now. It moved the keyboard into the load order, and
			// how that side is reached is being reconsidered -- so it is swallowed rather than left
			// to fall through to the browser's own navigation, which would walk off this screen
			// entirely and look like the key doing something arbitrary.
			return true;
		}

		return false;
	}

	// [rc4l] The NEW screen's keys.
	//
	// The search field claims the keyboard while it has focus, for the reason every field on this
	// menu does: a printable key is a letter being typed and must never also be a shortcut. Away
	// from the field, the keys are the three verbs this screen has.
	// [rc4l] The CUSTOM tab's keys: Tab between its three regions, and the search box while it has
	// them. Everything else it does arrives as an MKEY and is answered in MenuEvent.
	bool CustomKeyEvent( event_t *ev )
	{
		if ( CustomEntries( ).empty( ))
			return false;

		if (( ev->data1 == GK_TAB ) &&
			(( ev->subtype == EV_GUI_KeyDown ) || ( ev->subtype == EV_GUI_KeyRepeat ) ||
			 ( ev->subtype == EV_GUI_Char )))
		{
			if ( ev->subtype != EV_GUI_KeyDown )
				return true;

			static const CustomFocus kOrder[] = { CustomFocus::Search, CustomFocus::List,
				CustomFocus::Buttons };
			const int count = static_cast<int>( countof( kOrder ));

			int at = 0;
			for ( int i = 0; i < count; ++i )
			{
				if ( kOrder[i] == g_CustomFocus )
					at = i;
			}

			const bool bBack = (( ev->data3 & GKM_SHIFT ) != 0 );
			g_CustomFocus = kOrder[( at + ( bBack ? count - 1 : 1 )) % count];

			M_ReleaseMenuButtons( );
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
			return true;
		}

		if ( g_CustomFocus != CustomFocus::Search )
			return false;

		zx::TextInput next = g_CustomSearch;

		switch ( EditTextField( next, ev, 48, false, false, false ))
		{
		case FieldKey::Escape:
		case FieldKey::Enter:
		case FieldKey::Down:
			g_CustomFocus = CustomFocus::List;
			return true;

		case FieldKey::Up:
			return true;

		case FieldKey::Handled:
			if ( next.text != g_CustomSearch.text )
			{
				// A narrower list has a different row under the cursor, so the cursor goes back to
				// the top rather than to whatever happened to land there.
				g_CustomSel = 0;
				g_CustomScroll = 0;
			}

			g_CustomSearch = next;
			return true;

		case FieldKey::Unclaimed:
		case FieldKey::Left:
		case FieldKey::Right:
			return false;
		}

		return false;
	}

	bool NewKeyEvent( event_t *ev )
	{
		// [rc4l] TAB walks the SCREEN's regions, which the arrows cannot.
		//
		// The arrows walk WITHIN a region and step between neighbours, and that is right until a
		// region is four hundred rows long: reaching the three buttons under the wad list meant
		// holding Down past every file on the machine. Tab is what every other program on the desktop
		// uses for exactly this, and nothing on this screen wanted it.
		//
		// Answered FIRST, above the text boxes. A focused field swallows everything it is not asked
		// about -- correctly, or a letter would be a menu shortcut -- so Tab handled further down was
		// Tab that worked everywhere except in the one place you most want to leave.
		if (( g_NewModal == NewModal::None ) && ( ev->data1 == GK_TAB ) &&
			(( ev->subtype == EV_GUI_KeyDown ) || ( ev->subtype == EV_GUI_KeyRepeat ) ||
			 ( ev->subtype == EV_GUI_Char )))
		{
			// The repeat and the character event are swallowed rather than acted on, or one press
			// would move the focus twice.
			if ( ev->subtype != EV_GUI_KeyDown )
				return true;

			static const NewFocus kOrder[] = { NewFocus::Iwads, NewFocus::Search, NewFocus::Wads,
				NewFocus::Tools, NewFocus::Order, NewFocus::Buttons };
			const int count = static_cast<int>( countof( kOrder ));

			int at = 0;
			for ( int i = 0; i < count; ++i )
			{
				if ( kOrder[i] == g_NewFocus )
					at = i;
			}

			const bool bBack = (( ev->data3 & GKM_SHIFT ) != 0 );
			g_NewFocus = kOrder[( at + ( bBack ? count - 1 : 1 )) % count];

			// A key that changes which region owns the keyboard must not leave the old one latched;
			// the same rule the hosting panel's own halves follow.
			M_ReleaseMenuButtons( );
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
			return true;
		}

		// [rc4l] The save box's name field, which owns the keyboard while that box is up.
		//
		// ANY CHANGE TO THE NAME FORGETS THE REPLACE QUESTION. Having been asked about one name says
		// nothing about another, and a Confirm that replaced a preset the player was no longer
		// looking at would be the one unrecoverable mistake this box can make.
		if ( g_NewModal == NewModal::Save )
		{
			// [rc4l] TAB picks which button Enter presses.
			//
			// Left and right belong to the caret here -- this box is a name being typed -- so the
			// two buttons needed a key of their own, and Tab is the one every other form on the
			// desktop uses for exactly this. Without it Cancel was reachable only by Escape, which
			// is a way out but not the same as choosing the button that says so.
			if (( ev->data1 == GK_TAB ) &&
				(( ev->subtype == EV_GUI_KeyDown ) || ( ev->subtype == EV_GUI_KeyRepeat ) ||
				 ( ev->subtype == EV_GUI_Char )))
			{
				if ( ev->subtype == EV_GUI_KeyDown )
				{
					g_NewSaveBtnSel = ( g_NewSaveBtnSel == 0 ) ? 1 : 0;
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
				}

				return true;
			}

			zx::TextInput next = g_NewSaveName;

			switch ( EditTextField( next, ev, 48, false, false, false ))
			{
			case FieldKey::Escape:
				NewCloseSaveModal( );
				return true;

			case FieldKey::Enter:
				if ( g_NewSaveBtnSel == 0 )
					NewSaveConfirm( );
				else
					NewCloseSaveModal( );
				return true;

			case FieldKey::Up:
			case FieldKey::Down:
				// Nothing above or below the field but the two buttons, which left and right walk.
				return true;

			case FieldKey::Handled:
				if ( next.text != g_NewSaveName.text )
					g_NewSaveAsked = false;

				g_NewSaveName = next;
				return true;

			case FieldKey::Unclaimed:
			case FieldKey::Left:
			case FieldKey::Right:
				return false;
			}

			return false;
		}

		// [rc4l] A setting's own box, while it is being typed in.
		//
		// The value is taken on every keystroke that leaves it plausible and left alone otherwise, so
		// a half-typed "1" on the way to "16" is not refused and "1x" never reaches a server. What is
		// plausible is servervar_compute's answer rather than one worked out here.
		if (( g_NewModal != NewModal::None ) && ( g_NewModal != NewModal::Iwad ) &&
			g_NewSettingEditing.IsNotEmpty( ))
		{
			SettingRow row = SettingBeingEdited( );

			if ( row.name.empty( ))
			{
				g_NewSettingEditing = "";
				return false;
			}

			zx::TextInput next = SettingInput( row );

			switch ( EditTextField( next, ev, 10, ( row.kind != zx::VarKind::Fraction ), false,
				false ))
			{
			case FieldKey::Escape:
			case FieldKey::Enter:
			case FieldKey::Up:
			case FieldKey::Down:
				EndSettingEdit( );
				return true;

			case FieldKey::Handled:
				if ( zx::ServerVarAccepts( row.kind, next.text ))
				{
					SettingInput( row ) = next;
					NewSetCvar( row.name, next.text );
				}
				return true;

			case FieldKey::Unclaimed:
			case FieldKey::Left:
			case FieldKey::Right:
				return false;
			}

			return false;
		}

		// [rc4l] The flags box, while one of its number fields is being typed in. Everything else it
		// does arrives as an MKEY and is answered in MenuEvent -- see the note there.
		if (( g_NewModal != NewModal::None ) && ( g_NewModal != NewModal::Iwad ) &&
			( g_NewFlagEditing >= 0 ) &&
			( g_NewFlagEditing < static_cast<int>( g_NewFlagInput.size( ))))
		{
			zx::TextInput next = g_NewFlagInput[g_NewFlagEditing];

			switch ( EditTextField( next, ev, 10, true, false, false ))
			{
			case FieldKey::Escape:
			case FieldKey::Enter:
			case FieldKey::Up:
			case FieldKey::Down:
				g_NewFlagEditing = -1;
				return true;

			case FieldKey::Handled:
				g_NewFlagInput[g_NewFlagEditing] = next;
				NewFlagTextChanged( g_NewFlagEditing );
				return true;

			case FieldKey::Unclaimed:
			case FieldKey::Left:
			case FieldKey::Right:
				return false;
			}

			return false;
		}

		if ( g_NewModal == NewModal::Iwad )
		{
			if (( ev->subtype != EV_GUI_KeyDown ) && ( ev->subtype != EV_GUI_KeyRepeat ))
				return false;

			if ( ev->data1 == GK_RETURN )
			{
				NewCloseIwadModal( true );
				return true;
			}

			if ( ev->data1 == GK_ESCAPE )
			{
				// Out of the chooser, not out of the browser. Escape means "I did not want this",
				// which here is the selection left exactly as it was.
				NewCloseIwadModal( false );
				return true;
			}

			// Everything else is swallowed: a modal that let keys through to the screen behind it
			// would be a menu two things are listening to at once.
			return true;
		}

		if ( g_NewFocus == NewFocus::Search )
		{
			zx::TextInput next = g_NewSearch;

			switch ( EditTextField( next, ev, 48, false, false, false ))
			{
			case FieldKey::Escape:
				// Out of the box, not out of the browser.
				g_NewFocus = NewFocus::Wads;
				return true;

			case FieldKey::Enter:
			case FieldKey::Down:
				g_NewFocus = NewFocus::Wads;
				return true;

			case FieldKey::Up:
				g_NewFocus = NewFocus::Iwads;
				return true;

			case FieldKey::Handled:
				if ( next.text != g_NewSearch.text )
				{
					g_NewSearch = next;

					// Typing narrows the list, so the cursor goes back to the top: keeping its old
					// row would leave it on whatever happened to land there, which on a filtered
					// list is a different file every keystroke.
					g_NewWadSel = 0;
					g_NewWadScroll = 0;
					g_NewRowsValid = false;
				}
				else
				{
					g_NewSearch = next;
				}
				return true;

			// Not a key at all -- above all the mouse, which reaches a menu through this same
			// Responder and must be allowed past.
			case FieldKey::Unclaimed:
			case FieldKey::Left:
			case FieldKey::Right:
				return false;
			}

			return false;
		}

		if (( ev->subtype != EV_GUI_KeyDown ) && ( ev->subtype != EV_GUI_KeyRepeat ) &&
			( ev->subtype != EV_GUI_Char ))
		{
			return false;
		}

		switch ( ev->data1 )
		{
		case GK_RETURN:
			if ( g_NewFocus == NewFocus::Wads )
			{
				NewAddSelected( );
				return true;
			}
			if ( g_NewFocus == NewFocus::Iwads )
			{
				NewOpenIwadModal( );
				return true;
			}
			return false;

		case GK_DEL:
			if ( g_NewFocus == NewFocus::Order )
			{
				NewRemoveSelected( );
				return true;
			}
			return false;

		case '[':
			if ( g_NewFocus == NewFocus::Order )
			{
				NewMoveSelected( -1 );
				return true;
			}
			return false;

		case ']':
			if ( g_NewFocus == NewFocus::Order )
			{
				NewMoveSelected( +1 );
				return true;
			}
			return false;
		}

		return false;
	}

	void DrawHostPanel( )
	{
		const zx::HostState state = zx::HostCurrentState( );
		const bool bLive = zx::HostIsActive( );

		const int w = SB_HOST_RIGHT - SB_HOST_LEFT;
		const int h = SB_HOST_BOTTOM - SB_HOST_TOP;

		const zx::PanelColor topCol = { 22, 24, 34, 235 };
		const zx::PanelColor botCol = { 10, 11, 17, 245 };
		DrawRoundedPanel( SB_HOST_LEFT, SB_HOST_TOP, w, h, topCol, botCol, 8 );

		if ( g_HostKind == HostKind::New )
		{
			DrawNewPanel( );
			return;
		}

		if ( g_HostKind == HostKind::Custom )
		{
			DrawCustomPanel( );
			return;
		}

		// [rc4l] The right column gets the server list's detail backdrop, for the reason it reads as
		// the same kind of thing: a column describing whatever the list beside it has selected. Without
		// it the two tabs looked like different screens rather than two views of one browser.
		//
		// Drawn here, before any of the column's own content and before the clip that masks it, so
		// everything from the title to the foot buttons sits ON it rather than beside it.
		//
		// Inset by the same amount on every side. It was measured off the PANEL's right edge before,
		// which put thirteen units of black to the right of the text and six to the left of it.
		DrawDetailBackdrop( SB_HOST_RCOL_LEFT - SB_HOST_RCOL_INSET,
			SB_HOST_VIEW_TOP - SB_HOST_RCOL_INSET,
			SB_HOST_RCOL_RIGHT + SB_HOST_RCOL_INSET,
			SB_HOST_BTN_Y + SB_HOST_BTN_H + SB_HOST_RCOL_INSET );

		const int x = SB_HOST_LEFT + SB_HOST_PAD;

		// [rc4l] Running is a state of the RIGHT column, not a different screen.
		//
		// It used to replace the whole panel and return, which took the experience list away for as
		// long as you were hosting: you could not look at what else you might run without stopping
		// what you were already running first. The list stays; only the column that was describing
		// your selection now describes your server.
		if ( bLive || ( state == zx::HostState::Failed ))
		{
			DrawHostCatalogue( SB_HOST_LIST_LEFT );

			// Top half: whatever is SELECTED, so the list is worth browsing. Clipped and scrolled on
			// its own, exactly as on the form.
			ClampHostDetailScroll( );
			PushClip( serverbrowser_ToScreenY( SB_HOST_RTOP_TOP ),
				serverbrowser_ToScreenY( SB_HOST_RUN_TOP_BOT ));
			DrawHostDetail( );
			PopClip( );
			DrawHostRegionScrollBar( SB_HOST_RTOP_TOP, SB_HOST_RUN_TOP_BOT,
				HostDetailH( ), g_HostDetailScroll );

			// The seam, in the same faded rule the panel uses everywhere else.
			DrawSeparatorSpan( SB_HOST_RUN_SPLIT, SB_HOST_RCOL_LEFT, SB_HOST_RCOL_RIGHT );

			// Bottom half: what is RUNNING. Measured as it draws, so the scrollbar above knows what
			// it is looking at next frame.
			ClampHostStatusScroll( );

			// [rc4l] The text masks itself by skipping rows, because PushClip does not reach
			// screen->DrawText. See HostTextRowVisible.
			g_HostTextClipTop = SB_HOST_RUN_BOT_TOP;
			g_HostTextClipBottom = SB_HOST_RTOP_BOTTOM;

			const int statusTop = SB_HOST_RUN_BOT_TOP - g_HostStatusScroll;
			const int endY = DrawHostStatus( SB_HOST_RCOL_LEFT, statusTop, state );

			g_HostTextClipTop = 0;
			g_HostTextClipBottom = 0;

			g_HostStatusH = endY - statusTop;
			DrawHostRegionScrollBar( SB_HOST_RUN_BOT_TOP, SB_HOST_RTOP_BOTTOM,
				g_HostStatusH, g_HostStatusScroll );

			DrawHostFootButtons( );
			return;
		}

		// [rc4l] The heading names the ACTION, not the screen: "HOST" is already on the tab, and what
		// someone arriving here has to do is pick something. Centred over both columns, since it
		// belongs to the panel rather than to either one of them.
		{
			// [rc4l] While the files are coming down, the heading IS the transfer. It already spans
			// both columns and is the one line on this panel nothing else is using, so the progress
			// goes there rather than into a strip squeezed between the list and the buttons.
			const FString status = HostDownloadRunning( )
				? zx::waddownload::StatusLine( ) : FString( );

			const bool bBusy = status.IsNotEmpty( );
			const FString heading = bBusy ? status : FString( "SELECT AN EXPERIENCE TO HOST" );
			const int headingW = SmallFont->StringWidth( heading );

			screen->DrawText( SmallFont, bBusy ? CR_GOLD : CR_WHITE,
				SB_HOST_LEFT + (( SB_HOST_RIGHT - SB_HOST_LEFT ) - headingW ) / 2,
				SB_HOST_TOP + SB_HOST_PAD, heading,
				DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
		}

		// [rc4l] The settings are a MASKED, SCROLLING area. Everything between these two calls is
		// drawn at its scrolled position and cut off at the viewport edges -- so a row half in and
		// half out is half drawn, rather than spilling over the heading or the button.
		//
		// Every row's position comes from the same helpers the hit test uses. A field drawn somewhere
		// other than where it is clickable is the one bug this browser has avoided everywhere else by
		// never letting those two work it out separately, and scrolling is the easiest way to
		// reintroduce it.
		ClampHostScroll( );
		PushClip( serverbrowser_ToScreenY( SB_HOST_VIEW_TOP ),
			serverbrowser_ToScreenY( SB_HOST_VIEW_BOTTOM ));

		DrawHostCatalogue( SB_HOST_LIST_LEFT );

		PopClip( );

		// [rc4l] The right column gets its OWN clip, ending above the remix row when there is one.
		// It used to share the list's, which was fine while both columns ran to the same line; the
		// remix row belongs to this column alone, and the list must not lose height to it.
		PushClip( serverbrowser_ToScreenY( SB_HOST_VIEW_TOP ),
			serverbrowser_ToScreenY( HostRightBottom( )));

		// One face at a time. Both used to be on screen at once, each with its own bar, and the
		// settings bled over the boundary because two regions in one clip cannot mask each other.
		if ( g_HostShowSettings )
		{
			// [rc4l] THE OTHER FACE'S CLICK TARGETS GO WITH IT.
			//
			// The gameplay rows and sliders are recorded as they are drawn and cleared at the top of
			// the draw that records them -- which is the DETAIL face, and which is not called at all
			// while the form is up. So the rects from the last time it was drawn stayed live under
			// the form, and the mouse handler tests them before anything on it: a click anywhere
			// they happened to cover was answered by a control that was not on screen.
			//
			// Invisible-but-clickable, and it had been there unnoticed because everything the form
			// draws happened to sit above them. Found by putting a button underneath.
			g_HostGameRows.Clear( );
			g_HostSliders.Clear( );
			g_HostGameFocusRows.Clear( );

			int y = HostFirstFieldY( );
			for ( int i = 0; i < kHostFieldCount; ++i )
			{
				DrawHostField( i, SB_HOST_RCOL_LEFT, y );
				y += HostRowPitch( );
			}

			DrawHostVisibility( SB_HOST_RCOL_LEFT, HostVisibilityY( ));
			DrawHostCopyButton( );
		}
		else
		{
			DrawHostDetail( );
		}

		PopClip( );

		// [rc4l] The experience list's bar. It was the one scrolling region of the three with no bar
		// at all, so once the catalogue grew past a screenful there was nothing saying the list went
		// on -- the wheel worked and gave no reason to try it.
		DrawHostRegionScrollBar( SB_HOST_VIEW_TOP, SB_HOST_VIEW_BOTTOM, HostCatalogueH( ),
			g_HostListScroll, SB_HOST_LBAR_X );

		// [rc4l] The detail column's own bar. It had one only while a server was running, so for the
		// panel a player actually reads before pressing anything there was nothing saying the column
		// went on -- the same fault the list above had, and the reason a fourth gameplay mod could be
		// drawn past the bottom with nothing hinting it was there.
		if ( !g_HostShowSettings && !zx::HostIsActive( ))
		{
			DrawHostRegionScrollBar( SB_HOST_RTOP_TOP, HostDetailViewBottom( ), HostDetailH( ),
				g_HostDetailScroll );
		}

		DrawHostScrollBar( );

		// [rc4l] OUTSIDE the clip. Both buttons sit at the panel's foot, which is below the scrolling
		// viewport, so drawing them inside it cost them their backgrounds and left the labels
		// floating.
		DrawHostFootButtons( );
	}

	// One labelled field. The label sits to the left rather than above so six of them fit without the
	// form scrolling, which would put the START button somewhere the player has to go looking for it.
	void DrawHostField( int index, int x, int y )
	{
		// [rc4l] Measured from the right COLUMN, not the panel. Using the panel's edge here is what
		// pushed the boxes off the end of it when the form stopped owning the full width.
		const int fieldX = x + SB_HOST_RLABEL_W;
		const int fieldW = SB_HOST_RCOL_RIGHT - fieldX;

		// [rc4l] Every one of these has to be false for a field to look focused. Leaving the
		// visibility row out meant the last field kept its gold label and its caret while the row had
		// the keyboard -- two controls claiming the same thing, and the player believing the wrong one.
		const bool bFocused = ( HostFieldFocus( ) == index )
			&& ( g_Focus == zx::BrowserFocus::Host );

		const bool bLettering = HostRowFullyVisible( y, SB_HOST_FIELD_H );

		if ( bLettering )
		{
			screen->DrawText( SmallFont, bFocused ? CR_GOLD : CR_DARKGRAY, x,
				y + ( SB_HOST_FIELD_H - SmallFont->GetHeight( )) / 2 + 1, g_HostFieldLabels[index],
				DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
		}

		const int base = bFocused ? 30 : (( g_HostFieldHot == index ) ? 22 : 16 );
		const zx::PanelColor topCol = { static_cast<BYTE>( base ), static_cast<BYTE>( base ),
			static_cast<BYTE>( base + 10 ), 225 };
		const zx::PanelColor botCol = { static_cast<BYTE>( base + 18 ), static_cast<BYTE>( base + 18 ),
			static_cast<BYTE>( base + 30 ), 210 };
		DrawRoundedPanel( fieldX, y, fieldW, SB_HOST_FIELD_H, topCol, botCol, 5 );

		if ( bFocused )
			FocusAnchor( zx::BrowserFocus::Host, x - 5, y + SB_HOST_FIELD_H / 2 );

		serverbrowser_Tip( x, y, SB_HOST_RIGHT - SB_HOST_PAD - x, SB_HOST_FIELD_H,
			g_HostFieldTips[index] );

		// A password is masked here for the same reason it is masked when joining: it is the one
		// string on this screen that must not be legible over a shoulder.
		FString shown;
		if ( index == kHostFieldPassword )
		{
			for ( size_t i = 0; i < g_HostFields[index].text.size( ); ++i )
				shown += "*";
		}
		else
			shown = g_HostFields[index].text.c_str( );

		const int textY = y + ( SB_HOST_FIELD_H - SmallFont->GetHeight( )) / 2 + 1;
		const int textX = fieldX + 5;

		// The selection, under the text: a band behind the characters rather than an inversion of
		// them, so the letters keep the colour they had and stay readable either way. Same treatment
		// as the search box, because it is the same idea and a player should not have to learn two.
		if ( bFocused && zx::HasSelection( g_HostFields[index] ))
		{
			int from = static_cast<int>( zx::SelectionStart( g_HostFields[index] ));
			int to = static_cast<int>( zx::SelectionEnd( g_HostFields[index] ));
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

				DimClipped( PalEntry( 70, 95, 165 ), 0.85f, sx, sy, sw, sh );
			}
		}

		if ( bLettering )
		{
			screen->DrawText( SmallFont, CR_WHITE, textX, textY, shown,
				DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
		}

		if ( bLettering && bFocused && (( DMenu::MenuTime / 16 ) % 2 == 0 ))
		{
			const FString upTo = FString( shown.GetChars( )).Left(
				static_cast<long>( g_HostFields[index].caret ));
			const int caretX = textX + SmallFont->StringWidth( upTo );
			const int cx = serverbrowser_ToScreenX( caretX );
			const int cw = MAX( 1, serverbrowser_ToScreenX( caretX + 1 ) - cx );
			const int cy = serverbrowser_ToScreenY( textY );
			const int ch = serverbrowser_ToScreenY( textY + SmallFont->GetHeight( )) - cy;
			screen->Dim( PalEntry( 235, 235, 245 ), 0.85f, cx, cy, cw, ch );
		}
	}

	//*************************************************************************
	//
	// [rc4l] Local or global, as a choice the player makes rather than one we make for them.
	//
	// Local is the default and always works. Global depends on a port being forwarded, which we cannot
	// know from in here -- so it is offered, attempted, and then reported honestly rather than being
	// predicted.
	// [rc4l] What the selected entry will actually do, above the settings in the right column.
	//
	// Everything here is already computed for the START press -- PickIwad decides the IWAD and
	// HostPlan decides the files -- and none of it was shown anywhere. A player choosing between two
	// entries was choosing blind.
	// [rc4l] The description column: what the selection IS, in the order someone reads it.
	//
	// Title, the description in full, then a rule, then the files. The rule earns its place: the
	// description is prose about the mod and the file list is a fact about your disk, and with
	// nothing between them the WADs read as a fourth sentence.
	int HostDetailWrapWidth( )
	{
		return SB_HOST_RCOL_RIGHT - SB_HOST_RCOL_LEFT;
	}

	// How many lines the summary needs once wrapped, so the height and the drawing agree.
	int HostDetailSummaryLines( const std::string &summary )
	{
		if ( summary.empty( ))
			return 0;

		FBrokenLines *lines = V_BreakLines( SmallFont, HostDetailWrapWidth( ), summary.c_str( ));
		int count = 0;
		while ( lines[count].Width >= 0 )
			count++;
		V_FreeBrokenLines( lines );

		return count;
	}

	// [rc4l] Which of the selected entry's files this machine actually has, one entry per file.
	//
	// ONE answer, shared by the panel that colours the list and by the button that starts the server,
	// because they were disagreeing. Both used to ask D_AddFile with `check` set to false, and false
	// there does not mean "look quietly" -- it means do not look at all, so the call said yes to every
	// file including ones that were not on disk. Hosting Skulltag therefore passed its own missing-file
	// guard, spawned a server, and left the client's wad_reload to discover the truth and abort, with
	// the panel still claiming to be hosting.
	//
	// Cached per selection: the lookup touches the filesystem once per file and this is read while
	// drawing, so doing it fresh every frame would stat the whole list sixty times a second. A file
	// that appears while you are looking at the entry is picked up the next time the selection moves.
	// [rc4l] SIZE, which doubles as the answer to "do I have it": 0 means it is not on this machine.
	//
	// Measured off the disk rather than declared by the entry. A number baked into the catalogue
	// would describe the mirror's copy, not yours, and would go on claiming a size for a file you do
	// not have -- which is the opposite of what the column is for.
	//
	// By NAME, not by hash, and that limit is the price of running on every selection: checking the
	// content can mean reading a 240MB file, which is not something to do while someone arrows down
	// a list. So this answers "there is a loadable file by this name" and claims nothing more. A
	// different build under the right name still reads as present here.
	//
	// The button is where that gets settled. HostEntryVerifiedPaths asks by md5 once, on the press,
	// and a mismatch goes to the downloader like any other missing file.
	//
	// Keyed on the way of playing as well as the entry, because with per-variant wads those are two
	// different file lists under one selection: switching from Ghouls to Humans without re-measuring
	// would leave the panel showing the previous variant's sizes beside this one's names.
	const std::vector<unsigned long long> &HostEntryFileSizes( int entry, const char *variantId,
		const std::vector<zx::AddonFileRef> &files )
	{
		static int cached = -2;
		static FString cachedVariant;
		static int cachedGeneration = -1;
		static std::vector<unsigned long long> sizes;

		if (( entry != cached ) || ( cachedVariant.Compare( variantId ) != 0 )
			|| ( cachedGeneration != g_HostHaveGeneration )
			|| ( sizes.size( ) != files.size( )))
		{
			cached = entry;
			cachedVariant = variantId;
			cachedGeneration = g_HostHaveGeneration;
			sizes.clear( );

			for ( size_t i = 0; i < files.size( ); ++i )
			{
				// The search is stats only; the size is one more on the path it just resolved.
				const FString at = zx::FindFileInEngineSearchPaths( files[i].name.c_str( ));
				sizes.push_back( at.IsEmpty( ) ? 0 : zx::FileSizeOnDisk( at.GetChars( )));
			}
		}

		return sizes;
	}

	// [rc4l] The path of each of the entry's files whose CONTENT matches what the catalogue asked
	// for, "" where no copy on this disk does. Parallel to addon.files.
	//
	// Deliberately not what the panel draws from. The panel asks by name because it asks on every
	// selection and must stay free; this asks once, when the button is pressed, and is allowed to
	// read a file to be sure. Anything we downloaded is still a stat, since the store is keyed on
	// the same md5 the catalogue carries.
	//
	// Uncached on purpose. It runs once per press, and caching a verdict about files on disk is how
	// you end up hosting on a copy the player replaced since.
	std::vector<FString> HostEntryVerifiedPaths( const std::vector<zx::AddonFileRef> &files )
	{
		std::vector<FString> paths;
		paths.reserve( files.size( ));

		for ( size_t i = 0; i < files.size( ); ++i )
		{
			const zx::AddonFileRef &file = files[i];

			// An entry that ships no md5 cannot be checked, so fall back to the name rather than
			// call a file we have no opinion about missing and download it forever.
			if ( file.md5.empty( ))
				paths.push_back( zx::FindFileInEngineSearchPaths( file.name.c_str( )));
			else
				paths.push_back( zx::waddownload::FindVerifiedCopy( file.name.c_str( ),
					file.md5.c_str( )));
		}

		return paths;
	}

	// The verified path for `name`, or "" if that file was not one of the chosen variant's or did not
	// match.
	FString VerifiedPathFor( const std::vector<zx::AddonFileRef> &files,
		const std::vector<FString> &paths, const char *name )
	{
		for ( size_t i = 0; i < files.size( ); ++i )
		{
			if (( i < paths.size( )) && ( stricmp( files[i].name.c_str( ), name ) == 0 ))
				return paths[i];
		}

		return FString( );
	}

	// [rc4l] Whether THIS selection has a picture to draw in place of its name.
	//
	// Asked by the height as well as by the draw, and they must not answer differently: the region
	// scrolls by the height, so a disagreement is a panel that cannot reach its own last line.
	//
	// The file rather than the loaded texture, because the height is wanted before the draw has had
	// a chance to load anything, and on the first frame of a new selection the texture is still the
	// old one.
	bool HostHasArt( const zx::CatalogueEntry &entry )
	{
		const zx::VariantPick pick = zx::PickVariant( entry.addon, g_HostVariantId.GetChars( ));

		std::string variantId;
		if (( pick.index >= 0 ) && ( pick.index < static_cast<int>( entry.addon.variants.size( ))))
			variantId = entry.addon.variants[pick.index].id;

		return !zx::CatalogueArtPath( entry, variantId ).empty( );
	}

	int HostDetailH( )
	{
		const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( );

		if (( g_HostEntrySel < 0 ) || ( g_HostEntrySel >= static_cast<int>( entries.size( ))))
			return BigFont->GetHeight( ) + 4 + 6 + SB_HOST_LINE + 10;

		const zx::AddonEntry &a = entries[g_HostEntrySel].addon;

		// [rc4l] The file block is a FIXED height once anything is drawn under it, matching what
		// DrawHostWadList reserves. Measuring the lines actually used would make the region's height
		// change as a mod is picked, which is the shift the reservation exists to stop.
		// [rc4l] One answer whether or not there is a panel, now that the list is last either way and
		// capped either way. HostWadListLines measures what will be drawn, so this and the draw cannot
		// come to disagree about how far the region scrolls.
		const bool bSettings = HostHasGameplayRow( );
		const int fileLines = HostWadListLines( a );

		// [rc4l] The picture stands in for the title and is taller than it, so the region has to be
		// told. Without this the panel is short by the difference, which is how the file list ends up
		// cut off with a scrollbar that will not reach it.
		//
		// Asked the same way the draw asks: whether this selection HAS a picture, not whether any
		// does. The two must agree or the region scrolls past its own content.
		const int headH = HostHasArt( entries[g_HostEntrySel] )
			? SB_HOST_ART_H : BigFont->GetHeight( );

		int h = headH + 4
			+ 6								// the rule under the title
			+ HostDetailSummaryLines( a.summary ) * SB_HOST_LINE
			+ 4 + 6							// the rule above the files
			+ fileLines * SB_HOST_LINE
			+ SB_HOST_LINE					// the "N files" total
			+ 10;

		// [rc4l] And the gameplay panel, which the region used to know nothing about -- so it never
		// grew a scrollbar and anything past the fold simply could not be reached. Hard Doom was
		// drawn off the bottom of the panel with no way to scroll to it.
		if ( bSettings )
		{
			h += 4 + 6 + SB_HOST_LINE + 2;	// the rule, and the GAMEPLAY heading

			// One row now, not two: the label shares the control's line.
			if ( HostSelectedRotation( ).size( ) > 1 )
				h += SB_HOST_LINE + 3;

			if ( HostLivesControl( a ).adjustable )
				h += SB_HOST_LINE + 3;

			// Two lines, not one: this is the row whose label sits above its track.
			if ( HostFastWeaponsOffered( a ))
				h += SB_HOST_LINE * 2 + 3;

			if ( HostTeamsControl( a ).adjustable )
				h += SB_HOST_LINE + 3;

			const std::vector<zx::RemixGroup> groups = zx::GroupRemixes( HostOfferedRemixes( a ));

			// The same label column DrawHostGameplay measures, because it decides how wide the pills
			// have to wrap in and therefore how many rows they take.
			const int labelW = HostGameplayLabelW( groups );

			for ( size_t g = 0; g < groups.size( ); ++g )
			{
				if ( groups[g].choices.size( ) <= 1 )
					continue;

				// [rc4l] The SAME wrap DrawHostGameplay performs, from the same function. Two
				// measurements of one layout is exactly how a region ends up able to scroll past its
				// own end, so both ask LayoutWadList rather than each doing its own arithmetic.
				const zx::WadListLayout pills =
					HostPillGeometry( SB_HOST_RCOL_LEFT, groups[g] ).layout;

				h += static_cast<int>( pills.lines.size( )) *
					( SB_HOST_GAME_ROW_H + SB_HOST_PILL_VGAP ) + 3;
			}
		}

		return h;
	}

	// Which IWAD this entry will land on, in the same shape as the files below it, because it is one
	// of them: the game the PWADs sit on top of. Named plainly, so a host who wanted Doom II and is
	// getting Freedoom sees freedoom2.wad and knows.
	FString HostDetailIwadRow( const zx::AddonEntry &addon )
	{
		const zx::IwadPick pick = zx::PickIwad( addon.iwad, zx::AvailableIwads( addon.iwad ));

		FString row;
		if ( pick.choice == zx::IwadChoice::None )
			row.Format( "- %s", pick.wanted.empty( ) ? "no game to run on" : pick.wanted.c_str( ));
		else
			row.Format( "+ %s", pick.iwad.c_str( ));

		return row;
	}

	// [rc4l] The same answer for the running list, which has no room for a marker column. The name on
	// its own when we have it; the wanted name in brackets when we do not, because inside a comma
	// list a bare "-" reads as part of a filename rather than as a verdict about one.
	FString HostDetailIwadName( const zx::AddonEntry &addon )
	{
		const FString row = HostDetailIwadRow( addon );
		const char *const body = row.GetChars( ) + 2;		// past the marker and its space

		if ( row[0] == '+' )
			return FString( body );

		FString missing;
		missing.Format( "(%s)", body );
		return missing;
	}

	// The title names what the whole column is about, so it is the one line that should not look like
	// the rest: BigFont, centred over the column.
	void DrawHostDetailTitle( const char *title, int y )
	{
		if ( !HostDetailRowVisible( y, BigFont->GetHeight( )))
			return;

		const int w = SB_HOST_RCOL_RIGHT - SB_HOST_RCOL_LEFT;

		// Cut to the column, measured in the font it is actually drawn in. "Alpha and Delta Invasion"
		// is wider than the panel and ran off both ends of it, because centring a line that does not
		// fit puts half the overflow on each side.
		const FString fitted = serverbrowser_FitName( title, w, BigFont );

		screen->DrawText( BigFont, CR_WHITE,
			SB_HOST_RCOL_LEFT + ( w - BigFont->StringWidth( fitted )) / 2, y, fitted,
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
	}

	// [rc4l] The picture in place of the title, when the catalogue shipped one.
	//
	// Three times the text's height, which is what it took to be legible: at one the logos are a
	// smudge and at two only the wordmarks read. Returns false when there is nothing to draw, and the
	// caller falls back to the name.
	//
	// Drawn in SCREEN pixels rather than virtual ones, like the country flags and for the same
	// reason: DTA_Clip* is measured there, and it is the only clip that actually reaches a texture.
	// The column's own PushClip is ours and only DimClipped reads it, so a scrolled picture would
	// otherwise draw straight over the panel edge.
	bool DrawHostArtRow( int y, int h )
	{
		// [rc4l] The mix's picture is an ADDITION to the experience's, never a replacement for it.
		//
		// Without this, an experience with no picture whose mix has one would draw the mix's logo
		// where its own name belongs, which says the wrong thing entirely: you would be looking at a
		// header reading BRUTAL DOOM while hosting something else. The mix is already named by its
		// lit pill a few lines below.
		//
		// It also keeps this in step with HostHasArt, which the region's height is measured from. The
		// two answering differently is a panel that cannot scroll to its own last line.
		if ( g_HostArtMain.pTex == NULL )
			return false;

		std::vector<std::pair<int, int> > sizes;
		FTexture *tex[2] = { g_HostArtMain.pTex, g_HostArtMix.pTex };
		int count = 0;

		for ( int i = 0; i < 2; ++i )
		{
			if ( tex[i] == NULL )
				continue;
			tex[count] = tex[i];
			sizes.push_back( std::make_pair( tex[i]->GetWidth( ), tex[i]->GetHeight( )));
			++count;
		}

		if ( count == 0 )
			return false;

		const std::vector<zx::ArtRect> rects = zx::LayoutMenuArt(
			SB_HOST_RCOL_LEFT, y, SB_HOST_RCOL_RIGHT - SB_HOST_RCOL_LEFT, h, SB_HOST_ART_GAP, sizes );

		// The band the column is allowed to paint in, so a picture scrolled half out of the region is
		// cut rather than drawn over whatever is above it.
		const int clipTop = serverbrowser_ToScreenY( SB_HOST_RTOP_TOP );
		const int clipBottom = serverbrowser_ToScreenY( HostDetailViewBottom( ));

		for ( size_t i = 0; i < rects.size( ); ++i )
		{
			const int px = serverbrowser_ToScreenX( rects[i].x );
			const int py = serverbrowser_ToScreenY( rects[i].y );
			const int pw = serverbrowser_ToScreenX( rects[i].x + rects[i].w ) - px;
			const int ph = serverbrowser_ToScreenY( rects[i].y + rects[i].h ) - py;

			if (( pw <= 0 ) || ( ph <= 0 ))
				continue;

			screen->DrawTexture( tex[i], px, py,
				DTA_DestWidth, pw,
				DTA_DestHeight, ph,
				DTA_ClipTop, clipTop,
				DTA_ClipBottom, clipBottom,
				TAG_DONE );
		}

		return true;
	}

	void DrawHostDetail( )
	{
		const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( );
		const int x = SB_HOST_RCOL_LEFT;
		int y = SB_HOST_RTOP_TOP - g_HostDetailScroll;

		// Rebuilt every frame from what actually gets drawn, so a row that scrolled away or belongs
		// to a selection that has changed cannot be left behind as a live click target.
		g_HostGameRows.Clear( );
		g_HostSliders.Clear( );
		g_HostGameFocusRows.Clear( );

		// [rc4l] Reached only when there is nothing to describe. This used to be the Custom row's
		// panel; with that row gone, an out-of-range selection means the catalogue is empty.
		if (( g_HostEntrySel < 0 ) || ( g_HostEntrySel >= static_cast<int>( entries.size( ))))
		{
			DrawHostDetailTitle( "Nothing to host", y );
			y += BigFont->GetHeight( ) + 4;

			if ( HostDetailRowVisible( y, 2 ))
				DrawSeparatorSpan( y, SB_HOST_RCOL_LEFT, SB_HOST_RCOL_RIGHT );
			y += 6;

			if ( HostDetailRowVisible( y, SB_HOST_LINE ))
			{
				// [rc4l] Centred like the title above it, not left-aligned at the column edge --
				// an empty state is a single caption under a single heading, and the two hanging
				// at different alignments read as a layout accident.
				const char *caption = "No experiences are installed";
				const int capW = SB_HOST_RCOL_RIGHT - SB_HOST_RCOL_LEFT;
				screen->DrawText( SmallFont, CR_WHITE,
					SB_HOST_RCOL_LEFT + ( capW - SmallFont->StringWidth( caption )) / 2, y, caption,
					DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
			}
			return;
		}

		const zx::AddonEntry &addon = entries[g_HostEntrySel].addon;
		const zx::VariantPick pick = zx::PickVariant( addon, g_HostVariantId.GetChars( ));

		// [rc4l] Whatever the catalogue has for what is actually selected, refreshed here rather than
		// on the keypress: the selection can change from the list, from a pill, from the keyboard or
		// from a remembered preference being resolved, and the draw is the one place that sees all
		// of them. Reloads only when the path changes, so this costs a string compare per frame.
		// [rc4l] The id off the variant itself, since the pick answers with an index. Empty for an
		// entry that plays one way, which is exactly what names its picture art.png.
		std::string variantId;
		if (( pick.index >= 0 ) && ( pick.index < static_cast<int>( addon.variants.size( ))))
			variantId = addon.variants[pick.index].id;

		serverbrowser_SetArt( g_HostArtMain,
			zx::CatalogueArtPath( entries[g_HostEntrySel], variantId ).c_str( ));

		const std::vector<zx::RemixPick> picks = HostRemixPicks( addon );
		FString mixArt;
		for ( size_t i = 0; i < picks.size( ); ++i )
		{
			// The baseline mix adds nothing and is not a thing you chose, so it brings no picture.
			if ( picks[i].index <= 0 )
				continue;

			const std::string at = zx::CatalogueRemixArtPath( picks[i].id );
			if ( !at.empty( ))
			{
				mixArt = at.c_str( );
				break;
			}
		}
		serverbrowser_SetArt( g_HostArtMix, mixArt );

		// [rc4l] The picture stands in for the name; the name is drawn when there is no picture.
		//
		// The VARIANT's name, not the entry's, because the variant is what the panel below describes
		// and what the pills change. The entry's name is on the row you selected in the list beside
		// this, so nothing is lost by not repeating it here.
		if ( DrawHostArtRow( y, SB_HOST_ART_H ))
		{
			y += SB_HOST_ART_H + 4;
		}
		else
		{
			DrawHostDetailTitle( pick.name.empty( ) ? addon.name.c_str( ) : pick.name.c_str( ), y );
			y += BigFont->GetHeight( ) + 4;
		}

		// [rc4l] A rule under the title as well, so the column reads as three bands -- what it is
		// called, what it is, what it loads -- rather than a heading with a paragraph stuck to it.
		if ( HostDetailRowVisible( y, 2 ))
			DrawSeparatorSpan( y, SB_HOST_RCOL_LEFT, SB_HOST_RCOL_RIGHT );
		y += 6;

		// Wrapped rather than cut. A summary is a sentence, and truncating one reads as a bug.
		if ( !addon.summary.empty( ))
		{
			FBrokenLines *lines = V_BreakLines( SmallFont, HostDetailWrapWidth( ),
				addon.summary.c_str( ));

			for ( int i = 0; lines[i].Width >= 0; ++i )
			{
				if ( HostDetailRowVisible( y, SB_HOST_LINE ))
				{
					screen->DrawText( SmallFont, CR_WHITE, x, y, lines[i].Text,
						DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
				}
				y += SB_HOST_LINE;
			}

			V_FreeBrokenLines( lines );
		}


		// [rc4l] What you play it WITH, ABOVE what it loads rather than below.
		//
		// The order used to be the other way and it could not be made to sit still: picking a mix adds
		// a file, the list grows a line, and everything under it moves -- while the pointer is on the
		// pill that was just clicked, so the next click lands on a different setting. Two attempts to
		// reserve the room instead both paid for it with blank lines on every draw.
		//
		// Putting the controls first ends the argument. Nothing the list does can push them, so it
		// needs no reservation and no blank lines, and the settings are at the top of the scroll
		// region where they no longer have to be scrolled to at all. The list is what the growth is
		// FOR, and it grows downwards into the space it was always going to occupy.
		y = DrawHostGameplay( x, y, addon );

		// The same faded rule the server detail panel puts between what a server IS and what it wants
		// you to load, for exactly the same reason.
		y += 4;
		if ( HostDetailRowVisible( y, 2 ))
			DrawSeparatorSpan( y, SB_HOST_RCOL_LEFT, SB_HOST_RCOL_RIGHT );
		y += 6;

		// [rc4l] WHAT it loads, and how big. Not whether you have it.
		//
		// Every row used to be marked + or - and coloured for it, which answered a question nobody
		// standing here is asking: missing files are fetched when you press the button, so the list
		// was reporting an inventory the player has no use for and colouring half of it like a
		// fault. Size is the fact that changes what someone decides -- the server list has shown it
		// for the same reason, from SQF2_WAD_SIZES, and this is the same answer from the catalogue.
		const std::vector<zx::AddonFileRef> loads = HostSelectedFiles( addon );
		const std::vector<unsigned long long> &sizes = HostEntryFileSizes( g_HostEntrySel,
			g_HostVariantId.GetChars( ), loads );

		// [rc4l] The IWAD leads the list rather than sitting on a line of its own above it. It IS one
		// of the files the server loads, and giving it a private row said it was a different kind of
		// thing -- which cost a whole line to say something the name already says.
		TArray<FString> names;
		names.Push( HostDetailIwadName( addon ));
		for ( size_t i = 0; i < loads.size( ); ++i )
			names.Push( FString( loads[i].name.c_str( )));

		unsigned long long total = 0;
		for ( size_t i = 0; i < sizes.size( ) && i < loads.size( ); ++i )
			total += sizes[i];

		// Nothing follows it now, so it runs to the cap and the hover carries the rest.
		DrawHostWadList( x, y, names, total, true );
	}

	// [rc4l] How many lines the file list actually takes, for the region height to allow for.
	//
	// Measured, not reserved. There were two goes at reserving room instead -- a flat three lines,
	// then the widest this entry's list could reach across its mixes -- both to stop the block
	// changing height when picking a mix adds a file, since everything below then moves a line while
	// the pointer is still over the pill that was just clicked.
	//
	// Both spent the room whether or not it was needed, which is the wrong trade in a column this
	// short: the blank lines are there on every draw, and the shift they prevent happens only in the
	// moment after a click. The panel scrolls less now, which is worth more than a settled pointer.
	int HostWadListLines( const zx::AddonEntry &addon )
	{
		// [rc4l] The column the detail panel draws in, which is NOT SB_HOST_LEFT: that is the panel's
		// own left edge and the detail sits in the right column. Measuring against the wrong one gives
		// a line count the draw does not agree with, and a region that can scroll past its own end.
		const int wrapW = SB_HOST_RCOL_RIGHT - SB_HOST_RCOL_LEFT;

		std::vector<int> widths;
		widths.push_back( SmallFont->StringWidth( HostDetailIwadName( addon )));

		const std::vector<zx::AddonFileRef> loads = HostSelectedFiles( addon );
		for ( size_t i = 0; i < loads.size( ); ++i )
			widths.push_back( SmallFont->StringWidth( loads[i].name.c_str( )));

		const zx::WadListLayout layout = zx::LayoutWadList( widths, SmallFont->StringWidth( ", " ),
			SmallFont->StringWidth( ", ..." ), wrapW, SB_HOST_WADS_MAXLINES );

		return MAX( 1, static_cast<int>( layout.lines.size( )));
	}

	// [rc4l] The files, as running text rather than a table. Returns the y below what it drew.
	//
	// `bCapped` is whether something is drawn underneath: the list stops at three lines to leave room
	// for it, and runs as long as it likes when nothing is.
	int DrawHostWadList( int x, int y, const TArray<FString> &names,
		unsigned long long total, bool bCapped )
	{
		if ( names.Size( ) == 0 )
			return y;

		const int wrapW = SB_HOST_RCOL_RIGHT - x;
		const int sepW = SmallFont->StringWidth( ", " );
		const int dotsW = SmallFont->StringWidth( ", ..." );

		std::vector<int> widths;
		widths.reserve( names.Size( ));
		for ( unsigned i = 0; i < names.Size( ); ++i )
			widths.push_back( SmallFont->StringWidth( names[i] ));

		// Capped only when something is drawn underneath. With the whole column to itself the list
		// runs as long as it likes.
		const zx::WadListLayout layout = zx::LayoutWadList( widths, sepW, dotsW, wrapW,
			bCapped ? SB_HOST_WADS_MAXLINES : 0 );

		const int listTop = y;

		for ( size_t ln = 0; ln < layout.lines.size( ); ++ln )
		{
			const zx::WadListLine &line = layout.lines[ln];

			if ( HostDetailRowVisible( y, SB_HOST_LINE ))
			{
				FString text;
				for ( size_t i = line.first; i < line.end; ++i )
				{
					if ( i > line.first )
						text += ", ";
					text += names[static_cast<unsigned>( i )];
				}

				// A comma after the last name on a line that is not the last: the break is a wrap,
				// not the end of the list, and without it the line reads as finished.
				const bool bLastLine = ( ln + 1 == layout.lines.size( ));
				if ( !bLastLine )
					text += ",";
				else if ( layout.truncated )
					text += ", ...";

				// [rc4l] A single filename can be wider than the whole column -- Super Demon ships a
				// forty-character one -- and the layout puts it on a line of its own rather than
				// losing it. Cutting it is this end's job: without this it ran straight out past the
				// panel edge and over whatever was beside it.
				if ( SmallFont->StringWidth( text ) > wrapW )
					text = serverbrowser_FitName( text, wrapW );

				screen->DrawText( SmallFont, CR_GRAY, x, y, text,
					DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
			}
			y += SB_HOST_LINE;
		}

		// [rc4l] One total instead of a size per name. Per-file sizes needed a right-hand column to
		// line up in, and that column is what stopped the names running on. What actually decides
		// anything here is how big the download is, and that is one number.
		//
		// Against the names it counts, with nothing reserved between them. See HostWadListLines for
		// what used to sit in that gap and why it is gone.
		if ( HostDetailRowVisible( y, SB_HOST_LINE ))
		{
			FString summary;
			summary.Format( "%u file%s", names.Size( ), ( names.Size( ) == 1 ) ? "" : "s" );

			// Only what this machine can actually measure. Files not downloaded yet have no size, so
			// a total is a floor rather than the whole story -- saying nothing beats saying a number
			// that quietly means something else.
			if ( total > 0 )
			{
				summary += ", ";
				summary += zx::FormatByteSize( total ).c_str( );
			}

			screen->DrawText( SmallFont, CR_DARKGRAY, x, y, summary,
				DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
		}
		y += SB_HOST_LINE;

		// [rc4l] The whole list, every time, not only when the cap bit. A name can be cut for being
		// wider than the column while nothing was dropped at all, and from the reader's side those
		// are the same problem: text they can see is missing. Hung on the drawn block so it appears
		// where they are already looking.
		FString all;
		for ( unsigned i = 0; i < names.Size( ); ++i )
		{
			if ( i > 0 )
				all += ", ";
			all += names[i];
		}

		serverbrowser_Tip( x, listTop, wrapW, y - listTop, all.GetChars( ));

		return y;
	}

	// [rc4l] How many lives, as a track rather than a list of named options.
	//
	// It is a number with a range, and a slider is what that is. It also stops the catalogue needing
	// a remix folder per value, which is what the three it replaced were: one line of cfg each,
	// setting one integer, and doing nothing at all on three of the entries that offered them.
	//
	// [rc4l] One end-stop of the lives track. Rounded like a pill, since it is the same kind of small
	// pressable thing, and dimmed when the value is already against that end.
	void DrawHostSliderStep( int bx, int by, int bw, const char *glyph, bool bEnabled )
	{
		zx::PanelColor top, bot;
		if ( bEnabled )
		{
			top.r = 62; top.g = 68; top.b = 90; top.a = 225;
			bot.r = 44; bot.g = 48; bot.b = 66; bot.a = 225;
		}
		else
		{
			top.r = 34; top.g = 36; top.b = 46; top.a = 160;
			bot.r = 28; bot.g = 30; bot.b = 40; bot.a = 160;
		}

		DrawRoundedPanel( bx, by - 1, bw, SB_HOST_GAME_ROW_H, top, bot, SB_HOST_PILL_RADIUS );

		screen->DrawText( SmallFont, bEnabled ? CR_WHITE : CR_DARKGRAY,
			bx + ( bw - SmallFont->StringWidth( glyph )) / 2, by, glyph,
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
	}

	// [rc4l] NOT drawn at all when it does not apply.
	//
	// It was greyed in place at first, on the reasoning that a control which vanishes teaches nothing
	// and that a fixed height cannot shift. Both true, and both beaten by the fact that two of the
	// column's few lines were going to "this way of playing has no lives" on every deathmatch entry
	// in the catalogue. Saying nothing is worth more than saying that.
	//
	// It costs no layout shift either, because whether lives apply is decided by the variant's
	// gamemode. Picking a remix cannot change it, so nothing moves while a setting is being used --
	// only when the way of playing changes, which redraws the panel anyway.

	// [rc4l] A slider row: a label, a step button at each end, a track between them, and the value.
	//
	// Generic on purpose. Lives is the only setting shaped like this today and will not be the last,
	// so the geometry, the rounding, the disabled ends and the recorded hit rects are written once
	// here and keyed by `id`. A second slider is a call, not a copy.
	//
	// `valueText` rather than the number, because what a value MEANS is the caller's business: zero
	// lives is "Unlimited", and zero of something else will be something else again.
	int DrawHostSlider( const char *id, const char *label, int x, int y, int labelW,
		int minV, int maxV, int value, const char *valueText, const char *tip,
		bool bLabelAbove = false )
	{
		// [rc4l] The label shares the control's row rather than taking one above it. Three axes on a
		// co-op entry is three lines saved, in a column that has none to spare.
		//
		// One exception, and it earns it: WEAPON SPEED is wider than the shared column can carry
		// without pushing every other control across the panel, and the abbreviation it fitted in --
		// "WEAPONS" -- did not say what the setting does. A header line for that one costs a line and
		// leaves the column sized to the labels that do fit.
		if ( bLabelAbove )
		{
			if ( HostDetailRowVisible( y, SB_HOST_LINE ))
			{
				screen->DrawText( SmallFont, CR_DARKGRAY, x, y, label,
					DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
			}

			y += SB_HOST_LINE;
			labelW = 0;
		}

		// [rc4l] Registered BEFORE the visibility test, unlike a hit rect. A pointer cannot click what
		// it cannot see, but the keyboard is entitled to reach a row that has scrolled off -- and
		// RevealHostFocus is what brings it back.
		{
			HostGameFocusRow row;
			row.bSlider = true;
			row.id = id;
			row.y = y;
			row.h = SB_HOST_GAME_ROW_H;
			g_HostGameFocusRows.Push( row );
		}

		const bool bFocused = ( HostGameplayFocus( ) ==
			static_cast<int>( g_HostGameFocusRows.Size( )) - 1 ) &&
			( g_Focus == zx::BrowserFocus::Host );

		if ( bFocused )
			FocusAnchor( zx::BrowserFocus::Host, x - 5, y + SB_HOST_GAME_ROW_H / 2 );

		const bool bDraw = HostDetailRowVisible( y, SB_HOST_LINE );

		if ( bDraw && !bLabelAbove )
		{
			screen->DrawText( SmallFont, CR_DARKGRAY, x, y, label,
				DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
		}

		if ( bDraw )
		{
			DrawSliderTrack( id, x + labelW, y, SB_HOST_RCOL_RIGHT, minV, maxV, value, valueText,
				tip );
		}

		return y + SB_HOST_LINE + 3;
	}

	// [rc4l] The slider ITSELF: two steps, a track, a knob and the readout, between `rowX` and
	// `rightEdge`.
	//
	// Split out of the row above so a slider can be drawn somewhere that is not the hosting panel --
	// the settings boxes want the same control and must not grow a second one that merely resembles
	// it. What stayed behind is what only that panel has: its label column, its scroll clipping and
	// its keyboard rows.
	void DrawSliderTrack( const char *id, int rowX, int y, int rightEdge, int minV, int maxV,
		int value, const char *valueText, const char *tip )
	{
		// [rc4l] The VALUE is measured first and the track takes what is left, so a track never runs
		// under its own readout. Measured against the WIDEST it could say rather than what it says
		// now, or the track would resize as the value changed and the knob would move twice.
		const int widest = MAX( SmallFont->StringWidth( "Unlimited" ),
			SmallFont->StringWidth( valueText ));
		const int valueX = rightEdge - widest;

		const int stepW = SmallFont->StringWidth( "-" ) + 8;
		const int minusX = rowX;
		const int plusX = ( valueX - 6 ) - stepW;
		const int trackX = minusX + stepW + 4;
		const int trackW = ( plusX - 4 ) - trackX;

		if ( trackW > 8 )
		{
			HostSliderRect rec;
			rec.id = id;
			rec.trackX = trackX; rec.trackY = y; rec.trackW = trackW;
			rec.minusX = minusX; rec.plusX = plusX; rec.stepW = stepW;
			rec.min = minV; rec.max = maxV; rec.value = value;
			g_HostSliders.Push( rec );

			// Drawn dim at the end they cannot move from, so a button that would do nothing says so
			// before it is pressed rather than by not responding.
			DrawHostSliderStep( minusX, y, stepW, "-", value > minV );
			DrawHostSliderStep( plusX, y, stepW, "+", value < maxV );

			const int mid = y + SmallFont->GetHeight( ) / 2;

			// The track, then the part of it that is filled, then the knob. Three dims rather than a
			// texture, the same way every other bar in this browser is drawn.
			screen->Dim( PalEntry( 90, 100, 130 ), 0.55f,
				serverbrowser_ToScreenX( trackX ), serverbrowser_ToScreenY( mid ),
				serverbrowser_ToScreenX( trackX + trackW ) - serverbrowser_ToScreenX( trackX ),
				MAX( 1, serverbrowser_ToScreenY( mid + 1 ) - serverbrowser_ToScreenY( mid )));

			const int span = MAX( 1, maxV - minV );
			const int filled = ( trackW * ( value - minV )) / span;

			screen->Dim( PalEntry( 120, 200, 140 ), 0.85f,
				serverbrowser_ToScreenX( trackX ), serverbrowser_ToScreenY( mid ),
				serverbrowser_ToScreenX( trackX + filled ) - serverbrowser_ToScreenX( trackX ),
				MAX( 1, serverbrowser_ToScreenY( mid + 1 ) - serverbrowser_ToScreenY( mid )));

			const bool bHot = ( g_HostSliderHot.Compare( id ) == 0 );
			const int knobX = trackX + filled;

			screen->Dim( bHot ? PalEntry( 235, 240, 255 ) : PalEntry( 190, 205, 235 ), 1.0f,
				serverbrowser_ToScreenX( knobX - 2 ), serverbrowser_ToScreenY( mid - 3 ),
				serverbrowser_ToScreenX( knobX + 2 ) - serverbrowser_ToScreenX( knobX - 2 ),
				serverbrowser_ToScreenY( mid + 4 ) - serverbrowser_ToScreenY( mid - 3 ));

			if (( tip != NULL ) && ( tip[0] != 0 ))
				serverbrowser_Tip( trackX, y - 1, trackW, SB_HOST_LINE, tip );
		}

		// [rc4l] Dimmed with the rest of the row when the value cannot move. A white readout beside
		// two greyed steps and a dead track says the number is live when nothing else on the row does.
		screen->DrawText( SmallFont, ( minV < maxV ) ? CR_WHITE : CR_DARKGRAY, valueX, y, valueText,
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
	}

	// How many lives, as one instance of the slider above.
	int DrawHostLives( int x, int y, int labelW, const zx::AddonEntry &addon )
	{
		const zx::LivesControl lives = HostLivesControl( addon );

		if ( !lives.adjustable )
			return y;

		FString value;
		if ( lives.unlimited )
			value = "Unlimited";
		else
			value.Format( "%d", lives.value );

		return DrawHostSlider( "lives", "LIVES", x, y, labelW, lives.min, lives.max, lives.value,
			value.GetChars( ),
			lives.unlimited
				? "No limit. Die as often as you like."
				: "How many times each player may die before they are out." );
	}

	// [rc4l] How fast weapons fire, as the second instance of the slider.
	//
	// sv_fastweapons is 0 to 2 and the cvar clamps itself to that, so the range is the engine's
	// rather than a number chosen here. What each stop MEANS is named rather than shown as a digit:
	// "2" tells nobody that the states without an action function drop to no ticks at all.
	int DrawHostFastWeapons( int x, int y, int labelW, const zx::AddonEntry &addon )
	{
		if ( !HostFastWeaponsOffered( addon ))
			return y;

		const zx::WeaponsPlan plan = HostWeaponsPlan( addon );
		const int value = plan.speed;

		static const char *const kNames[3] = { "Normal", "Fast", "Fastest" };
		static const char *const kTips[3] = {
			"The weapons as the pack timed them.",
			"Every weapon state cut to a single tick, and the ammo made infinite to match.",
			"As Fast, and the states with nothing to do take no time at all.",
		};

		// [rc4l] Pinned rather than hidden while a mix owns the weapons. The row is the only place
		// that can say WHY the setting is unavailable, and a control that vanishes says nothing --
		// the player is left thinking the panel forgot it. A ceiling equal to the floor makes both
		// steps draw dim and the track inert, through the slider's own end-stop rules.
		const int top = plan.speedAdjustable ? zx::FastWeaponsMax( ) : 0;

		return DrawHostSlider( "fastweapons", "WEAPON SPEED", x, y, labelW, 0, top,
			value, kNames[value],
			plan.speedAdjustable
				? kTips[value]
				: "The mix brings its own weapons, with their own timings. Choose Vanilla to speed them up.",
			true );
	}

	// [rc4l] Where to open, as a slider over the rotation the pack already writes.
	//
	// A slider and not pills: thirty-two maps is not a row of chips, and the thing being chosen has a
	// natural ORDER, which is exactly what a track shows and a set of pills does not. The steps at
	// each end move one map at a time, so a precise pick does not depend on dragging accurately.
	//
	// It names the map rather than the position. "MAP07" is what the rotation says and what the
	// server will report; "7 of 32" would be a number the player has to translate.
	//
	// Only where there is something to choose. A pack with no written rotation has no list to index
	// into, and one with a single map has one answer.
	int DrawHostStartMap( int x, int y, int labelW )
	{
		const std::vector<std::string> &maps = HostSelectedRotation( );

		if ( maps.size( ) <= 1 )
			return y;

		const int index = HostStartMapIndex( );

		FString tip;
		tip.Format( "Open on %s, map %d of %d in the rotation.", maps[index].c_str( ),
			index + 1, static_cast<int>( maps.size( )));

		return DrawHostSlider( "startmap", "FIRST MAP", x, y, labelW, 0,
			static_cast<int>( maps.size( )) - 1, index, maps[index].c_str( ), tip.GetChars( ));
	}

	// [rc4l] How many sides, as the third instance of the slider.
	//
	// This replaced two pills labelled with gamemode names, which was the wrong question twice over:
	// they were really asking about TEAMS, and they could only answer yes or no when the engine has
	// carried sv_maxteams and four of them all along. The stops skip 1, so the slider moves an INDEX
	// and teamspick_compute owns the mapping in both directions.
	int DrawHostTeams( int x, int y, int labelW, const zx::AddonEntry &addon )
	{
		const zx::TeamsControl teams = HostTeamsControl( addon );

		if ( !teams.adjustable )
			return y;

		FString value;
		if ( teams.count < 2 )
			value = "Off";
		else
			value.Format( "%d", teams.count );

		return DrawHostSlider( "teams", "TEAMS", x, y, labelW, 0, zx::TeamsStopCount( ) - 1,
			teams.stop, value.GetChars( ),
			( teams.count < 2 )
				? "Everyone for themselves."
				: "Sides, sharing their frags and their colour." );
	}

	// [rc4l] The pill axes, one pass per band.
	//
	// MIX leads the panel, being the setting a host most likely came here to change and the only
	// one whose row count grows with the catalogue.
	//
	// MODE comes next, above the sliders rather than below them, because teams and lives both read
	// the gamemode and a control belongs above what it changes.
	//
	// Everything else follows the sliders, where any axis added later lands unless argued for.
	enum class HostAxisBand { Mix, Mode, Rest };

	// Which band an axis is in, with MODE recognised by what its choices DO rather than by what the
	// catalogue called the group, since a magic group name would be a rule the schema never made.
	HostAxisBand HostBandOf( const zx::RemixGroup &group )
	{
		if ( group.id == kHostMixGroup )
			return HostAxisBand::Mix;

		for ( size_t i = 0; i < group.choices.size( ); ++i )
		{
			if ( group.choices[i].gameMode != zx::HostGameMode::Unknown )
				return HostAxisBand::Mode;
		}

		return HostAxisBand::Rest;
	}

	int DrawHostRemixAxes( int x, int y, int labelW, const std::vector<zx::RemixGroup> &groups,
		const zx::WeaponsPlan &plan, HostAxisBand band )
	{
		for ( size_t g = 0; g < groups.size( ); ++g )
		{
			const std::vector<zx::AddonRemix> &choices = groups[g].choices;
			if ( choices.size( ) <= 1 )
				continue;

			if ( HostBandOf( groups[g] ) != band )
				continue;

			// The axis is one row as far as the keyboard is concerned, however many lines of pills it
			// wraps onto. Same registry the sliders use, in the same draw order.
			{
				HostGameFocusRow row;
				row.bSlider = false;
				row.id = groups[g].id;
				row.y = y;
				row.h = SB_HOST_GAME_ROW_H;
				g_HostGameFocusRows.Push( row );
			}

			const bool bAxisFocused = ( HostGameplayFocus( ) ==
				static_cast<int>( g_HostGameFocusRows.Size( )) - 1 ) &&
				( g_Focus == zx::BrowserFocus::Host );

			// [rc4l] The lock, and it has to be read HERE rather than taken from HostRemixPicks: this
			// draw walks the groups itself, and a mix group drawn from the raw preference would show
			// the choice the player made rather than the baseline actually being served.
			const bool bAxisLocked = ( groups[g].id == kHostMixGroup ) && plan.mixLocked;

			const zx::RemixPick pick = zx::PickRemix( choices,
				bAxisLocked ? std::string( ) : HostRemixWanted( groups[g].id ));

			// [rc4l] The label sits on the FIRST row of pills rather than above them, which is a line
			// back per axis. Wrapped rows hang under the pills, not under the label, so the block
			// still reads as one thing.
			if ( !groups[g].id.empty( ) && HostDetailRowVisible( y, SB_HOST_LINE ))
			{
				FString label = groups[g].id.c_str( );
				label.ToUpper( );

				screen->DrawText( SmallFont, CR_DARKGRAY, x, y, label,
					DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
			}

			// [rc4l] Pills, WRAPPED across as many lines as the axis needs.
			//
			// This was "pills if they all fit on one line, otherwise a plain list", which sent Mix's
			// four options back to four rows over a few pixels. Pills that wrap are still pills:
			// everything stays visible and four options cost two lines instead of four. The breaks
			// come from wadlist_compute, the same greedy fill the file list uses, so there is one
			// place that decides how a row of measured things becomes lines.
			//
			// A list survives only where the pills would be one per line anyway, which is an axis
			// whose options are too wide to pack at all -- and there a list says it better.
			//
			// Room for the dot and the gaps either side of it, plus the trailing gap after the label.
			const HostPillGeom geom = HostPillGeometry( x, groups[g] );

			const std::vector<int> &pillWidths = geom.widths;
			const zx::WadListLayout &pills = geom.layout;
			const int pillGap = geom.gap;
			const int pillLeft = geom.left;
			const int pillRoom = SB_HOST_RCOL_RIGHT - pillLeft;

			for ( size_t ln = 0; ln < pills.lines.size( ); ++ln )
			{
				const zx::WadListLine &pline = pills.lines[ln];

				if ( HostDetailRowVisible( y, SB_HOST_GAME_ROW_H ))
				{
					int px = pillLeft;

					for ( size_t i = pline.first; i < pline.end; ++i )
					{
						const bool bOn = ( choices[i].id == pick.id );

						// [rc4l] The BASELINE is never locked, because it is not the thing the lock is
						// about. Vanilla adds no weapons, so it is exactly what a raised weapon speed
						// is compatible with -- greying it out said the whole axis was unavailable
						// when what is unavailable is replacing the weapons. It stays lit, and stays
						// pressable, which costs nothing: pressing what is already chosen does what
						// it always did.
						const bool bLocked = bAxisLocked && ( i > 0 );

						// [rc4l] An option wider than the whole column gets its own line from the
						// layout and is cut here, the same division of labour the file list uses.
						// Skulltag's "Team Last Man Standing" is the one that needs it.
						const int pw = MIN( pillWidths[i], pillRoom );

						// [rc4l] A locked axis registers NO rect, which is what actually makes it
						// unpressable: the hit test walks these, so a pill that never lands in the
						// list cannot be clicked, hovered or reached by the keyboard. One rule in one
						// place, rather than a bLocked test at each of the three.
						bool bHot = false;

						if ( !bLocked )
						{
							HostGameplayRow rec;
							rec.x = px;
							rec.w = pw;
							rec.y = y;
							rec.h = SB_HOST_GAME_ROW_H;
							rec.group = groups[g].id;
							rec.id = choices[i].id;
							g_HostGameRows.Push( rec );

							bHot = ( g_HostGameHot == static_cast<int>( g_HostGameRows.Size( ) - 1 ));
						}

						// [rc4l] Rounded, through the same DrawRoundedPanel every other soft-cornered
						// thing in this browser uses. A pill with square corners is a table cell, and
						// the shape is most of what says these are one-of-N rather than a list.
						//
						// The drawing itself is DrawGameplayPill, shared with the IWAD chooser so
						// that one is made of this control rather than of something resembling it.
						DrawGameplayPill( px, y - 1, pw, SB_HOST_GAME_ROW_H,
							choices[i].name.c_str( ), bOn, bHot, bLocked );

						// [rc4l] The glow sits on the pill that is ON, not at the head of the row. That
						// is where the answer is, and it is what makes left and right read as moving
						// the marker along the axis rather than as changing something elsewhere.
						if ( bAxisFocused && bOn )
							FocusAnchor( zx::BrowserFocus::Host, px - 5, y + SB_HOST_GAME_ROW_H / 2 );

						if ( !choices[i].summary.empty( ))
						{
							serverbrowser_Tip( px, y - 1, pw, SB_HOST_GAME_ROW_H,
								choices[i].summary.c_str( ));
						}

						px += pw + pillGap;
					}
				}

				// [rc4l] A gap BETWEEN wrapped lines as well as between the pills on one. Without it
				// the rounded ends of one line sat against the next and the block read as a slab
				// rather than as separate chips.
				y += SB_HOST_GAME_ROW_H + SB_HOST_PILL_VGAP;
			}

			y += 3;			// a gap between axes, so two blocks do not read as one long list
		}

		return y;
	}

	// [rc4l] What this experience can be played WITH, as settings rather than a modal.
	//
	// One block per AXIS. Axes with a single choice are skipped: nothing to decide is not a setting,
	// and a row that cannot change is a row spent saying nothing.
	int DrawHostGameplay( int x, int y, const zx::AddonEntry &addon )
	{
		const std::vector<zx::RemixGroup> groups = zx::GroupRemixes( HostOfferedRemixes( addon ));

		bool bAnything = HostLivesControl( addon ).adjustable || HostFastWeaponsOffered( addon ) ||
			HostTeamsControl( addon ).adjustable || ( HostSelectedRotation( ).size( ) > 1 );
		for ( size_t g = 0; g < groups.size( ); ++g )
			bAnything = bAnything || ( groups[g].choices.size( ) > 1 );

		if ( !bAnything )
			return y;		// the file list takes the room instead

		y += 4;
		if ( HostDetailRowVisible( y, 2 ))
			DrawSeparatorSpan( y, SB_HOST_RCOL_LEFT, SB_HOST_RCOL_RIGHT );
		y += 6;

		if ( HostDetailRowVisible( y, SB_HOST_LINE ))
		{
			screen->DrawText( SmallFont, CR_GOLD, x, y, "GAMEPLAY",
				DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
		}
		y += SB_HOST_LINE + 2;

		// [rc4l] ONE label column for every axis, measured across all of them so the controls line up
		// under each other. Sized to the widest label rather than fixed, or a longer setting name
		// later would either overlap its own control or leave a gap in front of every other.
		const int labelW = HostGameplayLabelW( groups );

		const zx::WeaponsPlan plan = HostWeaponsPlan( addon );

		// The mix leads, then the mode, then the sliders those two decide, then any other axis.
		// See DrawHostRemixAxes.
		y = DrawHostRemixAxes( x, y, labelW, groups, plan, HostAxisBand::Mix );
		y = DrawHostRemixAxes( x, y, labelW, groups, plan, HostAxisBand::Mode );
		y = DrawHostStartMap( x, y, labelW );
		y = DrawHostLives( x, y, labelW, addon );
		y = DrawHostFastWeapons( x, y, labelW, addon );
		y = DrawHostTeams( x, y, labelW, addon );
		y = DrawHostRemixAxes( x, y, labelW, groups, plan, HostAxisBand::Rest );

		return y;
	}

	// [rc4l] WHAT to run. The catalogue answers this; the fields beside it answer how.
	void DrawHostCatalogue( int x )
	{
		const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( );
		const std::vector<zx::HostListRow> rows = HostListRows( );
		const int selRow = HostSelectedRow( rows );

		for ( int row = SB_HOST_CATALOGUE_FIRST; row < static_cast<int>( rows.size( )); ++row )
		{
			const int rowY = HostCatalogueRowY( row );
			if ( !HostRowVisible( rowY, SB_HOST_ENTRY_H ))
				continue;

			const zx::HostListRow &r = rows[row];
			const zx::CatalogueEntry &entry = entries[r.entry];

			const bool bSel = ( row == selRow );
			const bool bHot = ( row == g_HostEntryHot );

			// [rc4l] The travelling marker, on the selected row -- the same one the server list puts
			// beside its own rows, in the same place relative to the text.
			//
			// The list never had it, because until the arrows could reach these rows there was no
			// keyboard position to mark. Anchoring it here rather than drawing it here is what makes
			// it LERP: DrawFocusTravel moves the one marker toward whatever asked for it this frame,
			// so crossing from a row to the button slides instead of jumping.
			if ( bSel && ( g_Focus == zx::BrowserFocus::Host ) && HostOnList( ))
			{
				FocusAnchor( zx::BrowserFocus::Host, SB_HOST_LIST_LEFT - 9,
					rowY + SB_HOST_ENTRY_H / 2 );
			}

			// [rc4l] The row being SERVED is tinted green, the same way CANCEL is tinted while a
			// download runs: a state the row is in, said in colour rather than in another word.
			// It survives the selection moving away, which is the whole point of showing it.
			//
			// The experience's own row AND the one way of playing the server was started as -- never
			// the others, because tinting all six would say the opposite of what the tint means. An
			// opened experience showing a green heading over six identical rows told you a server was
			// running and then refused to say which of them it was running.
			const bool bRunning = zx::HostIsActive( ) && ( r.entry == g_HostingEntry ) &&
				(( r.variant < 0 ) || ( g_HostingVariantId.IsNotEmpty( ) &&
					( g_HostingVariantId.Compare(
						entry.addon.variants[r.variant].id.c_str( )) == 0 )));

			// [rc4l] Which of the three things this row is, decided in one place. The SERVER LIST
			// has the same shape of problem in the row for the server you are connected to, and
			// used to answer it differently; computation/liverow_compute is now the single rule and
			// each list keeps its own colours and weights. See 3e08af3.
			const zx::RowPaint paint = zx::PaintListRow( bSel, bRunning, bHot );

			// The selected row gets a bar behind it rather than only a colour: on a dark panel a
			// colour change alone is easy to miss, and this is the one choice that decides what the
			// whole rest of the screen is about.
			//
			// The HOVER band is the SERVER LIST's, to the pixel: the same faint colour at the same
			// alpha. It used to recolour the label gold instead, which said the wrong thing twice
			// over. Gold is what a FOCUSED field wears elsewhere in this browser, so sweeping the
			// pointer down the list looked like the keyboard was following it; and a row that
			// changes colour under the pointer claims something happened, when hovering is only a
			// hint about what clicking would do.
			if ( paint.band != zx::RowBand::None )
			{
				PalEntry bar( 150, 170, 215 );
				float alpha = 0.06f;

				if ( paint.band == zx::RowBand::Live )
				{
					bar = PalEntry( 40, 96, 52 );
					alpha = bSel ? 0.55f : 0.4f;
				}
				else if ( paint.band == zx::RowBand::Selection )
				{
					bar = PalEntry( 60, 70, 96 );
					alpha = 0.55f;
				}

				screen->Dim( bar, alpha,
					serverbrowser_ToScreenX( x - 4 ),
					serverbrowser_ToScreenY( rowY - 1 ),
					serverbrowser_ToScreenX( SB_HOST_ROW_RIGHT ) -
						serverbrowser_ToScreenX( x - 4 ),
					serverbrowser_ToScreenY( rowY + SB_HOST_ENTRY_H - 1 ) -
						serverbrowser_ToScreenY( rowY - 1 ));
			}

			const bool bIsVariant = ( r.variant >= 0 );

			// Hover is deliberately not in here. What a row IS -- selected, being served, neither --
			// is all the label has to say.
			EColorRange col = CR_GRAY;

			if ( paint.label == zx::RowLabel::Selected )
				col = CR_WHITE;
			else if ( paint.label == zx::RowLabel::Live )
				col = CR_GREEN;

			// [rc4l] Cut to what is left after the badge, not to the column: "Team Last Man
			// Standing" is wider than the room before the PvP mark and ran straight under it. The
			// budget is measured against the thing it must not touch, so a longer badge or a longer
			// name cannot reintroduce the overlap.
			const int labelLeft = bIsVariant ? ( x + 12 ) : x;
			int labelRight = SB_HOST_ROW_RIGHT - 4;

			if ( bIsVariant )
			{
				labelRight -= SmallFont->StringWidth(
					zx::DescribeVariantKind( entry.addon.variants[r.variant].kind )) + 6;
			}
			else if ( !entry.addon.variants.empty( ))
			{
				labelRight -= SmallFont->StringWidth( ">" ) + 6;
			}
			else
			{
				labelRight -= SmallFont->StringWidth(
					zx::DescribeVariantKind( entry.addon.kind )) + 6;
			}

			const FString label = serverbrowser_FitName( bIsVariant
				? entry.addon.variants[r.variant].name.c_str( )
				: entry.addon.name.c_str( ), labelRight - labelLeft );

			// [rc4l] Centred in the row rather than drawn at its top edge. The highlight bar is
			// SB_HOST_ENTRY_H tall and the glyphs are shorter, so drawing at rowY sat the text high
			// inside its own bar.
			const int textY = rowY + ( SB_HOST_ENTRY_H - SmallFont->GetHeight( )) / 2;

			// [rc4l] A curated experience is drawn in TWO colours: its leading word green, the rest
			// white. That marks the group these entries belong to without recolouring a whole row,
			// which is how the browser says something is selected or being served.
			//
			// It beats Selected, which is why this is not folded into `col` above. Selected paints
			// the label white, so an accented row that happened to be under the cursor came out
			// looking like every other row -- and the cursor starts on the first row, so the top
			// entry was never marked at all.
			//
			// Live still wins outright: a row you are being served is a fact about right now, and it
			// matters more than what group the entry is in.
			//
			// The split is taken AFTER the fit. Fitting the two halves separately would measure each
			// against the whole budget and let the pair overflow the room the row actually has.
			const bool bAccent = ( !bIsVariant && entry.addon.accent &&
				( paint.label != zx::RowLabel::Live ));

			if ( bAccent )
			{
				const char *const space = strchr( label.GetChars( ), ' ' );
				const size_t split = ( space != NULL )
					? static_cast<size_t>( space - label.GetChars( )) : label.Len( );

				const FString head = label.Left( split );
				const FString tail = label.Mid( split );

				screen->DrawText( SmallFont, CR_GREEN, labelLeft, textY, head,
					DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );

				if ( tail.IsNotEmpty( ))
				{
					screen->DrawText( SmallFont, CR_WHITE,
						labelLeft + SmallFont->StringWidth( head ), textY, tail,
						DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
				}
			}
			else
			{
				// A way of playing is indented, so it reads as belonging to the experience above it
				// rather than as another experience.
				screen->DrawText( SmallFont, col, labelLeft, textY, label,
					DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
			}

			// PvE or PvP. An experience that offers several ways to play has no single answer to
			// give, so the badge belongs on each way rather than on the row above them; one that
			// offers a single way does have one, and hiding it there taught the reader that the
			// mark means "has variants" when it means what the row plays like.
			//
			// The parser requires `kind` of whichever thing is actually the experience, so exactly
			// one of these two branches always has a real answer to draw.
			if ( bIsVariant || entry.addon.variants.empty( ))
			{
				const zx::VariantKind kind = bIsVariant
					? entry.addon.variants[r.variant].kind : entry.addon.kind;
				const char *kindText = zx::DescribeVariantKind( kind );

				screen->DrawText( SmallFont, ( kind == zx::VariantKind::PvE ) ? CR_GREEN : CR_ORANGE,
					SB_HOST_ROW_RIGHT - SmallFont->StringWidth( kindText ) - 4, textY, kindText,
					DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );

				// Only a way of playing carries one. An experience says what it is in its summary,
				// which the detail panel beside this already shows in full.
				if ( bIsVariant && !entry.addon.variants[r.variant].tooltip.empty( ))
				{
					serverbrowser_Tip( x - 4, rowY - 1, SB_HOST_ROW_RIGHT - x + 4, SB_HOST_ENTRY_H,
						entry.addon.variants[r.variant].tooltip.c_str( ));
				}
			}
			else if ( !entry.addon.variants.empty( ))
			{
				// [rc4l] The caret, on the RIGHT of the row: it is about this row's own state rather
				// than a step to the side, and putting it at the left would read as an indent that
				// the rows under it then repeat.
				const bool bOpen = HostEntryIsOpen( r.entry );
				const char *caret = bOpen ? "v" : ">";

				// Grey on every experience that has one, whatever the row's own state. Taking it out
				// of the label's colour keeps it quiet next to a marked name, which is what it is:
				// the shape of the row, not something to look at.
				screen->DrawText( SmallFont, CR_GRAY,
					SB_HOST_ROW_RIGHT - SmallFont->StringWidth( caret ) - 4, textY, caret,
					DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
			}

			// No file count here on purpose: the detail panel beside this already says the files and
			// the IWAD, and a narrow list repeating it crowded itself for no new information.
		}
	}

	// [rc4l] COPY TO NEW, under the visibility row.
	//
	// Inside the settings' clip and the settings' scroll, because it IS one of them as far as the
	// panel is concerned -- it scrolls with the fields above it and it is gone with them when the
	// face changes. Which is why it is not on the foot row beside PLAY NOW: the foot is what this
	// screen DOES, and copying is leaving it.
	void DrawHostCopyButton( )
	{
		if ( !HostCopyOffered( ))
			return;

		const int y = HostCopyY( );
		if ( !HostRowVisible( y, HostCopyH( )))
			return;

		const int x = SB_HOST_RCOL_LEFT;
		const int w = SB_HOST_RCOL_RIGHT - x;
		const bool bOn = ( g_HostFocus.slot == zx::HostSlot::Copy );

		DrawRoundedButton( x, y, w, HostCopyH( ), "COPY TO NEW", bOn || g_HostCopyHot );

		if ( bOn && ( g_Focus == zx::BrowserFocus::Host ))
			FocusAnchor( zx::BrowserFocus::Host, x - 5, y + HostCopyH( ) / 2 );

		// Only while it is really in view: a tip for a button scrolled out of the viewport is the
		// invisible-but-hoverable half of the same bug the hit test avoids.
		if ( HostRowFullyVisible( y, HostCopyH( )))
		{
			serverbrowser_Tip( x, y, w, HostCopyH( ),
				"Open these files and settings on the NEW tab to change them" );
		}
	}

	void DrawHostVisibility( int x, int y )
	{
		if ( HostRowFullyVisible( y - SB_HOST_LINE, SB_HOST_LINE ))
		{
			screen->DrawText( SmallFont, CR_DARKGRAY, x, y - SB_HOST_LINE, "VISIBILITY",
				DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
		}

		const int rowX = x;
		const int rowW = SB_HOST_RCOL_RIGHT - x;

		static const char *const labels[kHostVisCount] = {
			"Internet", "Home",
		};

		// [rc4l] INTERNET says whether it will actually work, before anyone commits to it.
		//
		// Green when the registry has reached this port from outside, red when the check ran and it
		// could not, and plain white when we do not know -- untested, still running, or the check
		// itself failed. That last case is white and not red on purpose: a registry that never
		// answered has told us about our own service, and painting the player's router as shut on
		// that basis would be blaming them for our outage.
		//
		// Deliberately NOT disabled even when red: the check can be wrong in the player's favour, and
		// a form that refuses to let someone try their own network is worse than one that warns them.
		// [rc4l] Ask about the port the server actually holds, not the one in the form.
		//
		// A server takes the next free port when the one you set is busy, and the check is a question
		// about ONE port. Asking about the configured one while running elsewhere is how a forwarded
		// 10666 reported "reachable" with the server on 10670: true of the port tested, useless to
		// the player, and it reads as "hosting is fine" while nobody outside can get in.
		//
		// There is usually no cached answer for the port it moved to, so this shows unknown rather
		// than green. That is the honest state, and an honest unknown beats a confident wrong.
		const int runningPort = HostRunningPort( );
		const int configuredPort = HostConfiguredPort( );

		const zx::ProbePhase reach = zx::ReachProbeStatus(
			zx::PortToCheck( runningPort, configuredPort ));

		// The drift itself is announced where it happens, at the child's ready line in zx_hosting.cpp.
		// Saying it from a draw would mean saying it every frame, and this panel is not even on screen
		// while a server runs.

		EColorRange visColors[kHostVisCount];

		switch ( zx::ProbeDisplayFor( reach ))
		{
		case zx::ProbeDisplay::Reachable:	visColors[kHostVisGlobal] = CR_GREEN; break;
		case zx::ProbeDisplay::Unreachable:	visColors[kHostVisGlobal] = CR_DARKRED; break;
		default:							visColors[kHostVisGlobal] = CR_WHITE; break;
		}

		visColors[kHostVisLocal] = ( g_HostAdvertise == false ) ? CR_WHITE : CR_GRAY;

		DrawChoiceRow( rowX, y, rowW, kHostVisCount, labels,
			g_HostAdvertise ? kHostVisGlobal : kHostVisLocal,
			g_HostVisHot,
			(( g_Focus == zx::BrowserFocus::Host ) && HostOnVisibility( )) ? g_HostVisCursor : -1,
			visColors );

		// [rc4l] The glow goes to the SELECTED CELL, not to the label.
		//
		// Anchoring it to the label was inherited from the fields above, where it is right because a
		// field has one focused thing. A choice row has two, and left and right move between them --
		// so a glow that stayed put gave no feedback at all for the one key that does anything here,
		// leaving the player to read the markers to find out what they had just changed.
		if ( HostOnVisibility( ) && ( g_Focus == zx::BrowserFocus::Host ))
		{
			// The CURSOR, not the answer. They were the same thing while the arrows decided as they
			// moved; now that they do not, the marker has to follow the key rather than the value,
			// or moving along the row would look like nothing had happened.
			const zx::ChoiceCell at = zx::ChoiceCellAt( g_HostVisCursor, kHostVisCount, rowX, rowW,
				SB_CHOICE_GAP );

			if ( at.valid )
				FocusAnchor( zx::BrowserFocus::Host, at.x - 5, y + SB_CHOICE_H / 2 );
		}

		// One tip per cell rather than one for the row: the two answers have different consequences,
		// and a single tip would have to describe both or neither.
		for ( int i = 0; i < kHostVisCount; ++i )
		{
			const zx::ChoiceCell cell = zx::ChoiceCellAt( i, kHostVisCount, rowX, rowW, SB_CHOICE_GAP );
			if ( !cell.valid )
				continue;

			if ( i == kHostVisGlobal )
			{
				// [rc4l] One line, and it says which of the four states this is -- the colour alone
				// cannot tell "we have not asked yet" from "we asked and the answer was no", and
				// those lead to different actions.
				const char *pszWhy =
					( reach == zx::ProbePhase::Reachable ) ? "Your port is open"
					: ( reach == zx::ProbePhase::Unreachable ) ? "Nothing outside reached this port. Forward it, or host locally"
					: ( reach == zx::ProbePhase::Failed ) ? "Untested, but it may still work"
					: "Checking the port...";

				FString tip;
				tip << "Listed publicly so anyone can join\n" << pszWhy;
				serverbrowser_Tip( cell.x, y, cell.width, SB_CHOICE_H, tip );
			}
			else
			{
				serverbrowser_Tip( cell.x, y, cell.width, SB_CHOICE_H,
					"Not listed anywhere\nPlayers on your own network find it automatically" );
			}
		}
	}

	//*************************************************************************
	//
	// [rc4l] What our server is doing, once there is one. Replaces the form rather than sitting under
	// it: while a server is running, the fields describe something that has already happened.
	// [rc4l] Returns the y it finished at, so the caller can measure how tall it turned out. The text
	// wraps, so the height is not something the layout can work out without drawing it.
	int DrawHostStatus( int x, int y, zx::HostState state )
	{
		// [rc4l] GREEN once it is up. The heading is the one line on this panel that is unambiguously
		// good news, and it was gold -- the same colour the panel uses for "still waiting", so the
		// state everyone is looking for wore the colour of the state before it.
		if ( HostTextRowVisible( y, SB_HOST_LINE ))
		{
			const EColorRange headColour = ( state == zx::HostState::Running ) ? CR_GREEN : CR_GOLD;

			screen->DrawText( SmallFont, headColour, x, y, zx::HostStateSummary( state ),
				DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
		}
		y += SB_HOST_LINE + 6;

		// Wrapped to the COLUMN now, not to the panel: the status shares the right column with the
		// details above it rather than owning the full width.
		const int wrapW = SB_HOST_RCOL_RIGHT - x;

		if ( state == zx::HostState::Failed )
		{
			y = DrawWrappedIn( zx::HostReason( ), x, y, wrapW, CR_WHITE );
		}
		else
		{
			// [rc4l] The server's own name and loopback address used to be printed here. Both are gone:
			// 127.0.0.1 is the address for the one person who cannot need it, and putting an address on
			// screen invites it to be shared, when the thing worth sharing is not this one.
			if ( state == zx::HostState::Running )
			{
				y = DrawHostReach( x, y, wrapW );

				// [rc4l] SAY IT WHERE IT SURVIVES.
				//
				// This was already reported when the child announced its port, and that message is
				// destroyed seconds later: joining restarts the engine, and the fresh console begins
				// at V_INIT with the warning gone. We told the player something they could never read.
				//
				// A port nobody forwarded is the likeliest reason a working setup stops working, it
				// cannot be seen from inside the game, and only the player can fix it. So it belongs
				// on the panel that stays up for as long as the server does, not in a log that a
				// restart eats.
				if ( zx::PortDriftNeedsWarning( HostRunningPort( ), HostConfiguredPort( )))
				{
					FString drift;
					drift.Format( "Port %d was busy, so this server is on %d. If you forwarded %d, "
						"players outside your network cannot reach it.",
						HostConfiguredPort( ), HostRunningPort( ), HostConfiguredPort( ));

					y += 4;
					y = DrawWrappedIn( drift.GetChars( ), x, y, wrapW, CR_ORANGE );
				}

				y += 4;
				y = DrawWrappedIn( "You are the administrator of this server. Use the console, "
					"rcon <command>, to run anything on it.", x, y, wrapW, CR_DARKGRAY );
			}
		}

		// The buttons belong to the panel rather than to this text, so DrawHostFootButtons draws them:
		// they sit at the panel's foot, not under whatever this happened to write last.
		return y;
	}

	//*************************************************************************
	//
	// [rc4l] Which of the four reachability states we are in.
	int HostStatusNow( )
	{
		switch ( zx::HostReachability( ))
		{
		case zx::HostReach::NotPublic:		return static_cast<int>( zx::HostStatus::LanOnly );
		case zx::HostReach::Reachable:		return static_cast<int>( zx::HostStatus::Open );
		case zx::HostReach::Unreachable:	return static_cast<int>( zx::HostStatus::NoReply );
		default:							return static_cast<int>( zx::HostStatus::Checking );
		}
	}

	// [rc4l] Whether the outside world can reach this server -- ONE LINE, and a hover for the rest.
	//
	// This was five wrapped lines: the verdict, what the router said, that the server still works
	// locally, and which port to forward on which protocols. All of it true, all of it in a region
	// that also has to carry what the server is and how to administer it, for a question with four
	// possible answers. The useful part ended up the smallest thing on screen.
	//
	// The code is the line; the paragraph is the tooltip. Same trade the registry bars make, and for
	// the same reason: a state that is usually fine does not deserve permanent prose.
	int DrawHostReach( int x, int y, int width )
	{
		const zx::HostStatus status = static_cast<zx::HostStatus>( HostStatusNow( ));

		const char *const router = zx::PortMapStatusText( );
		const std::string tip = zx::HostStatusTooltip( status,
			zx::HostCurrentConfig( ).port,
			(( router != NULL ) && ( router[0] != 0 )) ? std::string( router ) : std::string( ));

		EColorRange colour = CR_GOLD;
		switch ( zx::HostToneFor( status ))
		{
		case zx::HostTone::Good:	colour = CR_GREEN; break;
		case zx::HostTone::Bad:		colour = CR_ORANGE; break;
		case zx::HostTone::Info:	colour = CR_DARKGRAY; break;
		default:					colour = CR_GOLD; break;
		}

		const char *const code = zx::HostStatusCode( status );

		if ( HostTextRowVisible( y, SB_HOST_LINE ))
		{
			screen->DrawText( SmallFont, colour, x, y, code,
				DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );

			// Only while the row is actually on screen. A tooltip for a line the mask is hiding is a
			// hover target over something the player cannot see.
			serverbrowser_Tip( x, y, SmallFont->StringWidth( code ), SB_HOST_LINE, tip.c_str( ));
		}

		return y + SB_HOST_LINE;
	}

	// Wrapped text inside an arbitrary width, returning the y below it. DrawWrapped is fixed to the
	// detail panel's column; this one takes the width because the hosting panel is a different shape.
	// [rc4l] Whether a line at `vy` may be drawn at all.
	//
	// PushClip only feeds DimClipped; it does NOT clip screen->DrawText, which is why every scrolling
	// region here masks itself by SKIPPING rows rather than by setting a rectangle. The status half
	// had a PushClip around it and nothing else, so its text scrolled straight up over the seam and
	// out of the panel.
	bool HostTextRowVisible( int vy, int vh )
	{
		if ( g_HostTextClipBottom <= g_HostTextClipTop )
			return true;			// no region set: draw normally, which is every other caller

		return zx::RowFullyInView( vy, vh, g_HostTextClipTop, g_HostTextClipBottom );
	}

	int DrawWrappedIn( const FString &text, int x, int y, int width, EColorRange colour )
	{
		FString line;
		long start = 0;

		while ( start <= static_cast<long>( text.Len( )))
		{
			long space = text.IndexOf( " ", start );
			const bool last = ( space < 0 );
			if ( last )
				space = static_cast<long>( text.Len( ));

			const FString word = text.Mid( start, space - start );
			const FString candidate = line.IsEmpty( ) ? word : ( line + " " + word );

			if ( line.IsNotEmpty( ) && ( SmallFont->StringWidth( candidate ) > width ))
			{
				if ( HostTextRowVisible( y, SB_HOST_LINE ))
				{
					screen->DrawText( SmallFont, colour, x, y, line,
						DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
				}
				y += SB_HOST_LINE;
				line = word;
			}
			else
				line = candidate;

			if ( last )
				break;
			start = space + 1;
		}

		// [rc4l] The leftover, and it needs the SAME gate the loop above uses.
		//
		// It did not have one, so the mask worked on every line of a paragraph except its last. That
		// is a strange enough shape to be worth naming: the top of the region masked correctly, whole
		// paragraphs masked correctly, and then one trailing line per paragraph drew straight through
		// the bottom edge and over the STOP SERVER button.
		if ( line.IsNotEmpty( ))
		{
			if ( HostTextRowVisible( y, SB_HOST_LINE ))
			{
				screen->DrawText( SmallFont, colour, x, y, line,
					DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
			}
			y += SB_HOST_LINE;
		}

		return y;
	}

	void DrawSearchBox( )
	{
		serverbrowser_Tip( SB_SEARCH_LEFT, SB_SEARCH_TOP, SB_SEARCH_W, SB_SEARCH_H,
			"Filter the list by name\nUpper and lower case are the same" );

		FocusAnchor( zx::BrowserFocus::Search, SB_SEARCH_LEFT - 5, SB_SEARCH_TOP + SB_SEARCH_H / 2 );

		// [rc4l] Through the shared drawer, which this one used to BE. Everything it does -- the
		// caret, the blink, the selection band, the scroll that keeps the caret in view -- is now
		// had by every other field for free, which is the point: the second field on this menu was
		// drawn by hand and silently had none of it.
		DrawTextField( SB_SEARCH_LEFT, SB_SEARCH_TOP, SB_SEARCH_W, SB_SEARCH_H, g_Search,
			( g_Focus == zx::BrowserFocus::Search ), g_SearchHot, "Search", false,
			g_SearchFirstChar );
	}

	//*************************************************************************
	//
	// [rc4l] A TEXT FIELD, whole: the box, the text scrolled to the caret, the selection band and
	// the blinking caret itself.
	//
	// Split out of the server search because the second field on this menu came out looking right
	// and behaving wrong. It was drawn by hand -- a box and a string -- so it had no caret, nothing
	// blinked, and a selection was invisible. None of that is decoration: a field with no caret does
	// not look focused, and one that cannot show a selection cannot show what backspace is about to
	// take.
	//
	// So there is one field drawer now, and any box added after this gets all of it for free.
	// `firstChar` is returned so the caller's hit test can map a click back to a character through
	// the same scroll the draw used.
	void DrawTextField( int vx, int vy, int vw, int vh, const zx::TextInput &field, bool bFocused,
		bool bHot, const char *prompt, bool bMasked, int &firstChar )
	{
		const int left = serverbrowser_ToScreenX( vx );
		const int right = serverbrowser_ToScreenX( vx + vw );
		const int top = serverbrowser_ToScreenY( vy );
		const int bottom = serverbrowser_ToScreenY( vy + vh );

		const int w = right - left;
		const int h = bottom - top;
		if (( w <= 0 ) || ( h <= 0 ))
			return;

		// Lighter when it has the keyboard, the same lift the tabs and the buttons use for the same
		// reason: "what would a key do right now" should be answerable by looking.
		const int base = bFocused ? 30 : ( bHot ? 22 : 16 );
		const zx::PanelColor topCol = { static_cast<BYTE>( base ), static_cast<BYTE>( base ),
			static_cast<BYTE>( base + 10 ), 225 };
		const zx::PanelColor botCol = { static_cast<BYTE>( base + 18 ), static_cast<BYTE>( base + 18 ),
			static_cast<BYTE>( base + 30 ), 210 };

		for ( int row = 0; row < h; ++row )
		{
			const int inset = zx::ComputeRoundedInset( row, h, h / 3 );
			const int rowW = w - 2 * inset;
			if ( rowW <= 0 )
				continue;

			const zx::PanelColor c = zx::ComputePanelGradient( row, h, topCol, botCol );
			screen->Dim( PalEntry( c.r, c.g, c.b ), c.a / 255.f, left + inset, top + row, rowW, 1 );
		}

		const int textY = vy + ( vh - SmallFont->GetHeight( )) / 2 + 1;
		const int textX = vx + SB_SEARCH_PAD;
		const int textW = vw - 2 * SB_SEARCH_PAD;

		firstChar = 0;

		if ( field.text.empty( ) && !bFocused )
		{
			// A prompt rather than a blank box: an empty rounded rectangle says nothing about what
			// it is for.
			if ( prompt != NULL )
			{
				screen->DrawText( SmallFont, CR_DARKGRAY, textX, textY, prompt,
					DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true,
					TAG_DONE );
			}
			return;
		}

		// A password is read over shoulders.
		FString full = field.text.c_str( );
		if ( bMasked )
		{
			FString dots;
			for ( size_t i = 0; i < field.text.size( ); ++i )
				dots += '*';
			full = dots;
		}

		// Scrolled to keep the CARET visible rather than the start of the string: once the text is
		// longer than the box, what matters is the end you are typing at.
		FString shown = full;
		int first = 0;
		while (( shown.Len( ) > 0 ) && ( SmallFont->StringWidth( shown ) > textW ))
		{
			shown = shown.Mid( 1 );
			++first;
		}
		firstChar = first;

		int caretChars = static_cast<int>( field.caret ) - first;
		if ( caretChars < 0 )
			caretChars = 0;

		// The selection, under the text: a band behind the characters rather than an inversion of
		// them, so the letters keep the colour they had and stay readable either way.
		if ( zx::HasSelection( field ))
		{
			int from = static_cast<int>( zx::SelectionStart( field )) - first;
			int to = static_cast<int>( zx::SelectionEnd( field )) - first;
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
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );

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
				lines[i].Text, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
			y += SmallFont->GetHeight( ) + 2;
		}
		V_FreeBrokenLines( lines );

		y += 6;
		const char *const dismiss = "press a key";
		screen->DrawText( SmallFont, CR_DARKGRAY,
			(( SB_PANEL_LEFT + SB_DETAIL_RIGHT ) / 2 ) - ( SmallFont->StringWidth( dismiss ) / 2 ), y,
			dismiss, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
	}

	//*************************************************************************
	//
	// [rc4l] Both answers, in one place, because the important part is that each releases the hold
	// exactly once. `stop` false is "keep going", which also covers the download having finished while
	// the question was on screen -- ReleaseJoinResume then runs the join it was holding.
	// [rc4l] Switching tabs resets the selection and the scroll: the row that was highlighted belongs
	// to a list that is no longer on screen, and carrying its INDEX across would highlight whatever
	// unrelated server happened to land in that position.
	// [rc4l] Each LIST keeps its own place, which is the sub-tab now rather than the tab. Coming back
	// and finding it scrolled to the top again means losing your spot every time you glance at the
	// other one, and the row INDEX cannot simply be carried across, because it points into a list that
	// is no longer there and would land on an unrelated server.
	void SelectSubTab( BrowseKind kind )
	{
		if ( g_Browse == kind )
			return;

		const int leaving = static_cast<int>( g_Browse );
		g_TabScroll[leaving] = g_ScrollFirst;
		g_TabSelected[leaving] = g_Selected;

		g_Browse = kind;

		const int entering = static_cast<int>( kind );
		g_ScrollFirst = g_TabScroll[entering];
		g_Selected = g_TabSelected[entering];

		// Rebuild first, then let the clamp in DrawRows deal with a remembered position that no
		// longer fits: servers come and go between visits, so the spot we saved may be past the end.
		serverbrowser_RebuildList( );
		S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
	}

	// [rc4l] The same for the hosting row. Nothing to save or restore between the two: PRESETS keeps
	// its own scroll and selection in the host form, which this does not touch.
	void SelectHostKind( HostKind kind )
	{
		if ( g_HostKind == kind )
			return;

		g_HostKind = kind;

		// The form goes away with PRESETS, so the keyboard cannot stay in it. Same rule the search
		// box gets when its row leaves: focus comes back to the thing that is still on screen.
		if (( kind != HostKind::Presets ) && ( g_Focus == zx::BrowserFocus::Host ))
			SetFocus( zx::BrowserFocus::SubTabs );

		S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
	}

	// Whichever row is showing, told to go to one of its pills.
	void SelectSubTabIndex( int i )
	{
		if (( i < 0 ) || ( i >= SubTabCount( )))
			return;

		if ( g_Tab == BrowserTab::Browse )
			SelectSubTab( static_cast<BrowseKind>( i ));
		else
			SelectHostKind( static_cast<HostKind>( i ));
	}

	void SelectTab( BrowserTab tab )
	{
		if ( g_Tab == tab )
			return;

		// [rc4l] Leaving BROWSE takes the search box with it, so a caret focused there has to come up
		// to the tabs before it goes: a caret blinking in a box that is no longer on screen is a lie
		// about where the next keystroke lands. ComputeNav cannot fix this after the fact -- it is
		// told what exists NOW, and by then the focus is already pointing at nothing.
		//
		// The SUB-TABS no longer need this. Both tabs have a second row, so focus on it stays valid
		// across the change and lands on the row the new tab brought with it.
		if (( tab != BrowserTab::Browse ) && ( g_Focus == zx::BrowserFocus::Search ))
			SetFocus( zx::BrowserFocus::SubTabs );

		g_Tab = tab;

		// Arriving at the hosting tab: fill the form from what was used last time, and put the
		// keyboard on its first field rather than on a list that is not there.
		if ( tab == BrowserTab::Host )
		{
			LoadHostForm( );
			g_HostFocus = zx::HostFocusPos( zx::HostSlot::List, 0 );

			// Unless the sub-tab showing has no form, in which case there is no first field to land
			// on and the keyboard belongs on the row that IS drawn.
			if ( g_HostKind != HostKind::Presets )
				SetFocus( zx::BrowserFocus::SubTabs );
		}

		// The list belongs to BROWSE, so arriving there rebuilds it for whichever sub-tab was last
		// chosen. Leaving does not need to save anything: the sub-tab owns that now, and it has not
		// changed underneath us.
		if ( tab == BrowserTab::Browse )
			serverbrowser_RebuildList( );

		S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
	}

	// [rc4l] Committing to the selected server. One implementation, reached from the keyboard and from
	// the button, so the two can never come to mean different things.
	// [rc4l] The port out of an "ip:port" string. Taken from the text rather than from usPort because
	// that field is in network byte order and this file has no business knowing that.
	int PortOfAddress( const FString &address )
	{
		const long colon = address.LastIndexOf( ":" );
		if ( colon < 0 )
			return 0;

		return atoi( address.Mid( colon + 1, address.Len( )).GetChars( ));
	}

	// The port our running server actually holds, taken from the address we join it on rather than
	// from the form field beside it: the field is editable while the server runs, so it can be
	// describing a server that does not exist yet. 0 when we hold nothing.
	int HostRunningPort( )
	{
		return PortOfAddress( zx::HostConnectAddress( ));
	}

	// Whether a row in the list is the server WE are running. See RowIsOwnServer for why the address
	// on the row is not enough on its own.
	// [rc4l] Is this row the server we are PLAYING ON right now? Drives the green name.
	//
	// Two ways to be, and the second is the one a plain address compare misses. Somebody else's
	// server matches on the address we are connected to. Our OWN server does not: we join it on
	// 127.0.0.1 while the browser knows it by its LAN address and its public one, so the row we are
	// sitting in never equals the address in hand -- the same trap that made JOIN offer to stop our
	// own server in order to travel to it.
	bool RowIsTheServerWeAreOn( int lServer )
	{
		if ( NETWORK_GetState( ) != NETSTATE_CLIENT )
			return false;

		const NETADDRESS_s connected = CLIENT_GetServerAddress( );

		if ( BROWSER_GetAddress( lServer ).Compare( connected ))
			return true;

		// Our own server, reached through the loopback address that no row carries.
		return zx::HostOwnsAddress( FString( connected.ToString( ))) && RowIsOurOwnServer( lServer );
	}

	bool RowIsOurOwnServer( int lServer )
	{
		const NETADDRESS_s address = BROWSER_GetAddress( lServer );
		const FString full = address.ToString( );

		// [rc4l] Resolved at most once per frame, not once per ROW per frame.
		//
		// This runs from the row drawing, and NETWORK_GetLocalAddress is not a cheap accessor: on
		// macOS it walks every network interface with getifaddrs. Calling it for every visible row
		// on every frame was a syscall sweep inside the render loop, and it is what turned an
		// occasional discovery message into a console filling up forever.
		static FString s_localIp;
		static int s_localIpFrame = -1;
		if ( s_localIpFrame != static_cast<int>( gametic ))
		{
			s_localIpFrame = static_cast<int>( gametic );
			s_localIp = "";
			if ( NETWORK_GetState( ) != NETSTATE_SINGLE )
				s_localIp = NETWORK_GetLocalAddress( ).ToStringNoPort( );
		}

		return zx::RowIsOwnServer( address.ToStringNoPort( ), PortOfAddress( full ),
			HostRunningPort( ), s_localIp.GetChars( ), zx::ReachProbePublicIp( ));
	}

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
			// The player asked to JOIN; the download is only how that is being carried out. Asking about
			// the join is asking about the thing they chose. What has already arrived is kept either
			// way, which is the one fact that changes the answer.
			ShowDialog( DialogAction::CancelDownload, "Cancel join?",
				"Any download in progress stops. What has arrived so far is kept.",
				"YES", 'y', "NO", 'n' );
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
			return;
		}

		// [rc4l] Where the player is now, and what going somewhere else would cost them. See
		// joinintent_compute.h -- the case that matters is a host pressing JOIN on their own row.
		{
			const int total = static_cast<int>( g_SortedServers.Size( ));
			bool bTargetIsCurrent = false;

			const bool bConnected = ( NETWORK_GetState( ) == NETSTATE_CLIENT );

			if ( bConnected && ( g_Selected >= 0 ) && ( g_Selected < total ))
			{
				bTargetIsCurrent = BROWSER_GetAddress( g_SortedServers[g_Selected] )
					.Compare( CLIENT_GetServerAddress( ));
			}

			const zx::HostState hostState = zx::HostCurrentState( );
			const bool bHoldsServer = (( hostState == zx::HostState::Starting )
				|| ( hostState == zx::HostState::Running )
				|| ( hostState == zx::HostState::Stopping ));

			// [rc4l] The wider "is this row OURS", because the narrow test above never says yes about
			// our own server. We connect to it on 127.0.0.1, LAN discovery lists it on this machine's
			// local address and the registry lists it on our public one, so the browser shows two rows
			// and neither matches what we are connected on. JOIN therefore read our own server as
			// somewhere else and offered to stop it in order to go there.
			const bool bTargetIsOwn = bHoldsServer && ( g_Selected >= 0 ) && ( g_Selected < total )
				&& RowIsOurOwnServer( g_SortedServers[g_Selected] );

			// And whether the connection we are holding is to that server, which is what separates
			// "you are already here" from "then go there".
			const bool bOnOwnServer = bConnected
				&& zx::HostOwnsAddress( FString( CLIENT_GetServerAddress( ).ToString( )));

			switch ( zx::DecideJoinIntent( bHoldsServer, bConnected, bTargetIsCurrent,
				bTargetIsOwn, bOnOwnServer ))
			{
			case zx::JoinIntent::AlreadyThere:
				// Just leave. No sound of a decision being made, because none was.
				M_ClearMenus( );
				return;

			case zx::JoinIntent::RejoinOwnServer:
				// Our server, and we are not in it. Connect on the address we know works rather than
				// on whichever of its several spellings the row happened to be listed under, and stop
				// nothing on the way.
				JoinOwnServer( );
				return;

			case zx::JoinIntent::ConfirmStopHosting:
				ShowDialog( DialogAction::StopHostingAndJoin, "Stop your server?",
					"Joining another server closes the one you are running. Anyone playing on it will "
					"be disconnected.", "JOIN", 'j', "CANCEL", 'c' );
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
				return;

			case zx::JoinIntent::Join:
				break;
			}
		}

		// [rc4l] A protected server wants the password BEFORE anything is downloaded.
		//
		// Asking afterwards is the arrangement nobody would choose deliberately: the player waits out
		// a transfer that may be minutes long, and only then finds out they cannot get in. The
		// password is also the cheapest thing to check -- it costs one prompt against a download that
		// costs a connection and a disk.
		const int total = static_cast<int>( g_SortedServers.Size( ));
		if (( g_Selected >= 0 ) && ( g_Selected < total ))
		{
			const int lServer = g_SortedServers[g_Selected];
			if ( BROWSER_IsPasswordProtected( lServer ))
			{
				// No server name in the body. The row is selected, its name is in the detail panel behind
			// this, and repeating it here only pushes the field further from the question.
			ShowDialog( DialogAction::JoinPassword, "This server needs a password", NULL,
					"JOIN", 0, "CANCEL", 0, true, "Password", true );
				return;
			}
		}

		DoJoinSelected( );
	}

	void AnswerCancelConfirm( bool stop )
	{
		if ( !g_Dialog.open )
			return;

		CloseDialog( );

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
			? "Cancel joining this server\nYou will be asked to confirm"
			: "Join this server\nAnything missing is downloaded first" );

		FocusAnchor( zx::BrowserFocus::Action, SB_BUTTON_LEFT - 5, SB_BUTTON_TOP + SB_BUTTON_H / 2 );

		const char *const label = bCancel ? "CANCEL" : "JOIN";
		const int textY = SB_BUTTON_TOP + ( SB_BUTTON_H - SmallFont->GetHeight( )) / 2 + 1;
		screen->DrawText( SmallFont, bCancel ? CR_ORANGE : CR_WHITE,
			( SB_BUTTON_LEFT + SB_BUTTON_RIGHT ) / 2 - ( SmallFont->StringWidth( label ) / 2 ),
			textY, label, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
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
			DrawWrapped( "The download is still running. Stop it, or let it finish and use the file.",
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
		// name with an ellipsis tells you less than two short lines do. Plain, like every other name
		// the browser draws: the heading is where a name from a palette mod would be biggest and most
		// likely to land in an unreadable colour.
		const FString title = serverbrowser_PlainName( BROWSER_GetHostName( lServer ));
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
		y = DrawWadList( x, y );

		// [rc4l] Under the files, behind its own rule. Two lists stacked with nothing between them
		// would read as one list that changed its mind about what it was listing halfway down.
		//
		// Only when there is honestly room for it: the files answer whether you CAN join and the names
		// only answer whether you want to, so on a panel already full of WADs the names are what gives
		// way rather than what pushes the JOIN button off the bottom.
		if (( y + 6 + SB_DETAIL_LINE ) <= SB_DETAIL_TEXT_BOTTOM )
		{
			y += 3;
			DrawSeparator( y );
			y += 6;
			DrawPlayerList( x, y, lServer );
		}
		else
		{
			// Nothing was drawn, so nothing may be scrolled -- otherwise a wheel notch over the space
			// where the list would have been moves an invisible one.
			g_PlayerListTop = g_PlayerListBottom = g_PlayerListRows = 0;
		}

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
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true,
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
	// [rc4l] Who is actually in there, under the files.
	//
	// The list already says 7/8, and that number is the question "is anyone playing" answered without
	// saying who -- which is the part that decides whether you join a server with your friends on it
	// or a server with seven strangers. Names are also the only place a bot-stuffed server gives
	// itself away by inspection rather than by arithmetic.
	//
	// SPECTATORS AND BOTS ARE SHOWN, dimmed and labelled, rather than filtered out. Hiding them would
	// make the list disagree with the count beside it -- and a name list that is shorter than the
	// number above it for reasons it does not explain is worse than one that explains itself.
	//
	// Frags are omitted for anyone not playing: a spectator's frag count is whatever it was when they
	// stopped, and a bot's is not a comparison anyone is making.
	int DrawPlayerList( int x, int y, int lServer )
	{
		const int total = static_cast<int>( BROWSER_GetNumPlayers( lServer ));

		// The two silences that are not the same thing. A server that did not send player rows gets
		// told apart from one that genuinely has nobody in it, because "unknown" and "empty" lead to
		// different decisions and this panel should not turn one into the other.
		if ( BROWSER_HasPlayerData( lServer ) == false )
		{
			DrawInPanel( CR_DARKGRAY, x, y, "Player list not reported" );
			return y + SB_DETAIL_LINE;
		}

		if ( total <= 0 )
		{
			DrawInPanel( CR_DARKGRAY, x, y, "No one playing" );
			return y + SB_DETAIL_LINE;
		}

		const int bottom = ( y + SB_PLAYERLIST_MAX_H < SB_DETAIL_TEXT_BOTTOM )
			? ( y + SB_PLAYERLIST_MAX_H ) : SB_DETAIL_TEXT_BOTTOM;

		int rows = ( bottom - y ) / SB_DETAIL_LINE;
		if ( rows < 1 )
			rows = 1;

		g_PlayerScroll = zx::ComputeRestoredScroll( g_PlayerScroll, total, rows );

		g_PlayerListTop = y;
		g_PlayerListBottom = y + rows * SB_DETAIL_LINE;
		g_PlayerListRows = rows;

		const bool bScrolls = ( total > rows );
		const int textW = SB_DETAIL_TEXT_W - ( bScrolls ? ( SB_PLRBAR_W + 3 ) : 0 );

		for ( int i = 0; ( i < rows ) && (( g_PlayerScroll + i ) < total ); i++ )
		{
			const int entry = g_PlayerScroll + i;
			const int lineY = y + i * SB_DETAIL_LINE;

			const bool bBot = BROWSER_IsPlayerBot( lServer, entry );
			const bool bSpec = ( BROWSER_GetPlayerSpectating( lServer, entry ) != 0 );

			// Right-hand column first, exactly as the WAD list sizes its files: it is short and fixed,
			// so the name gets whatever is left rather than the other way round.
			FString right;
			if ( bBot )
				right = "BOT";
			else if ( bSpec )
				right = "SPEC";
			else
				right.Format( "%d", static_cast<int>( BROWSER_GetPlayerFragcount( lServer, entry )));

			const int rightW = SmallFont->StringWidth( right );
			const int gap = SmallFont->StringWidth( " " );

			// [rc4l] Player names run long and carry colour codes, so this is the server-name row's
			// problem exactly and gets the server-name row's answer. serverbrowser_FitName colorizes,
			// measures the VISIBLE width -- escapes cost characters but no pixels -- and cuts only at
			// offsets that cannot land inside an escape, which would otherwise leave a dangling code
			// to eat the next glyph and tint the rest of the line. It returns the name untouched when
			// it already fits, so there is nothing to test for first.
			const FString name = serverbrowser_FitName( BROWSER_GetPlayerName( lServer, entry ),
				textW - rightW - gap );

			DrawInPanel( ( bBot || bSpec ) ? CR_DARKGRAY : CR_WHITE, x, lineY, name );
			DrawInPanel( CR_DARKGRAY, x + textW - rightW, lineY, right );

			// The row is not a control, but it is a rectangle, so it can say the things the drawn line
			// had to drop: the untruncated name, and the ping nothing else has room for.
			//
			// [rc4l] Plain here too. A tooltip is the panel explaining itself, so it is the last place
			// that should be rendering a colour it got from somewhere else.
			//
			// [rc4l] AND FORMATTED, NOT STREAMED. FString::operator<< has overloads for FString, const
			// char *, char and FName -- and none for int. Streaming a number therefore converts it to
			// char and appends one byte: a ping of 0 appends NUL, which DrawText treats as the end of
			// the string, so the rest of that line silently vanishes. It compiles without a murmur.
			{
				FString tip = serverbrowser_PlainName( BROWSER_GetPlayerName( lServer, entry ));

				if ( bBot )
					tip << "\nBot";
				else
				{
					FString detail;
					if ( bSpec )
						detail.Format( "\nSpectating\n%d ms",
							static_cast<int>( BROWSER_GetPlayerPing( lServer, entry )));
					else
						detail.Format( "\n%d frags\n%d ms",
							static_cast<int>( BROWSER_GetPlayerFragcount( lServer, entry )),
							static_cast<int>( BROWSER_GetPlayerPing( lServer, entry )));
					tip << detail;
				}

				serverbrowser_Tip( x, lineY, textW, SB_DETAIL_LINE, tip );
			}
		}

		if ( bScrolls )
			DrawPlayerScrollbar( total, rows );

		return g_PlayerListBottom;
	}

	//*************************************************************************
	//
	// [rc4l] The player list's bar. Same column, width and arithmetic as the WAD list's -- see
	// DrawWadScrollbar; the duplication is two call sites of one unit, not two implementations.
	void DrawPlayerScrollbar( int total, int rows )
	{
		const int left = serverbrowser_ToScreenX( SB_PLRBAR_X );
		const int width = MAX( 1, serverbrowser_ToScreenX( SB_PLRBAR_X + SB_PLRBAR_W ) - left );
		const int top = serverbrowser_ToScreenY( g_PlayerListTop );
		const int height = serverbrowser_ToScreenY( g_PlayerListBottom ) - top;
		if ( height <= 0 )
			return;

		screen->Dim( PalEntry( 120, 140, 180 ), 0.14f, left, top, width, height );

		const int minThumb = serverbrowser_ToScreenY( 6 ) - serverbrowser_ToScreenY( 0 );
		const int thumbH = zx::ComputeThumbHeight( height, rows, total, minThumb );
		const int thumbY = top + zx::ComputeThumbTop( height, thumbH, g_PlayerScroll, total - rows );

		screen->Dim( PalEntry( 170, 190, 230 ), 0.55f, left, thumbY, width, thumbH );
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
	// [rc4l] How long REFRESH still owes, or 0 if it may be pressed. One helper so the mouse, the
	// keyboard and the label can never disagree about whether the button is available.
	static int RefreshWaitSeconds( void )
	{
		zx::RefreshGateIn in;
		in.msSinceLastRefresh = static_cast<int>( BROWSER_MSSinceRefresh( ));
		in.minIntervalMs = SB_REFRESH_FLOOR_MS;

		const zx::RefreshGateOut out = zx::GateRefresh( in );
		return ( out.allowed ? 0 : out.waitSeconds );
	}

	//*************************************************************************
	//
	// [rc4l] Pressing REFRESH, by whichever route. Both callers used to inline the same two calls, and
	// a floor added to one of them would have been a floor the other walked straight through.
	static void PressRefresh( void )
	{
		if ( RefreshWaitSeconds( ) > 0 )
		{
			// Refused, and it says so: the label becomes the countdown (see DrawRefreshButton), so
			// the press is visibly declined rather than visibly ignored.
			g_RefreshRefused = true;
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/invalid", snd_menuvolume, ATTN_NONE );
			return;
		}

		g_RefreshRefused = false;
		BROWSER_RefreshListedServers( );
		BROWSER_QueryServerRegistry( );
	}

	//*************************************************************************
	//
	// [rc4l] Bottom left, and it does two jobs.
	//
	// It gives the player a way to ask, which is what was missing. But the re-check on open already
	// ran silently under a list that kept its rows -- correct behaviour that is indistinguishable
	// from nothing happening, and therefore read as an over-eager cache. So the button also REPORTS,
	// and an automatic refresh lights it up exactly as a pressed one does. The complaint was never
	// that the list was stale; it was that the work was invisible.
	void DrawRefreshButton( void )
	{
		const bool bBusy = BROWSER_IsRefreshInFlight( );

		const int left = serverbrowser_ToScreenX( SB_REFRESH_X );
		const int right = serverbrowser_ToScreenX( SB_REFRESH_X + SB_REFRESH_W );
		const int top = serverbrowser_ToScreenY( SB_REFRESH_Y );
		const int bottom = serverbrowser_ToScreenY( SB_REFRESH_Y + SB_REFRESH_H );

		const int w = right - left;
		const int h = bottom - top;
		if (( w <= 0 ) || ( h <= 0 ))
			return;

		// The glow anchors to its left edge, the same way every other focusable control does.
		FocusAnchor( zx::BrowserFocus::Refresh, SB_REFRESH_X - 5, SB_REFRESH_Y + SB_REFRESH_H / 2 );

		// Same oval as the tabs: this switches nothing and is not a surface, it is a thing you press.
		const int radius = h / 2;
		const int base = bBusy ? 74 : ( g_RefreshHot ? 62 : 38 );

		const zx::PanelColor topCol = { static_cast<BYTE>( base ), static_cast<BYTE>( base ),
			static_cast<BYTE>( base + 24 ), 200 };
		const zx::PanelColor botCol = { static_cast<BYTE>( base / 2 ), static_cast<BYTE>( base / 2 ),
			static_cast<BYTE>( base / 2 + 18 ), 215 };

		for ( int row = 0; row < h; ++row )
		{
			const int inset = zx::ComputeRoundedInset( row, h, radius );
			const int rowW = w - 2 * inset;
			if ( rowW <= 0 )
				continue;

			const zx::PanelColor c = zx::ComputePanelGradient( row, h, topCol, botCol );
			screen->Dim( PalEntry( c.r, c.g, c.b ), c.a / 255.f, left + inset, top + row, rowW, 1 );
		}

		// While busy the label says what is happening rather than what to press: the button is not
		// disabled, and pressing it again while it works is harmless, but it should not be the only
		// thing on screen claiming nothing is going on.
		//
		// [rc4l] And once it has turned a press away, it counts down to being pressable instead. A
		// silent no is indistinguishable from a dead control, and the player's next move after one is
		// to press it harder.
		const int wait = RefreshWaitSeconds( );
		const bool bRefused = ( wait > 0 ) && g_RefreshRefused;

		FString countdown;
		if ( bRefused )
			countdown.Format( "WAIT %ds", wait );

		const char *const label = bRefused ? countdown.GetChars( ) : ( bBusy ? "CHECKING" : "REFRESH" );
		const int textW = SmallFont->StringWidth( label );

		screen->DrawText( SmallFont, bRefused ? CR_BRICK : ( bBusy ? CR_GOLD : CR_GRAY ),
			SB_REFRESH_X + ( SB_REFRESH_W - textW ) / 2, SB_REFRESH_Y + 3, label,
			DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );

		// [rc4l] How old the list is, under what the button does. A list that looks populated says
		// nothing about whether it is current, and this is the one control whose whole job is that
		// question -- so the answer belongs on it rather than being inferred from rows that may have
		// been sitting there for an hour.
		// [rc4l] Composed WHEN THE TOOLTIP APPEARS, then left alone until it appears again.
		//
		// It counts in seconds, and rebuilding it every frame meant the box resized under the pointer
		// as "9 secs ago" became "10 secs ago", then again at 100. A tooltip is a thing you are in
		// the middle of reading; text that reflows while you read it is worse than text that is a few
		// seconds stale, and the cure for stale is to look again, which is exactly what rebuilds it.
		if ( !g_RefreshHot )
		{
			g_RefreshTipShown = false;
		}
		else if ( !g_RefreshTipShown || g_RefreshTip.IsEmpty( ))
		{
			g_RefreshTipShown = true;

			const LONG lAgo = BROWSER_SecondsSinceRefresh( );

			FString tip;
			tip << "Refresh all servers\n"
				<< zx::LastRefreshedLine( lAgo >= 0, static_cast<int>( lAgo )).c_str( );

			// Say what the wait is FOR. "Available in 6s" on its own reads as an arbitrary rule; the
			// registry refusing us is a fact about the world, and it is also the reason a player
			// should not want to press this again yet.
			if ( wait > 0 )
				tip.AppendFormat( "\nAvailable in %ds. The registry ignores faster requests", wait );

			// Right-click is invisible unless something says so, and the whole point of it is that
			// the player who suspects ONE row is stale does not have to spend the whole-list floor.
			tip << "\nRight-click a server to re-check just that one";

			g_RefreshTip = tip;
		}

		serverbrowser_Tip( SB_REFRESH_X, SB_REFRESH_Y, SB_REFRESH_W, SB_REFRESH_H, g_RefreshTip );
	}

	//*************************************************************************
	//
	// [rc4l] One small bar per configured server registry, beside the refresh button.
	//
	// The browser queries several registries and unions the results, and until now the screen said
	// nothing about which ones answered. A mistyped registry, a dead local one and a healthy network
	// all produced the same list, so there was no way to tell "nobody is hosting" from "we never
	// heard back". Each bar carries its address and its status code on hover.
	//
	// Bars rather than text because the count is open-ended: a player may list several registries, and
	// names as long as registry.cantstopscrolling.net do not fit a footer three times over.
	void DrawRegistryBars( )
	{
		const unsigned int count = BROWSER_GetServerRegistryCount( );
		if ( count == 0 )
			return;

		int x = SB_REGBAR_X;

		for ( unsigned int i = 0; i < count; ++i )
		{
			std::string host;
			int port = 0;
			zx::RegistryStatus status = zx::RegistryStatus::Pending;

			if ( BROWSER_GetServerRegistryStatus( i, host, port, status ) == false )
				continue;

			PalEntry colour;
			switch ( zx::RegistryToneFor( status ))
			{
			case zx::RegistryTone::Good:	colour = PalEntry( 80, 200, 80 ); break;
			case zx::RegistryTone::Warn:	colour = PalEntry( 220, 170, 60 ); break;
			case zx::RegistryTone::Bad:		colour = PalEntry( 200, 60, 60 ); break;
			default:						colour = PalEntry( 110, 110, 110 ); break;
			}

			const int left = serverbrowser_ToScreenX( x );
			const int right = serverbrowser_ToScreenX( x + SB_REGBAR_W );
			const int top = serverbrowser_ToScreenY( SB_REGBAR_Y );
			const int bottom = serverbrowser_ToScreenY( SB_REGBAR_Y + SB_REGBAR_H );

			screen->Dim( colour, 0.85f, left, top, MAX( right - left, 1 ), MAX( bottom - top, 1 ));

			// [rc4l] The hover target is padded either side of the bar itself, which is only a few
			// pixels wide: a two pixel target is one nobody can hit on purpose.
			serverbrowser_Tip( x - 2, SB_REGBAR_Y - 2, SB_REGBAR_W + 4, SB_REGBAR_H + 4,
				zx::RegistryTooltip( host, port, status ).c_str( ));

			x += SB_REGBAR_W + SB_REGBAR_GAP;
		}
	}

	//*************************************************************************
	//
	void DrawFooter( zx::BrowserPhase phase, const zx::BrowserCounts &counts )
	{
		const int y = SB_FOOTER_Y;

		DrawRefreshButton( );
		DrawRegistryBars( );
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
		else if ( phase == zx::BrowserPhase::Empty )
		{
			// [rc4l] Which of several true things to say, decided in replyrouting_compute.h so the
			// ordering is tested rather than argued about. The ordering matters: each answer sends the
			// player somewhere different, and only one of them is where the problem actually is.
			const int active = serverbrowser_CountActive( );
			const int mismatched = static_cast<int>( BROWSER_CountVersionMismatched( ));
			const int silent = counts.timedOut + counts.badResponse;

			switch ( zx::ExplainEmptyList( !g_Search.text.empty( ), active, mismatched, silent ))
			{
			case zx::EmptyReason::HiddenBySearch:
				// The placeholder already said nothing matched. Repeating "nothing is being hosted"
				// under it would be a second, wrong answer to the same question -- there ARE servers.
				text.Format( "%d hidden by the search", active );
				break;

			case zx::EmptyReason::WrongVersion:
				// These answered us. Hiding them without saying so is what makes one player insist a
				// server exists while another cannot find it anywhere.
				text.Format( "%d hidden, running a different version", mismatched );
				break;

			case zx::EmptyReason::NoResponse:
				text.Format( "%d did not respond", silent );
				break;

			case zx::EmptyReason::NothingHosted:
				text = "Nothing is being hosted right now";
				break;
			}
		}

		if ( text.IsNotEmpty( ))
			screen->DrawText( SmallFont, CR_DARKGRAY,
				( SB_VIRT_W / 2 ) - ( SmallFont->StringWidth( text ) / 2 ), y, text, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
	}

	//*************************************************************************
	//
	const char *Spinner( )
	{
		static const char *const frames[] = { "|", "/", "-", "\\" };
		return frames[zx::ComputeSpinnerFrame( static_cast<int>( DMenu::MenuTime ), 4, 4 )];
	}

	// [rc4l] "Loading." through "Loading...", for a row whose answer is being worked out on a
	// worker. Through the same frame unit the spinner uses, so the two tick together rather than
	// drifting into a busy corner of the screen where two things blink out of step.
	//
	// Slower than the spinner on purpose: dots that change four times a second read as an error
	// flashing rather than as something in progress.
	const char *LoadingText( )
	{
		static const char *const frames[] = { "Loading.", "Loading..", "Loading..." };
		return frames[zx::ComputeSpinnerFrame( static_cast<int>( DMenu::MenuTime ), 3, 10 )];
	}

	//*************************************************************************
	//
	// [rc4l] One rectangle, any tint. It used to hard-code the selection's blue, which meant the row
	// for the server you are ON had nowhere to put its green. See 3e08af3.
	void DimRow( int y, PalEntry tint, float alpha )
	{
		const int left = serverbrowser_ToScreenX( SB_PANEL_LEFT + 4 );
		const int right = serverbrowser_ToScreenX( SB_ROW_RIGHT );
		const int top = serverbrowser_ToScreenY( y - 2 );
		const int bottom = serverbrowser_ToScreenY( y - 2 + SB_ROW_HEIGHT );

		screen->Dim( tint, alpha, left, top, right - left, bottom - top );
	}

	//*************************************************************************
	//
	void DrawRightAligned( FFont *font, EColorRange color, int right, int y, const char *text )
	{
		screen->DrawText( font, color, right - font->StringWidth( text ), y, text, DTA_VirtualWidth, SB_VIRT_W, DTA_VirtualHeight, SB_VIRT_H, DTA_KeepRatio, true, TAG_DONE );
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
		// [rc4l] The bar above has the arrows, so the browser has no cursor to show. Leaving the glow
		// lit puts two markers on screen at once, and only one of them is where the next keypress
		// actually goes: the browser's would be pointing at the tab the player just left.
		if ( zx::GlobalHeader_HasFocus( ))
			return;

		if ( g_Focus != owner )
			return;

		g_FocusGlowX = vcx;
		g_FocusGlowY = vcy;
		g_FocusGlowValid = true;
	}

	void DrawFocusGlow( int vcx, int vcy )
	{
		// [rc4l] The orb itself now lives in features/menu-focus, because the global tab bar draws
		// the same marker and two copies would drift. What stays here is the browser's own scale:
		// how many real pixels 100 of ITS virtual units cover.
		//
		// Measured over a long span rather than one unit. The mapping is fractional (the panel's 640
		// units cover about 940 real ones), so the width of a single virtual pixel rounds to 1 at
		// some x and 2 at others, and the orb was HALF THE SIZE depending on where it landed,
		// flickering between the two every tic while it travelled: travelling is exactly changing x.
		const int span = serverbrowser_ToScreenX( 100 ) - serverbrowser_ToScreenX( 0 );

		zx::DrawFocusGlow( serverbrowser_ToScreenX( vcx ), serverbrowser_ToScreenY( vcy ), span );
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
		if ( g_Dialog.open )
			return DialogMouseEvent( type, x, y );


		// The search box, which shares the row with the tabs.
		g_SearchHot = false;
		{
			const bool bOverSearch = (( parts & zx::kPartTabs ) != 0 ) &&
				( g_Tab == BrowserTab::Browse ) &&
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
			// Only under BROWSE: the box is not drawn on PLAY, and a hit test for something that is
			// not there would swallow clicks in the empty space beside the tabs.
			const bool bOverSearch = ( g_Tab == BrowserTab::Browse ) &&
				( x >= serverbrowser_ToScreenX( SB_SEARCH_LEFT )) &&
				( x < serverbrowser_ToScreenX( SB_SEARCH_RIGHT )) &&
				( y >= serverbrowser_ToScreenY( SB_SEARCH_TOP )) &&
				( y < serverbrowser_ToScreenY( SB_SEARCH_TOP + SB_SEARCH_H ));

			if ( bOverSearch && ( type == MOUSE_Click ))
				SetFocus( zx::BrowserFocus::Search );

			// [rc4l] Press, double-press and drag, through FieldMouse -- which this block used to
			// BE. Shared so the field added after it behaves the same rather than nearly the same:
			// that one had no caret placement, no word select and no drag at all, because all three
			// lived here under a name that said "search".
			//
			// Only under BROWSE, where the box is drawn. FieldMouse tests the rectangle and knows
			// nothing about tabs.
			if ( g_Tab == BrowserTab::Browse )
			{
				if ( FieldMouse( type, x, y, SB_SEARCH_LEFT, SB_SEARCH_TOP, SB_SEARCH_W, SB_SEARCH_H,
					g_Search, g_SearchFirstChar, g_SearchDragging, g_SearchClickTime ))
				{
					return true;
				}
			}
		}

		// [rc4l] The refresh button. Checked before everything else because it sits under the list, on
		// the footer line, where nothing else claims a click -- and being first means it can never
		// lose one to a hit test that happens to be generous at its edges.
		{
			g_RefreshHot = (( x >= serverbrowser_ToScreenX( SB_REFRESH_X )) &&
				( x < serverbrowser_ToScreenX( SB_REFRESH_X + SB_REFRESH_W )) &&
				( y >= serverbrowser_ToScreenY( SB_REFRESH_Y )) &&
				( y < serverbrowser_ToScreenY( SB_REFRESH_Y + SB_REFRESH_H )));

			if ( g_RefreshHot )
			{
				if ( type == MOUSE_Release )
				{
					// Rows are kept and re-checked underneath, so pressing this never empties the
					// screen. It makes the checking visible, and picks up servers that have appeared
					// since. PressRefresh also owns the floor, which the keyboard route shares.
					PressRefresh( );
				}

				return true;
			}
		}

		// The tabs.
		if ( parts & zx::kPartTabs )
		{
			g_TabHot = -1;
			for ( int i = 0; i < kTabCount; ++i )
			{
				const int vLeft = TabLeft( i );
				if (( x < serverbrowser_ToScreenX( vLeft )) ||
					( x >= serverbrowser_ToScreenX( vLeft + TabW( i ))) ||
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

			// [rc4l] The sub-tabs of whichever tab is showing. Both have a row now, and it is hit
			// tested through the same helpers that draw it, so a pill cannot be clickable anywhere
			// other than where it is drawn.
			g_SubTabHot = -1;
			for ( int i = 0; i < SubTabCount( ); ++i )
			{
				const int vLeft = SubTabLeft( i );
				if (( x < serverbrowser_ToScreenX( vLeft )) ||
					( x >= serverbrowser_ToScreenX( vLeft + SubTabW( i ))) ||
					( y < serverbrowser_ToScreenY( SB_SUBTAB_TOP )) ||
					( y >= serverbrowser_ToScreenY( SB_SUBTAB_TOP + SB_SUBTAB_H )))
				{
					continue;
				}

				g_SubTabHot = i;
				if ( type == MOUSE_Release )
				{
					SetFocus( zx::BrowserFocus::SubTabs );
					SelectSubTabIndex( i );
				}
				return true;
			}
		}

		// The hosting panel, which stands where the list and the detail panel would be. Checked before
		// them so a click cannot be claimed twice.
		if ( parts & zx::kPartHost )
		{
			if ( HostMouseEvent( type, x, y ))
				return true;
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

			// [rc4l] The player list's bar, same treatment. It shares the WAD bar's column, so the two
			// are told apart purely by which list's rows the pointer is level with -- which is why the
			// vertical test uses the recorded box of each list and not the column.
			const bool bHaveSel = ( g_Selected >= 0 )
				&& ( g_Selected < static_cast<int>( g_SortedServers.Size( )));
			const int plrTotal = bHaveSel
				? static_cast<int>( BROWSER_GetNumPlayers( g_SortedServers[g_Selected] )) : 0;
			const int plrHeight = serverbrowser_ToScreenY( g_PlayerListBottom )
				- serverbrowser_ToScreenY( g_PlayerListTop );

			const bool bOverPlrBar = ( plrTotal > g_PlayerListRows ) && ( g_PlayerListRows > 0 ) &&
				( x >= serverbrowser_ToScreenX( SB_PLRBAR_X - 3 )) &&
				( x < serverbrowser_ToScreenX( SB_PLRBAR_X + SB_PLRBAR_W + 3 )) &&
				( y >= serverbrowser_ToScreenY( g_PlayerListTop )) &&
				( y < serverbrowser_ToScreenY( g_PlayerListBottom ));

			if ( type == MOUSE_Click )
				g_DraggingPlayerBar = bOverPlrBar;

			if ( g_DraggingPlayerBar && ( plrHeight > 0 ) && ( plrTotal > g_PlayerListRows ))
			{
				const int top = serverbrowser_ToScreenY( g_PlayerListTop );
				const int minThumb = serverbrowser_ToScreenY( 6 ) - serverbrowser_ToScreenY( 0 );
				const int thumbH = zx::ComputeThumbHeight( plrHeight, g_PlayerListRows, plrTotal,
					minThumb );

				g_PlayerScroll = zx::ComputeFirstFromPointer( y - top, plrHeight, thumbH,
					plrTotal - g_PlayerListRows );
			}

			if ( g_DraggingPlayerBar )
			{
				if ( type == MOUSE_Release )
					g_DraggingPlayerBar = false;
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

				if (( type == MOUSE_Click ) || ( type == MOUSE_Click2 ))
				{
					g_MousePressRow = row;
					// True also arms the capture that makes MOUSE_Release arrive at all (DMenu::
					// Responder only forwards a release while captured).
					return true;
				}

				// [rc4l] Right button: re-check THIS server and nothing else.
				//
				// The whole-list refresh is rationed because it costs a packet per row and a registry
				// request. But the question a player usually has is about one row, the one they are
				// looking at, and making them spend the whole-list floor to ask it is why the floor
				// would feel like an obstacle rather than a courtesy. One row is one packet to one
				// machine, so it needs no ration and does not touch the registry at all.
				if ( type == MOUSE_Release2 )
				{
					const bool bOnPressRow = ( row == g_MousePressRow );
					g_MousePressRow = -1;
					if ( !bOnPressRow )
						return true;

					// Move the selection too. Asking about a row and then reading the detail panel of
					// a different one is the sort of mismatch nobody notices until it misleads them.
					g_Selected = row;
					g_RevealSelection = true;
					SetFocus( zx::BrowserFocus::Rows );

					BROWSER_RecheckServer( static_cast<ULONG>( g_SortedServers[row] ));
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
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

		if (( type == MOUSE_Release ) || ( type == MOUSE_Release2 ))
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

		// [rc4l] A dialog owns the keyboard outright while it is up, and answers three ways at once:
		// a shortcut letter, the arrows plus Enter, and the mouse. They are alternatives, not modes --
		// a player should never have to work out which one this particular box wants.
		if ( g_Dialog.open && ( ev != NULL ) && ( ev->type == EV_GUI_Event ))
		{
			if ( ev->subtype == EV_GUI_Char )
			{
				// A FIELD TAKES PRECEDENCE over the shortcuts. In a password box 's' is a character of
				// the password, not the STOP button -- a dialog that stole letters out of its own text
				// field would be unusable.
				if ( g_Dialog.hasInput )
				{
					g_DialogInput = zx::InsertChar( g_DialogInput, ev->data1, 64 );
					return true;
				}

				std::vector<char> keys;
				for ( int i = 0; i < g_Dialog.count; ++i )
					keys.push_back( g_Dialog.shortcuts[i] );

				const int picked = zx::ComputeDialogShortcut( keys, ev->data1 );
				if ( picked >= 0 )
					AnswerDialog( picked );
				return true;
			}

			if ( ev->subtype == EV_GUI_KeyDown )
			{
				if (( ev->data1 == '' ) && g_Dialog.hasInput )
				{
					g_DialogInput = zx::Backspace( g_DialogInput );
					return true;
				}
			}

			// Everything else is swallowed. A question on screen means the next keypress answers it,
			// not that it does something underneath.
			if (( ev->subtype == EV_GUI_KeyDown ) || ( ev->subtype == EV_GUI_KeyRepeat ))
				return true;
		}

		// [rc4l] Typing into the search box.
		//
		// Taken here, ahead of everything, because a focused text field owns the keyboard: 'y' is a
		// letter while you are typing a query, not an answer to a question that is not on screen, and
		// a printable key must never also be a menu shortcut. Only characters and the editing keys are
		// claimed -- the arrows still navigate, which is what moves focus back OUT of the box.
		// [rc4l] Same rule for the hosting form: a focused field owns the keyboard, so a server name
		// containing the letter 'p' does not also press something. Only when a FIELD has focus -- on
		// the button the keys belong to the menu again, which is what makes enter work there.
		// [rc4l] The NEW screen's own keys, before the preset form's: it has a search field of its
		// own, and the two forms are never on screen at the same time.
		if (( ev != NULL ) && ( ev->type == EV_GUI_Event ) && !g_Dialog.open && g_Notice.IsEmpty( ) &&
			( g_Tab == BrowserTab::Host ) && ( g_HostKind == HostKind::New ) &&
			( NewOwnsKeyboard( ) || ( g_Focus == zx::BrowserFocus::Host )))
		{
			if ( NewKeyEvent( ev ))
				return true;
		}

		// The CUSTOM tab's own, for the same reason: it has a search field, and no two of these
		// screens are ever up at once.
		if (( ev != NULL ) && ( ev->type == EV_GUI_Event ) && !g_Dialog.open && g_Notice.IsEmpty( ) &&
			( g_Tab == BrowserTab::Host ) && ( g_HostKind == HostKind::Custom ) &&
			( g_Focus == zx::BrowserFocus::Host ))
		{
			if ( CustomKeyEvent( ev ))
				return true;
		}

		if (( ev != NULL ) && ( ev->type == EV_GUI_Event ) && !g_Dialog.open && g_Notice.IsEmpty( ) &&
			( g_Tab == BrowserTab::Host ) && ( g_HostKind == HostKind::Presets ) &&
			( g_Focus == zx::BrowserFocus::Host ) && HostInAField( ))
		{
			if ( EditHostField( ev ))
				return true;
		}

		if (( ev != NULL ) && ( ev->type == EV_GUI_Event ) && !g_Dialog.open && g_Notice.IsEmpty( ) &&
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
		if (( ev != NULL ) && ( ev->type == EV_GUI_Event ) && !g_Dialog.open && g_Notice.IsEmpty( ))
		{
			const int total = static_cast<int>( g_SortedServers.Size( ));
			if (( ev->subtype == EV_GUI_WheelUp ) || ( ev->subtype == EV_GUI_WheelDown ))
			{
				const int step = ( ev->subtype == EV_GUI_WheelUp ) ? -3 : 3;

				// [rc4l] The IWAD chooser takes the notch while it is up, wherever the pointer is.
				// It is modal: scrolling the screen behind it would move something the player cannot
				// see and cannot have meant.
				//
				// One row a notch rather than three. The grid is six rows tall, so three would jump
				// most of the way down it and land nowhere anybody aimed.
				if (( g_Tab == BrowserTab::Host ) && ( g_HostKind == HostKind::New ) &&
					(( g_NewModal == NewModal::Flags ) || ( g_NewModal == NewModal::Gameplay )))
				{
					int &scroll = BoxScroll( g_NewModal );
					scroll = zx::ComputeClampedSelection( scroll + step,
						BoxMaxScroll( g_NewModal ) + 1 );
					return true;
				}

				if (( g_Tab == BrowserTab::Host ) && ( g_HostKind == HostKind::New ) &&
					( g_NewModal == NewModal::Maps ))
				{
					g_NewMapScroll = zx::ComputeClampedSelection( g_NewMapScroll + step,
						NewMapMaxScroll( ) + 1 );
					return true;
				}

				// [rc4l] The CUSTOM tab's two columns, each taking the notch when the pointer is
				// over it. Two views side by side, and a wheel that moved both would move the one
				// nobody was looking at.
				// The read-only map list takes the notch while it is up, wherever the pointer is.
				if (( g_Tab == BrowserTab::Host ) && ( g_HostKind == HostKind::Custom ) &&
					g_CustomMapsOpen )
				{
					const zx::CustomEntry *const chosen = CustomSelected( );
					const int lines = ( chosen != NULL )
						? static_cast<int>( chosen->maps.size( )) : 0;
					const int visible = MAX( 1,
						( NewBigButtonTop( ) - 8 - NewBigContentTop( )) / SB_NEW_ROW_H );

					g_CustomMapsScroll = zx::ClampScroll( g_CustomMapsScroll + step,
						MAX( 0, lines - visible ));
					return true;
				}

				if (( g_Tab == BrowserTab::Host ) && ( g_HostKind == HostKind::Custom ) &&
					!CustomEntries( ).empty( ))
				{
					const bool bOverList =
						( g_MouseX < serverbrowser_ToScreenX( SB_HOST_RCOL_LEFT - 8 ));

					if ( bOverList )
					{
						const int rows = static_cast<int>( CustomRows( ).size( ));
						g_CustomScroll = zx::ClampScroll( g_CustomScroll + step,
							MAX( 0, rows - CustomRowsVisible( )));
					}
					else
					{
						const zx::CustomEntry *const chosen = CustomSelected( );
						const int lines = ( chosen != NULL )
							? static_cast<int>( CustomDetailCached( *chosen ).size( )) : 0;

						g_CustomDetailScroll = zx::ClampScroll( g_CustomDetailScroll + step,
							MAX( 0, lines - CustomDetailRowsShown( )));
					}

					return true;
				}

				if (( g_Tab == BrowserTab::Host ) && ( g_HostKind == HostKind::New ) &&
					( g_NewModal == NewModal::Iwad ))
				{
					const int maxRows = NewModalMaxScroll( );
					g_NewIwadModalScroll = zx::ComputeClampedSelection(
						g_NewIwadModalScroll + (( step < 0 ) ? -1 : 1 ), maxRows + 1 );
					return true;
				}

				// [rc4l] The wad list, when the pointer is over the left column. Three rows a notch
				// here, unlike the chooser's one: this list is long and a notch that moved it by a
				// row would be a lot of wheel for twenty thousand files.
				if (( g_Tab == BrowserTab::Host ) && ( g_HostKind == HostKind::New ) &&
					( g_MouseX >= serverbrowser_ToScreenX( SB_HOST_LIST_LEFT - 6 )) &&
					( g_MouseX < serverbrowser_ToScreenX( SB_HOST_LIST_RIGHT + 6 )) &&
					( g_MouseY >= serverbrowser_ToScreenY( SB_NEW_WADS_TOP )) &&
					( g_MouseY < serverbrowser_ToScreenY( SB_NEW_WADS_BOTTOM )))
				{
					g_NewWadScroll = zx::ComputeClampedSelection( g_NewWadScroll + step,
						NewWadMaxScroll( ) + 1 );
					return true;
				}

				// And the load order, on the other side of the panel.
				if (( g_Tab == BrowserTab::Host ) && ( g_HostKind == HostKind::New ) &&
					( g_MouseX >= serverbrowser_ToScreenX( SB_HOST_RCOL_LEFT - 6 )) &&
					( g_MouseX < serverbrowser_ToScreenX( SB_HOST_BAR_X + 6 )) &&
					( g_MouseY >= serverbrowser_ToScreenY( SB_NEW_ORDER_TOP )) &&
					( g_MouseY < serverbrowser_ToScreenY( SB_NEW_ORDER_BOTTOM )))
				{
					g_NewOrderScroll = zx::ComputeClampedSelection( g_NewOrderScroll + step,
						NewOrderMaxScroll( ) + 1 );
					return true;
				}

				// [rc4l] While hosting, the right column is two scrollable halves, so the notch goes
				// to whichever half the pointer is in. Checked before the settings below, which are
				// not on screen in this state at all.
				if (( g_Tab == BrowserTab::Host ) && zx::HostIsActive( ) &&
					( g_MouseX >= serverbrowser_ToScreenX( SB_HOST_RCOL_LEFT - 6 )) &&
					( g_MouseX < serverbrowser_ToScreenX( SB_HOST_RCOL_RIGHT + 6 )))
				{
					if (( g_MouseY >= serverbrowser_ToScreenY( SB_HOST_RTOP_TOP )) &&
						( g_MouseY < serverbrowser_ToScreenY( SB_HOST_RUN_TOP_BOT )))
					{
						g_HostDetailScroll += step * 6;
						ClampHostDetailScroll( );
						return true;
					}

					if (( g_MouseY >= serverbrowser_ToScreenY( SB_HOST_RUN_BOT_TOP )) &&
						( g_MouseY < serverbrowser_ToScreenY( SB_HOST_RTOP_BOTTOM )))
					{
						g_HostStatusScroll += step * 6;
						ClampHostStatusScroll( );
						return true;
					}
				}

				// [rc4l] Over the experience list, the notch belongs to the list. Checked BEFORE the
				// settings below, whose test spans the whole panel and would otherwise answer for a
				// notch aimed at the left column -- and answer with nothing at all whenever the
				// settings happen to fit, which is why the list would not scroll by wheel.
				if (( g_Tab == BrowserTab::Host ) && ( HostListMaxScroll( ) > 0 ) &&
					( g_MouseY >= serverbrowser_ToScreenY( SB_HOST_VIEW_TOP )) &&
					( g_MouseY < serverbrowser_ToScreenY( SB_HOST_VIEW_BOTTOM )) &&
					( g_MouseX >= serverbrowser_ToScreenX( SB_HOST_LIST_LEFT - 6 )) &&
					( g_MouseX < serverbrowser_ToScreenX( SB_HOST_RCOL_LEFT - 6 )))
				{
					g_HostListScroll += step * 6;
					ClampHostListScroll( );
					return true;
				}

				// [rc4l] Over the DETAIL column while not hosting, the notch belongs to it.
				//
				// This only existed for the hosting case above, so the panel that describes an
				// experience could not be scrolled at all until a server was running. That was
				// harmless while it only held a summary and a file list, and stopped being harmless
				// the moment it grew gameplay settings: a fourth mod was drawn past the bottom of the
				// column with no way to reach it. Checked before the settings, whose test spans the
				// whole panel.
				if (( g_Tab == BrowserTab::Host ) && !zx::HostIsActive( ) &&
					!g_HostShowSettings && ( HostDetailMaxScroll( ) > 0 ) &&
					( g_MouseY >= serverbrowser_ToScreenY( SB_HOST_VIEW_TOP )) &&
					( g_MouseY < serverbrowser_ToScreenY( SB_HOST_VIEW_BOTTOM )) &&
					( g_MouseX >= serverbrowser_ToScreenX( SB_HOST_RCOL_LEFT - 6 )) &&
					( g_MouseX < serverbrowser_ToScreenX( SB_HOST_RCOL_RIGHT + 6 )))
				{
					g_HostDetailScroll += step * 6;
					ClampHostDetailScroll( );
					return true;
				}

				// [rc4l] Over the hosting settings, the notch belongs to them. Same rule as the WAD
				// list below: one wheel and more than one scrollable thing means it drives whichever
				// one the pointer is actually over.
				if (( g_Tab == BrowserTab::Host ) && ( HostMaxScroll( ) > 0 ) &&
					( g_MouseY >= serverbrowser_ToScreenY( SB_HOST_VIEW_TOP )) &&
					( g_MouseY < serverbrowser_ToScreenY( SB_HOST_VIEW_BOTTOM )) &&
					( g_MouseX >= serverbrowser_ToScreenX( SB_HOST_LEFT )) &&
					( g_MouseX < serverbrowser_ToScreenX( SB_HOST_RIGHT )))
				{
					// Three rows' worth per notch, measured in the units the layout is in rather
					// than in rows -- the settings are not all the same height.
					g_HostScroll += step * 6;
					ClampHostScroll( );
					return true;
				}

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

				// And the player list below it, by the same rule. Its box is tested separately rather
				// than as "the detail panel" because the two lists sit one above the other inside that
				// panel, and a notch aimed at the names must not move the files.
				if (( g_PlayerListRows > 0 ) && ( g_Selected >= 0 ) &&
					( g_Selected < static_cast<int>( g_SortedServers.Size( ))))
				{
					const int plrTotal = static_cast<int>(
						BROWSER_GetNumPlayers( g_SortedServers[g_Selected] ));

					if (( plrTotal > g_PlayerListRows ) &&
						( g_MouseY >= serverbrowser_ToScreenY( g_PlayerListTop )) &&
						( g_MouseY < serverbrowser_ToScreenY( g_PlayerListBottom )) &&
						( g_MouseX >= serverbrowser_ToScreenX( SB_DETAIL_LEFT )) &&
						( g_MouseX < serverbrowser_ToScreenX( SB_DETAIL_RIGHT )))
					{
						g_PlayerScroll = zx::ComputeRestoredScroll( g_PlayerScroll + step, plrTotal,
							g_PlayerListRows );
						return true;
					}
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
		// [rc4l] Let go of every latched menu key whenever the focus actually moves.
		//
		// M_Responder latches on the way down and unlatches on the way up, and M_Ticker repeats
		// whatever is still latched -- but a focus change can turn TranslateKeyboardEvents off
		// mid-press, and the release then arrives untranslated and never unlatches anything. The
		// button repeats forever and drags the focus onward while the player has already let go.
		//
		// This used to fire only when focus entered the SEARCH box, which was the only field at the
		// time. The hosting form's fields do exactly the same thing, so the condition is now the one
		// that was always meant: any real change of focus.
		//
		// Releasing more often than strictly necessary is not a cost -- it is the behaviour anyway. A
		// held arrow that has just moved the focus somewhere else should stop there, not keep going.
		if ( focus != g_Focus )
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
		// A focused text field -- the search box, a field on the hosting form, or the wad search on
		// NEW -- needs the raw events. On the form's BUTTON it does not: there the keys are
		// navigation again.
		//
		// [rc4l] THE NEW SCREEN'S BOX BELONGS IN THIS LIST, and leaving it out is why that field
		// looked like a text box and behaved like nothing. Backspace never reached it: M_Responder
		// was still translating, so the key arrived as MKEY_Clear somewhere else entirely, and the
		// arrows arrived as MKEY_Left and MKEY_Right and moved the SELECTION instead of the caret.
		// The field was drawn correctly the whole time, which is what made it look like a drawing
		// bug rather than a routing one.
		const bool bInAField = ( g_Focus == zx::BrowserFocus::Search )
			|| (( g_Focus == zx::BrowserFocus::Host ) && HostInAField( ))
			|| (( g_Focus == zx::BrowserFocus::Host ) && ( g_Tab == BrowserTab::Host ) &&
				( g_HostKind == HostKind::New ) && ( g_NewFocus == NewFocus::Search ) &&
				( g_NewModal == NewModal::None ))
			// And a box inside one of the settings panels, for the same reason: backspace has to
			// reach it rather than being turned into a menu key on the way. Either a flag number in
			// the footer, or a setting's own value.
			|| (( g_Tab == BrowserTab::Host ) && ( g_HostKind == HostKind::New ) &&
				( g_NewModal != NewModal::None ) && ( g_NewModal != NewModal::Iwad ) &&
				(( g_NewFlagEditing >= 0 ) || g_NewSettingEditing.IsNotEmpty( )))

			// The save box is a name being typed the whole time it is open, so it always wants the
			// raw keys. Asked through NewOwnsKeyboard so this and the guard that delivers them
			// cannot answer differently.
			|| ( NewOwnsKeyboard( ) && ( g_NewModal == NewModal::Save ))

			// And the CUSTOM tab's search box, for the reason the wad search needed it: backspace
			// has to reach the field rather than becoming a menu key on the way.
			|| (( g_Tab == BrowserTab::Host ) && ( g_HostKind == HostKind::Custom ) &&
				( g_CustomFocus == CustomFocus::Search ));

		return ( bInAField == false ) || g_Dialog.open || g_Notice.IsNotEmpty( );
	}

	//*************************************************************************
	//
	// [rc4l] Everything a text field does, in ONE place, for every field in this browser.
	//
	// The search box got all of this first -- selection, clipboard, word jumps, ctrl+backspace -- and
	// the hosting form was written afterwards with a reduced copy, on the reasoning that its fields
	// are short and mostly retyped whole. That reasoning is how a second-class field happens: nobody
	// decides a box should be worse to type in, it just never gets the things the other one has, and
	// the player who tries to paste a server name finds out.
	//
	// So the EDITING lives here and the field-specific parts stay outside: what escape leaves to,
	// what enter commits, and where up and down go are the caller's, because only the caller knows
	// what is around it. Everything between those is identical by construction.
	//
	// Returns what the caller still has to answer.
	enum class FieldKey
	{
		Handled,		// the field took it
		Escape,
		Enter,
		Up,
		Down,

		// [rc4l] NOT A KEYBOARD EVENT AT ALL, and therefore none of a text field's business.
		//
		// This state exists because leaving it out cost the whole menu its mouse. Mouse events reach
		// a menu through Responder, not MouseEvent -- menu.cpp hands every non-keyboard GUI event to
		// CurrentMenu->Responder -- and the guard that routes keys to a focused field tests
		// ev->type == EV_GUI_Event, which is true for mouse events too. They arrived here, fell past
		// the character and key-down cases, and were reported as Handled.
		//
		// The caller then answered true, the event never reached MouseEvent, and the browser went
		// deaf to the pointer for as long as any field had focus: frozen tooltip, dead clicks, dead
		// tabs. Saying "not mine" is what lets it fall through to the mouse path.
		Unclaimed,

		// [rc4l] The caret ran out of text and there is something beside the box, so the arrow means
		// what it means everywhere else on the screen. Only ever returned when the caller said there
		// was somewhere to go -- a field with nothing beside it keeps the key.
		Left,
		Right,
	};

	// `digitsOnly` is for the port and player-limit boxes. A port with a letter in it is not a port,
	// and refusing the keystroke says so at the moment it happens rather than when the server fails
	// to start.
	FieldKey EditTextField( zx::TextInput &field, event_t *ev, size_t maxLength, bool digitsOnly,
		bool canExitLeft, bool canExitRight )
	{
		// [rc4l] Cmd counts as Ctrl. The Cocoa layer reports it as GKM_META, so honouring both here
		// is the whole of macOS support -- Cmd+A, Cmd+C, Cmd+V and Cmd+X land where a Mac user
		// expects without a second code path to keep in step.
		const bool bCtrl = (( ev->data3 & ( GKM_CTRL | GKM_META )) != 0 );
		const bool bShift = (( ev->data3 & GKM_SHIFT ) != 0 );

		// [rc4l] A HELD key repeats, the way it does in every other text box on the machine.
		//
		// EV_GUI_KeyRepeat was rejected here, so holding left did nothing at all -- one press, one
		// character, and a long name had to be walked one keystroke at a time. Backspace, delete,
		// home and end were the same.
		const bool bRepeat = ( ev->subtype == EV_GUI_KeyRepeat );

		// Anything that is not a key belongs to somebody else -- above all the mouse, which reaches a
		// menu through this same Responder and must be allowed past.
		if (( ev->subtype != EV_GUI_Char ) && ( ev->subtype != EV_GUI_KeyDown ) && !bRepeat )
			return FieldKey::Unclaimed;

		if ( ev->subtype == EV_GUI_Char )
		{
			// Ctrl+letter arrives here too on some layouts; those are commands, not text.
			if ( bCtrl )
				return FieldKey::Handled;

			if ( digitsOnly && (( ev->data1 < '0' ) || ( ev->data1 > '9' )))
				return FieldKey::Handled;

			field = zx::InsertChar( field, ev->data1, maxLength );
			return FieldKey::Handled;
		}

		if (( ev->subtype != EV_GUI_KeyDown ) && !bRepeat )
			return FieldKey::Unclaimed;

		const int key = ev->data1;

		if ( bCtrl )
		{
			switch ( key )
			{
			case 'a': case 'A':
				field = zx::SelectAll( field );
				return FieldKey::Handled;

			case 'c': case 'C':
				if ( zx::HasSelection( field ))
					I_PutInClipboard( zx::SelectedText( field ).c_str( ));
				return FieldKey::Handled;

			case 'x': case 'X':
				if ( zx::HasSelection( field ))
				{
					I_PutInClipboard( zx::SelectedText( field ).c_str( ));
					field = zx::DeleteSelection( field );
				}
				return FieldKey::Handled;

			case 'v': case 'V':
				{
					FString pasted = I_GetFromClipboard( false );

					// A pasted port is as likely to arrive with a newline as typed one is to arrive
					// with a letter, and the same rule applies: keep what belongs, drop what does not.
					if ( digitsOnly )
					{
						FString digits;
						for ( unsigned i = 0; i < pasted.Len( ); ++i )
						{
							if (( pasted[i] >= '0' ) && ( pasted[i] <= '9' ))
								digits += pasted[i];
						}
						pasted = digits;
					}

					field = zx::InsertText( field, pasted.GetChars( ), maxLength );
					return FieldKey::Handled;
				}

			case GK_LEFT:
			case GK_RIGHT:
				field = zx::MoveWord( field, ( key == GK_RIGHT ), bShift );
				return FieldKey::Handled;

			case '\b':
				// Ctrl+Backspace erases the word behind the caret, which is the fastest way to undo a
				// mistyped entry without holding the key down.
				if ( !zx::HasSelection( field ))
					field = zx::MoveWord( field, false, true );
				field = zx::DeleteSelection( field );
				return FieldKey::Handled;

			default:
				// Swallowed: a chord the field does not use is still not a menu shortcut.
				return FieldKey::Handled;
			}
		}

		switch ( key )
		{
		// [rc4l] These two do NOT repeat. Everything else here is a movement or a deletion, which is
		// exactly what holding a key should do more of; leaving and submitting are decisions, and a
		// held key must not make one twice.
		case GK_ESCAPE:	return bRepeat ? FieldKey::Handled : FieldKey::Escape;
		case GK_RETURN:	return bRepeat ? FieldKey::Handled : FieldKey::Enter;
		case GK_UP:		return FieldKey::Up;
		case GK_DOWN:	return FieldKey::Down;

		case '\b':
			field = zx::Backspace( field );
			return FieldKey::Handled;

		case GK_DEL:
			field = zx::DeleteForward( field );
			return FieldKey::Handled;

		case GK_HOME:
			field = zx::CaretHome( field, bShift );
			return FieldKey::Handled;

		case GK_END:
			field = zx::CaretEnd( field, bShift );
			return FieldKey::Handled;

		case GK_LEFT:
		case GK_RIGHT:
			{
				// [rc4l] The caret's FIRST, and the row's once the caret runs out.
				//
				// Moving through what you typed is what these keys mean inside a field, and a box
				// that jumped to the next control immediately would be one you could not edit. But a
				// box that never gave them back is one the keyboard goes into and cannot leave
				// sideways -- so at the edge, with something actually beside us, the press moves on.
				// ArrowLeavesField owns when that is; see its header for shift and selections.
				// [rc4l] A HELD key never leaves, it stops at the end of the text.
				//
				// Running the caret to the edge and then sliding out of the box on the next repeat
				// means you cannot hold left to get to the start without overshooting into another
				// control. Leaving is a decision, and holding a key is not how decisions are made --
				// the press AFTER the release is.
				const bool bRight = ( key == GK_RIGHT );
				const bool bMayLeave = !bRepeat && ( bRight ? canExitRight : canExitLeft );

				if ( zx::ArrowLeavesField( field, bRight, bMayLeave, bShift ))
					return bRight ? FieldKey::Right : FieldKey::Left;

				field = zx::MoveCaret( field, bRight ? 1 : -1, bShift );
				return FieldKey::Handled;
			}

		default:
			// Everything else is swallowed while the field has the keyboard. A letter is a letter, not
			// a menu shortcut, and the alternative is 'y' answering a question that is not on screen.
			return FieldKey::Handled;
		}
	}

	//*************************************************************************
	//
	// [rc4l] Editing one field of the hosting form.
	//
	// The same editor the search box uses, so a field on this screen is not a lesser one: selection,
	// clipboard, word jumps and ctrl+backspace all work here because they work there. Only the ways
	// OUT differ, and those are what this function is for.
	bool EditHostField( event_t *ev )
	{
		if ( HostInAField( ) == false )
			return false;

		const bool bDigits = ( HostFieldFocus( ) == kHostFieldPort )
			|| ( HostFieldFocus( ) == kHostFieldMaxPlayers );

		// [rc4l] There IS something to the left: the experience list. Passing false here is what made
		// the form a place the keyboard could go and not come out of sideways -- LEFT moved the caret
		// forever and never reached the rows. Nothing sits to the right, so that one stays shut.
		switch ( EditTextField( g_HostFields[HostFieldFocus( )], ev, SB_HOST_MAXLEN, bDigits,
			true, false ))
		{
		case FieldKey::Escape:
			// Out of the form, not out of the browser -- the same rule the search box follows, so a
			// second escape then closes the menu.
			SetFocus( zx::BrowserFocus::Tabs );
			g_HostFieldDragging = false;
			return true;

		case FieldKey::Enter:
		case FieldKey::Down:
			MoveHostFocus( 1 );
			return true;

		case FieldKey::Up:
			MoveHostFocus( -1 );
			return true;

		case FieldKey::Left:
			// The caret ran out and the list is beside us, so the arrow means what it means
			// everywhere else on this screen. Where it lands is the unit's answer, not one written
			// out again here.
			M_ReleaseMenuButtons( );
			g_HostFocus = zx::HostLeftOfTheForm( );
			RevealHostFocus( );
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
			return true;

		case FieldKey::Right:
		case FieldKey::Unclaimed:
			// Nothing sits beside these fields, so the editor never reports the first two -- and a
			// non-key belongs to the mouse.
			return false;

		case FieldKey::Handled:
			break;
		}

		return true;
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
		zx::TextInput next = g_Search;

		switch ( EditTextField( next, ev, SB_SEARCH_MAXLEN, false, true, false ))
		{
		case FieldKey::Escape:
			// Out of the box, not out of the browser. A second escape then closes the menu, which is
			// the ordinary meaning restored as soon as the field stops claiming it.
			SetFocus( zx::BrowserFocus::Tabs );
			g_SearchDragging = false;
			return true;

		case FieldKey::Enter:
			if ( static_cast<int>( g_SortedServers.Size( )) > 0 )
			{
				SetFocus( zx::BrowserFocus::Rows );
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
			}
			return true;

		// Navigation, which the framework would normally have translated for us -- while the field
		// holds the keyboard it has to pass these on itself.
		case FieldKey::Up:
			Navigate( zx::NavKey::Up, static_cast<int>( g_SortedServers.Size( )));
			return true;

		case FieldKey::Down:
			Navigate( zx::NavKey::Down, static_cast<int>( g_SortedServers.Size( )));
			return true;

		case FieldKey::Left:
			// Off the left of the search box is the tab row it shares.
			SetFocus( zx::BrowserFocus::Tabs );
			g_SearchDragging = false;
			return ApplyEdit( next );

		case FieldKey::Right:
		case FieldKey::Unclaimed:
			return false;			// nothing to the right, and non-keys belong to the mouse

		case FieldKey::Handled:
			break;
		}

		return ApplyEdit( next );
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

	// [rc4l] Which character of a field a screen x lands on, for click and drag.
	//
	// Walks the drawn text measuring as it goes rather than dividing by an average width, because
	// SmallFont is not monospace -- dividing would put the caret a character or two off in a string
	// with any 'i' or 'm' in it, which is exactly where a click has to be exact.
	//
	// Takes the field and its box so every field can use it, rather than reading the search box's
	// globals. The second field on this menu got no caret placement at all because this was written
	// for one field and named after it.
	size_t FieldCharAt( const zx::TextInput &field, int vx, int firstChar, int px )
	{
		const int textX = vx + SB_SEARCH_PAD;
		const FString all = field.text.c_str( );
		const int first = ( firstChar < static_cast<int>( all.Len( ))) ? firstChar : 0;

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

	size_t SearchCharAt( int px )
	{
		return FieldCharAt( g_Search, SB_SEARCH_LEFT, g_SearchFirstChar, px );
	}

	// [rc4l] Press, double-press and drag on a text field, for any field.
	//
	// The three together are what makes a box feel like a text box: a press puts the caret, a second
	// press takes the word, and a drag turns the caret into a selection. Split out of the search
	// box's own handler because the field added after it had none of them -- you could not put the
	// caret anywhere, and dragging selected nothing.
	//
	// Returns true when the event was the field's. `dragging` and `clickTime` are the caller's, one
	// pair per field, because two fields dragging through one flag would fight.
	bool FieldMouse( int type, int x, int y, int vx, int vy, int vw, int vh, zx::TextInput &field,
		int firstChar, bool &dragging, int &clickTime )
	{
		const bool bOver = ( x >= serverbrowser_ToScreenX( vx )) &&
			( x < serverbrowser_ToScreenX( vx + vw )) &&
			( y >= serverbrowser_ToScreenY( vy )) &&
			( y < serverbrowser_ToScreenY( vy + vh ));

		if ( bOver && ( type == MOUSE_Click ))
		{
			const int now = static_cast<int>( DMenu::MenuTime );
			const bool bDouble = (( now - clickTime ) < 15 );
			clickTime = now;

			if ( bDouble )
			{
				// Double-click takes the word under the pointer, or everything when there is no word
				// there. No drag afterwards: a second press that started selecting again would undo
				// what the player just asked for before they let go.
				field = zx::SelectWordOrAll( field, FieldCharAt( field, vx, firstChar, x ));
				dragging = false;
			}
			else
			{
				dragging = zx::BeginDrag( );
				field = zx::SetCaret( field, FieldCharAt( field, vx, firstChar, x ), bShiftHeld( ));
			}

			return true;
		}

		// Same unit, same rule as the search box. Two fields sharing one drag rule is the point.
		const zx::DragOutcome drag = zx::StepDrag( dragging, PointerEventOf( type ));
		dragging = drag.dragging;

		if ( drag.consumed )
		{
			field = zx::SetCaret( field, FieldCharAt( field, vx, firstChar, x ), true );
			return true;
		}

		return bOver;
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

		// [rc4l] On the hosting tab, DOWN off anything that is not the form enters the form.
		//
		// ComputeNav cannot answer this: it does not know which tab is showing, so from the search
		// box it sends DOWN to the rows, finds none on this tab, and falls back to the tabs -- which
		// looks like the key jumping sideways onto the HOST button instead of going down.
		//
		// Checked for every origin rather than just the tabs, because the search box shares that row
		// and is just as much "above the form" as they are.
		// [rc4l] DOWN off the SECOND ROW enters whichever screen the tab is showing.
		//
		// From the second row and not from the first, which is a change: this used to fire from any
		// origin, on the reasoning that everything above the form was equally "above the form". That
		// was true while hosting had no sub-tab row -- and the moment it got one, DOWN from the tabs
		// flew straight past it into the form and the sub-tabs became unreachable by keyboard. You
		// could not get to CUSTOM or NEW without a mouse.
		//
		// CUSTOM is left out because it has no screen to enter yet.
		const bool bAboveTheForm = ( g_Focus == zx::BrowserFocus::SubTabs ) ||
			( g_Focus == zx::BrowserFocus::Search );

		if (( g_Tab == BrowserTab::Host ) && ( g_HostKind != HostKind::Custom )
			&& ( key == zx::NavKey::Down ) && bAboveTheForm )
		{
			SetFocus( zx::BrowserFocus::Host );

			// Each screen has its own idea of where the keyboard should land first.
			if ( g_HostKind == HostKind::New )
			{
				g_NewFocus = NewFocus::Wads;
			}
			else
			{
				g_HostFocus = zx::HostFocusPos( zx::HostSlot::List, 0 );

				// [rc4l] Arrive on the experience's own HEADING, not its default way-of-playing, so
				// the first Enter OPENS it (revealing Sunder/HR2/etc.) exactly as a click on the row
				// does. Only meaningful for a collapsed multi-variant entry; the Enter handler falls
				// back to hosting for anything else, so this is safe for single-experience rows too.
				g_HostOnEntryRow = true;
			}

			S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
			return true;
		}

		// [rc4l] ALL FOUR arrows, through computation/hostfocus_compute.
		//
		// Up and down used to be the only two handled here, and left and right were special-cased for
		// the visibility row right above them -- so the list could not be reached at all and the
		// settings toggle had no keyboard. What each key means is now one answer from one place, the
		// way the rest of the browser gets its answer from browserfocus_compute.
		if (( g_Focus == zx::BrowserFocus::Host ) && ( g_HostKind == HostKind::New ))
			return NewNavigate( key );

		if (( g_Focus == zx::BrowserFocus::Host ) && ( g_HostKind == HostKind::Custom ))
			return CustomNavigate( key );

		if ( g_Focus == zx::BrowserFocus::Host )
		{
			switch ( key )
			{
			case zx::NavKey::Up:	NavigateHostFocus( zx::HostNavKey::Up ); break;
			case zx::NavKey::Down:	NavigateHostFocus( zx::HostNavKey::Down ); break;
			case zx::NavKey::Left:	NavigateHostFocus( zx::HostNavKey::Left ); break;
			case zx::NavKey::Right:	NavigateHostFocus( zx::HostNavKey::Right ); break;
			}
			return true;
		}

		// [rc4l] WHERE BOTH ROWS STAND, not just the top one. A position and a count per row is what
		// lets the unit answer left and right for either of them, and `subCount` of zero is how it is
		// told that PLAY has no sub-tab row to walk into at all.
		const zx::NavWhere where( total > 0, static_cast<int>( g_Tab ), kTabCount,
			SubTabIndex( ), SubTabCount( ),
			g_Selected <= 0 ); // at the first row (or none) -> Up leaves the list for the filter above

		zx::NavResult nav = zx::ComputeNav( g_Focus, key, where );

		// [rc4l] Off the end of the sub-tabs is the search box, which only BROWSE has. The unit is
		// told where the rows stand and not which tab they belong to, so it cannot know that; on the
		// hosting row, RIGHT off the last pill stays where it is rather than moving the caret into a
		// box that is not on screen.
		if (( g_Tab != BrowserTab::Browse ) && ( nav.focus == zx::BrowserFocus::Search ))
			nav.focus = zx::BrowserFocus::SubTabs;

		const zx::BrowserFocus was = g_Focus;
		SetFocus( nav.focus );

		if ( nav.tabStep != 0 )
		{
			// A step along the row, not a flip: ComputeNav has already refused to step off either end,
			// so a non-zero step always has somewhere to land.
			const int next = static_cast<int>( g_Tab ) + nav.tabStep;
			if (( next >= 0 ) && ( next < kTabCount ))
				SelectTab( static_cast<BrowserTab>( next ));
			return true;
		}

		if ( nav.subStep != 0 )
		{
			SelectSubTabIndex( SubTabIndex( ) + nav.subStep );
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
	// [rc4l] Up off the browser's own top row goes to the global tab bar, exactly as it does from a
	// stock menu. The browser's tab row IS its top row, so this is the one focus zone that answers
	// yes; everything else in the browser has somewhere of its own to go up to.
	//
	// Dialog is deliberately not included. A modal that the arrows can walk out of is not modal.
	//
	bool AtTopRow( )
	{
		return ( g_Focus == zx::BrowserFocus::Tabs );
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

		// [rc4l] While a dialog is up it is the only thing that answers. Arrows walk the buttons, Enter
		// presses the focused one, and Escape resolves to the SAFE choice rather than the focused one
		// -- see computation/dialog_compute for why that distinction is worth a function.
		if ( g_Dialog.open )
		{
			switch ( mkey )
			{
			case MKEY_Left:
			case MKEY_Up:
				g_Dialog.focus = zx::ComputeDialogFocus( g_Dialog.focus, g_Dialog.count, zx::DialogKey::Left );
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
				break;

			case MKEY_Right:
			case MKEY_Down:
				g_Dialog.focus = zx::ComputeDialogFocus( g_Dialog.focus, g_Dialog.count, zx::DialogKey::Right );
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
				break;

			case MKEY_Enter:
				AnswerDialog( g_Dialog.focus );
				break;

			case MKEY_Back:
				{
					const int out = zx::ComputeDialogEscape( g_Dialog.cancelIndex, g_Dialog.count );
					if ( out >= 0 )
						AnswerDialog( out );
				}
				break;

			default:
				break;
			}

			return true;
		}

		// [rc4l] The CUSTOM tab answers its own Enter, for the same reason the NEW screen does: it
		// never reaches Responder, and falling through would act on the PRESETS panel's idea of what
		// is selected.
		// The read-only map list: the arrows scroll it, Escape and Enter close it. Nothing in it can
		// be changed, so there is nothing else for a key to do.
		if (( g_Tab == BrowserTab::Host ) && ( g_HostKind == HostKind::Custom ) && g_CustomMapsOpen )
		{
			const zx::CustomEntry *const chosen = CustomSelected( );
			const int lines = ( chosen != NULL ) ? static_cast<int>( chosen->maps.size( )) : 0;
			const int visible = MAX( 1,
				( NewBigButtonTop( ) - 8 - NewBigContentTop( )) / SB_NEW_ROW_H );

			if (( mkey == MKEY_Back ) || ( mkey == MKEY_Enter ))
			{
				g_CustomMapsOpen = false;
				return true;
			}

			if (( mkey == MKEY_Up ) || ( mkey == MKEY_Down ))
			{
				g_CustomMapsScroll = zx::ClampScroll(
					g_CustomMapsScroll + (( mkey == MKEY_Up ) ? -1 : 1 ),
					MAX( 0, lines - visible ));
			}

			return true;		// everything else is swallowed rather than reaching the tab behind
		}

		if (( g_Tab == BrowserTab::Host ) && ( g_HostKind == HostKind::Custom ) &&
			( g_Focus == zx::BrowserFocus::Host ) && ( mkey == MKEY_Enter ))
		{
			if ( CustomEntries( ).empty( ))
			{
				// The one button there is.
				SelectSubTabIndex( static_cast<int>( HostKind::New ));
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
				return true;
			}

			if ( g_CustomFocus == CustomFocus::Buttons )
				CustomPressButton( g_CustomBtnSel );
			else
				CustomPlay( );		// on a row, Enter is what the row is for

			return true;
		}

		// [rc4l] THE NEW SCREEN ANSWERS ITS OWN ENTER, before the generic one below can.
		//
		// Enter is translated into MKEY_Enter and delivered HERE -- it never reaches Responder, and
		// so never reached the handler on the NEW screen at all. It fell through to "act on whatever
		// has focus", which on that screen meant the hosting form's idea of focus, and pressing
		// Enter on a wad started the experience that happened to be selected over on PRESETS.
		//
		// Escape belongs to the chooser too while it is up: the browser's own Escape closes the
		// whole menu, which is the wrong size of exit from a box you opened to look at a list.
		if (( g_Tab == BrowserTab::Host ) && ( g_HostKind == HostKind::New ) &&
			( g_Focus == zx::BrowserFocus::Host ))
		{
			// The map list: the arrows walk it, Enter takes a map out, Escape closes it.
			if ( g_NewModal == NewModal::Maps )
			{
				if ( mkey == MKEY_Back )
				{
					g_NewModal = NewModal::None;
					return true;
				}

				// [rc4l] RESET, by the key that means clear everywhere else in this engine.
				//
				// MKEY_Clear is BACKSPACE, not Delete -- menu.cpp maps GK_BACKSPACE to it, and the
				// tooltip says so for the same reason this comment does: guessing it was Delete is
				// exactly what made the first attempt do nothing at all.
				//
				// DONE has no key of its own here because Escape IS it, so the button beside it
				// needs one rather than a focus slot nothing else on this box uses.
				if ( mkey == MKEY_Clear )
				{
					NewAskReset( );
					return true;
				}

				return NewMapsMenuKey( mkey );
			}

			// [rc4l] The settings boxes: the arrows walk them, Enter presses what is under the
			// cursor, Escape closes them. Answered here for the reason the chooser is -- these keys
			// never reach Responder at all.
			if (( g_NewModal == NewModal::Flags ) || ( g_NewModal == NewModal::Gameplay ))
			{
				if ( mkey == MKEY_Back )
				{
					EndSettingEdit( );
					g_NewModal = NewModal::None;
					g_NewFlagEditing = -1;
					return true;
				}

				// Same key as the map list's, and only on the box that has the button: GAMEPLAY
				// shares this branch and has no reset, its numbers being re-applied by the mode.
				//
				// A number field being edited never gets here: while one has the caret the raw keys
				// go to it and Delete is a character deletion, not a menu key.
				if (( mkey == MKEY_Clear ) && NewBoxHasReset( ))
				{
					NewAskReset( );
					return true;
				}

				if ( BoxMenuKey( mkey ))
					return true;

				// Everything else is swallowed rather than reaching the screen behind it.
				return true;
			}

			if ( g_NewModal == NewModal::Iwad )
			{
				if ( mkey == MKEY_Enter )
				{
					NewCloseIwadModal( true );
					return true;
				}
				if ( mkey == MKEY_Back )
				{
					NewCloseIwadModal( false );
					return true;
				}
			}
			else if ( mkey == MKEY_Enter )
			{
				if ( g_NewFocus == NewFocus::Wads )

				{
					NewAddSelected( );
					return true;
				}
				if ( g_NewFocus == NewFocus::Iwads )
				{
					NewOpenIwadModal( );
					return true;
				}
				if ( g_NewFocus == NewFocus::Tools )
				{
					NewOpenTool( g_NewToolSel );
					return true;
				}
				if ( g_NewFocus == NewFocus::Buttons )
				{
					if ( g_NewButtonSel == 0 )
						NewOpenSaveModal( );
					else
						NewStartHosting( );

					return true;
				}

				// [rc4l] On the load order, Enter presses the button the cursor is on.
				//
				// It was deliberately nothing, on the reasoning that no single act is the obvious
				// meaning of "enter" for a file in a list. True while the buttons could not be
				// reached at all; now that left and right walk them, Enter has exactly one meaning
				// again -- press this one.
				if ( g_NewFocus == NewFocus::Order )
				{
					if ( g_NewOrderBtnSel == 0 )
						NewRemoveSelected( );
					else
						NewMoveSelected(( g_NewOrderBtnSel == 1 ) ? -1 : 1 );

					return true;
				}
			}
		}

		switch ( mkey )
		{
		case MKEY_Up:		return Navigate( zx::NavKey::Up, total );
		case MKEY_Down:		return Navigate( zx::NavKey::Down, total );
		case MKEY_Left:		return Navigate( zx::NavKey::Left, total );
		case MKEY_Right:	return Navigate( zx::NavKey::Right, total );

		// [rc4l] Escape LEAVES, rather than stepping back to whatever menu happened to open this one.
		//
		// The browser is a destination, not a stop on the way to the main menu: the player got here
		// from the tab bar, which is above every menu and belongs to none of them, so "one screen up"
		// has no meaning to answer with. Backing out of the last thing you opened should put you back
		// in the game, and a dialog on top of the browser still eats its own Escape first, so the
		// step-at-a-time behaviour survives exactly where there is a step to take.
		case MKEY_Back:
			M_ClearMenus( );
			S_Sound( CHAN_VOICE | CHAN_UI, "menu/clear", snd_menuvolume, ATTN_NONE );
			return true;

		// [rc4l] Enter acts on whatever has focus. On the tabs that is the tab -- which is already
		// selected, so it is the way into the list without reaching for Down.
		case MKEY_Enter:
			// [rc4l] On the hosting form, enter presses whatever the form is offering: from a field
			// it walks on, from the button it starts the server. Enter in a field meaning "start"
			// would fire the moment somebody finished typing a name, which is not what finishing
			// typing a name means.
			if ( g_Focus == zx::BrowserFocus::Host )
			{
				// [rc4l] Enter acts on whatever the glow is sitting on, which is now something this
				// can simply ask. It used to guess -- "the button, or the only control at the foot"
				// -- and got the toggle wrong, because the toggle had no keyboard to be on.
				ClampHostFocus( );

				if ( HostOnButton( ))
					PressHostAction( );
				else if ( HostOnToggle( ))
					PressHostSettingsToggle( );
				else if ( HostOnList( ))
				{
					// [rc4l] On an UNOPENED entry that offers ways of playing, Enter OPENS it to reveal
					// them -- exactly as clicking the row does -- instead of hosting its default and
					// leaving Sunder/HR2/etc. reachable only by mouse. On a variant row, or an entry
					// with no variants, Enter still means "host this one".
					const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( );
					if ( g_HostOnEntryRow && ( g_HostEntrySel >= 0 )
						&& ( g_HostEntrySel < static_cast<int>( entries.size( )))
						&& !entries[g_HostEntrySel].addon.variants.empty( )
						&& !HostEntryIsOpen( g_HostEntrySel ))
					{
						HostToggleEntryOpen( g_HostEntrySel );
						g_HostOnEntryRow = false; // cursor drops to the default variant, as the click does
						S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
					}
					else
						PressHostAction( );		// the list's enter is "host this one"
				}
				else if ( HostOnVisibility( ))
					PressHostVisibility( );
				else if ( g_HostFocus.slot == zx::HostSlot::Copy )
					HostPressCopy( );
				else
					NavigateHostFocus( zx::HostNavKey::Down );
				return true;
			}


			// [rc4l] The refresh button, which until now had no keyboard route to press it at all.
			// Same calls the click makes, so the two ways of pressing it cannot come to mean
			// different things.
			//
			// It used to be RefreshListedServers alone while the click also asked the registry, so
			// the comment above was already claiming a sameness that was not there: pressing REFRESH
			// by keyboard re-checked the servers you already had and could never find a new one.
			if ( g_Focus == zx::BrowserFocus::Refresh )
			{
				// The floor lives in PressRefresh, and so does the refusal sound, so the "chose it"
				// noise is only played when something was actually chosen.
				const bool bAllowed = ( RefreshWaitSeconds( ) == 0 );
				PressRefresh( );
				if ( bAllowed )
					S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
				return true;
			}

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
			else if ( g_Focus == zx::BrowserFocus::Rows )
			{
				// [rc4l] Enter on a row COMMITS TO THE ROW, it does not join. Focus moves to the button
				// and a second Enter presses it.
				//
				// Same shape as the mouse, which has always taken one click to look and two to commit:
				// joining is a minutes-long download and a reload of the whole game, and a single
				// keystroke that starts all of that from a list you are still arrowing through is one
				// fumbled keypress away from a mistake you cannot take back. The second press is also
				// where the button gets to say CANCEL instead of JOIN, so what is about to happen is on
				// screen before it happens.
				if (( g_Selected >= 0 ) && ( g_Selected < total ))
				{
					SetFocus( zx::BrowserFocus::Action );
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
