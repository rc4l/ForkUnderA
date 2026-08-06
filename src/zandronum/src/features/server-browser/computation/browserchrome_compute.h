// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Which parts of the browser are on screen right now.
//
// The browser has five movable parts -- tabs, list, detail panel, placeholder, footer -- and which
// of them belong on screen depends on what the browser is doing. That used to be decided part by
// part, at the point each one drew itself, which produced the state this unit exists to prevent: a
// browser still looking for servers drew its tabs, its rule, its column headings and an entirely
// empty black detail panel around the word "Looking for servers". Every one of those is a promise
// that there is something there. A surface with nothing in it does not read as "waiting", it reads
// as "broken".
//
// WHY A BITMASK. One unsigned, computed once per frame, no allocation and no branching per part at
// the call site -- and, more to the point, ONE PLACE where the answer lives. Drawing and hit-testing
// both ask this, so a control cannot be invisible and still clickable, which is what happens the
// moment those two work it out separately. Adding a part later is one bit here and one row in the
// table below, not a phase check threaded through another draw function.
//
// It is also strictly cheaper than what it replaces: the old code drew the detail panel's rounded
// gradient -- a Dim call per scanline -- every frame of the loading state and then drew nothing into
// it. Not drawing a thing is faster than drawing it.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_BROWSERCHROME_COMPUTE_H
#define ZX_BROWSERCHROME_COMPUTE_H

#include "features/server-browser/computation/serverbrowser_compute.h"

namespace zx
{

// One bit per movable part. The background panel is not here: it is the menu itself, and it is drawn
// whatever else is true.
enum BrowserPart
{
	kPartTabs        = 1u << 0,   // the PUBLIC/PRIVATE ovals and the rule beneath them
	kPartList        = 1u << 1,   // column headings, rows, and the list's scrollbar
	kPartDetail      = 1u << 2,   // the black panel, its contents, and the JOIN/CANCEL button
	kPartPlaceholder = 1u << 3,   // the spinner, or "No servers found"
	kPartFooter      = 1u << 4,   // the server count, or a running transfer's progress
	kPartHost        = 1u << 5,   // the hosting panel, which stands in place of the list and detail
};

// [rc4l] The HOST tab is not a filter over the same list, the way PUBLIC and PRIVATE are -- it is a
// different screen that happens to hang off the same row of tabs. So it does not narrow what is
// listed; it replaces it. Everything about a server you might join is meaningless on the screen
// where you are making one.
//
// A transfer still running is the exception, and the same exception as everywhere else: its progress
// and the control that stops it survive any change of tab, because the download does not care which
// screen the player wandered onto and losing the CANCEL button would strand them.
unsigned ComputeHostParts( bool downloadRunning );

// `hasSelection` is whether a server is actually selected -- there is no panel without one, since
// every line in it describes that server.
//
// `downloadRunning` keeps two things alive that a phase would otherwise take away.
//
// The FOOTER, because reopening the browser mid-download puts it back in the loading phase for a
// moment and blanking the progress readout then hides the one thing the player is watching.
//
// And the DETAIL PANEL, because the CANCEL button lives in it. A server can die while its transfer
// is still running: it times out of the list, the selection goes with it, and without this the
// player is left watching a frozen progress bar with no control that can stop it. The one button
// that ends a download must not depend on the thing that started it still existing.
unsigned ComputeVisibleParts( BrowserPhase phase, bool hasSelection, bool downloadRunning );

} // namespace zx

#endif // ZX_BROWSERCHROME_COMPUTE_H
