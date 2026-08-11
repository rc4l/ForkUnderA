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
};

// [rc4l] Whether an experience is people against each other or people against the game.
//
// It is the first thing anybody wants to know and the one thing a name reliably fails to say:
// "Skulltag" and "Invasion" tell you nothing until you already know the pack. So it is REQUIRED, and
// an entry that does not say is refused by name at startup rather than shown unlabelled.
//
// Unknown exists so a value that is present but unrecognised has somewhere to land, which keeps the
// refusal specific: "kind is not pve or pvp" says more than "malformed value".
enum class VariantKind
{
	Unknown,
	PvE,
	PvP,
};

// The word for a kind, for a panel or a message. Never empty, so a caller cannot print nothing.
const char *DescribeVariantKind(VariantKind kind);

// [rc4l] One way to play an entry. Skulltag is a deathmatch pack, a duel pack, an invasion pack and
// a CTF pack, and that used to be one cfg with every map of all four in one rotation.
//
// A variant always changes the cfg, and MAY add files of its own. What it loads is the entry's list
// followed by its own, which covers both shapes with one rule: Skulltag puts everything in the
// entry and nothing in its variants, Ghouls vs Humans puts nothing in the entry and a whole map pack
// in each variant, and a pack with a shared base plus per-mode extras falls out in between.
//
// ADDED, never replacing. A variant that restated the shared files would hold a copy of them, and
// copies drift: update the base, miss one variant, and that variant quietly loads something else.
// The failure is invisible until somebody cannot join.
struct AddonVariant
{
	std::string id;			// stable; what a remembered choice is keyed on
	std::string name;		// what the panel shows
	std::string cfg;		// bare filename, beside the addon.json
	std::string tooltip;	// optional; what this way of playing actually is
	VariantKind kind;		// required; see VariantKind

	// Loaded AFTER the entry's own, so an entry can carry what every way of playing shares and a
	// variant only what is peculiar to it. Empty for a pack whose variants differ by cfg alone.
	std::vector<AddonFileRef> files;

	// Which one a player who has expressed no preference gets. Exactly one may claim it.
	bool isDefault;

	AddonVariant() : kind(VariantKind::Unknown), isDefault(false) {}
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

	// [rc4l] The entry's own label, for a pack that plays one way and so has no variant to carry it.
	// Required exactly as a variant's is: an unlabelled experience is the thing this is here to stop,
	// and a pack having only one way to play is not a reason to know less about it.
	VariantKind kind;

	// [rc4l] Empty for an entry that plays one way, which is most of them. NOT filled in with a
	// synthetic single variant: "this pack has one way to play" and "this pack has one variant" look
	// the same in a list and are different things to say, and the panel should draw nothing rather
	// than a row of one.
	//
	// An older build ignores this field entirely and plays server.cfg, which is why the default
	// variant's cfg should BE server.cfg. Then old and new agree about what an unchosen entry does.
	std::vector<AddonVariant> variants;

	bool valid;
	std::string error;		// why not, when invalid

	AddonEntry() : kind(VariantKind::Unknown), valid(false) {}
};

// `id` is supplied by the caller from the directory name rather than trusted from the file: the
// folder is what a player renames, and two sources for one identity can only ever disagree.
AddonEntry ParseAddonFile(const std::string &id, const std::string &json);

} // namespace zx

#endif // ZX_ADDONFILE_COMPUTE_H
