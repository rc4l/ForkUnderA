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
		// Nothing has answered yet, so there is nothing to put tabs on, nothing to list, and nothing
		// to describe. The spinner alone -- which is the entire honest content of this moment.
		parts = kPartPlaceholder;
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
		parts |= kPartFooter;

	return parts;
}

} // namespace zx
