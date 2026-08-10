// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Which section the menus should open on.
//
// This exists because the answer was got wrong three times running, each time by reasoning about a
// different moment in the teardown, and each wrong answer looked fine until a particular order of
// keypresses. The rule is small enough to state in full and so belongs somewhere it can be stated in
// full, with the orders that broke it written down as tests rather than remembered.
//
// The rule:
//   1. A finished join outranks everything. The player was promised the browser and is owed it.
//   2. In a game, never the browser. Escape there means the in-game menu, the one thing on screen
//      that can get them out again.
//   3. Otherwise, wherever they last were.
//
// "Wherever they last were" is OBSERVED while the menus are up, not recorded when they close. Two
// earlier versions hooked the close and both were wrong in opposite directions: DMenu::Close moves
// CurrentMenu to the parent before M_ClearMenus runs, so asking there asks at the one moment nothing
// is open; and switching tabs replaces the menu without closing anything, so a close hook never
// fires and its answer goes stale in the other direction. Hence NoteSectionShown, called from the
// drawing: the last frame that had a menu on it cannot be wrong about which menu that was.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_MENURESUME_COMPUTE_H
#define ZX_MENURESUME_COMPUTE_H

namespace zx
{

enum class MenuSection
{
	MainMenu,
	Browser,
};

struct MenuResumeIn
{
	// The section the player was last looking at, per NoteSectionShown.
	MenuSection lastShown;

	// A join finished while they were away and the band told them to open the menu.
	bool joinReady;

	// Connected to a server, so the menus belong to the game rather than to browsing for one.
	bool inGame;

	MenuResumeIn()
		: lastShown(MenuSection::MainMenu), joinReady(false), inGame(false) {}
};

MenuSection ComputeMenuToOpen(const MenuResumeIn &in);

} // namespace zx

#endif // ZX_MENURESUME_COMPUTE_H
