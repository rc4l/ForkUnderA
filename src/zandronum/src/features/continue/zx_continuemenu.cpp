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

#include "computation/listaction_compute.h"
#include "computation/virtualspace_compute.h"
#include "features/continue/computation/continuecard_compute.h"
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
	const zx::VirtualSpace s = zx::ComputeVirtualSpace( screen->GetWidth( ), screen->GetHeight( ),
		kLayoutW, kLayoutH );
	vw = s.width;
	vh = s.height;
}

int VirtW( ) { int vw = 0, vh = 0; VirtSize( vw, vh ); return vw; }
int VirtH( ) { int vw = 0, vh = 0; VirtSize( vw, vh ); return vh; }

int ToScreenX( int vx )
{
	int vw = 0, vh = 0;
	VirtSize( vw, vh );
	return zx::VirtualToScreen( vx, screen->GetWidth( ), vw );
}

int ToScreenY( int vy )
{
	int vw = 0, vh = 0;
	VirtSize( vw, vh );
	return zx::VirtualToScreen( vy, screen->GetHeight( ), vh );
}

// [rc4l] Screen pixels back to virtual ones, DERIVED from the forward mapping rather than written
// out a second time: evaluated at two points it recovers the scale exactly, so the two cannot
// disagree the first time either is touched.
int ToVirtualX( int px )
{
	return zx::ScreenToVirtual( px, ToScreenX( 0 ), ToScreenX( 100 ), 100 );
}

int ToVirtualY( int py )
{
	return zx::ScreenToVirtual( py, ToScreenY( 0 ), ToScreenY( 100 ), 100 );
}

// The card, in virtual units.
// [rc4l] The card's WIDTHS live in continuecard_compute with the rest of the geometry; what stays
// here is only what the drawing itself needs.

// [rc4l] Two lines to a row: the headline, and under it what kind of session it was and what it was
// played with. One line could not tell two rows apart -- "MAP01" is a slot number and a server name
// says nothing about the game behind it -- and the missing half will not fit beside the headline
// without turning every row into a paragraph.
const int kLineH = 10;
const int kRowH = ( 2 * kLineH ) + 3;
const int kMaxVisibleRows = 7;		// beyond this the card would fill the window; it scrolls instead

// The status dot, in its own gutter to the left of the text so the headlines stay aligned.
const int kDotW = 9;
const int kDotR = 2;
const int kWhenColumnW = 96;
const int kScrollbarW = 4;

// [rc4l] The card's geometry belongs to computation/continuecard_compute, where it can be asserted:
// the one bug this menu shipped was a layout bug, and nothing could catch it while the arithmetic
// lived inside a Drawer. This only supplies the sizes the engine knows and carries the row count.
struct Layout : public zx::ContinueCardLayout
{
	int rows;						// how many fit on screen
	int total;
};

