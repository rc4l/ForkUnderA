// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "zx_globalheader.h"
#include "features/continue/zx_continue.h"

#include "doomtype.h"
#include "v_video.h"
#include "v_text.h"
#include "v_font.h"
#include "network.h"
#include "menu/menu.h"

#include "features/global-header/computation/globalheader_compute.h"
#include "features/global-header/computation/headerreach_compute.h"
#include "features/global-header/computation/menuresume_compute.h"
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

// The space the bar's metrics are written in. Not the space they are DRAWN in: see HeaderVirtW.
const int HEADER_LAYOUT_W = 640;
const int HEADER_LAYOUT_H = 400;

const char *const kTabLabels[kHeaderTabCount] = { "Main Menu", "Play Online!", "Continue" };

// [rc4l] Continue is the only tab that comes and goes, so everything the bar does has to ask how
// many tabs there are rather than assume. It is last in the enum and drawn first on the bar; see
// globalheader_compute.h on why those two orders are allowed to differ.
int TabCount( )
{
	return Continue_IsShown( ) ? kHeaderTabCount : kHeaderTabCountNoContinue;
}

int PinnedIndex( )
{
	return Continue_IsShown( ) ? static_cast<int>( HeaderTab::Continue ) : -1;
}

// Where the keyboard is on the bar, and whether the bar owns it at all. The LIT tab is not stored
// here -- that is asked of the world in CurrentTab() -- this is only the cursor while somebody is
// walking along the row before they press anything.
int  g_FocusTab = 0;
bool g_HasFocus = false;

// Whether the last menu session ended on the browser. See GlobalHeader_NoteMenusClosing.
bool g_ResumeBrowser = false;

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
// [rc4l] The virtual space to DRAW the bar in, sized so this screen's mapping comes out UNIFORM.
//
// The bar used to be drawn in a fixed 640x400 through VirtualToRealCoords, and that is the wrong
// tool for chrome. Virtual scaling fits a space to the window by stretching each axis into whatever
// is left of it, so on a wide window the pills came out squat and wide while the menu beside them
// kept its shape. That difference is not upstream's and not unavoidable: V_CalcCleanFacs
// deliberately forces CleanXfac and CleanYfac equal, and everything the stock menus draw goes
// through that one factor, which is exactly why none of it ever stretches.
//
// So the bar borrows the same factor. Asking for a virtual space of screen/(CleanXfac/2) makes
// VirtualToRealCoords scale by CleanXfac/2 on both axes with no centring at all, which is uniform by
// construction AND flush to the corner by construction. That second property replaced a pair of
// measured bias corrections that existed only to undo the centring afterwards.
//
// Half the Clean factor because the metrics are written in 640x400 and CleanXfac is for 320x200.
// zoomPercent is the dial: asking for FEWER virtual units across the same screen makes every unit
// cover more pixels, so the bar, its pills, their labels and the orb all grow together. Growing the
// metrics instead would have grown the pills and left everything drawn inside them behind.
int HeaderZoom( )
{
	const int z = DefaultHeaderMetrics( ).zoomPercent;
	return ( z > 0 ) ? z : 100;
}

int HeaderVirtW( )
{
	const int fac = ( CleanXfac > 0 ) ? CleanXfac : 1;
	const int v = ( screen->GetWidth( ) * 200 ) / ( fac * HeaderZoom( ));
	return ( v > 0 ) ? v : HEADER_LAYOUT_W;
}

int HeaderVirtH( )
{
	const int fac = ( CleanYfac > 0 ) ? CleanYfac : 1;
	const int v = ( screen->GetHeight( ) * 200 ) / ( fac * HeaderZoom( ));
	return ( v > 0 ) ? v : HEADER_LAYOUT_H;
}

