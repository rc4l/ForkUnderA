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

	// [rc4l] Where this one opens, when it is not where the entry opens. Empty means the entry's.
	//
	// Needed the moment several packs live under one entry: three invasion packs open on alinv01,
	// MAP01 and Z1INV01, and one entry-level answer cannot be all three. Without it the only way to
	// place the start was to write out a rotation per variant, which is a lot of lines to maintain
	// for a fact the pack's own mapinfo already states.
	std::string map;

	// Loaded AFTER the entry's own, so an entry can carry what every way of playing shares and a
	// variant only what is peculiar to it. Empty for a pack whose variants differ by cfg alone.
	std::vector<AddonFileRef> files;

	// [rc4l] Which remixes THIS way of playing can take, when that is not the same as the entry's.
	//
	// Skulltag is the case that needs it: three lives is a real choice for its Invasion and nonsense
	// for its Duel, and the two live under one entry. Empty means "whatever the entry says", so a
	// pack whose ways of playing all take the same remixes states it once.
	std::vector<std::string> remixes;

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

	// [rc4l] Which remixes this entry can be played with, BY ID -- the remixes themselves live once
	// in catalogue/remix and are shared.
	//
	// Ids rather than definitions because the same few remixes apply to many entries: "three lives"
	// means the same thing to every invasion pack, and restating its cfg and files in each addon.json
	// is the copies-drift problem this schema refuses everywhere else. An entry says which apply,
	// which is the only part that is actually about the entry.
	//
	// Empty for an entry nothing can be played with, which is most of them: a pack bringing its own
	// weapons and classes has nowhere to put someone else's.
	std::vector<std::string> remixes;

	// [rc4l] Where this sits in the list. Higher floats nearer the top; everything defaults to 0 and
	// so keeps the folder order it has always had.
	//
	// Explicit rather than derived from the folder name, because the two experiences this exists for
	// are meant to lead the list and their ids happen to sort where they sort. A curated position that
	// depends on spelling is one rename away from moving on its own.
	int order;

	// [rc4l] Draw this entry's name in the accent colour instead of the ordinary one, to mark it out
	// as curated. Separate from `order` on purpose: being first and being highlighted are different
	// claims, and an entry may want one without the other.
	//
	// Only the resting colour changes. Selected and being-served still say what they say, because
	// those are about what is happening rather than about what the entry is.
	bool accent;

	bool valid;
	std::string error;		// why not, when invalid

	AddonEntry() : kind(VariantKind::Unknown), order(0), accent(false), valid(false) {}
};

// [rc4l] Something you can play an entry WITH, on top of whichever way of playing you chose.
//
// A third axis, and it has to be one. "Three lives" means the same thing to every invasion pack, and
// Brutal Doom means the same thing to every plain mapset. Written as variants instead, each would be
// restated once per pack, and n packs times m of these is a lot of copies of one idea.
//
// So a remix is defined ONCE, in catalogue/remix/<id>/remix.json, and an entry names the ones it can
// take. The entry says what applies to it, which is the only part actually about the entry; the
// remix says what it does, which is the only part actually about the remix.
//
// Both halves are optional and both get used. A RULES remix changes cvars and loads nothing, which
// is what Survival is: one line of cfg every invasion pack understands. A CONTENT remix brings
// files, which is what Brutal Doom would be. One that does neither is not an error either: that is
// the baseline, the "as the pack ships" option, and it needs a name like the rest so the picker has
// something to put at the top.
struct AddonRemix
{
	std::string id;			// the folder name; never read from inside the file
	std::string name;		// what the picker shows
	std::string summary;	// optional; what this actually changes
	std::string cfg;		// optional; bare filename, beside the remix.json

	// Loaded AFTER the entry's files and the variant's, by the same rule and for the same reason:
	// added, never replacing, so nothing has to restate what it is being added to.
	std::vector<AddonFileRef> files;

	bool valid;
	std::string error;		// why not, when invalid

	AddonRemix() : valid(false) {}
};

// `id` is supplied by the caller from the directory name rather than trusted from the file: the
// folder is what a player renames, and two sources for one identity can only ever disagree.
AddonEntry ParseAddonFile(const std::string &id, const std::string &json);

// The same restricted JSON, read by the same reader, for the other kind of document in the
// catalogue. Kept in this unit rather than a sibling precisely so there is ONE reader: two would
// drift, and the drift would show up as a file that parses in one place and not the other.
AddonRemix ParseRemixFile(const std::string &id, const std::string &json);

} // namespace zx

#endif // ZX_ADDONFILE_COMPUTE_H
