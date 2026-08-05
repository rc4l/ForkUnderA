// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/panel-menu/computation/panelmenu_compute.h"

namespace zx
{

MenuExtent ComputeListMenuExtent(const MenuItemBox *items, int count, int selectOfsX, int linespacing)
{
	MenuExtent e;
	e.valid = false;
	e.left = e.right = e.top = e.bottom = 0;

	if (items == 0 || count <= 0)
		return e;

	bool any = false;
	int left = 0, right = 0, top = 0, bottom = 0;

	for (int i = 0; i < count; ++i)
	{
		const MenuItemBox &it = items[i];
		// Negative y is the CleanNoMove path -- drawn outside the virtual page, so not part of it.
		if (it.y < 0)
			continue;
		// An item that cannot report a width contributes no extent.
		if (it.w <= 0)
			continue;

		const int drawnLeft = it.x + (selectOfsX < 0 ? selectOfsX : 0);
		const int drawnRight = it.x + it.w;
		const int drawnTop = it.y;
		// Fall back to linespacing only for items that cannot report their own height.
		const int drawnBottom = it.y + (it.h > 0 ? it.h : linespacing);

		if (!any)
		{
			left = drawnLeft;
			right = drawnRight;
			top = drawnTop;
			bottom = drawnBottom;
			any = true;
			continue;
		}

		if (drawnLeft < left) left = drawnLeft;
		if (drawnRight > right) right = drawnRight;
		if (drawnTop < top) top = drawnTop;
		if (drawnBottom > bottom) bottom = drawnBottom;
	}

	if (!any || right <= left)
		return e;

	e.valid = true;
	e.left = left;
	e.right = right;
	e.top = top;
	e.bottom = bottom;
	return e;
}

// [rc4l] One sixteenth of each axis, so the margin scales with resolution instead of being a pixel
// count that looks generous at 4K and swallows the card at 640x480.
static int MarginOf(int extent)
{
	const int m = extent / 16;
	return m > 0 ? m : 0;
}

PanelBounds ComputePanelBounds(int screenW, int screenH, int wantW, int wantTop, int wantBottom)
{
	PanelBounds b;
	b.w = 0; b.top = 0; b.bottom = 0;
	if (screenW <= 0 || screenH <= 0)
		return b;

	const int marginX = MarginOf(screenW);
	const int marginY = MarginOf(screenH);

	const int maxW = screenW - 2 * marginX;
	b.w = wantW < maxW ? wantW : maxW;
	if (b.w < 0) b.w = 0;

	b.top = wantTop < marginY ? marginY : wantTop;
	const int maxBottom = screenH - marginY;
	b.bottom = wantBottom > maxBottom ? maxBottom : wantBottom;
	// A content span taller than the screen would invert here; collapse to the margin band instead of
	// handing back a negative height.
	if (b.bottom < b.top) b.bottom = b.top;
	return b;
}

} // namespace zx