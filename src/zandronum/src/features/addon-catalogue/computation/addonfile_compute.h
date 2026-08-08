// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Reading one catalogue entry's addon.json.
//
// An entry is a folder holding what to load (this file) and how it plays (server.cfg, which we hand
// to the server with +exec and never parse). Every entry is complete and hostable on its own: pick
// it, start it. There is deliberately no notion here of combining two entries.
//
// The reader accepts a restricted JSON: one flat object, string and integer values, and one array of
// flat objects for `files`. That is the whole schema, so accepting more would only widen what can go
// wrong. Anything it does not understand is a rejection with a reason, never a guess -- these files
// come off a player's disk as readily as out of our release.

#ifndef ZX_ADDONFILE_COMPUTE_H
#define ZX_ADDONFILE_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// The newest schema this build understands. An entry claiming a higher one is skipped rather than
// read with today's meanings, because a field that changed sense is worse than a missing entry.
const int kAddonSchema = 1;

struct AddonFileRef
{
	std::string name;	// bare filename, as the loader will ask for it
	std::string md5;	// lower-case hex; what the by-hash store is keyed on

	// [rc4l] Bytes, or 0 when the entry does not say. Shown beside the name so the panel can answer
	// "how big is this" before anything is fetched -- the same question the server list answers from
	// SQF2_WAD_SIZES, which is not available here because there is no server to ask yet.
	//
	// Optional on purpose: an entry written before this existed still loads, and simply says nothing
	// about size rather than claiming zero.
	unsigned long long size;

	AddonFileRef() : size(0) {}
};

struct AddonEntry
{
	std::string id;			// the folder name; never read from inside the file
	std::string name;		// what the picker shows
	std::string summary;
	std::string iwad;		// preferred; PickIwad decides what actually gets used
	// [rc4l] Which map to open on. Not the same as the first of server.cfg's rotation: Duel 40 opens
	// on START, a welcome map it deliberately leaves OUT of the rotation, so deriving this from the
	// cfg would land players on a duel map they never chose.
	std::string map;
	std::vector<AddonFileRef> files;	// load order, as listed

	bool valid;
	std::string error;		// why not, when invalid

	AddonEntry() : valid(false) {}
};

// `id` is supplied by the caller from the directory name rather than trusted from the file: the
// folder is what a player renames, and two sources for one identity can only ever disagree.
AddonEntry ParseAddonFile(const std::string &id, const std::string &json);

} // namespace zx

#endif // ZX_ADDONFILE_COMPUTE_H
