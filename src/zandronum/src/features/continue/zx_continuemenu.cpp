// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The picker behind the Continue pill: the last several things the player did, and which one
// to go back to.
//
// A card rather than a full screen, because it is a question with a short answer. The browser and
// the updater's notice already draw this shape, and it is drawn here from the same tested geometry
// (ComputeRoundedInset, ComputePanelGradient) rather than a second gradient that agrees with the
// first only until one of them is touched.
//
// THE SCROLLING IS NOT NEW EITHER. ComputeRowWindow, ComputeRestoredScroll, ComputeThumbHeight,
// ComputeThumbTop and ComputeFirstFromPointer are the server browser's, already unit-tested and
// already the answer to "which rows are on screen and where is the thumb". What is genuinely new is
// only the keyboard contract -- Home and End, which nothing else in this engine implements -- and
// that lives in computation/continuelist_compute where it can be asserted.
//
// TWO COLUMNS AND NO MORE. What it was, and when. A third column of detail would be a table to read
// rather than a list to point at, and the tooltip on the pill already says where one press goes.

#include "features/continue/zx_continue.h"

#include "features/continue/computation/continuelist_compute.h"
#include "features/menu-focus/zx_focusglow.h"
#include "features/server-browser/computation/scrollbar_compute.h"
#include "features/server-browser/computation/scrollview_compute.h"
#include "features/server-browser/computation/serverbrowser_compute.h"
#include "features/updater/computation/promptpanel_compute.h"

#include "c_dispatch.h"
#include "d_event.h"
#include "d_gui.h"
#include "menu/menu.h"
#include "s_sound.h"
#include "v_font.h"
#include "v_palette.h"
#include "v_video.h"
#include "zstring.h"