Layout Measure( int total, int reasonLines = 0 )
{
	zx::ContinueCardMetrics m;
	m.virtualW = VirtW( );
	m.virtualH = VirtH( );
	m.rowCount = total;
	m.rowHeight = kRowH;
	m.lineHeight = kLineH;
	m.maxVisibleRows = kMaxVisibleRows;

	Layout out;
	static_cast<zx::ContinueCardLayout &>( out ) = zx::ComputeContinueCard( m, reasonLines );
	out.rows = out.visibleRows;
	out.total = total;
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

// [rc4l] Whether the row will WORK, as a colour: green go, yellow a download away, red not from
// here. The verdict is continuestatus_compute's; this only paints it.
//
// A dot rather than words because it is glanced at, not read: the reason in words is one line of
// tooltip away, and spelling it out on every row would bury the thing the row is actually about.
void DrawStatusDot( int vx, int vy, int status )
{
	static const zx::PanelColor kColours[3] =
	{
		{ 90, 210, 110, 255 },		// green
		{ 230, 190, 70, 255 },		// yellow
		{ 225, 85, 85, 255 },		// red
	};

	const zx::PanelColor c = kColours[( status >= 0 && status <= 2 ) ? status : 0];

	const int left = ToScreenX( vx - kDotR ), right = ToScreenX( vx + kDotR + 1 );
	const int top = ToScreenY( vy - kDotR ), bottom = ToScreenY( vy + kDotR + 1 );
	const int w = right - left, h = bottom - top;
	if (( w <= 0 ) || ( h <= 0 ))
		return;

	// Rounded off with the same inset the panels use, so it reads as a dot rather than a pixel block.
	for ( int row = 0; row < h; ++row )
	{
		const int inset = zx::ComputeRoundedInset( row, h, h / 2 );
		const int rowW = w - 2 * inset;
		if ( rowW > 0 )
			DimClipped( PalEntry( c.r, c.g, c.b ), 0.95f, left + inset, top + row, rowW, 1 );
	}
}

// The browser's button, drawn the same way for the same reason: one press ends the question, and a
// button that looked different here would read as a different kind of thing.
void DrawRoundedButton( int vx, int vy, int vw, int vh, const char *label, bool bHot, bool bEnabled )
{
	const int base = bHot ? 70 : 45;
	const zx::PanelColor topCol = { static_cast<BYTE>( base ), static_cast<BYTE>( base + 25 ),
		static_cast<BYTE>( base ), 220 };
	const zx::PanelColor botCol = { static_cast<BYTE>( base / 2 ), static_cast<BYTE>( base + 5 ),
		static_cast<BYTE>( base / 2 ), 235 };

	DrawRoundedPanel( vx, vy, vw, vh, topCol, botCol, 4 );

	const EColorRange col = bEnabled ? ( bHot ? CR_WHITE : CR_GREEN ) : CR_DARKGRAY;
	screen->DrawText( SmallFont, col,
		vx + ( vw / 2 ) - ( SmallFont->StringWidth( label ) / 2 ),
		vy + ( vh - SmallFont->GetHeight( )) / 2 + 1, label,
		DTA_VirtualWidth, VirtW( ), DTA_VirtualHeight, VirtH( ), DTA_KeepRatio, true, TAG_DONE );
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

// [rc4l] A sentence broken across lines at its spaces, for the panel. The reason a row is refused is
// the one line a player has to READ rather than glance at, and ellipsising it cuts off the half that
// says what to do about it.
//
// Words only: a word longer than the column is left to overrun rather than chopped mid-way, because
// that only happens to a filename and half a filename is worse than a wide one.
void WrapInto( const char *text, int maxWidth, TArray<FString> &lines, unsigned maxLines )
{
	FString current;

	const FString whole = text;
	long start = 0;

	while (( start <= (long)whole.Len( )) && ( lines.Size( ) < maxLines ))
	{
		long space = whole.IndexOf( ' ', start );
		if ( space < 0 )
			space = whole.Len( );

		const FString word = whole.Mid( start, space - start );
		FString candidate = current.IsEmpty( ) ? word : ( current + " " + word );

		if ( current.IsNotEmpty( ) && ( SmallFont->StringWidth( candidate ) > maxWidth ))
		{
			lines.Push( current );
			current = word;
		}
		else
		{
			current = candidate;
		}

		start = space + 1;
	}

	if ( current.IsNotEmpty( ) && ( lines.Size( ) < maxLines ))
		lines.Push( current );
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
	// [rc4l] Parented to whatever was open, so Escape goes back to it rather than closing every menu
	// and dropping the player onto the title screen they opened this from.
	DFUAContinueMenu( DMenu *parent = NULL )
		: DMenu( parent ), mSelected( 0 ), mFirst( 0 ), mHot( -1 ), mButtonHot( false ),
		  mDraggingScrollbar( false ), mReveal( true ),
		  mZone( zx::ListActionZone::List )
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
	bool mButtonHot;
	bool mDraggingScrollbar;

	// [rc4l] The selection has MOVED and the view must catch up with it. Without this the window is
	// derived from the selection on every frame, so anything that scrolls without selecting -- the
	// wheel, the scrollbar -- is undone before it can be seen. The browser keeps the same flag for
	// the same reason; taking its scrolling maths without it is what left this list unscrollable.
	bool mReveal;

	// [rc4l] Which half of the card the keyboard is on. The route between them is the shared
	// list-and-button contract (computation/listaction_compute), the same one the server browser's
	// list and JOIN button use.
	zx::ListActionZone mZone;

	// [rc4l] Leaving is a ROW while we are in a session, not a separate act performed by the same
	// press. It sits at the top and starts selected, so the immediate leave the button used to do is
	// still one keystroke away -- and going straight to another session no longer means leaving
	// first and pressing the pill again.
	bool HasLeaveRow( ) const { return zx::Continue_IsDisconnect( ); }
	int LeaveRows( ) const { return HasLeaveRow( ) ? 1 : 0; }

	// Every row on screen, the leave row included.
	int Total( ) const { return zx::Continue_HistoryCount( ) + LeaveRows( ); }

	// The history row a screen row means, or -1 for the leave row.
	int EntryIndex( int row ) const { return row - LeaveRows( ); }

	// [rc4l] The button names the ACT, not the feature: a player reads it to find out what pressing
	// it does to the thing they have highlighted.
	// Whether the button can be pressed at all: a red row draws it greyed, and the keyboard must not
	// be able to walk onto something that refuses.
	bool ActionAvailable( ) const
	{
		if ( Total( ) <= 0 )
			return false;

		const int entry = EntryIndex( mSelected );
		return ( entry < 0 ) || ( zx::Continue_EntryStatus( entry ) != 2 );
	}

	const char *ButtonLabel( ) const
	{
		const int entry = EntryIndex( mSelected );
		if ( entry < 0 )
			return "LEAVE";

		switch ( zx::Continue_EntryKind( entry ))
		{
		case 2:  return "RECONNECT";
		case 3:  return "HOST AGAIN";
		default: return "CONTINUE";
		}
	}

	void Step( zx::ContinueListKey key );
	void Activate( );
	void Forget( );
	int RowAt( int vx, int vy ) const;
	bool OnButton( int vx, int vy ) const;

	// [rc4l] ONE source for where the bar is, used by the drawing AND the hit test. Written out
	// twice, they drift, and a thumb that is drawn a few pixels from where it can be grabbed is a
	// scrollbar that feels broken without ever looking wrong.
	bool ScrollbarRect( const Layout &layout, int &left, int &top, int &width, int &height ) const;
	bool ScrollbarDrag( int type, int screenY );
	void DrawRows( const Layout &layout );
	void DrawScrollbar( const Layout &layout );
	void DrawDetail( const Layout &layout );
};

IMPLEMENT_CLASS( DFUAContinueMenu )

//=============================================================================
//
void DFUAContinueMenu::Step( zx::ContinueListKey key )
{
	const int total = Total( );
	const int was = mSelected;

	mSelected = zx::StepContinueList( key, mSelected, total, Measure( total ).rows );
	mReveal = true;

	if ( mSelected != was )
		S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );

	// [rc4l] The row the player is on is asked about, and only then. Fifty servers queried the moment
	// a menu opened would be a storm sent on somebody else's behalf.
	zx::Continue_ProbeEntry( EntryIndex( mSelected ));
}

void DFUAContinueMenu::Activate( )
{
	if ( Total( ) <= 0 )
		return;

	const int entry = EntryIndex( mSelected );
	if ( entry < 0 )
	{
		S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
		zx::Continue_LeaveToMenu( );
		return;
	}

	// [rc4l] A red row refuses without touching the menus, so the list is still here to say no ON.
	// Saying it out loud beats a row that looks pressable and then appears to do nothing at all --
	// the same reasoning the tab bar uses for a tab it cannot go to.
	if ( zx::Continue_ActivateEntry( entry ) == false )
	{
		S_Sound( CHAN_VOICE | CHAN_UI, "menu/invalid", snd_menuvolume, ATTN_NONE );
		return;
	}

	// Closes the menus itself, and on the path that works it does not return: the WAD reload throws.
	S_Sound( CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE );
}

void DFUAContinueMenu::Forget( )
{
	if ( Total( ) <= 0 )
		return;

	// Leaving is not a row anybody can forget.
	const int entry = EntryIndex( mSelected );
	if ( entry < 0 )
		return;

	zx::Continue_ForgetEntry( entry );
	S_Sound( CHAN_VOICE | CHAN_UI, "menu/clear", snd_menuvolume, ATTN_NONE );

	// [rc4l] The list just got shorter under the cursor. Pulled back in here rather than left for
	// the next keypress, because the DRAWING is what happens next and it would otherwise paint a
	// highlight on a row that is not there.
	mSelected = zx::ComputeClampedSelection( mSelected, Total( ));
	mReveal = true;

	// Nothing left to choose between: the menu has answered its own question.
	if ( Total( ) <= 0 )
		Close( );
}

//=============================================================================
//
bool DFUAContinueMenu::MenuEvent( int mkey, bool fromcontroller )
{
	// [rc4l] Paging and jumping belong to the list, so they pull the focus back to it rather than
	// doing nothing while the button holds the keyboard.
	switch ( mkey )
	{
	case MKEY_PageUp:
		mZone = zx::ListActionZone::List;
		Step( zx::ContinueListKey::PageUp );
		return true;

	case MKEY_PageDown:
		mZone = zx::ListActionZone::List;
		Step( zx::ContinueListKey::PageDown );
		return true;

	default:
		break;
	}

	zx::ListActionKey key;
	switch ( mkey )
	{
	case MKEY_Up:		key = zx::ListActionKey::Up;	break;
	case MKEY_Down:		key = zx::ListActionKey::Down;	break;
	case MKEY_Left:		key = zx::ListActionKey::Left;	break;
	case MKEY_Right:	key = zx::ListActionKey::Right;	break;
	case MKEY_Enter:	key = zx::ListActionKey::Enter;	break;
	default:
		return Super::MenuEvent( mkey, fromcontroller );
	}

	const zx::ListActionZone was = mZone;
	const zx::ListActionStep step = zx::StepListAction( mZone, key, ActionAvailable( ));

	if ( step.activate )
	{
		Activate( );
		return true;
	}

	// [rc4l] Off the pair entirely -- Left out of the list, Up off the button. There is nothing
	// outside the card to reach, so the focus comes to rest on the list rather than nowhere.
	mZone = ( step.zone == zx::ListActionZone::Outside ) ? zx::ListActionZone::List : step.zone;

	if ( step.rowStep < 0 )
		Step( zx::ContinueListKey::Up );
	else if ( step.rowStep > 0 )
		Step( zx::ContinueListKey::Down );
	else if ( mZone != was )
		S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );

	return true;
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
		case GK_HOME:
			mZone = zx::ListActionZone::List;
			Step( zx::ContinueListKey::Home );
			return true;

		case GK_END:
			mZone = zx::ListActionZone::List;
			Step( zx::ContinueListKey::End );
			return true;
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

bool DFUAContinueMenu::OnButton( int vx, int vy ) const
{
	const Layout layout = Measure( Total( ));

	return ( vx >= layout.buttonX ) && ( vx < layout.buttonX + layout.buttonW )
		&& ( vy >= layout.buttonY ) && ( vy < layout.buttonY + layout.buttonH );
}

bool DFUAContinueMenu::MouseEvent( int type, int x, int y )
{
	const int vx = ToVirtualX( x ), vy = ToVirtualY( y );

	// [rc4l] The bar gets asked BEFORE the rows, and a drag in progress gets asked before anything:
	// once the button is down the pointer belongs to the thumb wherever it goes.
	{
		const Layout layout = Measure( Total( ));

		int left = 0, top = 0, width = 0, height = 0;
		const bool bHasBar = ScrollbarRect( layout, left, top, width, height );

		// A few pixels of slack either side, because a four-pixel target is a target nobody hits.
		const int grab = MAX( 1, ToScreenX( 3 ) - ToScreenX( 0 ));
		const bool bOverBar = bHasBar && ( x >= left - grab ) && ( x < left + width + grab )
			&& ( y >= top ) && ( y < top + height );

		if ( type == MOUSE_Click )
			mDraggingScrollbar = bOverBar;

		if ( mDraggingScrollbar )
			return ScrollbarDrag( type, y );
	}

	mHot = RowAt( vx, vy );
	mButtonHot = OnButton( vx, vy );

	if ( type == MOUSE_Click )
	{
		// [rc4l] A click on a row SELECTS it and nothing else. It used to act, which put a WAD
		// reload one stray click away and gave the player nowhere to read what the row was before
		// committing to it. The panel is that somewhere, and the button is the commitment.
		if ( mHot >= 0 )
		{
			mZone = zx::ListActionZone::List;
			if ( mSelected != mHot )
			{
				mSelected = mHot;
				mReveal = true;
				S_Sound( CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE );
				zx::Continue_ProbeEntry( EntryIndex( mSelected ));
			}
			return true;
		}

		if ( mButtonHot )
		{
			mZone = zx::ListActionZone::Action;
			Activate( );
			return true;
		}
	}

	// Double-clicking a row is the shortcut for the two presses, which is what a list of things to
	// open does everywhere else.
	if (( type == MOUSE_Release ) && ( mHot >= 0 ) && ( mHot == mSelected ))
		return true;

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

	// ...but only when the SELECTION moved. Done every frame, "keep the selection visible" drags the
	// window back the instant anything scrolls without selecting.
	if ( mReveal )
	{
		const zx::RowWindow window = zx::ComputeRowWindow( total, layout.rows, mSelected, mFirst );
		mFirst = window.first;
		mReveal = false;
	}

	// Every frame, though: rows come and go while the menu is open, so a position that was in range
	// a moment ago may not be one now.
	mFirst = zx::ComputeRestoredScroll( mFirst, total, layout.rows );

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

		const int entry = EntryIndex( row );
		const int textY = y + 1;

		// [rc4l] Leaving, drawn as the row it now is. No "last played" against it: it is not
		// somewhere the player has been, it is the way out of where they are.
		if ( entry < 0 )
		{
			DrawTextAt( bSelected ? CR_WHITE : CR_GRAY, layout.listX, textY,
				"Leave and go to the main menu" );
			DrawTextAt( CR_DARKGRAY, layout.listX, textY + kLineH, "Stop playing and go back" );

			if ( bSelected )
			{
				zx::DrawFocusGlow( ToScreenX( layout.cardX + 6 ), ToScreenY( y + ( kRowH / 2 )),
					ToScreenX( 100 ) - ToScreenX( 0 ));
			}
			continue;
		}

		// [rc4l] A server that has stopped answering is DIMMED AND LABELLED, not removed. Rows that
		// vanish from under a pointer are how a click lands on something the player did not read,
		// and the press still costs at worst one trip back to the browser with a reason -- the path
		// a failed join already takes.
		// [rc4l] A server that has stopped answering is DIMMED, not removed. Rows that vanish from
		// under a pointer are how a click lands on something the player did not read -- and the dot
		// beside it already says, in a colour, that pressing it will not get anywhere.
		const int status = zx::Continue_EntryStatus( entry );
		const bool bDead = ( status == 2 );

		const EColorRange labelCol = bDead ? CR_DARKGRAY : ( bSelected ? CR_WHITE : CR_GRAY );

		DrawTextAt( labelCol, layout.listX, textY,
			Ellipsised( zx::Continue_EntryLabel( entry ), labelW ));

		// What it was and what it was played with, dimmer and underneath: the half that tells two
		// rows apart, in the place where it does not compete with the name.
		DrawTextAt( CR_DARKGRAY, layout.listX, textY + kLineH,
			Ellipsised( zx::Continue_EntryDetail( entry ), layout.listW ));

		const char *when = zx::Continue_EntryWhen( entry );
		DrawTextAt( bSelected ? CR_GOLD : CR_DARKGRAY,
			layout.listX + layout.listW - SmallFont->StringWidth( when ), textY, when );

		DrawStatusDot( layout.listX - kDotW, y + ( kRowH / 2 ), status );

		// The same focus orb the browser and the tab bar use, so "you are here" does not change shape
		// halfway through a gesture -- and it is on the row only while the keyboard is, or the card
		// would claim the cursor is in two places.
		if ( bSelected && ( mZone == zx::ListActionZone::List ))
		{
			zx::DrawFocusGlow( ToScreenX( layout.cardX + 6 ), ToScreenY( y + ( kRowH / 2 )),
				ToScreenX( 100 ) - ToScreenX( 0 ));
		}
	}
}

