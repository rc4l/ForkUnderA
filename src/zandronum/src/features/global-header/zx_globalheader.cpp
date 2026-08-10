// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "zx_globalheader.h"

#include "doomtype.h"
#include "v_video.h"
#include "v_text.h"
#include "v_font.h"
#include "network.h"
#include "menu/menu.h"

#include "features/global-header/computation/globalheader_compute.h"
#include "features/global-header/computation/headerreach_compute.h"
#include "features/server-browser/browser.h"
#include "features/server-browser/zx_joinserver.h" // IsServerBrowserOpen: which tab is lit
#include "features/server-browser/computation/registrystatus_compute.h"
#include "features/server-browser/computation/tooltip_compute.h"
// The rounded-panel gradient the update notice, the link prompt and the browser's own chrome all
// share. The bar uses it too, so there is one look to adjust rather than four.
#include "features/updater/computation/promptpanel_compute.h"
// The focus orb and the curve it travels along, both shared with the server browser so the marker
// the player is following does not change shape halfway up the screen.
#include "features/menu-focus/zx_focusglow.h"
#include "features/server-browser/computation/glowtravel_compute.h"

#include "i_system.h"   // I_MSTime: the orb is timed in real milliseconds, not tics
#include "s_sound.h"

#include <string>
#include <vector>