namespace
{

// [rc4l] The layout is written in a 640x400 space and stretched to whatever the window is, exactly
// as the browser's is. The virtual space is given the SCREEN'S aspect rather than a fixed one, so
// the mapping below is a plain scale and a rectangle cannot land a pixel away from its neighbour.
const int kLayoutW = 640;
const int kLayoutH = 400;

void VirtSize( int &vw, int &vh )
{
	const int sw = screen->GetWidth( );
	const int sh = screen->GetHeight( );

	if (( sw <= 0 ) || ( sh <= 0 ))
	{
		vw = kLayoutW;
		vh = kLayoutH;
		return;
	}

	if (( sw * kLayoutH ) <= ( sh * kLayoutW ))
	{
		// Width runs out first: the layout spans the window and the space is taller than 400.
		vw = kLayoutW;
		vh = ( sh * kLayoutW ) / sw;
	}
	else
	{
		vw = ( sw * kLayoutH ) / sh;
		vh = kLayoutH;
	}
}

int VirtW( ) { int vw = 0, vh = 0; VirtSize( vw, vh ); return vw; }
int VirtH( ) { int vw = 0, vh = 0; VirtSize( vw, vh ); return vh; }

int ToScreenX( int vx )
{
	int vw = 0, vh = 0;
	VirtSize( vw, vh );
	return ( vw > 0 ) ? ( vx * screen->GetWidth( ) / vw ) : 0;
}

int ToScreenY( int vy )
{
	int vw = 0, vh = 0;
	VirtSize( vw, vh );
	return ( vh > 0 ) ? ( vy * screen->GetHeight( ) / vh ) : 0;
}

// [rc4l] Screen pixels back to virtual ones, DERIVED from the forward mapping rather than written
// out a second time: evaluated at two points it recovers the scale exactly, so the two cannot
// disagree the first time either is touched.
int ToVirtualX( int px )
{
	const int at0 = ToScreenX( 0 ), at100 = ToScreenX( 100 );
	return ( at100 != at0 ) ? ((( px - at0 ) * 100 ) / ( at100 - at0 )) : 0;
}

int ToVirtualY( int py )
{
	const int at0 = ToScreenY( 0 ), at100 = ToScreenY( 100 );
	return ( at100 != at0 ) ? ((( py - at0 ) * 100 ) / ( at100 - at0 )) : 0;
}

// The card, in virtual units.
const int kCardW = 460;
const int kRowH = 13;
const int kMaxVisibleRows = 12;		// beyond this the card would fill the window; it scrolls instead
const int kPadX = 14;
const int kWhenColumnW = 96;
const int kScrollbarW = 4;

struct Layout
{
	int cardX, cardY, cardW, cardH;
	int listX, listY, listW;
	int rows;						// how many fit on screen
	int total;
};

Layout Measure( int total )
{
	Layout out;
	out.total = total;

	const int vw = VirtW( ), vh = VirtH( );

	out.cardW = ( kCardW < vw - 40 ) ? kCardW : ( vw - 40 );
	if ( out.cardW < 160 )
		out.cardW = 160;			// a window too narrow for the card still gets a card

	// The list is at most kMaxVisibleRows tall, and shorter when there is less to show: a card sized
	// for twelve rows with three in it is a box of empty space with a list at the top of it.
	const int fits = zx::ComputeContinueVisibleRows( kMaxVisibleRows * kRowH, kRowH );
	out.rows = ( total < fits ) ? total : fits;
	if ( out.rows < 1 )
		out.rows = 1;

	const int headerH = 46;			// title, column headings and the rule under them
	const int footerH = 22;			// the key hints

	out.cardH = headerH + ( out.rows * kRowH ) + footerH;
	out.cardX = ( vw - out.cardW ) / 2;
	out.cardY = ( vh - out.cardH ) / 2;

	out.listX = out.cardX + kPadX;
	out.listY = out.cardY + headerH;
	out.listW = out.cardW - ( 2 * kPadX );

	return out;
}

void DimClipped( PalEntry colour, float alpha, int x, int y, int w, int h )
{
	if (( w <= 0 ) || ( h <= 0 ))
		return;

	screen->Dim( colour, alpha, x, y, w, h );
}

// Same rounded gradient as the browser and the update notice, from the same tested geometry.
void DrawRoundedPanel( int vx, int vy, int vw, int vh, const zx::PanelColor &topCol,
	const zx::PanelColor &botCol, int vradius )
{
	const int left = ToScreenX( vx ), right = ToScreenX( vx + vw );
	const int top = ToScreenY( vy ), bottom = ToScreenY( vy + vh );
	const int radius = ToScreenY( vradius ) - ToScreenY( 0 );

	const int w = right - left, h = bottom - top;
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

void DrawTextAt( EColorRange colour, int vx, int vy, const char *text )
{
	screen->DrawText( SmallFont, colour, vx, vy, text,
		DTA_VirtualWidth, VirtW( ), DTA_VirtualHeight, VirtH( ), DTA_KeepRatio, true, TAG_DONE );
}

// [rc4l] The label, cut to fit its column with an ellipsis rather than run under the next one.
//
// Measured against the font rather than a character count: SmallFont is proportional, so "MAP01 in
// WWWWWW.wad" and "MAP01 in iiiiii.wad" are not the same width and a count would clip one of them
// early and let the other overrun.
FString Ellipsised( const char *text, int maxWidth )
{
	FString out = text;
	if ( SmallFont->StringWidth( out ) <= maxWidth )
		return out;

	const int dots = SmallFont->StringWidth( "..." );

	while (( out.Len( ) > 0 ) && ( SmallFont->StringWidth( out ) + dots > maxWidth ))
		out.Truncate( out.Len( ) - 1 );

	out += "...";
	return out;
}

} // namespace

//=============================================================================
//
// [rc4l] DFUAContinueMenu -- the list itself.
//
//=============================================================================

class DFUAContinueMenu : public DMenu
{
	DECLARE_CLASS( DFUAContinueMenu, DMenu )

public:
	DFUAContinueMenu( )
		: mSelected( 0 ), mFirst( 0 ), mHot( -1 )
	{
	}

	void Drawer( );
	bool MenuEvent( int mkey, bool fromcontroller );
	bool Responder( event_t *ev );
	bool MouseEvent( int type, int x, int y );

private:
	int mSelected;
	int mFirst;						// the row at the top of the window
	int mHot;						// the row under the pointer, or -1

	int Total( ) const { return zx::Continue_HistoryCount( ); }