//*****************************************************************************
//
// Virtual coordinates to screen pixels.
//
// The engine's own arithmetic for the non-aspect branch of VirtualToRealCoords, repeated rather than
// approximated, so a pill and the label drawn on it can never land a pixel apart. Widths come from
// the mapped far edge minus the mapped near one, the way the engine does it, so integer truncation
// cannot open a seam between two rectangles that share a boundary.
void ToScreen( int vx, int vy, int vw, int vh, int &x, int &y, int &w, int &h )
{
	const int sw = screen->GetWidth( ), sh = screen->GetHeight( );
	const int vW = HeaderVirtW( ), vH = HeaderVirtH( );

	x = vx * sw / vW;
	y = vy * sh / vH;
	w = ( vx + vw ) * sw / vW - x;
	h = ( vy + vh ) * sh / vH - y;
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
	for ( int i = 0; i < TabCount( ); ++i )
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
	// handed coordinates already pulled the same way ToScreen pulls the rect.
	const int textX = r.x + ( r.w / 2 ) - ( SmallFont->StringWidth( label ) / 2 );
	const int textY = r.y + ( r.h - SmallFont->GetHeight( )) / 2 + 1;
	const EColorRange textCol = !bEnabled ? CR_DARKGRAY : (( bLit || bRaised ) ? CR_WHITE : CR_GRAY );

	screen->DrawText( SmallFont, textCol, textX, textY, label,
		DTA_VirtualWidth, HeaderVirtW( ), DTA_VirtualHeight, HeaderVirtH( ),
		DTA_KeepRatio, true, TAG_DONE );
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
		ToVirtualY( g_PointerY ), boxW, boxH, HeaderVirtW( ), HeaderVirtH( ), 10, 3 );

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
	const int textX = box.x + padX;
	int y = box.y + padY;
	for ( size_t i = 0; i < lines.size( ); ++i )
	{
		screen->DrawText( SmallFont, ( i == 0 ) ? CR_WHITE : CR_GRAY, textX, y,
			lines[i].c_str( ), DTA_VirtualWidth, HeaderVirtW( ), DTA_VirtualHeight, HeaderVirtH( ),
			DTA_KeepRatio, true, TAG_DONE );
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
	// [rc4l] Drawn ONLY while the bar holds the arrows -- it is the keyboard cursor, not a permanent
	// "you are here" marker. Left on when unfocused it reads as stale: pressing Up lands focus on a tab
	// that was already wearing the orb, so nothing visibly changes and it is not clear the arrows now
	// belong to the bar. Off when unfocused, it snaps on at the focused tab the instant Up reaches the
	// bar -- which is exactly the change the player needs to see.
	if ( !g_HasFocus )
	{
		g_GlowPlaced = false; // next appearance snaps to the focused tab, not slid in from a stale spot
		return;
	}

	const int lit = g_FocusTab;

	const HeaderRect r = HeaderTabRect( m, widths, TabCount( ), lit, PinnedIndex( ) );
	// From the metrics, not a literal, so the padding that has to hold this orb is checked against
	// the same number the orb is drawn at.
	const GlowPos want( r.x - m.glowInset, r.y + r.h / 2 );

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
	const int next = StepHeaderTabPinned( g_FocusTab, TabCount( ), PinnedIndex( ), step );
	if ( next == g_FocusTab )
		return true;   // consumed, the bar still owns the key; there is simply nowhere further to go

	g_FocusTab = next;
	S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
	return true;
}

// [rc4l] The shipped metrics, with the bar's width filled in so the row of tabs can centre on it.
// Asked for in one place because the drawing and the two mouse handlers all have to be looking at
// the same row; a hit test built from a left-aligned copy would be a click that lands nowhere.
HeaderMetrics Metrics( )
{
	HeaderMetrics m = DefaultHeaderMetrics( );
	m.barW = HeaderVirtW( );
	return m;
}

// What the hovered tab should say. Play Online's tooltip is its verdict, because the colour raises
// the question and the tooltip is where the answer belongs.
const char *TooltipFor( int tab, HeaderReach reach )
{
	if ( tab == static_cast<int>( HeaderTab::PlayOnline ))
		return HeaderReachTooltip( reach );

	if ( tab == static_cast<int>( HeaderTab::Continue ))
		return "Pick up where you left off";

	return "Single player, options and everything else";
}

} // namespace

