// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "scrollview_compute.h"

namespace zx
{

bool RowIntersectsView(int rowTop, int rowHeight, int viewTop, int viewBottom)
{
	return ((rowTop + rowHeight) > viewTop) && (rowTop < viewBottom);
}

bool RowFullyInView(int rowTop, int rowHeight, int viewTop, int viewBottom)
{
	return (rowTop >= viewTop) && ((rowTop + rowHeight) <= viewBottom);
}

int ClampScroll(int scroll, int maxScroll)
{
	// Order matters when maxScroll is negative, which it is whenever the content is shorter than the
	// viewport. The lower bound is applied second so that case settles on 0 rather than on a negative
	// ceiling, which would scroll a form that fits.
	if (scroll > maxScroll)
		scroll = maxScroll;
	if (scroll < 0)
		scroll = 0;
	return scroll;
}

int ScrollToReveal(int scroll, int rowTop, int rowHeight, int viewTop, int viewBottom,
	int maxScroll)
{
	// Above the top and below the bottom are exclusive: a row taller than the viewport satisfies
	// neither test after the first correction, so it settles with its top edge showing rather than
	// oscillating between the two.
	if (rowTop < viewTop)
		scroll -= (viewTop - rowTop);
	else if ((rowTop + rowHeight) > viewBottom)
		scroll += ((rowTop + rowHeight) - viewBottom);

	return ClampScroll(scroll, maxScroll);
}

} // namespace zx
