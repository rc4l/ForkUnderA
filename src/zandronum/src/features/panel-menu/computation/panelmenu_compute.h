// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Pure layout decision for DFUAPanelListMenu (src/menu/listmenu.cpp): the extent of a list
// menu's drawn content, which the rounded panel is then sized around.
//
// Header-pure by the features/ rules -- no engine types. The caller flattens its FListMenuItem list
// into MenuItemBox values and hands them over.

#ifndef ZX_PANELMENU_COMPUTE_H
#define ZX_PANELMENU_COMPUTE_H

namespace zx
{

// One measurable menu item, in the menu's own virtual coordinates.
struct MenuItemBox
{
	// The DRAWN corner, not the item's stated position -- a patch paints at
	// (x - leftoffset, y - topoffset), and measuring the stated position instead builds a rectangle
	// the content does not sit in. The caller applies that correction (FListMenuItem::GetDrawnX /
	// GetDrawnY); by the time a box reaches here it is already where the pixels land.
	int x;
	int y;
	int w;
	// The item's own painted height, or 0 when it cannot say -- the extent then falls back to the
	// descriptor's linespacing for that item. Linespacing is the distance BETWEEN rows and runs
	// taller than the glyphs, so padding below it leaves the leftover leading as extra gap.
	int h;
};

struct MenuExtent
{
	// False when nothing could be measured (every item hidden, zero-width, or off-page). The caller
	// should draw unpanelled rather than invent a rectangle.
	bool valid;
	int left;
	int right;
	int top;
	int bottom;
};

// items/count may be null/zero. selectOfsX is the descriptor's cursor offset: the selection cursor
// hangs to the LEFT of its row when negative, so the row's drawn extent starts there rather than at
// the item's own x. linespacing is added once, to give the last row its own height.
MenuExtent ComputeListMenuExtent(const MenuItemBox *items, int count, int selectOfsX, int linespacing);

// The panel's screen rectangle, bounded so it always reads as a card over the screen rather than a
// sheet covering it.
struct PanelBounds
{
	int w;
	int top;
	int bottom;
};

// Clamp a wanted panel size to leave a visible margin on every side. Content is sized from menu items
// and menu items are mod-supplied -- a replaced logo lump can be arbitrarily large -- so the margin is
// enforced rather than merely hoped for: oversized artwork is clipped by the panel instead of
// inflating it past the screen. Degenerate screens (zero/negative) yield an empty rect.
PanelBounds ComputePanelBounds(int screenW, int screenH, int wantW, int wantTop, int wantBottom);

} // namespace zx

#endif // ZX_PANELMENU_COMPUTE_H