//*****************************************************************************
//
bool GlobalHeader_IsShown( )
{
	// [rc4l] Hidden on the JOIN FLOW and nowhere else: the menus a spectator gets for joining the
	// match they are already connected to. Those belong to the game rather than to choosing what to
	// play, and a Main Menu / Play Online strip over them turned backing out into a loop.
	//
	// NAMED, not inferred from "is there a netgame". The first attempt did that, and it took the bar
	// away from every menu in an online session including the main menu, which is still the main menu
	// whatever else is going on. Asked in ONE place so the drawing, the arrows and the pointer cannot
	// disagree about whether the bar exists: an invisible bar that still swallows Up is worse than a
	// visible one that misbehaves.
	const DMenu *menu = DMenu::CurrentMenu;
	if ( menu == NULL )
		return true;

	if ( !menu->IsKindOf( RUNTIME_CLASS( DOptionMenu )))
		return true;

	const FOptionMenuDescriptor *desc =
		static_cast<const DOptionMenu *>( menu )->GetDescriptor( );
	if ( desc == NULL )
		return true;

	// All three, because the flow walks between them: the plain join, the team picker, and the class
	// picker it can lead to. Showing the bar on one of the three would put the loop back for exactly
	// the players who reached the game a slightly different way.
	static const char *const kJoinFlow[] = {
		"ZA_JoinMenu", "ZA_JoinTeamMenu", "ZA_SelectClassMenu",
	};

	for ( size_t i = 0; i < ( sizeof kJoinFlow / sizeof kJoinFlow[0] ); ++i )
	{
		if ( desc->mMenuName == FName( kJoinFlow[i] ))
			return false;
	}

	return true;
}

