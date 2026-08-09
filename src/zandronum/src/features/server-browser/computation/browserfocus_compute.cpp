// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/browserfocus_compute.h"

namespace zx
{

namespace
{

// The region directly above the list: the sub-tabs when the selected tab has them, the tabs when it
// does not. Everything that leaves upwards asks this rather than naming a zone, so a tab without a
// sub-row does not strand focus on a row that is not drawn.
BrowserFocus AboveTheList( const NavWhere &where )
{
	return ( where.subCount > 0 ) ? BrowserFocus::SubTabs : BrowserFocus::Tabs;
}

// Down into the list, or stay put when there is nothing to land on.
BrowserFocus IntoTheList( const NavWhere &where, BrowserFocus stay )
{
	return where.hasRows ? BrowserFocus::Rows : stay;
}

} // namespace

NavResult ComputeNav( BrowserFocus focus, NavKey key, const NavWhere &where )
{
	NavResult out;
	out.focus = focus;

	// A focus that points at rows which are no longer there answers as the region above them, which
	// is always occupiable.
	if ( !where.hasRows && ( focus == BrowserFocus::Rows ))
		out.focus = focus = AboveTheList( where );

	// Same for a sub-tab row belonging to a tab that has none: switching tabs can pull it out from
	// under the focus.
	if (( where.subCount <= 0 ) && ( focus == BrowserFocus::SubTabs ))
		out.focus = focus = BrowserFocus::Tabs;

	switch ( focus )
	{
	case BrowserFocus::Tabs:
		if ( key == NavKey::Left )
		{
			// The start of the row is the start of the row. A tab with something to its left steps
			// left; the first one stays put rather than wrapping to the far end, for the same reason
			// Right does not wrap.
			out.tabStep = ( where.tabIndex > 0 ) ? -1 : 0;
		}
		else if ( key == NavKey::Right )
		{
			// Nothing sits past the last tab now that the search box has moved down to the row it
			// filters, so the end of the row is simply the end of the row.
			out.tabStep = ( where.tabIndex < where.tabCount - 1 ) ? 1 : 0;
		}
		else if ( key == NavKey::Down )
		{
			// Into the sub-tabs when this tab has them, otherwise straight past to the list. A tab
			// with neither keeps the focus, because the caller owns what down means there: PLAY hands
			// it to the hosting form.
			if ( where.subCount > 0 )
				out.focus = BrowserFocus::SubTabs;
			else
				out.focus = IntoTheList( where, BrowserFocus::Tabs );
		}
		break;

	case BrowserFocus::SubTabs:
		if ( key == NavKey::Left )
		{
			out.subStep = ( where.subIndex > 0 ) ? -1 : 0;
		}
		else if ( key == NavKey::Right )
		{
			// Off the end of the sub-tabs is the search box, not a wrap back to the first one. They
			// are stops on the same row, and looping among the sub-tabs would leave the box
			// unreachable, which is the bug the old single row had before the search box existed.
			if ( where.subIndex < where.subCount - 1 )
				out.subStep = 1;
			else
				out.focus = BrowserFocus::Search;
		}
		else if ( key == NavKey::Up )
			out.focus = BrowserFocus::Tabs;
		else if ( key == NavKey::Down )
			out.focus = IntoTheList( where, BrowserFocus::SubTabs );
		break;

	case BrowserFocus::Search:
		// [rc4l] LEFT AND RIGHT ARE NOT NAVIGATION HERE. They belong to the caret: a text field that
		// jumped to another control when you tried to move through what you had typed would be
		// unusable, and it is the one thing a focused field must claim.
		//
		// Up and down still leave, because this is a single line: there is nowhere within the field
		// for them to go, so they mean what they mean everywhere else on the screen.
		if ( key == NavKey::Up )
			out.focus = AboveTheList( where );
		else if ( key == NavKey::Down )
			out.focus = IntoTheList( where, AboveTheList( where ));
		break;

	case BrowserFocus::Rows:
		if ( key == NavKey::Up )
			out.rowStep = -1;
		else if ( key == NavKey::Down )
			out.rowStep = 1;
		else if ( key == NavKey::Right )
			out.focus = BrowserFocus::Action;
		// Left is deliberately nothing: there is no region to the left, and wrapping round to the
		// button would make the two horizontal keys disagree about which way the layout runs.
		break;

	case BrowserFocus::Dialog:
		// Modal. Nothing moves out of it, and dialog_compute decides what happens inside.
		break;

	case BrowserFocus::Host:
		// [rc4l] The form owns up and down, they move between its fields, and the caller does that
		// walk because only it knows how many there are. What this unit decides is the one edge that
		// leaves: off the TOP is the tabs, which is how the player gets back to the list. Left and
		// right belong to the caret in whichever field is focused, exactly as in the search box.
		break;

	case BrowserFocus::Action:
		if ( key == NavKey::Left )
			out.focus = IntoTheList( where, AboveTheList( where ));
		else if ( key == NavKey::Up )
			out.focus = AboveTheList( where );
		else if ( key == NavKey::Down )
			out.focus = BrowserFocus::Refresh;
		break;

	case BrowserFocus::Refresh:
		// [rc4l] Up goes back where it came from, and that is the WHOLE contract for this zone.
		//
		// A zone you can enter and not leave is worse than one you cannot enter at all: the mouse
		// user never noticed the button was unreachable, but a keyboard user who arrives and gets
		// stuck has lost the menu. So the return edge is the first thing here, not an afterthought.
		//
		// Left and right are deliberately nothing. The footer holds this button and the registry
		// status bar beside it, and that bar is a readout rather than a control -- there is nothing
		// horizontal to reach, and inventing a stop on a thing you cannot press would be worse than
		// the keys doing nothing.
		if ( key == NavKey::Up )
			out.focus = BrowserFocus::Action;
		break;
	}

	return out;
}

} // namespace zx
