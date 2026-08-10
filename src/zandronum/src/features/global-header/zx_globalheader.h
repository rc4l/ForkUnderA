// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The bar of tabs across the top of every menu.
//
// Drawn from M_Drawer, which is the one place every menu in the engine passes through, so the bar
// exists over stock menus, our own, and whatever a mod ships without any of them knowing about it.
// That is the whole reason it lives here rather than in a menu descriptor: MENUDEF is replaced
// wholesale by any total conversion, and a route to online play that a mod can delete by accident
// is not a route.
//
// WHICH TAB IS LIT IS NOT STORED. It is asked of the world every frame -- the browser being open IS
// "Play Online" being the current tab -- because a stored copy is a second source of truth that
// goes stale the moment anything reaches a menu by a path the bar did not drive. Escape, a console
// command and a mod's own submenu all move the player around without asking us first.
//
// Geometry and colour live in computation/, and the mouse and the drawing both read them from
// there, so a click cannot land somewhere the pill was not drawn.

#ifndef ZX_GLOBALHEADER_H
#define ZX_GLOBALHEADER_H

namespace zx
{

// Paint the bar. Called from M_Drawer AFTER the menu, so the bar is never drawn under the thing it
// is supposed to sit above.
void GlobalHeader_Draw();

// How far every menu must move down, in the stock menus' own 320x200 units. One number, asked for
// in one place, so the drawing and the mouse cannot disagree about it.
int GlobalHeader_MenuOffsetY();

// The screen y just past the bar's bottom edge, in REAL pixels, for chrome that is positioned in
// screen coordinates rather than in any virtual space. The back button is the one that needs it.
int GlobalHeader_ScreenBottom();

// Whether the last menu the player was looking at was the browser, so opening the menus again
// returns there rather than to the main menu. Observed while drawing; ask it when deciding.
bool GlobalHeader_ResumeBrowser();

// [rc4l] Move every menu descriptor down by that offset, once, after MENUDEF is fully parsed.
//
// THE DESCRIPTORS AND NOT THE DRAWING. Shifting at draw time would move the pixels and leave every
// menu's mouse hit-test where it was, and a click landing one row off is invisible in a screenshot
// -- it still looks right, it just does the wrong thing. Menus derive both their drawing and their
// hit-testing from these same positions, so moving the positions moves both by construction and
// there is no second place that has to remember.
//
// Runs at the end of parsing, so a mod's own menus are shifted too.
void GlobalHeader_ShiftMenusDown();

// Does the bar currently own the arrow keys? While it does, the menu underneath sees none of them.
bool GlobalHeader_HasFocus();

// Give the bar the arrows, landing on whichever tab is currently lit, or hand them back. Called
// when Up leaves the top of a menu, and when the player leaves the bar again by going down.
void GlobalHeader_TakeFocus();
void GlobalHeader_ReleaseFocus();

// The four navigation keys, offered to the bar first. Answers true when the bar consumed the key,
// which is also how the caller knows not to pass it on to the menu underneath.
bool GlobalHeader_NavLeft();
bool GlobalHeader_NavRight();
bool GlobalHeader_NavDown();

// Act on the lit tab. True when something happened.
bool GlobalHeader_Activate();

// Pointer moved, or was clicked, in screen pixels. Both answer true when the bar took the event.
//
// The bar answers for its whole surface and not merely its pills: a click on the background between
// two tabs must not fall through to the menu underneath, which is still there, still listening, and
// has a row sitting exactly where the player was not aiming.
bool GlobalHeader_MouseMove(int screenX, int screenY);
bool GlobalHeader_MouseClick(int screenX, int screenY);

} // namespace zx

#endif // ZX_GLOBALHEADER_H
