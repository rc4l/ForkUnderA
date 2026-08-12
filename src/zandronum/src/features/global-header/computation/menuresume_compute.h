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
//   2. Otherwise, wherever they last were.
//
// There used to be a third: never the browser while in a game. It was added while chasing an Escape
// that arrived at the browser from the in-game menu, and it was the wrong fix -- the cause was the
// tab bar making one section a child of the other, so backing out of one WAS arriving at the other.
// Once that was fixed the guard did nothing but overrule the player: somebody hosting a duel, who
// had been on the browser, was sent to the main menu because of where they were rather than what
// they had chosen. Being in a game is not a reason to forget.
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

	MenuResumeIn()
		: lastShown(MenuSection::MainMenu), joinReady(false) {}
};

MenuSection ComputeMenuToOpen(const MenuResumeIn &in);

} // namespace zx

#endif // ZX_MENURESUME_COMPUTE_H
