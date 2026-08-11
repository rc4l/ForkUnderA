// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Which tab the server browser opens on.
//
// MULTIPLAYER is the answer nearly every time, because browsing is what the screen is for and
// hosting is what you do when the browsing did not turn anything up. So the list comes first on the
// row and first on arrival.
//
// The exception is the case where browsing has nothing to offer: we have looked, and there are no
// servers. Landing on an empty list then is a dead end wearing the clothes of the main event, and
// HOST is not merely the better tab, it is the only thing on the screen the player can still do.
//
// DECIDED ONCE, WHEN THE SCREEN OPENS. That is why this takes what is already known and not what a
// refresh might report in a moment: a tab that changes underneath the player two seconds after they
// arrived moves the thing they were reaching for. A cold session therefore opens on MULTIPLAYER
// even though the list is empty, because "empty" and "not looked yet" are different answers and only
// the first of them justifies sending the player somewhere else.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_OPENINGTAB_COMPUTE_H
#define ZX_OPENINGTAB_COMPUTE_H

namespace zx
{

enum class OpeningTab
{
	Browse,
	Host,
};

// `knownServers` is how many servers are listed right now, which survives between visits, so the
// second visit of a session knows what the first one found. `listHasAnswered` is whether any refresh
// has run to completion yet: without it, a browser opened before its first reply would read its own
// empty list as proof the world is empty.
OpeningTab ComputeOpeningTab(int knownServers, bool listHasAnswered);

} // namespace zx

#endif // ZX_OPENINGTAB_COMPUTE_H
