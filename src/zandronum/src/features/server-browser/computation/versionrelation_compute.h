// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Whether a server's build is older than ours, newer, or the same one.
//
// The browser used to ask a narrower question -- does this server's version string START with ours
// -- which answers "can I play here" and nothing else. That was enough while a mismatched server was
// hidden, because every answer but "yes" led to the same place: dropped from the list.
//
// It is not enough once the row is drawn, because the two ways of being wrong are not alike:
//
//   OLDER: the HOST has not updated. There is nothing the player can do, and no reason to rank it
//          among servers they can actually join, so it sinks to the bottom of its group.
//   NEWER: WE have not updated. The row is a destination reachable by updating, so it keeps its
//          normal place and carries the offer to update.
//
// That asymmetry is the whole reason this exists. A prefix test cannot express it: "different" is
// one answer where the browser needs three.
//
// WHAT A VERSION LOOKS LIKE. `git describe` gives "v0.2.19" at a tag and "v0.2.19-29-gde55d35" for
// any commit after one (see features/fua-branding). The server sends a string beginning with that
// tag, so the leading numeric part is what carries meaning and everything after it is decoration --
// except that its PRESENCE means "some commits past the tag", which is genuinely newer than the tag.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_VERSIONRELATION_COMPUTE_H
#define ZX_VERSIONRELATION_COMPUTE_H

#include <string>

namespace zx
{

enum class VersionRelation
{
	// [rc4l] Unparseable, on either side. Deliberately its own answer rather than folded into Older:
	// a server we cannot read the version of is not one we know to be behind, and guessing "behind"
	// would sink a row for a reason nobody could verify. Treated like Older for ordering, because
	// both are "cannot join and cannot fix", but named apart so the panel can say which it is.
	Unknown,

	Older,		// their build predates ours: the host needs to update, we can do nothing
	Same,		// joinable
	Newer,		// our build predates theirs: WE can update, and the row says so
};

// [rc4l] Compares the leading version tags of two build strings. Either may carry a `git describe`
// suffix or trailing text; only the dotted numbers and the presence of a suffix are read.
//
// Component counts may differ: the shorter is padded with zeroes, so "v0.2" and "v0.2.0" are Same.
VersionRelation CompareFuaVersions(const std::string &theirs, const std::string &ours);

// Whether a relation permits joining. Only Same does, and it is a function rather than a comparison
// at each call site so that adding a relation later cannot quietly become joinable by default.
bool VersionRelationCanJoin(VersionRelation relation);

// Whether a relation should sink to the bottom of its group. Older and Unknown do: nothing the
// player does changes them. Newer does not, because updating reaches it.
bool VersionRelationSinks(VersionRelation relation);

} // namespace zx

#endif // ZX_VERSIONRELATION_COMPUTE_H
