// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Whether a row will actually work, as one of three colours.
//
// The list says what each session WAS. It could not say whether pressing it would get anywhere, so a
// row whose mod has since been deleted looked exactly like the one the player played an hour ago,
// and the only way to find out was to press it and read an error. A dot on the row answers it before
// the press.
//
// Three states, and the split between the last two is the one that matters:
//
//   GREEN   -- go. Every file is here, and for a server it answered and we can join it.
//   YELLOW  -- fixable without leaving. Something is missing that CAN be fetched: a mod, or a free
//              IWAD. Pressing it costs a download, not a dead end.
//   RED     -- not from here. A commercial IWAD is missing and no amount of downloading will produce
//              it; or the server is gone; or it is running a version we cannot join.
//
// The distinction is about what the PLAYER has to do, not about how broken the row is. Yellow means
// "press it and wait"; red means "this needs something we cannot give you". Collapsing them would
// throw away the only part of the answer that changes their next action.
//
// TWO JUDGEMENT CALLS, written down because they are arguable:
//
//   A server that has not been asked yet is GREEN. It is the same asymmetry the button already
//   settles: an unasked server is not a dead one, and painting "we have not checked" as a fault
//   would mark every row red for the second it takes to answer.
//
//   A server running DIFFERENT FILES than we recorded is YELLOW, not red. It is up and it will let
//   us in; what waits on the other side is a download. That it is no longer quite the game we left
//   is true and is not the same as being unreachable.
//
// Header-pure by the features/ rules: every fact arrives as a parameter, so this can be tested
// without a disk, a mirror or a server.

#ifndef ZX_CONTINUESTATUS_COMPUTE_H
#define ZX_CONTINUESTATUS_COMPUTE_H

#include "features/continue/computation/continuerecord_compute.h"
#include "features/continue/computation/continueshow_compute.h"

namespace zx
{

enum class ContinueStatus
{
	Ready,		// green
	Fixable,	// yellow
	Broken,		// red
};

struct ContinueStatusInputs
{
	ContinueKind kind;

	// The IWAD this session used is on this machine.
	bool iwadPresent;

	// [rc4l] ...and if it is not, whether it is one we are allowed to fetch. The answer comes from
	// the download policy's own allowlist (IsFreeIwadName), never from a second table here: a list of
	// free IWADs that disagrees with the one the downloader enforces would promise a fetch that then
	// gets refused.
	bool iwadIsFree;

	// How many of the session's other files are not on this machine. Any of them is a download, which
	// is a wait rather than a wall.
	int missingMods;

	// Server only.
	ServerProbe probe;
	bool versionCompatible;

	ContinueStatusInputs()
		: kind(ContinueKind::None), iwadPresent(true), iwadIsFree(false), missingMods(0),
		  probe(ServerProbe::Unknown), versionCompatible(true) {}
};

ContinueStatus DecideContinueStatus(const ContinueStatusInputs &in);

// Why it is that colour, in a few words, for the row's tooltip. Never null, and empty for green:
// a row that works needs no explanation of why it works.
const char *ContinueStatusReason(const ContinueStatusInputs &in);

} // namespace zx

#endif // ZX_CONTINUESTATUS_COMPUTE_H