void DFUAContinueMenu::DrawDetail( const Layout &layout )
{
	// The sunken backdrop the browser puts its detail on, so the two read as the same kind of place.
	const zx::PanelColor topCol = { 6, 7, 12, 220 };
	const zx::PanelColor botCol = { 3, 4, 8, 235 };
	DrawRoundedPanel( layout.panelX, layout.panelY, layout.panelW, layout.panelH, topCol, botCol, 6 );

	const int x = layout.panelX + 7;
	const int w = layout.panelW - 14;
	int y = layout.panelY + 6;

	const int entry = EntryIndex( mSelected );
	const bool bFocused = ( mZone == zx::ListActionZone::Action );

	if ( entry < 0 )
	{
		// [rc4l] Leaving has no session behind it, so the panel says what will happen rather than
		// leaving a blank square where every other row has a description.
		DrawTextAt( CR_WHITE, x, y, "Leave" );
		y += kLineH + 2;
		DrawTextAt( CR_DARKGRAY, x, y, Ellipsised( "Stop playing and go", w ));
		y += kLineH;
		DrawTextAt( CR_DARKGRAY, x, y, Ellipsised( "back to the main menu.", w ));

		DrawRoundedButton( layout.buttonX, layout.buttonY, layout.buttonW, layout.buttonH,
			ButtonLabel( ), mButtonHot || bFocused, true );

		if ( bFocused )
		{
			zx::DrawFocusGlow( ToScreenX( layout.buttonX - 6 ),
				ToScreenY( layout.buttonY + ( layout.buttonH / 2 )),
				ToScreenX( 100 ) - ToScreenX( 0 ));
		}
		return;
	}

	// [rc4l] Where the flowing half has to stop. The reason sits above the button and the button is
	// pinned to the bottom, so everything written downwards from the top has a floor -- without one,
	// a panel with an address and a refusal in it drew the refusal over the date.
	TArray<FString> reasonLines;
	const char *reason = zx::Continue_EntryStatusReason( entry );
	if ( *reason != 0 )
		WrapInto( reason, w, reasonLines, 3 );

	// The floor comes from the layout unit, asked again now that the refusal's height is known --
	// recomputing it here is how the drawing and the tested geometry would come to disagree.
	const int limit = Measure( layout.total, (int)reasonLines.Size( )).panelFlowLimit;

	DrawTextAt( CR_WHITE, x, y, Ellipsised( zx::Continue_EntryLabel( entry ), w ));
	y += kLineH + 1;

	// The summary, not the row's line: the files it would have tacked on are listed in full below.
	if ( y + kLineH <= limit )
	{
		DrawTextAt( CR_GRAY, x, y, Ellipsised( zx::Continue_EntrySummary( entry ), w ));
		y += kLineH + 3;
	}

	const char *address = zx::Continue_EntryAddress( entry );
	if (( *address != 0 ) && ( y + kLineH <= limit ))
	{
		DrawTextAt( CR_DARKGRAY, x, y, Ellipsised( address, w ));
		y += kLineH + 2;
	}

	if ( y + ( 2 * kLineH ) <= limit )
	{
		DrawTextAt( CR_DARKGRAY, x, y, "LAST PLAYED" );
		y += kLineH;
		DrawTextAt( CR_GOLD, x, y, zx::Continue_EntryWhen( entry ));
		y += kLineH + 3;
	}

	// [rc4l] Every file, not the two the row has room for. "Which Doom was this" is exactly the
	// question a row two years old raises, and the row cannot answer it.
	const int files = zx::Continue_EntryFileCount( entry );

	// Whatever fits between here and the floor. A file list that ran past it would be a panel
	// writing over itself.
	const int room = ( limit - ( y + kLineH )) / kLineH;

	// A heading with nothing under it is worse than no heading: it promises a list and then shows the
	// player an empty strip of panel.
	if (( files > 0 ) && ( room > 0 ))
	{
		DrawTextAt( CR_DARKGRAY, x, y, "FILES" );
		y += kLineH;

		for ( int i = 0; ( i < files ) && ( i < room ); ++i )
		{
			if (( i == room - 1 ) && ( files > room ))
			{
				char more[32];
				snprintf( more, sizeof more, "+%d more", files - i );
				DrawTextAt( CR_DARKGRAY, x, y, more );
			}
			else
			{
				DrawTextAt( CR_GRAY, x, y, Ellipsised( zx::Continue_EntryFile( entry, i ), w ));
			}
			y += kLineH;
		}
	}

	// Why it is the colour it is, in words, immediately above the button that will act on it --
	// WRAPPED, because this is the one line in the panel that has to be read rather than glanced at.
	if ( reasonLines.Size( ) > 0 )
	{
		const int status = zx::Continue_EntryStatus( entry );
		int ry = limit;

		for ( unsigned i = 0; i < reasonLines.Size( ); ++i )
		{
			DrawTextAt( ( status == 2 ) ? CR_BRICK : CR_ORANGE, x, ry, reasonLines[i] );
			ry += kLineH;
		}
	}

	// A row that cannot work still draws its button, greyed: a button that VANISHES leaves the
	// player wondering whether they missed it, and one that is visibly refused says what it means.
	DrawRoundedButton( layout.buttonX, layout.buttonY, layout.buttonW, layout.buttonH,
		ButtonLabel( ), mButtonHot || bFocused, ( zx::Continue_EntryStatus( entry ) != 2 ));

	if ( bFocused )
	{
		zx::DrawFocusGlow( ToScreenX( layout.buttonX - 6 ),
			ToScreenY( layout.buttonY + ( layout.buttonH / 2 )),
			ToScreenX( 100 ) - ToScreenX( 0 ));
	}
}

