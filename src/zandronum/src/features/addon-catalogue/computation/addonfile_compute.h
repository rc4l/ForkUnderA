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

struct AddonFileRef
{
	std::string name;	// bare filename, as the loader will ask for it
	std::string md5;	// lower-case hex; what the by-hash store is keyed on

	// [rc4l] What this file IS, when a mod might bring its own copy of the same thing. Optional, and
	// empty for almost every file.
	//
	// A role rather than a filename, because a filename carries a version. Name one in a mix and the
	// day the file is replaced by its next release, the mix goes on naming something nobody loads and
	// says nothing about the file that took its place -- silently, since a name that matches nothing
	// is the ordinary case. The role outlives the release: the tag moves with the filename it sits
	// beside, which is the line an upgrade has to touch anyway.
	//
	// Freeform, lower case, and shared by convention rather than declared anywhere. Two files given
	// the same role are claiming to be interchangeable, which is exactly the claim being made.
	std::string provides;
};

// [rc4l] Whether an experience is people against each other or people against the game.
//
// It is the first thing anybody wants to know and the one thing a name reliably fails to say:
// "Skulltag" and "Invasion" tell you nothing until you already know the pack. So it is REQUIRED, and
// an entry that does not say is refused by name at startup rather than shown unlabelled.
//
// Unknown exists so a value that is present but unrecognised has somewhere to land, which keeps the
// refusal specific: "kind is not pve or pvp" says more than "malformed value".
// [rc4l] Which Zandronum gamemode a way of playing runs in, DECLARED rather than inferred.
//
// The cfg already says this by setting `cooperative`, `survival`, `invasion` and so on, but the cfg
// is exec'd by the server and never read by the client, so the panel has no way to know. It needs to
// know because whether a setting is even meaningful depends on it: only four of these honour
// sv_maxlives, and one of them means something different by zero.
//
// Unknown is not an error. Most entries never declare it, and everything that reads this treats not
// knowing as "offer nothing that depends on the gamemode", which is the safe answer.
enum class HostGameMode
{
	Unknown,

	Cooperative,
	Survival,
	Invasion,

	Deathmatch,
	TeamDeathmatch,
	Duel,
	LastManStanding,
	TeamLastManStanding,
	Possession,
	TeamPossession,
	Terminator,

	// The three that take their sides from the map rather than from a cvar, so a pack can only offer
	// them on maps built for them. The first two want a flag on the floor; Teamgame wants team
	// starts, which come one set per side and are placed by the mapper.
	CaptureTheFlag,
	Skulltag,
	Teamgame,
};

