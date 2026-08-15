// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See zx_menupin.h for why this exists.

#include "features/menu-pin/zx_menupin.h"

#include "doomtype.h"
#include "menu/menu.h"

// [rc4l] optionmenuitems.h is not a standalone header. It defines every menu item class inline and
// leans on whatever its one real includer, menu/menudef.cpp, happens to have pulled in first:
// C_DoCommand, screen, the DTA_* tags, FKeyBindings, C_NameKeys. Including it cold gives a wall of
// undeclared identifiers that look like our bug and are not. This mirrors menudef.cpp's list for the
// symbols that header actually reaches for.
#include "c_dispatch.h"
#include "c_bind.h"
#include "c_console.h"		// Printf
#include "v_font.h"
#include "v_video.h"
#include "v_palette.h"
#include "d_player.h"
#include "g_level.h"
#include "i_system.h"
#include "gi.h"
#include "templates.h"
#include "menu/optionmenuitems.h"

namespace zx
{

void MenuPin_RestoreFuaOptions( )
{
	// NAME_Optionsmenu, lowercase m: that is how namedef.h spells it, and FName lookups are exact.
	FMenuDescriptor **desc = MenuDescriptors.CheckKey( NAME_Optionsmenu );
	if (( desc == NULL ) || ( *desc == NULL ))
		return;

	// Only an option menu has items we can inspect. A mod turning OptionsMenu into something else
	// entirely is beyond what a one-line re-pin should be reaching into.
	if (( *desc )->mType != MDESC_OptionsMenu )
		return;

	FOptionMenuDescriptor *od = static_cast<FOptionMenuDescriptor *>( *desc );

	// Matched on the ACTION rather than the label: the action is the menu we link to, which is ours
	// and fixed, while a label is display text that a translation or a re-skin could legitimately
	// change. Looking for the words "FUA Options" would miss a renamed-but-present entry and add a
	// duplicate.
	const FName want( "FUAOptions" );
	for ( unsigned i = 0; i < od->mItems.Size( ); ++i )
	{
		if ( od->mItems[i] == NULL )
			continue;

		int param = 0;
		if ( od->mItems[i]->GetAction( &param ) == want )
			return;			// still there, nothing to do
	}

	// Gone. Put it back at the top, where our own menudef.txt puts it, so its position does not
	// depend on which mod happens to be loaded.
	FOptionMenuItem *item = new FOptionMenuItemSubmenu( "FUA Options", "FUAOptions" );
	od->mItems.Insert( 0, item );

	// Said out loud rather than fixed silently. If a mod is replacing OptionsMenu it is probably
	// dropping other engine entries too, and whoever is looking at a menu that lost something wants
	// to know the replacement happened at all.
	Printf( TEXTCOLOR_ORANGE "menu: a loaded mod replaced OptionsMenu without FUA Options; "
		"restored it.\n" );
}

} // namespace zx
