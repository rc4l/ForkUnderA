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
};

// `hasSelection` is whether a server is actually selected -- there is no panel without one, since
// every line in it describes that server.
//
// `downloadRunning` keeps the FOOTER alive through a phase that otherwise has none. A transfer
// started from this browser reports its progress there, and reopening the browser mid-download puts
// it back in the loading phase for a moment; hiding the footer then would blank the only progress
// readout the player has, which is the one thing they are watching.
unsigned ComputeVisibleParts( BrowserPhase phase, bool hasSelection, bool downloadRunning );

} // namespace zx

#endif // ZX_BROWSERCHROME_COMPUTE_H