bool DFUAContinueMenu::ScrollbarRect( const Layout &layout, int &left, int &top,
	int &width, int &height ) const
{
	if ( layout.total <= layout.rows )
		return false;				// a list that fits needs no bar

	const int vx = layout.listX + layout.listW + 6;

	left = ToScreenX( vx );
	width = MAX( 1, ToScreenX( vx + kScrollbarW ) - left );
	top = ToScreenY( layout.listY );
	height = ToScreenY( layout.listY + layout.rows * kRowH ) - top;

	return ( height > 0 );
}

void DFUAContinueMenu::DrawScrollbar( const Layout &layout )
{
	const int total = layout.total;

	int left = 0, top = 0, width = 0, height = 0;
	if ( ScrollbarRect( layout, left, top, width, height ) == false )
		return;

	screen->Dim( PalEntry( 120, 140, 180 ), 0.14f, left, top, width, height );

	// Geometry from the browser's own units, which the hit test would use too: the two working it
	// out separately is exactly how clicking a bar came to jump somewhere the thumb was not.
	const int minThumb = ToScreenY( 8 ) - ToScreenY( 0 );
	const int thumbH = zx::ComputeThumbHeight( height, layout.rows, total, minThumb );
	const int thumbY = top + zx::ComputeThumbTop( height, thumbH, mFirst, total - layout.rows );

	screen->Dim( PalEntry( 170, 190, 230 ), mDraggingScrollbar ? 0.8f : 0.55f,
		left, thumbY, width, thumbH );
}

