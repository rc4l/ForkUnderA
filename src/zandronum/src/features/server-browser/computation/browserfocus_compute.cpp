// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/browserfocus_compute.h"

namespace zx
{

NavResult ComputeNav( BrowserFocus focus, NavKey key, bool hasRows )
{
	NavResult out;
	out.focus = focus;
	out.tabStep = 0;
	out.rowStep = 0;

	// A focus that points at rows which are no longer there answers as the tabs, which is the one
	// region that is always occupiable.
	if ( !hasRows && ( focus == BrowserFocus::Rows ))
		out.focus = focus = BrowserFocus::Tabs;

	switch ( focus )
	{
	case BrowserFocus::Tabs:
		if ( key == NavKey::Left )
			out.tabStep = -1;
		else if ( key == NavKey::Right )
			out.tabStep = 1;
		else if (( key == NavKey::Down ) && hasRows )
			out.focus = BrowserFocus::Rows;
		break;

	case BrowserFocus::Rows:
		if ( key == NavKey::Up )
			out.rowStep = -1;
		else if ( key == NavKey::Down )
			out.rowStep = 1;
		else if ( key == NavKey::Right )
			out.focus = BrowserFocus::Action;
		// Left is deliberately nothing: there is no fourth region, and wrapping round to the button
		// would make the two horizontal keys disagree about which way the layout runs.
		break;

	case BrowserFocus::Action:
		if ( key == NavKey::Left )
			out.focus = hasRows ? BrowserFocus::Rows : BrowserFocus::Tabs;
		else if ( key == NavKey::Up )
			out.focus = BrowserFocus::Tabs;
		break;
	}

	return out;
}

} // namespace zx
