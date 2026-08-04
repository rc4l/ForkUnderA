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

} // namespace zx
