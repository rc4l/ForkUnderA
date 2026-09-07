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

#include "features/server-hosting/computation/hostargs_compute.h"

#include <string>
#include <utility>
#include <vector>

namespace zx
{

enum class ContinueKind
{
	None,	// nothing to continue
	// [rc4l] One kind for everything played locally: single player, single player with addbot, and
	// an offline skirmish alike. They differ only in NETSTATE_SINGLE_MULTIPLAYER, which the engine
	// already records in the save's own mpEm chunk, and all three snapshot the same way.
	Single,
	Server,	// a server we were connected to: an address, rejoined
	// [rc4l] A game we HOSTED from the presets. The world lived in a child process, so once that
	// process is gone there is nothing to reconnect to and nothing to load: the only way back is to
	// start it again from the same settings. A fresh match on the same terms, not the match we left.
	Hosted,
};

struct ContinueRecord
{
	ContinueKind kind;

	// Single.
	std::string savePath;
	int saveVersion;
	std::string mapName;
	std::string mapWad;		// the file the map itself came from, for the tooltip

	// Server.
	std::string address;
	std::string password;	// empty when the server had none
	std::string serverName;	// as the server called itself, empty if it never said

	// Hosted. The whole config, reused rather than re-listed: hostargs_compute already describes
	// what a server needs and already refuses values that could be read as another flag.
	//
	// rconSecret is deliberately NOT carried. It is documented as worth nothing after the process it
	// was made for, so a rehost must mint a new one rather than replay a dead one.
	HostConfig host;

	// [rc4l] Which record was written most recently, so "most recently left" survives a restart
	// without needing a clock. Bumped past whatever the other record holds on every write.
	int stamp;

	// [rc4l] When it was last played, as seconds since the epoch. The ORDER still comes from `stamp`
	// -- a counter cannot be dragged backwards by a wrong system clock, and a list that reorders
	// itself because the machine corrected its time is a list nobody can trust. This is only ever
	// shown, never compared: a history that lists what you played needs to say WHEN, and no counter
	// can be turned into "yesterday". Zero for a record written before this field existed, which the
	// column renders as a dash rather than as 1970.
	long long playedAt;

	// Both, because both have to land on the same files we left.
	std::string iwad;
	std::string iwadHash;
	// [rc4l] One loaded file: what it is called, what it contains, and where it was when we last
	// held it open.
	//
	// The path is a HINT and is checked before it is trusted, never a substitute for the digest. It
	// exists because name and digest between them still cannot find a file the engine would not
	// find on its own, and the commonest place to keep a mod -- the folder a browser downloaded it
	// into -- is exactly such a place. Refusing there means telling a player a file is missing
	// while it sits where they put it.
	struct Wad
	{
		std::string name;	// bare filename
		std::string hash;	// MD5, may be empty
		std::string path;	// where it was opened from, may be empty
	};

	std::vector<Wad> wads;

	ContinueRecord() : kind(ContinueKind::None), saveVersion(0), stamp(0), playedAt(0) {}
};

// The format this build writes. Bumped only when a field changes meaning; adding an optional field
// does not need it, since an older reader ignoring an unknown key is safe and a newer record is
// refused outright anyway. mapWad and serverName were added this way and cost an older build
// nothing -- it simply describes the session slightly less well in a tooltip.
const int kContinueFormat = 1;

// Render the record. Returns an empty string for a record with nothing to continue, so a caller that
// writes the result unconditionally still cannot leave a half-record on disk.
std::string SerialiseContinue(const ContinueRecord &record);

// Read one back. False for anything that does not add up: wrong magic, a format this build does not
// know, an unknown kind, or a kind missing the fields it cannot work without.
bool ParseContinue(const std::string &text, ContinueRecord &out);

// [rc4l] The same record without the magic line, for a container that has already established the
// format on its own behalf.
//
// The history file states the magic and the version once and then holds many records, so repeating
// the header per entry would be a version number that a file could disagree with itself about.
// Exposed rather than duplicated: one description of what a record looks like, whether it is alone
// in a file or the fourth of fifty.
std::string SerialiseContinueBody(const ContinueRecord &record);
bool ParseContinueBody(const std::string &text, ContinueRecord &out);

// Where all of this lives: a `continue/` folder of its own under the per-user config root, alongside
// `identity/` rather than loose beside it. Files that only mean anything together, in a folder named
// after what they are, so deleting the feature's state is one obvious action rather than knowing
// which of the loose files belonged to it.

// [rc4l] The snapshot belonging to ONE entry of the history.
//
// One slot per entry rather than one slot for the feature. The single slot was right while there was
// a single offline record: "where you left off" is one place. A history of them is a list of
// different places, and every one of them that is a local game needs its own snapshot or the list
// would be ten rows pointing at the same save -- nine of them lying about which map they lead to.
//
// Numbered by the entry's stamp, which is already unique and already increasing, so no second
// counter has to be kept in step with the first. Files whose entry has fallen off the end are
// deleted by the caller; the name is the link between the two.
std::string ContinueSaveSlotPath(const std::string &configRoot, int instance, int slot);

// The whole history, one file. See continuehistory_compute for what is in it.
std::string ContinueHistoryPath(const std::string &configRoot, int instance);

// The folder itself, for the caller that has to create it before writing.
//
// [rc4l] One folder per INSTANCE, numbered exactly as the account keys are: a second copy of the
// engine is a second player, and two of them sharing one record would have each overwriting the
// other's session. The first instance keeps the plain name so the folder a player finds is the one
// the documentation names.
std::string ContinueDir(const std::string &configRoot, int instance);

// The two records, kept apart so the server you last played and the offline game you last played are
// remembered independently -- joining a server must not forget the campaign you were half way
// through. Independent files also mean a corrupt one cannot take the other down with it.
std::string ContinueOfflinePath(const std::string &configRoot, int instance);
std::string ContinueServerPath(const std::string &configRoot, int instance);

} // namespace zx

#endif // ZX_CONTINUERECORD_COMPUTE_H