	void Step( zx::ContinueListKey key );
	void Activate( );
	void Forget( );
	int RowAt( int vx, int vy ) const;
	void DrawRows( const Layout &layout );
	void DrawScrollbar( const Layout &layout );
};

IMPLEMENT_CLASS( DFUAContinueMenu )

//=============================================================================
//
void DFUAContinueMenu::Step( zx::ContinueListKey key )
{
	const int total = Total( );
	const int was = mSelected;

	mSelected = zx::StepContinueList( key, mSelected, total, Measure( total ).rows );

	if ( mSelected != was )
		S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );

	// [rc4l] The row the player is on is asked about, and only then. Fifty servers queried the moment
	// a menu opened would be a storm sent on somebody else's behalf.
	zx::Continue_ProbeEntry( mSelected );
}

void DFUAContinueMenu::Activate( )
{
	if ( Total( ) <= 0 )
		return;

	S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );

	// Closes the menus itself, and on the path that works it does not return: the WAD reload throws.
	zx::Continue_ActivateEntry( mSelected );
}

void DFUAContinueMenu::Forget( )
{
	if ( Total( ) <= 0 )
		return;

	zx::Continue_ForgetEntry( mSelected );
	S_Sound( CHAN_VOICE | CHAN_UI, "menu/clear", snd_menuvolume, ATTN_NONE );

	// [rc4l] The list just got shorter under the cursor. Pulled back in here rather than left for
	// the next keypress, because the DRAWING is what happens next and it would otherwise paint a
	// highlight on a row that is not there.
	mSelected = zx::ComputeClampedSelection( mSelected, Total( ));

	// Nothing left to choose between: the menu has answered its own question.
	if ( Total( ) <= 0 )
		Close( );
}

//=============================================================================
//
bool DFUAContinueMenu::MenuEvent( int mkey, bool fromcontroller )
{
	switch ( mkey )
	{
	case MKEY_Up:		Step( zx::ContinueListKey::Up );		return true;
	case MKEY_Down:		Step( zx::ContinueListKey::Down );		return true;
	case MKEY_PageUp:	Step( zx::ContinueListKey::PageUp );	return true;
	case MKEY_PageDown:	Step( zx::ContinueListKey::PageDown );	return true;

	case MKEY_Enter:
		Activate( );
		return true;

	default:
		break;
	}

	return Super::MenuEvent( mkey, fromcontroller );
}

bool DFUAContinueMenu::Responder( event_t *ev )
{
	if (( ev->type == EV_GUI_Event ) && ( ev->subtype == EV_GUI_KeyDown ))
	{
		// [rc4l] Home and End arrive raw: the menu framework translates the arrows and the page keys
		// into MKEY_* and has never had a name for these two, so nothing in this engine implements
		// them. A fifty-row list is exactly where their absence is felt.
		switch ( ev->data1 )
		{
		case GK_HOME:	Step( zx::ContinueListKey::Home );	return true;
		case GK_END:	Step( zx::ContinueListKey::End );	return true;
		case GK_DEL:	Forget( );							return true;
		default:
			break;
		}
	}

	if ( ev->type == EV_GUI_Event )
	{
		// A wheel notch moves the VIEW and leaves the selection alone: scrolling to look at what
		// else is there must not change what pressing Enter would do.
		if (( ev->subtype == EV_GUI_WheelUp ) || ( ev->subtype == EV_GUI_WheelDown ))
		{
			const Layout layout = Measure( Total( ));
			const int step = ( ev->subtype == EV_GUI_WheelUp ) ? -3 : 3;

			mFirst = zx::ClampScroll( mFirst + step, Total( ) - layout.rows );
			return true;
		}
	}

	return Super::Responder( ev );
}

//=============================================================================
//
int DFUAContinueMenu::RowAt( int vx, int vy ) const
{
	const Layout layout = Measure( Total( ));

	if (( vx < layout.listX ) || ( vx >= layout.listX + layout.listW ))
		return -1;
	if (( vy < layout.listY ) || ( vy >= layout.listY + layout.rows * kRowH ))
		return -1;

	const int row = mFirst + (( vy - layout.listY ) / kRowH );
	return ( row < Total( )) ? row : -1;
}

