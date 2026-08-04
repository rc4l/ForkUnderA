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
	int x;
	int y;
	int w;
	// Rows of empty space between y and where the item starts painting. Zero for text, whose box is
	// its ink; non-zero for artwork authored with slack above it (a title patch). Measuring the box
	// instead of the ink is what puts a visibly larger gap above a logo than below the rows under it.
	int inkTop;
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

} // namespace zx

#endif // ZX_PANELMENU_COMPUTE_H
