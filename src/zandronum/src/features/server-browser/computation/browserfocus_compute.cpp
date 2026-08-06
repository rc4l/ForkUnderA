// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/browserfocus_compute.h"

namespace zx
{

NavResult ComputeNav( BrowserFocus focus, NavKey key, bool hasRows, bool onLastTab )
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
		{
			// The start of the row is the start of the row. Only a tab that has something to its left
			// steps left; the first one stays put rather than wrapping to the far end, for the same
			// reason Right does not wrap.
			out.tabStep = onLastTab ? -1 : 0;
		}
		else if ( key == NavKey::Right )
		{
			// Off the end of the tabs is the search box, not a wrap back to the first one -- they are
			// stops on the same row, and looping among the tabs would leave the box unreachable.
			if ( onLastTab )
				out.focus = BrowserFocus::Search;
			else
				out.tabStep = 1;
		}
		else if (( key == NavKey::Down ) && hasRows )
			out.focus = BrowserFocus::Rows;
		break;

	case BrowserFocus::Search:
		// [rc4l] LEFT AND RIGHT ARE NOT NAVIGATION HERE. They belong to the caret -- a text field that
		// jumped to another control when you tried to move through what you had typed would be
		// unusable, and it is the one thing a focused field must claim.
		//
		// Up and down still leave, because this is a single line: there is nowhere within the field
		// for them to go, so they mean what they mean everywhere else on the screen. Down lands in the
		// list; up goes back to the tabs, the only other thing on this row.
		if ( key == NavKey::Up )
			out.focus = BrowserFocus::Tabs;
		else if ( key == NavKey::Down )
			out.focus = hasRows ? BrowserFocus::Rows : BrowserFocus::Tabs;
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
