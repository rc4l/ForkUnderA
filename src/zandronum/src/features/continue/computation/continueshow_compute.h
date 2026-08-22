// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Whether the Continue button is there at all.
//
// A button that is offered and then fails is worse than no button: the player has already decided
// to press it. So every reason it could not work is asked BEFORE it is drawn, and the button simply
// is not there when any of them holds.
//
// PROOF HIDES IT, PENDING DOES NOT. Whether the server we remember is still up cannot be known
// instantly, and the two ways of handling the gap are not symmetric. Hiding until a probe answers
// makes the button appear a second after the menu, under the player's cursor, which is how a
// misclick becomes a reconnect. Showing until a probe says otherwise costs at worst one press that
// lands back in the browser with a reason -- the path a failed join already takes. Same reasoning as
// headerreach_compute, where "checking" must not be painted as "broken".
//
// Header-pure by the features/ rules: no engine types, so the caller supplies the facts.

#ifndef ZX_CONTINUESHOW_COMPUTE_H
#define ZX_CONTINUESHOW_COMPUTE_H

#include "features/continue/computation/continuerecord_compute.h"

namespace zx
{

// What we know about the server a Server record names.
enum class ServerProbe
{
	Unknown,		// not asked yet, or no answer so far
	Alive,			// answered, and running the same WADs we recorded
	Gone,			// asked and did not answer
	WadsDiffer,		// answered, but it is not the game we left
};

struct ContinueShowInputs
{
	bool recordParsed;		// the file was there and made sense
	ContinueKind kind;
	bool saveFileExists;	// Single: the snapshot the record points at is still on disk
	int saveVersion;		// Single: the version stamped in that snapshot
	int minSaveVersion;		// this build's MINSAVEVER
	ServerProbe probe;		// Server: what we know about the address

	ContinueShowInputs()
		: recordParsed(false), kind(ContinueKind::None), saveFileExists(false),
		  saveVersion(0), minSaveVersion(0), probe(ServerProbe::Unknown) {}
};

enum class ContinueVisibility
{
	Hidden,
	Shown,
};

ContinueVisibility DecideContinueVisibility(const ContinueShowInputs &in);

// Convenience for the one place that only wants a yes or no.
bool ContinueIsShown(const ContinueShowInputs &in);

} // namespace zx

#endif // ZX_CONTINUESHOW_COMPUTE_H