bool DFUAContinueMenu::MouseEvent( int type, int x, int y )
{
	const int vx = ToVirtualX( x ), vy = ToVirtualY( y );

	mHot = RowAt( vx, vy );

	if (( type == MOUSE_Click ) && ( mHot >= 0 ))
	{
		// [rc4l] One click, because the menu IS the question and the row IS the answer. It never
		// appears under the pointer -- it opens from a press on the header bar and draws centred --
		// so the misclick that a one-click list would ordinarily invite has nowhere to come from.
		mSelected = mHot;
		Activate( );
		return true;
	}

	return Super::MouseEvent( type, x, y );
}

//=============================================================================
//
void DFUAContinueMenu::DrawRows( const Layout &layout )
{
	const int total = layout.total;

	// [rc4l] The view follows the cursor, and the cursor is clamped first: the history can shorten
	// while this menu is open -- a probe answers, a snapshot goes -- so the row the keyboard was on
	// may no longer exist by the time it is drawn.
	mSelected = zx::ComputeClampedSelection( mSelected, total );

	const zx::RowWindow window = zx::ComputeRowWindow( total, layout.rows, mSelected, mFirst );
	mFirst = zx::ComputeRestoredScroll( window.first, total, layout.rows );

	const int whenX = layout.listX + layout.listW - kWhenColumnW;
	const int labelW = whenX - layout.listX - 8;

	for ( int i = 0; i < layout.rows; ++i )
	{
		const int row = mFirst + i;
		if ( row >= total )
			break;

		const int y = layout.listY + ( i * kRowH );
		const bool bSelected = ( row == mSelected );

		if ( bSelected )
		{
			DimClipped( PalEntry( 90, 110, 160 ), 0.35f, ToScreenX( layout.listX - 4 ), ToScreenY( y - 1 ),
				ToScreenX( layout.listX + layout.listW + 4 ) - ToScreenX( layout.listX - 4 ),
				ToScreenY( y + kRowH - 1 ) - ToScreenY( y - 1 ));
		}
		else if ( row == mHot )
		{
			DimClipped( PalEntry( 90, 110, 160 ), 0.15f, ToScreenX( layout.listX - 4 ), ToScreenY( y - 1 ),
				ToScreenX( layout.listX + layout.listW + 4 ) - ToScreenX( layout.listX - 4 ),
				ToScreenY( y + kRowH - 1 ) - ToScreenY( y - 1 ));
		}

		// [rc4l] A server that has stopped answering is DIMMED AND LABELLED, not removed. Rows that
		// vanish from under a pointer are how a click lands on something the player did not read,
		// and the press still costs at worst one trip back to the browser with a reason -- the path
		// a failed join already takes.
		const int probe = zx::Continue_EntryProbe( row );
		const bool bDead = ( probe == 2 ) || ( probe == 3 );

		FString label = zx::Continue_EntryLabel( row );
		if ( probe == 2 )
			label += " (not answering)";
		else if ( probe == 3 )
			label += " (different files)";

		const EColorRange labelCol = bDead ? CR_DARKGRAY : ( bSelected ? CR_WHITE : CR_GRAY );

		DrawTextAt( labelCol, layout.listX, y, Ellipsised( label, labelW ) );

		const char *when = zx::Continue_EntryWhen( row );
		DrawTextAt( bSelected ? CR_GOLD : CR_DARKGRAY,
			layout.listX + layout.listW - SmallFont->StringWidth( when ), y, when );

		if ( bSelected )
		{
			// The same focus orb the browser and the tab bar use, so "you are here" does not change
			// shape halfway through a gesture.
			zx::DrawFocusGlow( ToScreenX( layout.listX - 9 ), ToScreenY( y + ( kRowH / 2 )),
				ToScreenX( 100 ) - ToScreenX( 0 ));
		}
	}
}