// [rc4l] Grabbing the bar. The thumb follows the pointer for as long as the button is held, even
// once it wanders off the bar, which is what dragging a scrollbar means everywhere else.
bool DFUAContinueMenu::ScrollbarDrag( int type, int screenY )
{
	const Layout layout = Measure( Total( ));

	int left = 0, top = 0, width = 0, height = 0;
	if ( ScrollbarRect( layout, left, top, width, height ) == false )
	{
		mDraggingScrollbar = false;
		return false;
	}

	// Same geometry the drawing uses, so the thumb lands where it was grabbed.
	const int minThumb = ToScreenY( 8 ) - ToScreenY( 0 );
	const int thumbH = zx::ComputeThumbHeight( height, layout.rows, layout.total, minThumb );

	// Moves the VIEW only. What is selected is none of the scrollbar's business -- the same rule the
	// wheel keeps, and the reason dragging past a row does not arm the button on it.
	mFirst = zx::ComputeFirstFromPointer( screenY - top, height, thumbH,
		layout.total - layout.rows );

	if ( type == MOUSE_Release )
		mDraggingScrollbar = false;

	return true;
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

	// [rc4l] It is a different question in a session: not "what do you want to continue" but "where
	// do you want to go", and leaving is one of the answers.
	const char *const title = HasLeaveRow( ) ? "WHERE TO?" : "CONTINUE";
	DrawTextAt( CR_WHITE, layout.cardX + ( layout.cardW - SmallFont->StringWidth( title )) / 2,
		layout.cardY + 10, title );

	// The column headings, which are what makes the right-hand numbers mean something.
	DrawTextAt( CR_DARKGRAY, layout.listX, layout.cardY + 30, "ACTIVITY" );

	const char *const whenHeading = "LAST PLAYED";
	DrawTextAt( CR_DARKGRAY,
		layout.listX + layout.listW - SmallFont->StringWidth( whenHeading ), layout.cardY + 30,
		whenHeading );

	DrawTextAt( CR_DARKGRAY, layout.panelX + 7, layout.cardY + 30, "DETAILS" );

	DimClipped( PalEntry( 120, 140, 180 ), 0.25f, ToScreenX( layout.listX ),
		ToScreenY( layout.cardY + 41 ),
		ToScreenX( layout.listX + layout.listW ) - ToScreenX( layout.listX ),
		MAX( 1, ToScreenY( 1 ) - ToScreenY( 0 )));

	DrawRows( layout );
	DrawScrollbar( layout );
	DrawDetail( layout );

	Super::Drawer( );
}

//=============================================================================
//
namespace zx
{

bool Continue_IsListOpen( void )
{
	return ( DMenu::CurrentMenu != NULL )
		&& DMenu::CurrentMenu->IsKindOf( RUNTIME_CLASS( DFUAContinueMenu ));
}

void Continue_CloseList( void )
{
	if ( Continue_IsListOpen( ))
		DMenu::CurrentMenu->Close( );
}

void Continue_OpenList( void )
{
	// Already showing: a second press is not a second list.
	if ( Continue_IsListOpen( ))
		return;

	// [rc4l] Constructed rather than declared in menudef. The list has no items a descriptor could
	// describe -- its rows come from the history at the moment it opens -- so a descriptor would be
	// an empty menu whose only purpose was to name a class.
	M_ActivateMenu( new DFUAContinueMenu( DMenu::CurrentMenu ));

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
