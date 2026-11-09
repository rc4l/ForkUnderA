// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/controls-search/controlsmenu.h"

#include "features/controls-search/computation/controlfilter_compute.h"

#include "menu/menu.h"
#include "c_dispatch.h"
#include "w_wad.h"
#include "sc_man.h"
#include "v_font.h"
#include "g_level.h"
#include "d_player.h"
#include "v_video.h"
#include "gi.h"
#include "i_system.h"
#include "c_bind.h"
#include "v_palette.h"
#include "d_event.h"
#include "d_gui.h"
#include "gstrings.h"
#include "menu/optionmenuitems.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace
{

const int kMaxSearchLength = 32;

// [Doruk-Aksoy] A label as drawn: a `$NAME` label is looked up in the string table first.
std::string ResolvedLabel( FOptionMenuItem *item )
{
	const char *label = item->GetLabel( );
	if ( label == NULL )
		return std::string( );
	if ( *label == '$' )
		label = GStrings( label + 1 );
	return ( label != NULL ) ? std::string( label ) : std::string( );
}

// [Doruk-Aksoy] How the filter sees a menu row: anything selectable by label and action (a binding's action is its command), untitled blanks as spacers, other text as section titles.
zx::ControlRow RowOf( FOptionMenuItem *item )
{
	const std::string label = ResolvedLabel( item );

	if ( item->Selectable( ))
		return zx::ControlRow( zx::ControlRowKind::Item, label, item->GetAction( NULL ).GetChars( ));

	const bool bBlank = ( label.find_first_not_of( " 	" ) == std::string::npos );
	return zx::ControlRow( bBlank ? zx::ControlRowKind::Spacer : zx::ControlRowKind::Heading, label );
}

} // namespace

//*****************************************************************************
//
// [Doruk-Aksoy] The search row: its label on the left, the typed text or a hint on the right, and how many bindings match.
class FOptionMenuItemControlSearch : public FOptionMenuItem
{
public:
	FString mText;
	int mMatches;

	FOptionMenuItemControlSearch( ) : FOptionMenuItem( "Search", NAME_None ), mMatches( -1 ) { }

	int Draw( FOptionMenuDescriptor *desc, int y, int indent, bool selected )
	{
		drawLabel( indent, y, selected ? OptionSettings.mFontColorSelection : OptionSettings.mFontColor );

		const int textY = y + ( OptionSettings.mLinespacing - 8 ) * CleanYfac_1;
		int x = indent + CURSORSPACE;

		if ( mText.IsEmpty( ) && !selected )
		{
			screen->DrawText( SmallFont, CR_DARKGRAY, x, textY, "type to filter, * and ? match anything",
				DTA_CleanNoMove_1, true, TAG_DONE );
			return indent;
		}

		FString shown = mText;
		if ( selected && (( DMenu::MenuTime % 8 ) < 6 ))
			shown += "_";
		screen->DrawText( SmallFont, OptionSettings.mFontColorValue, x, textY, shown, DTA_CleanNoMove_1, true, TAG_DONE );

		if ( mText.IsNotEmpty( ) && ( mMatches >= 0 ))
		{
			FString count;
			count.Format( "  %d found", mMatches );
			x += ( SmallFont->StringWidth( mText ) + SmallFont->StringWidth( "_" )) * CleanXfac_1;
			screen->DrawText( SmallFont, ( mMatches > 0 ) ? CR_DARKGRAY : CR_RED, x, textY, count,
				DTA_CleanNoMove_1, true, TAG_DONE );
		}
		return indent;
	}

	bool Selectable( )
	{
		return true;
	}
};

//*****************************************************************************
//
// [Doruk-Aksoy] Runs a private copy of the controls descriptor whose rows are the menu's fixed top, the search row, and what the filter keeps.
class DFUAControlsMenu : public DOptionMenu
{
	DECLARE_CLASS( DFUAControlsMenu, DOptionMenu )

	FOptionMenuDescriptor *mOriginal;
	FOptionMenuDescriptor mFiltered;
	FOptionMenuItemControlSearch *mSearch;
	FOptionMenuItemStaticText *mGap;
	int mSearchIndex;

public:
	DFUAControlsMenu( ) : mOriginal( NULL ), mSearch( NULL ), mGap( NULL ), mSearchIndex( 0 ) { }

	~DFUAControlsMenu( )
	{
		// [Doruk-Aksoy] The rows belong to the shared descriptor, so the copy lets go of them before its array would delete them.
		mFiltered.mItems.Clear( );
		delete mSearch;
		delete mGap;
	}

