// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Whether REFRESH may be pressed yet, and how long is left if not.
//
// The registry enforces its own floor and says so out loud: ask again too soon and it answers
// SRSC_REQUESTIGNORED with "please wait 10 seconds". That reply costs the player their refresh AND
// puts the client on a flood queue, so a button that lets them earn it is a button that punishes
// them for using it. Our floor is therefore the registry's floor, held on this side, where refusing
// is free.
//
// Blocking is not the same as doing nothing. A press inside the floor still has to say what it did
// with itself, or the button reads as broken, so the caller is handed the seconds remaining and is
// expected to show them.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_REFRESHGATE_COMPUTE_H
#define ZX_REFRESHGATE_COMPUTE_H

namespace zx
{

struct RefreshGateIn
{
	// Negative means no refresh has ever run. A cold list must always be allowed to fill, or a
	// browser opened in the first seconds of a session has no route to its first server at all.
	int msSinceLastRefresh;

	// Zero is a caller that has not opted into a floor, and is read as "no floor". Reading it as
	// "block everything" would be a silent way to disable the button.
	int minIntervalMs;

	RefreshGateIn( )
		: msSinceLastRefresh( -1 ), minIntervalMs( 0 ) { }
};

struct RefreshGateOut
{
	bool allowed;

	// Whole seconds still to wait, rounded UP, and never 0 while blocked: a countdown that shows 0
	// and still refuses is worse than no countdown at all.
	int waitSeconds;

	RefreshGateOut( )
		: allowed( true ), waitSeconds( 0 ) { }
};

RefreshGateOut GateRefresh(const RefreshGateIn &in);

} // namespace zx

#endif // ZX_REFRESHGATE_COMPUTE_H
