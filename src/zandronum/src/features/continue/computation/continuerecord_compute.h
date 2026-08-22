// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] What "where you were" is written down as, and how it is read back.
//
// One record, one file, rewritten whole. There is no merging and no partial update: a record that
// half-describes a session is worse than none, because Continue would offer to take the player
// somewhere that never existed. Anything that does not parse cleanly is treated as no record.
//
// THE FORMAT CARRIES ITS OWN VERSION and a record from a newer engine is REFUSED rather than read
// optimistically. A field added later would otherwise be read as absent by an older build, which is
// exactly how "Continue" starts putting people on the wrong map with the wrong WADs.
//
// The WAD list is names AND hashes, borrowed from the join path's own reasoning: a name alone
// cannot tell nine releases of doom2.wad apart, so a record that only remembered names would happily
// reconnect against a different game.
//
// Header-pure by the features/ rules: no engine types, and paths arrive as strings so the engine
// keeps ownership of where its config lives.

#ifndef ZX_CONTINUERECORD_COMPUTE_H
#define ZX_CONTINUERECORD_COMPUTE_H

#include <string>
#include <utility>
#include <vector>

namespace zx
{

enum class ContinueKind
{
	None,	// nothing to continue
	Single,	// a saved offline session: a map, restored from a snapshot
	Server,	// a server we were connected to: an address, rejoined
};

struct ContinueRecord
{
	ContinueKind kind;

	// Single.
	std::string savePath;
	int saveVersion;
	std::string mapName;

	// Server.
	std::string address;
	std::string password;	// empty when the server had none

	// Both, because both have to land on the same files we left.
	std::string iwad;
	std::string iwadHash;
	std::vector<std::pair<std::string, std::string> > wads;	// bare name, MD5 (may be empty)

	ContinueRecord() : kind(ContinueKind::None), saveVersion(0) {}
};

// The format this build writes. Bumped only when a field changes meaning; adding an optional field
// does not need it, since an older reader ignoring an unknown key is safe and a newer record is
// refused outright anyway.
const int kContinueFormat = 1;

// Render the record. Returns an empty string for a record with nothing to continue, so a caller that
// writes the result unconditionally still cannot leave a half-record on disk.
std::string SerialiseContinue(const ContinueRecord &record);

// Read one back. False for anything that does not add up: wrong magic, a format this build does not
// know, an unknown kind, or a kind missing the fields it cannot work without.
bool ParseContinue(const std::string &text, ContinueRecord &out);

// Where the record lives, beside the identity keys, so one folder per user holds everything that
// describes this player rather than this installation.
std::string ContinueRecordPath(const std::string &configRoot);

} // namespace zx

#endif // ZX_CONTINUERECORD_COMPUTE_H