HostGameMode ParseGameMode(const std::string &s);

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

	// [rc4l] Optional, and falls back to the entry's. Declared per variant because it genuinely
	// differs per variant: Ragnarok's Deathmatch and its Last Man Standing are the same files and
	// the same maps, and only one of them has lives.
	HostGameMode gameMode;

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
	// for its Duel, and the two live under one entry. A variant that does not WRITE the key takes
	// whatever the entry says, so a pack whose ways of playing all take the same remixes states it
	// once.
	std::vector<std::string> remixes;

	// [rc4l] Whether the key was there at all, which is what makes "none" sayable.
	//
	// The override used to be keyed on the list being non-empty, so "remixes": [] read as silence and
	// fell back to the entry's. Both Doom Barracks Zones need the opposite: they sit in an entry whose
	// other variants take four mixes, and neither of them can take any -- the pack replaces the
	// weapons itself. Presence is the override, emptiness is the answer.
	bool remixesSet;

	// [rc4l] Whether to offer the team-count control, and the same question `fastWeapons` answers at
	// entry level: is this axis a real choice here, or is it the pack's own business?
	//
	// Per variant rather than per entry because it genuinely differs per variant. Skulltag's
	// Deathmatch and its Last Man Standing take it; its Duel, its CTF and its Skulltag variant do not,
	// and the last of those declares deathmatch while running a mode of its own -- see
	// teamspick_compute.h for why reading the gamemode alone would get that one wrong.
	bool teams;

	// [rc4l] The entry's lives, for a way of playing that does not want the entry's answer. NEGATIVE
	// means unset, which is why they are not plain ints: a ceiling of 0 is meaningful -- it is how a
	// variant opts out of the control altogether -- so emptiness needs a value of its own.
	//
	// Needed as soon as an entry gathers packs that are not alike. Popular Co-op Maps holds six
	// campaign mapsets that can sensibly be run as Survival and four co-op packs that cannot, and one
	// entry-level answer put a lives slider on Destination Unknown, which has no survival way of
	// playing it at all.
	int defaultLives;
	int maxLives;

	// [rc4l] Whether THIS way of playing offers the weapon speed. Added to the entry's rather than
	// replacing it, so an entry may say it once and a variant may say it for itself.
	//
	// The same lesson as the lives above, found the same way: the entry said yes for four packs built
	// on never letting go of the trigger, and Hell Revealed II, which is not one of them, got the
	// slider too.
	bool fastWeapons;

	// Which one a player who has expressed no preference gets. Exactly one may claim it.
	bool isDefault;

	AddonVariant() : kind(VariantKind::Unknown), gameMode(HostGameMode::Unknown), remixesSet(false),
		teams(false), defaultLives(-1), maxLives(-1), fastWeapons(false), isDefault(false) {}
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

	// [rc4l] The gamemode every way of playing runs in unless it says otherwise, and what the lives
	// control reads to know whether it means anything.
	HostGameMode gameMode;

	// [rc4l] How many lives this entry wants when nobody has chosen, and the most it will offer.
	//
	// Both are the entry's business rather than a global: Bosses from Hell is a boss rush and says
	// it wants unlimited, and an invasion pack balanced around three has no use for a slider that
	// goes to twenty. A max of 0 means the entry offers no lives control at all, whatever its
	// gamemode, which is how a pack opts out.
	int defaultLives;
	int maxLives;

	// [rc4l] Whether to offer the weapon-speed control. sv_fastweapons runs 0 to 2: normal, every
	// weapon state cut to one tick, and then the states with no action function cut to none at all.
	//
	// Opt-in per entry rather than offered everywhere, because it is a taste rather than a fix. A
	// pack built around its own weapon timings has nothing to gain from it and every reason not to
	// invite it.
	bool fastWeapons;

	// [rc4l] Whether to offer the team-count control, for an entry that plays ONE way and so has no
	// variant to carry it. An entry saying yes says it for every way of playing it has; a variant may
	// say it for itself. See AddonVariant::teams, and teamspick_compute.h for what it costs to get
	// this wrong.
	bool teams;

	// [rc4l] Mark this entry as curated: its name is drawn with the leading word in the accent colour
	// and the rest plain. Separate from `order` on purpose: being first and being marked are
	// different claims, and an entry may want one without the other.
	//
	// The mark beats being selected, since the cursor starts on the first row and an accent that lost
	// to selection would leave the top entry unmarked. Being SERVED still wins outright: that is a
	// fact about right now, and it matters more than which group the entry is in.
	bool accent;

	bool valid;
	std::string error;		// why not, when invalid

	// In DECLARATION order, which gcc requires and which msvc does not check: `order` is declared
	// above the gamemode and has to be initialised there too.
	AddonEntry() : kind(VariantKind::Unknown), order(0), gameMode(HostGameMode::Unknown),
		defaultLives(0), maxLives(0), fastWeapons(false), teams(false), accent(false),
		valid(false) {}
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

	// [rc4l] Which AXIS this belongs to. Remixes sharing a group are alternatives to each other;
	// remixes in different groups apply together.
	//
	// Without it every combination is its own remix, and the combinations multiply: two ways to play
	// and three gameplay mods is six remixes to express five choices, and adding a fourth mod means
	// writing another two. With it they are two independent questions, which is what they are. You
	// pick Brutal Doom AND two lives; you do not pick "Brutal two-life".
	//
	// Empty means the default group, so every remix written before this existed keeps behaving as one
	// flat mutually-exclusive list and no catalogue edit is forced by this field arriving.
	std::string group;

	// Loaded AFTER the entry's files and the variant's, by the same rule and for the same reason:
	// added, never replacing, so nothing has to restate what it is being added to.
	std::vector<AddonFileRef> files;

	// [rc4l] What this remix ALREADY CONTAINS, so a file filling the same role is not loaded beside
	// it.
	//
	// A mod is free to bundle something an experience also loads on its own. The two are not a
	// duplicate file -- the names differ -- they are the same thing twice, and what the player gets
	// is every announcement played over itself.
	//
	// Declared by the REMIX, because the remix is the thing that knows what is inside it. An entry
	// cannot be asked to list what each of its mixes happens to carry: it would need editing every
	// time any of them gained something, and that is not a fact about the entry.
	//
	// Roles, matched against AddonFileRef::provides, and never filenames. See there for why. A role
	// nothing on the entry fills is not an error: the same mix is offered by entries that never
	// loaded one.
	std::vector<std::string> provides;

	// [rc4l] The way of playing this mix switches to, when it switches to one at all.
	//
	// A mix that names a gamemode is an AXIS OF MODES rather than of mods, and it has to say so
	// here because the teams and lives controls both read the mode before drawing themselves.
	//
	// Unknown, the default, means this mix is not about the mode and leaves it alone.
	HostGameMode gameMode;

	bool valid;
	std::string error;		// why not, when invalid

	AddonRemix() : gameMode(HostGameMode::Unknown), valid(false) {}
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