void DFUAContinueMenu::DrawScrollbar( const Layout &layout )
{
	const int total = layout.total;
	if ( total <= layout.rows )
		return;					// a list that fits needs no bar

	const int vx = layout.listX + layout.listW + 6;

	const int left = ToScreenX( vx );
	const int width = MAX( 1, ToScreenX( vx + kScrollbarW ) - left );
	const int top = ToScreenY( layout.listY );
	const int height = ToScreenY( layout.listY + layout.rows * kRowH ) - top;
	if ( height <= 0 )
		return;

	screen->Dim( PalEntry( 120, 140, 180 ), 0.14f, left, top, width, height );

	// Geometry from the browser's own units, which the hit test would use too: the two working it
	// out separately is exactly how clicking a bar came to jump somewhere the thumb was not.
	const int minThumb = ToScreenY( 8 ) - ToScreenY( 0 );
	const int thumbH = zx::ComputeThumbHeight( height, layout.rows, total, minThumb );
	const int thumbY = top + zx::ComputeThumbTop( height, thumbH, mFirst, total - layout.rows );

	screen->Dim( PalEntry( 170, 190, 230 ), 0.55f, left, thumbY, width, thumbH );
}

void DFUAContinueMenu::Drawer( )
{
	const int total = Total( );

	// [rc4l] Everything worth going back to has gone while the menu was open -- the last row forgotten,
	// or a reload that emptied it. There is no question left to ask.
	if ( total <= 0 )
	{
		Close( );
		return;
	}

	const Layout layout = Measure( total );

	const zx::PanelColor topCol = { 26, 28, 40, 236 };
	const zx::PanelColor botCol = { 8, 9, 15, 248 };
	DrawRoundedPanel( layout.cardX, layout.cardY, layout.cardW, layout.cardH, topCol, botCol, 10 );

	const char *const title = "CONTINUE";
	DrawTextAt( CR_WHITE, layout.cardX + ( layout.cardW - SmallFont->StringWidth( title )) / 2,
		layout.cardY + 10, title );

	// The column headings, which are what makes the right-hand numbers mean something.
	DrawTextAt( CR_DARKGRAY, layout.listX, layout.cardY + 30, "ACTIVITY" );

	const char *const whenHeading = "LAST PLAYED";
	DrawTextAt( CR_DARKGRAY,
		layout.listX + layout.listW - SmallFont->StringWidth( whenHeading ), layout.cardY + 30,
		whenHeading );

	DimClipped( PalEntry( 120, 140, 180 ), 0.25f, ToScreenX( layout.listX ),
		ToScreenY( layout.cardY + 41 ),
		ToScreenX( layout.listX + layout.listW ) - ToScreenX( layout.listX ),
		MAX( 1, ToScreenY( 1 ) - ToScreenY( 0 )));

	DrawRows( layout );
	DrawScrollbar( layout );

	const char *const hint = "Enter: continue      Del: forget      Esc: back";
	DrawTextAt( CR_DARKGRAY, layout.cardX + ( layout.cardW - SmallFont->StringWidth( hint )) / 2,
		layout.cardY + layout.cardH - 16, hint );

	Super::Drawer( );
}

//=============================================================================
//
namespace zx
{

void Continue_OpenList( void )
{
	// [rc4l] Constructed rather than declared in menudef. The list has no items a descriptor could
	// describe -- its rows come from the history at the moment it opens -- so a descriptor would be
	// an empty menu whose only purpose was to name a class.
	M_ActivateMenu( new DFUAContinueMenu( ));

	// The row one press would have gone to is the row the cursor starts on, so Enter straight away
	// does what pressing the pill with a single entry does.
	Continue_ProbeEntry( 0 );
}

} // namespace zx

//*****************************************************************************
//
// [rc4l] The list from the console, so an E2E can assert on the rows rather than on pixels.
CCMD( fua_continue_list )
{
	const int total = zx::Continue_HistoryCount( );
	if ( total <= 0 )
	{
		Printf( "fua_continue_list: nothing to continue.\n" );
		return;
	}

	for ( int i = 0; i < total; ++i )
	{
		Printf( "%2d  %-40s  %-14s  kind %d probe %d\n", i, zx::Continue_EntryLabel( i ),
			zx::Continue_EntryWhen( i ), zx::Continue_EntryKind( i ), zx::Continue_EntryProbe( i ));
	}
}
