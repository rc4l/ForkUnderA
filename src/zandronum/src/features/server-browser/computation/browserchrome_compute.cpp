// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/browserchrome_compute.h"

namespace zx
{

unsigned ComputeVisibleParts( BrowserPhase phase, bool hasSelection, bool downloadRunning )
{
	unsigned parts = 0;

	switch ( phase )
	{
	case BrowserPhase::Loading:
		// [rc4l] The spinner, and the tabs -- because THE TABS ARE NOT PART OF THE ANSWER, they are
		// how the player asks a different question.
		//
		// This used to be the spinner alone, on the reasoning that nothing had answered yet so there
		// was nothing to put tabs on. That was true while every tab was a filter over the same
		// not-yet-arrived list. HOST is not: it never depended on the query at all, and hiding it
		// while a query runs TRAPS the player away from it -- switch to PUBLIC while hosting, watch
		// the re-query start, and the panel with the button that stops your server is gone until
		// something answers. On a machine with no internet, that is until the query times out.
		//
		// The rest still goes: a list, a detail panel and a footer would all be promising content
		// that genuinely is not there yet.
		parts = kPartTabs | kPartPlaceholder;
		break;

	case BrowserPhase::Empty:
		// The tabs come back here even though the list is still empty: by far the likeliest reason a
		// player is looking at "No servers found" is that they are on the wrong tab, so the fix has to
		// be reachable from the screen that reports the problem.
		parts = kPartTabs | kPartPlaceholder | kPartFooter;
		break;

	case BrowserPhase::Ready:
		parts = kPartTabs | kPartList | kPartFooter;
		if ( hasSelection )
			parts |= kPartDetail;
		break;
	}

	if ( downloadRunning )
	{
		parts |= kPartFooter;

		// [rc4l] And the panel, whatever the phase and whether or not anything is selected -- because
		// the CANCEL button lives in it, and the server a transfer belongs to can DIE while the
		// transfer is still running. It then times out of the list, the selection goes with it, and
		// the player is left watching a frozen progress line with no way to stop it. The one control
		// that can end a download must not depend on the thing that started it still existing.
		parts |= kPartDetail;
	}

	return parts;
}

unsigned ComputeHostParts( bool foreignDownloadRunning )
{
	// The tabs stay -- they are how the player gets back -- and the hosting panel takes the space the
	// list and the detail panel would have had.
	unsigned parts = kPartTabs | kPartHost;

	// [rc4l] Only for a transfer that is NOT this panel's own.
	//
	// The detail panel is dragged in here to carry the CANCEL button for a download the player
	// started on another tab, which would otherwise have nowhere to be stopped from. Hosting now
	// downloads too, and its own button says CANCEL while it does -- so doing this for its own
	// transfer drew the server-list panel straight over the host panel, with two CANCELs on screen
	// describing one download and a "that server is no longer listed" underneath them.
	if ( foreignDownloadRunning )
		parts |= kPartFooter | kPartDetail;

	return parts;
}

} // namespace zx