void GlobalHeader_Draw( )
{
	if ( !GlobalHeader_IsShown( ))
	{
		// Nothing drawn, so nothing may be holding the arrows either.
		g_HasFocus = false;
		return;
	}

	// [rc4l] Which section the player is looking at, remembered every frame, so that opening the
	// menus again can put them back where they were.
	//
	// OBSERVED rather than hooked onto the teardown, which is where two attempts at this went wrong.
	// DMenu::Close moves CurrentMenu to the parent BEFORE it reaches M_ClearMenus, so a hook there
	// asks what is open once nothing is; and switching tabs replaces the menu without closing
	// anything at all, so a hook there never fires and the answer goes stale in the other direction.
	// The last frame that had a menu on it cannot be wrong about which menu that was.
	g_ResumeBrowser = ( CurrentTab( ) == HeaderTab::PlayOnline );

	const HeaderMetrics m = Metrics( );

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

	for ( int i = 0; i < TabCount( ); ++i )
	{
		const bool bOnline = ( i == static_cast<int>( HeaderTab::PlayOnline ));
		const ReachTint tint = bOnline ? HeaderReachTint( reach ) : ReachTint::Neutral;
		const bool bEnabled = !bOnline || PlayOnlineSelectable( reach );

		DrawPill( HeaderTabRect( m, widths, TabCount( ), i, PinnedIndex( ) ), kTabLabels[i], ( i == lit ),
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
	// A bar that is not there cannot hold the arrows. Without this, Up off the top row of an in-game
	// menu would hand them to something invisible and the menu would stop responding.
	if ( !GlobalHeader_IsShown( ))
		return;

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
bool GlobalHeader_ResumeBrowser( )
{
	// [rc4l] "Come back to the section you were in" means nothing where there are no sections to be
	// in. Without this the redirect still fired during an online session, which is the loop the bar
	// was hidden to stop: Escape closed the menu, the next Escape reopened it on the browser, and
	// leaving took as many presses as it took to walk back out.
	if ( !GlobalHeader_IsShown( ))
		return false;

	MenuResumeIn in;
	in.lastShown = g_ResumeBrowser ? MenuSection::Browser : MenuSection::MainMenu;

	// joinReady is deliberately NOT read here. It is a one-shot the caller consumes, and asking for
	// it twice is the bug that made Escape open the main menu over a browser it had just opened.
	in.joinReady = false;

	return ( ComputeMenuToOpen( in ) == MenuSection::Browser );
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

	const bool bOnline = ( tab == static_cast<int>( HeaderTab::PlayOnline ));

	if ( bOnline && !PlayOnlineSelectable( Reach( )))
		return false;

	// [rc4l] TORN DOWN FIRST, so the tab we are arriving at is not a CHILD of the tab we are leaving.
	//
	// M_SetMenu hangs the new menu off whatever is open at the time, and the bar is a switch between
	// siblings rather than a step deeper into anything. Parenting them is what made Escape on the
	// main menu land on the browser: the browser was its parent, so backing out of one WAS arriving
	// at the other. Nothing was reopening it, which is why every attempt to fix this by deciding what
	// to OPEN missed -- the menu had never been closed.
	// And opened WITHOUT the redirect, because we already know where we are going. Left on, it would
	// reopen the section we just closed and the tab actually clicked would be stacked on top of that
	// instead, which is the same parenting bug one step further along.
	M_ClearMenus( );
	M_StartControlPanel( false, false );

	if ( bOnline )
		M_SetMenu( "ZA_Browser", -1 );
	else
		M_SetMenu( NAME_Mainmenu, -1 );

	return true;
}

// [rc4l] Pressing a tab, by whichever route, and SAYING SO. One place, so the keyboard and the
// pointer cannot come to mean different things or make different noises.
//
// What it returns is whether the press was CONSUMED, which is not the same as whether it went
// anywhere. A refused press still has to be consumed, because the menu under the bar is listening to
// the same Enter: letting one through is how Enter on the bar used to start a new game.
bool PressTab( int tab, bool bDropFocus )
{
	// [rc4l] Continue is a button, not a place. It is never "where you are", so the already-here
	// branch below would never fire for it, and it must act before that branch reads CurrentTab and
	// decides it is somewhere to go. No confirmation: the decision was made when the button chose to
	// exist, and asking again is a second decision about the same thing.
	if ( tab == static_cast<int>( HeaderTab::Continue ))
	{
		if ( bDropFocus )
			g_HasFocus = false;

		S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
		Continue_Activate( );
		return true;
	}

	if ( tab == static_cast<int>( CurrentTab( )))
	{
		// Already here, so there is nothing to open and the useful thing left is to get out of the
		// way. A player pressing Enter on the tab they are already on is asking to be let into the
		// menu under it, which is exactly what Down does, so the two now agree. Re-opening the menu
		// would rebuild it underneath them and throw away where they had got to in it.
		if ( bDropFocus )
			g_HasFocus = false;

		S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
		return true;
	}

	if ( GoToTab( tab ))
	{
		S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
		return true;
	}

	// Somewhere we cannot go: Play Online with nothing reachable. Saying no out loud beats a tab that
	// looks pressable and then appears to do nothing at all.
	S_Sound( CHAN_VOICE | CHAN_UI, "menu/invalid", snd_menuvolume, ATTN_NONE );
	return true;
}

} // namespace

bool GlobalHeader_Activate( )
{
	if ( !g_HasFocus )
		return false;

	return PressTab( g_FocusTab, true );
}

//*****************************************************************************
//
bool GlobalHeader_MouseMove( int screenX, int screenY )
{
	if ( !GlobalHeader_IsShown( ))
		return false;

	g_PointerX = screenX;
	g_PointerY = screenY;

	const HeaderMetrics m = Metrics( );
	int widths[kHeaderTabCount];
	MeasureLabels( widths );

	const int vx = ToVirtualX( screenX );
	const int vy = ToVirtualY( screenY );

	g_HotTab = HeaderTabAtPoint( m, widths, TabCount( ), vx, vy, PinnedIndex( ) );

	// The bar swallows the move over its whole surface, pill or not, so the menu underneath does not
	// highlight a row the pointer is nowhere near.
	return HeaderBarContains( m, vy );
}

bool GlobalHeader_MouseClick( int screenX, int screenY )
{
	if ( !GlobalHeader_IsShown( ))
		return false;

	const HeaderMetrics m = Metrics( );
	int widths[kHeaderTabCount];
	MeasureLabels( widths );

	const int vx = ToVirtualX( screenX );
	const int vy = ToVirtualY( screenY );

	if ( !HeaderBarContains( m, vy ))
		return false;

	const int tab = HeaderTabAtPoint( m, widths, TabCount( ), vx, vy, PinnedIndex( ) );
	if ( tab >= 0 )
	{
		// The pointer parks the keyboard cursor where it clicked, WITHOUT claiming the arrows. A
		// mouse user who clicks Play Online wants to be in the browser, not left holding a bar
		// that swallows the first arrow key they press once they get there. Which is also why the
		// click never drops focus: it never took any.
		g_FocusTab = tab;
		PressTab( tab, false );
	}

	// Consumed either way: a click on the bar's background is still a click on the bar.
	return true;
}

} // namespace zx