	void Init( DMenu *parent = NULL, FOptionMenuDescriptor *desc = NULL )
	{
		Super::Init( parent, desc );
		if ( desc == NULL )
			return;

		mOriginal = desc;
		mFiltered.mMenuName = desc->mMenuName;
		mFiltered.mNetgameMessage = desc->mNetgameMessage;
		mFiltered.mType = desc->mType;
		mFiltered.mClass = desc->mClass;
		mFiltered.mTitle = desc->mTitle;
		mFiltered.mPosition = desc->mPosition;
		mFiltered.mIndent = desc->mIndent;
		mFiltered.mDontDim = desc->mDontDim;
		mFiltered.mNetgameOnly = desc->mNetgameOnly;
		mFiltered.mRequiresRCON = desc->mRequiresRCON;
		mFiltered.mDrawTop = desc->mDrawTop;

		mSearch = new FOptionMenuItemControlSearch( );
		mGap = new FOptionMenuItemStaticText( " ", false );
		mSearchIndex = std::min( std::max( desc->mScrollTop, 0 ), static_cast<int>( desc->mItems.Size( )));
		mFiltered.mScrollTop = mSearchIndex + 2;
		mFiltered.mSelectedItem = -1;
		mFiltered.mScrollPos = 0;

		mDesc = &mFiltered;
		Rebuild( false );
	}

	bool Responder( event_t *ev )
	{
		if (( mOriginal != NULL ) && ( ev->type == EV_GUI_Event ) && ( ev->subtype == EV_GUI_Char ))
		{
			const int ch = ev->data1;
			if (( ch >= ' ' ) && ( ch < 127 ) && ( static_cast<int>( mSearch->mText.Len( )) < kMaxSearchLength ))
			{
				mSearch->mText += static_cast<char>( ch );
				Rebuild( true );
			}
			return true;
		}
		return Super::Responder( ev );
	}

	bool MenuEvent( int mkey, bool fromcontroller )
	{
		const bool bOnSearch = ( mOriginal != NULL ) && ( mFiltered.mSelectedItem == mSearchIndex );

		if ( bOnSearch && ( mkey == MKEY_Clear ))
		{
			if ( mSearch->mText.IsNotEmpty( ))
			{
				mSearch->mText.Truncate( static_cast<long>( mSearch->mText.Len( )) - 1 );
				Rebuild( true );
			}
			return true;
		}

		// [Doruk-Aksoy] Escape on a non-empty search empties it first, as search boxes do, and leaves on the next press.
		if ( bOnSearch && ( mkey == MKEY_Back ) && mSearch->mText.IsNotEmpty( ))
		{
			mSearch->mText = "";
			Rebuild( true );
			return true;
		}

		if ( bOnSearch && ( mkey == MKEY_Enter ))
			return Super::MenuEvent( MKEY_Down, fromcontroller );

		return Super::MenuEvent( mkey, fromcontroller );
	}

private:
	// [Doruk-Aksoy] Refills the copy from the shared rows; typing puts the cursor back on the search row at the top.
	void Rebuild( bool bTyped )
	{
		TArray<FOptionMenuItem *> &all = mOriginal->mItems;

		std::vector<zx::ControlRow> rows;
		for ( unsigned i = mSearchIndex; i < all.Size( ); ++i )
			rows.push_back( RowOf( all[i] ));

		const std::vector<int> kept = zx::ComputeControlFilter( rows, mSearch->mText.GetChars( ));

		mFiltered.mItems.Clear( );
		for ( int i = 0; i < mSearchIndex; ++i )
			mFiltered.mItems.Push( all[i] );
		mFiltered.mItems.Push( mSearch );
		mFiltered.mItems.Push( mGap );

		int matches = 0;
		for ( size_t i = 0; i < kept.size( ); ++i )
		{
			mFiltered.mItems.Push( all[mSearchIndex + kept[i]] );
			if ( rows[kept[i]].kind == zx::ControlRowKind::Item )
				++matches;
		}
		mSearch->mMatches = matches;

		if ( bTyped || ( mFiltered.mSelectedItem < 0 ) || ( mFiltered.mSelectedItem >= static_cast<int>( mFiltered.mItems.Size( ))))
		{
			mFiltered.mSelectedItem = mSearchIndex;
			mFiltered.mScrollPos = 0;
		}
	}
};

IMPLEMENT_CLASS( DFUAControlsMenu )

namespace zx
{

void ControlsSearch_Install( FOptionMenuDescriptor *desc )
{
	if (( desc != NULL ) && ( desc->mClass == NULL ))
		desc->mClass = RUNTIME_CLASS( DFUAControlsMenu );
}

} // namespace zx
