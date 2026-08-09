// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Keep the main menu's FIRST entry leading somewhere useful, whatever a mod has done to it.
//
// Two cases, and the difference is whether anyone replaced our main menu.
//
// NOBODY DID. Our own menudef.txt makes index 0 "Play Now!" pointing straight at the browser, and
// this file does nothing at all. One keypress, no question in between.
//
// A MOD DID. Then index 0 is theirs, leads into their flow, and taking it would be taking their game
// away. It is rerouted through the Online/Offline choice instead, with their original destination
// moved onto Offline, so nothing is lost and the browser becomes reachable.
//
// The problem this solves: the server browser hung off our own FUANewGameMenu, wired in by editing
// the stock MENUDEF's "New Game" item. A mod's MENUDEF replaces ours wholesale -- MM8BDM and most
// total conversions ship their own main menu -- so under those mods our entry simply did not exist
// and the browser was unreachable. Editing menudef.txt harder cannot fix that; whoever loads last
// wins, and it is never us.
//
// So this reaches for the item by POSITION instead of by name or target. Index 0 of the main menu is
// "start playing" in every Doom-lineage menu ever shipped -- New Game, Single Player, whatever a
// conversion calls it -- while its label, its patch and the menu it opens are all mod-specific. The
// position is the only stable thing, so that is what we key on.
//
// The original destination is not discarded: it moves to the Offline entry. Picking Offline does
// exactly what index 0 did before, so a mod's own flow is preserved rather than replaced -- which
// is what makes this safe to do to a menu we did not write.

#include "doomtype.h"
#include "c_dispatch.h"
#include "menu/menu.h"

namespace zx
{

namespace
{

// The main menu's original index-0 destination, moved onto the Offline entry.
FName g_OriginalFirstAction = NAME_None;
int   g_OriginalFirstParam = 0;

// Find a descriptor's list-menu form, or NULL if it is absent or a different kind of menu.
FListMenuDescriptor *FindListMenu(FName name)
{
	FMenuDescriptor **desc = MenuDescriptors.CheckKey(name);
	if (desc == NULL || (*desc)->mType != MDESC_ListMenu)
		return NULL;
	return static_cast<FListMenuDescriptor *>(*desc);
}

// The first item a player can actually pick. Index 0 of mItems is usually the logo, not a row, so
// "first entry" means the first SELECTABLE one -- otherwise this would rewrite the artwork's action
// and leave the real menu untouched.
FListMenuItem *FirstSelectable(FListMenuDescriptor *ld)
{
	for (unsigned i = 0; i < ld->mItems.Size(); ++i)
	{
		if (ld->mItems[i]->Selectable())
			return ld->mItems[i];
	}
	return NULL;
}

} // namespace

void RouteMainMenuThroughOnlineOffline()
{
	FListMenuDescriptor *mainMenu = FindListMenu(NAME_Mainmenu);
	FListMenuDescriptor *ourMenu = FindListMenu("FUANewGameMenu");
	if (mainMenu == NULL || ourMenu == NULL)
		return;			// nothing to rewrite, or our own menu was overridden away

	FListMenuItem *first = FirstSelectable(mainMenu);
	if (first == NULL)
		return;

	int param = 0;
	const FName action = first->GetAction(&param);

	// Idempotent: re-running (a second MENUDEF pass, a restart) must not capture our own menu as the
	// "original", which would make Offline reopen the Online/Offline choice forever.
	if (action == FName("FUANewGameMenu"))
		return;

	// [rc4l] NOBODY REPLACED THE MAIN MENU, so leave it alone: our own menudef.txt already sends
	// "Play Now!" straight to the browser, and the Online/Offline step exists only to preserve a
	// destination we would otherwise be taking away.
	//
	// Reaching the browser in two keypresses when one would do is a step that asks a question the
	// player has already answered by pressing the first row of a menu called Play Now. The choice is
	// worth keeping only where there IS something else to choose: under a mod that ships its own main
	// menu, index 0 leads into that mod's flow, and Offline is what stops us stealing it.
	//
	// Testing the action rather than counting MENUDEF lumps is the more exact question. Plenty of
	// mods add an OptionMenu and never touch MainMenu, and those should still get the direct route.
	// What matters is whether index 0 is still ours, not whether a lump exists somewhere.
	if (action == FName("ZA_Browser"))
		return;

	g_OriginalFirstAction = action;
	g_OriginalFirstParam = param;
	first->SetAction("FUANewGameMenu");

	// Hand the original destination to Offline. Offline is the LAST selectable row of our menu --
	// it has exactly two, Online then Offline -- and we look it up rather than hardcode an index so
	// that adding a row later does not silently rewire the wrong one.
	FListMenuItem *offline = NULL;
	for (unsigned i = 0; i < ourMenu->mItems.Size(); ++i)
	{
		if (ourMenu->mItems[i]->Selectable())
			offline = ourMenu->mItems[i];
	}
	if (offline != NULL)
		offline->SetAction(g_OriginalFirstAction);
}

} // namespace zx
