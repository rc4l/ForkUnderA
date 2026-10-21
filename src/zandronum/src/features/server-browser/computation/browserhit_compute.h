// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which server a click landed on.
//
// The browser draws a fixed number of row slots and scrolls the server list underneath them, so the
// slot a player clicked is not the server they clicked -- the scroll offset sits between the two, and
// the last screenful is usually only partly filled. Getting that wrong joins the wrong server, which
// is the kind of bug that looks like the list is lying rather than like a mouse bug.
//
// The pixel side of hit-testing is deliberately NOT here. The menu walks the same
// serverbrowser_ToScreenY calls its highlight is drawn with, so the clickable box and the visible box
// cannot drift apart; reimplementing that mapping in a testable function would create the second
// source of truth it exists to avoid.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_BROWSERHIT_COMPUTE_H
#define ZX_BROWSERHIT_COMPUTE_H

namespace zx
{

// The server index shown in visible row `slot`, or -1 when that slot holds no server -- the click was
// outside the drawn rows, or on the blank space after the last one on a short final page.
int ComputeServerAtSlot(int slot, int visibleRows, int scrollFirst, int totalServers);

} // namespace zx

#endif // ZX_BROWSERHIT_COMPUTE_H