namespace zx
{

namespace
{

// The browser's own virtual space, so the bar and the browser's chrome are drawn to one scale and a
// pill on the bar is the same size as a pill in the browser under it.
const int HEADER_VIRT_W = 640;
const int HEADER_VIRT_H = 400;

const char *const kTabLabels[kHeaderTabCount] = { "Main Menu", "Play Online!" };

// Where the keyboard is on the bar, and whether the bar owns it at all. The LIT tab is not stored
// here -- that is asked of the world in CurrentTab() -- this is only the cursor while somebody is
// walking along the row before they press anything.
int  g_FocusTab = 0;
bool g_HasFocus = false;

// Hover, in tab index, and the pointer that produced it. Kept in screen pixels because that is what
// the events carry and the tooltip has to be placed against the real screen.
int g_HotTab = -1;
int g_PointerX = -1;
int g_PointerY = -1;

// [rc4l] Where the focus orb actually IS, as against where it belongs. The browser's marker travels
// between positions rather than teleporting, and the bar is the same marker, so it travels too:
// walking from a menu up onto the bar and along it should be one continuous gesture, not a cursor
// that blinks out in one place and reappears in another.
GlowTravel g_GlowTravel;
GlowPos    g_GlowAt;
bool       g_GlowPlaced = false;
int        g_GlowLastMs = 0;

//*****************************************************************************
//
// [rc4l] Virtual coordinates to screen pixels.
//
// The same conversion the server browser uses, including its short-circuit: when the surface is
// exactly the virtual size, DTA_Virtual* draws 1:1 and never calls VirtualToRealCoords, which is
// NOT identity at 640x400. Reproducing the shortcut is what keeps the pills attached to the text
// sitting on them under vid_scalemode 1.
void ToScreenRaw( int vx, int vy, int vw, int vh, int &x, int &y, int &w, int &h )
{
	x = vx;
	y = vy;
	w = vw;
	h = vh;

	if (( screen->GetWidth( ) == HEADER_VIRT_W ) && ( screen->GetHeight( ) == HEADER_VIRT_H ))
		return;

	screen->VirtualToRealCoordsInt( x, y, w, h, HEADER_VIRT_W, HEADER_VIRT_H, false, true );
}

//*****************************************************************************
//
// [rc4l] How far to lift everything so the bar sits against the real top of the screen.
//
// VirtualToRealCoords centres the virtual space for the aspect it is handed, which is right for a
// panel and wrong for chrome. On a window taller than 16:10 it left a band of game above the bar,
// so the header floated, attached to nothing, with the back arrows sitting in the gap.
//
// Measured rather than derived: two conversions tell us where virtual 0 landed and how big a virtual
// unit is, which is the same trick ToVirtualY uses and holds for whatever the mapping does next.
// The answer is in VIRTUAL units on purpose. Biasing there moves the Dim rects and the DTA_Virtual
// text by one identical amount, which is what keeps a label on its pill; a screen-space nudge would
// move only the half of the drawing that goes through this function.
int TopBiasV( )
{
	int x0 = 0, y0 = 0, w0 = 0, h0 = 0;
	ToScreenRaw( 0, 0, 0, 0, x0, y0, w0, h0 );
	if ( y0 <= 0 )
		return 0;

	int x1 = 0, y1 = 0, w1 = 0, h1 = 0;
	ToScreenRaw( 0, 100, 0, 0, x1, y1, w1, h1 );
	if ( y1 <= y0 )
		return 0;

	// Rounded AWAY from zero, because coming up a pixel short leaves a sliver of game above the bar
	// and coming a pixel over costs nothing anybody can see.
	return -((( y0 * 100 ) + ( y1 - y0 ) - 1 ) / ( y1 - y0 ));
}

void ToScreen( int vx, int vy, int vw, int vh, int &x, int &y, int &w, int &h )
{
	ToScreenRaw( vx, vy + TopBiasV( ), vw, vh, x, y, w, h );
}

int ToScreenX( int vx )
{
	int x, y, w, h;
	ToScreen( vx, 0, 0, 0, x, y, w, h );
	return x;
}

int ToScreenY( int vy )
{
	int x, y, w, h;
	ToScreen( 0, vy, 0, 0, x, y, w, h );
	return y;
}

// Screen pixels back to virtual, by inverting the mapping through two known points rather than
// reimplementing it. Two conversions that agree only on some aspect ratios is the bug this avoids.
int ToVirtualX( int px )
{
	const int at0 = ToScreenX( 0 );
	const int at100 = ToScreenX( 100 );
	if ( at100 == at0 )
		return 0;

	return (( px - at0 ) * 100 ) / ( at100 - at0 );
}

int ToVirtualY( int py )
{
	const int at0 = ToScreenY( 0 );
	const int at100 = ToScreenY( 100 );
	if ( at100 == at0 )
		return 0;

	return (( py - at0 ) * 100 ) / ( at100 - at0 );
}

//*****************************************************************************
//
// The measured width of every label, which is what the layout is built from.
void MeasureLabels( int *out )
{
	for ( int i = 0; i < kHeaderTabCount; ++i )
		out[i] = SmallFont->StringWidth( kTabLabels[i] );
}

// [rc4l] Which tab is LIT, asked rather than remembered.
//
// The browser being open IS "Play Online" being current. A stored copy would be a second source of
// truth, and it goes stale the first time anything moves the player without driving the bar --
// Escape, a console command, a mod's own submenu all do exactly that.
HeaderTab CurrentTab( )
{
	return IsServerBrowserOpen( ) ? HeaderTab::PlayOnline : HeaderTab::MainMenu;
}

//*****************************************************************************
//
// [rc4l] What the registries have told us, folded into the three signals the verdict is made from.
//
// Any one registry answering is enough for proof, and any one still outstanding is enough to keep
// the answer at Checking, which is why these are ORs across the whole list rather than a look at
// the first entry.
HeaderReach Reach( )
{
	ReachIn in;

	const unsigned int count = BROWSER_GetServerRegistryCount( );
	for ( unsigned int i = 0; i < count; ++i )
	{
		std::string host;
		int port = 0;
		RegistryStatus status = RegistryStatus::Pending;

		if ( !BROWSER_GetServerRegistryStatus( i, host, port, status ))
			continue;

		if ( status == RegistryStatus::Ok )
			in.anyRegistryAnswered = true;
		else if ( status == RegistryStatus::Pending )
			in.anyRegistryPending = true;
	}

	// A local address that is not loopback means there is a network attached, whether or not
	// anything on the far side of it has spoken to us. That is exactly the LAN-only case.
	const NETADDRESS_s local = NETWORK_GetLocalAddress( );
	in.haveLocalNetwork = ( local.abIP[0] != 0 ) && ( local.abIP[0] != 127 );

	return ComputeHeaderReach( in );
}

// The pill's base colour for a verdict. Neutral answers false and leaves the ordinary grey-blue
// alone, because "still asking" is not a state worth colouring.
bool TintColor( ReachTint tint, int &r, int &g, int &b )
{
	switch ( tint )
	{
	case ReachTint::Green:
		r = 46; g = 160; b = 72;
		return true;

	case ReachTint::Orange:
		r = 190; g = 116; b = 32;
		return true;

	case ReachTint::Grey:
		r = 74; g = 74; b = 80;
		return true;

	default:
		return false;
	}
}

//*****************************************************************************
//
// [rc4l] One pill on the bar. The same oval as the browser's tabs, for the same reason: these
// switch what you are looking at, they are not another surface to put things on.
void DrawPill( const HeaderRect &r, const char *label, bool bLit, bool bHot, bool bFocused,
	ReachTint tint, bool bEnabled )
{
	const int left = ToScreenX( r.x );
	const int right = ToScreenX( r.x + r.w );
	const int top = ToScreenY( r.y );
	const int bottom = ToScreenY( r.y + r.h );

	const int w = right - left;
	const int h = bottom - top;
	if (( w <= 0 ) || ( h <= 0 ))
		return;

	const int radius = h / 2;

	// [rc4l] The keyboard cursor brightens the pill it is on, exactly as hovering does, and the orb
	// sits beside it. Both together, the way the browser's tabs read: the orb alone says WHERE the
	// cursor is, and the lift says WHICH TAB it is on, which are different questions when the pill
	// and the marker beside it are a few pixels apart.
	const bool bRaised = bHot || bFocused;

	int baseR = 0, baseG = 0, baseB = 0;
	if ( !TintColor( tint, baseR, baseG, baseB ))
	{
		const int base = bLit ? 96 : ( bRaised ? 62 : 38 );
		baseR = base;
		baseG = base;
		baseB = base + 24;
	}
	else if ( !bLit )
	{
		// A tinted tab that is not the current one is dimmed rather than recoloured, so the colour
		// still says what it says while the bar still shows where you are.
		baseR = ( baseR * ( bRaised ? 78 : 58 )) / 100;
		baseG = ( baseG * ( bRaised ? 78 : 58 )) / 100;
		baseB = ( baseB * ( bRaised ? 78 : 58 )) / 100;
	}

	const PanelColor topCol = { static_cast<BYTE>( baseR ), static_cast<BYTE>( baseG ),
		static_cast<BYTE>( baseB ), static_cast<BYTE>( bLit ? 235 : 190 ) };
	const PanelColor botCol = { static_cast<BYTE>( baseR / 2 ), static_cast<BYTE>( baseG / 2 ),
		static_cast<BYTE>( baseB / 2 + 12 ), static_cast<BYTE>( bLit ? 245 : 205 ) };

	for ( int row = 0; row < h; ++row )
	{
		const int inset = ComputeRoundedInset( row, h, radius );
		const int rowW = w - 2 * inset;
		if ( rowW <= 0 )
			continue;

		const PanelColor c = ComputePanelGradient( row, h, topCol, botCol );
		screen->Dim( PalEntry( c.r, c.g, c.b ), c.a / 255.f, left + inset, top + row, rowW, 1 );
	}

	// Biased to match the pill under it: DTA_Virtual* runs the raw mapping, so the label has to be
	// handed a y that has already been lifted the same way ToScreen lifts the rect.
	const int textY = r.y + TopBiasV( ) + ( r.h - SmallFont->GetHeight( )) / 2 + 1;
	const EColorRange textCol = !bEnabled ? CR_DARKGRAY : (( bLit || bRaised ) ? CR_WHITE : CR_GRAY );

	screen->DrawText( SmallFont, textCol,
		r.x + ( r.w / 2 ) - ( SmallFont->StringWidth( label ) / 2 ), textY, label,
		DTA_VirtualWidth, HEADER_VIRT_W, DTA_VirtualHeight, HEADER_VIRT_H, TAG_DONE );
}

//*****************************************************************************
//
// [rc4l] The hover tooltip, laid out and drawn in virtual space the same way the browser's is.
void DrawTooltip( const char *text )
{
	const std::vector<std::string> lines = TooltipLines( text );
	if ( lines.empty( ))
		return;

	const int padX = 5;
	const int padY = 3;
	const int lineH = SmallFont->GetHeight( ) + 1;

	int widest = 0;
	for ( size_t i = 0; i < lines.size( ); ++i )
	{
		const int lw = SmallFont->StringWidth( lines[i].c_str( ));
		if ( lw > widest )
			widest = lw;
	}

	const int boxW = widest + 2 * padX;
	const int boxH = static_cast<int>( lines.size( )) * lineH + 2 * padY;

	const TooltipBox box = ComputeTooltipPlacement( ToVirtualX( g_PointerX ),
		ToVirtualY( g_PointerY ), boxW, boxH, HEADER_VIRT_W, HEADER_VIRT_H, 10, 3 );

	const int left = ToScreenX( box.x );
	const int top = ToScreenY( box.y );
	const int right = ToScreenX( box.x + box.w );
	const int bottom = ToScreenY( box.y + box.h );

	screen->Dim( PalEntry( 18, 19, 26 ), 0.94f, left, top, right - left, bottom - top );

	screen->Dim( PalEntry( 120, 130, 165 ), 0.55f, left, top, right - left, 1 );
	screen->Dim( PalEntry( 120, 130, 165 ), 0.55f, left, bottom - 1, right - left, 1 );
	screen->Dim( PalEntry( 120, 130, 165 ), 0.55f, left, top, 1, bottom - top );
	screen->Dim( PalEntry( 120, 130, 165 ), 0.55f, right - 1, top, 1, bottom - top );

	// Same bias as the box it sits in, for the same reason as the pill labels.
	int y = box.y + TopBiasV( ) + padY;
	for ( size_t i = 0; i < lines.size( ); ++i )
	{
		screen->DrawText( SmallFont, ( i == 0 ) ? CR_WHITE : CR_GRAY, box.x + padX, y,
			lines[i].c_str( ), DTA_VirtualWidth, HEADER_VIRT_W, DTA_VirtualHeight, HEADER_VIRT_H,
			TAG_DONE );
		y += lineH;
	}
}

//*****************************************************************************
//
// [rc4l] The focus orb, in the margin to the left of the focused pill.
//
// Beside the pill rather than around it, and the same offset the browser uses beside its own tabs,
// so the marker keeps its relationship to whatever it is marking as the player moves between the
// two. Drawn last, over the pills, because it is the answer to "where am I" and being half buried
// under the thing it points at is how a marker gets missed.
void DrawFocusOrb( const HeaderMetrics &m, const int *widths )
{
	if ( !g_HasFocus )
	{
		// Nothing focused, so there is nowhere for the next journey to set out FROM either.
		g_GlowPlaced = false;
		return;
	}

	const HeaderRect r = HeaderTabRect( m, widths, kHeaderTabCount, g_FocusTab );
	const GlowPos want( r.x - 5, r.y + r.h / 2 );

	// The first placement snaps. There is nowhere to have travelled from, and sliding in from a
	// position left over from the last time the bar had focus would be a lie about where it had been.
	if ( !g_GlowPlaced )
	{
		g_GlowAt = want;
		g_GlowTravel = BeginGlowTravel( want, want );
		g_GlowLastMs = static_cast<int>( I_MSTime( ));
		g_GlowPlaced = true;
	}
	else
	{
		const int now = static_cast<int>( I_MSTime( ));
		const int delta = now - g_GlowLastMs;
		g_GlowLastMs = now;

		// Set out again whenever the destination has moved, from wherever the orb has actually got
		// to. Passing the CURRENT point is what makes a change of mind mid-flight continue smoothly
		// instead of snapping back to where the last journey began.
		if (( g_GlowTravel.to.x != want.x ) || ( g_GlowTravel.to.y != want.y ))
			g_GlowTravel = BeginGlowTravel( g_GlowAt, want );

		g_GlowTravel = StepGlowTravel( g_GlowTravel, delta );
		g_GlowAt = GlowTravelPoint( g_GlowTravel );
	}

	// The bar's own scale: how many real pixels 100 of its virtual units cover. Measured over a long
	// span for the reason the browser measures it that way, see the note at its DrawFocusGlow.
	const int span = ToScreenX( 100 ) - ToScreenX( 0 );

	zx::DrawFocusGlow( ToScreenX( g_GlowAt.x ), ToScreenY( g_GlowAt.y ), span );
}

// [rc4l] Move the keyboard cursor along the bar, and SAY SO.
//
// The bar was silent while every other menu in the engine clicks, so walking onto it felt like the
// keyboard had stopped working. Only when the cursor actually moves: the ends of the bar clamp
// rather than wrap, and a sound on a keypress that changed nothing is a worse lie than silence.
bool StepFocus( int step )
{
	const int next = StepHeaderTab( g_FocusTab, kHeaderTabCount, step );
	if ( next == g_FocusTab )
		return true;   // consumed, the bar still owns the key; there is simply nowhere further to go

	g_FocusTab = next;
	S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
	return true;
}

// What the hovered tab should say. Play Online's tooltip is its verdict, because the colour raises
// the question and the tooltip is where the answer belongs.
const char *TooltipFor( int tab, HeaderReach reach )
{
	if ( tab == static_cast<int>( HeaderTab::PlayOnline ))
		return HeaderReachTooltip( reach );

	return "Single player, options and everything else";
}

} // namespace

//*****************************************************************************
//
void GlobalHeader_Draw( )
{
	const HeaderMetrics m = DefaultHeaderMetrics( );

	int widths[kHeaderTabCount];
	MeasureLabels( widths );

	// The bar's own surface: a gradient running dark at the bottom, so the menu below it reads as
	// being under something rather than merely starting lower down.
	const int barTop = ToScreenY( 0 );
	const int barBottom = ToScreenY( m.barH );
	const int barH = barBottom - barTop;

	const PanelColor barTopCol = { 26, 27, 38, 226 };
	const PanelColor barBotCol = { 12, 12, 19, 240 };

	for ( int row = 0; row < barH; ++row )
	{
		const PanelColor c = ComputePanelGradient( row, barH, barTopCol, barBotCol );
		screen->Dim( PalEntry( c.r, c.g, c.b ), c.a / 255.f, 0, barTop + row, screen->GetWidth( ), 1 );
	}

	// A bright hairline along the bottom edge. The bar is the thing people are meant to notice, and
	// an edge is what makes a surface look like one rather than a darker patch of screen.
	screen->Dim( PalEntry( 120, 140, 200 ), 0.75f, 0, barBottom - 1, screen->GetWidth( ), 1 );

	const HeaderReach reach = Reach( );
	const int lit = static_cast<int>( CurrentTab( ));

	for ( int i = 0; i < kHeaderTabCount; ++i )
	{
		const bool bOnline = ( i == static_cast<int>( HeaderTab::PlayOnline ));
		const ReachTint tint = bOnline ? HeaderReachTint( reach ) : ReachTint::Neutral;
		const bool bEnabled = !bOnline || PlayOnlineSelectable( reach );

		DrawPill( HeaderTabRect( m, widths, kHeaderTabCount, i ), kTabLabels[i], ( i == lit ),
			( i == g_HotTab ), ( g_HasFocus && ( i == g_FocusTab )), tint, bEnabled );
	}

	DrawFocusOrb( m, widths );

	if ( g_HotTab >= 0 )
		DrawTooltip( TooltipFor( g_HotTab, reach ));
}

//*****************************************************************************
//
int GlobalHeader_ScreenBottom( )
{
	// Real pixels, not virtual: the callers are drawing chrome of their own against the bar's edge,
	// and they work in the screen's own coordinates rather than ours.
	return ToScreenY( DefaultHeaderMetrics( ).barH );
}

//*****************************************************************************
//
int GlobalHeader_MenuOffsetY( )
{
	return MenuClearanceY( DefaultHeaderMetrics( ));
}

void GlobalHeader_ShiftMenusDown( )
{
	const int off = GlobalHeader_MenuOffsetY( );

	TMap<FName, FMenuDescriptor *>::Iterator it( MenuDescriptors );
	TMap<FName, FMenuDescriptor *>::Pair *pair;

	while ( it.NextPair( pair ))
	{
		FMenuDescriptor *desc = pair->Value;
		if ( desc == NULL )
			continue;

		if ( desc->mType == MDESC_ListMenu )
		{
			FListMenuDescriptor *ld = static_cast<FListMenuDescriptor *>( desc );
			ld->mYpos += off;

			// The items carry their own absolute y, so moving the descriptor alone would move the
			// caret and leave every row where it was.
			for ( unsigned i = 0; i < ld->mItems.Size( ); ++i )
				ld->mItems[i]->OffsetPositionY( off );
		}
		else if ( desc->mType == MDESC_OptionsMenu )
		{
			FOptionMenuDescriptor *od = static_cast<FOptionMenuDescriptor *>( desc );

			// [rc4l] mPosition carries two meanings in one int, and they move opposite ways.
			//
			// Positive is a literal y. Zero or negative means "work it out from the title", and the
			// drawer NEGATES it to get there -- so making an auto-positioned menu start lower means
			// subtracting, not adding. Getting this backwards moves the title menus UP, into the
			// exact overlap the shift exists to prevent.
			if ( od->mPosition > 0 )
				od->mPosition += off;
			else
				od->mPosition -= off;
		}
	}
}

bool GlobalHeader_HasFocus( )
{
	return g_HasFocus;
}

void GlobalHeader_TakeFocus( )
{
	// Land on the tab that is lit. Arriving anywhere else would mean the cursor appears somewhere
	// the player was not already looking.
	g_FocusTab = static_cast<int>( CurrentTab( ));
	g_HasFocus = true;
}

void GlobalHeader_ReleaseFocus( )
{
	g_HasFocus = false;
}

//*****************************************************************************
//
bool GlobalHeader_NavLeft( )
{
	if ( !g_HasFocus )
		return false;

	return StepFocus( -1 );
}

bool GlobalHeader_NavRight( )
{
	if ( !g_HasFocus )
		return false;

	return StepFocus( +1 );
}

bool GlobalHeader_NavDown( )
{
	if ( !g_HasFocus )
		return false;

	// Down leaves the bar and hands the arrows back to whatever is under it.
	g_HasFocus = false;
	return true;
}

//*****************************************************************************
//
namespace
{

// [rc4l] Go where a tab leads, or do nothing if we are already there.
//
// Re-opening the menu you are already in would rebuild it under the player and throw away where
// they were in it, which is a real cost for a keypress that was meant to do nothing.
bool GoToTab( int tab )
{
	if ( tab == static_cast<int>( CurrentTab( )))
		return false;

	if ( tab == static_cast<int>( HeaderTab::PlayOnline ))
	{
		if ( !PlayOnlineSelectable( Reach( )))
			return false;

		M_SetMenu( NAME_Mainmenu, -1 );
		M_SetMenu( "ZA_Browser", -1 );
		return true;
	}

	M_SetMenu( NAME_Mainmenu, -1 );
	return true;
}

} // namespace

bool GlobalHeader_Activate( )
{
	if ( !g_HasFocus )
		return false;

	return GoToTab( g_FocusTab );
}

//*****************************************************************************
//
bool GlobalHeader_MouseMove( int screenX, int screenY )
{
	g_PointerX = screenX;
	g_PointerY = screenY;

	const HeaderMetrics m = DefaultHeaderMetrics( );
	int widths[kHeaderTabCount];
	MeasureLabels( widths );

	const int vx = ToVirtualX( screenX );
	const int vy = ToVirtualY( screenY );

	g_HotTab = HeaderTabAtPoint( m, widths, kHeaderTabCount, vx, vy );

	// The bar swallows the move over its whole surface, pill or not, so the menu underneath does not
	// highlight a row the pointer is nowhere near.
	return HeaderBarContains( m, vy );
}

bool GlobalHeader_MouseClick( int screenX, int screenY )
{
	const HeaderMetrics m = DefaultHeaderMetrics( );
	int widths[kHeaderTabCount];
	MeasureLabels( widths );

	const int vx = ToVirtualX( screenX );
	const int vy = ToVirtualY( screenY );

	if ( !HeaderBarContains( m, vy ))
		return false;

	const int tab = HeaderTabAtPoint( m, widths, kHeaderTabCount, vx, vy );
	if ( tab >= 0 )
	{
		// The pointer parks the keyboard cursor where it clicked, WITHOUT claiming the arrows. A
		// mouse user who clicks Play Online wants to be in the browser, not left holding a bar
		// that swallows the first arrow key they press once they get there.
		g_FocusTab = tab;
		GoToTab( tab );
	}

	// Consumed either way: a click on the bar's background is still a click on the bar.
	return true;
}

} // namespace zx
