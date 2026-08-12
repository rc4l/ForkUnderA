// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Which remix is in force for an entry, and what it adds.
//
// The sibling of variantpick, and separate from it for the same reason that one is separate from the
// parsing: the files say what the choices ARE, this says which one applies given what the player
// last picked and what the pool currently holds. Those drift apart the moment a catalogue is
// updated, and the answer to a choice that no longer exists has to be a definite fallback rather
// than an empty panel.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_REMIXPICK_COMPUTE_H
#define ZX_REMIXPICK_COMPUTE_H

#include <string>
#include <utility>
#include <vector>

#include "features/addon-catalogue/computation/addonfile_compute.h"

namespace zx
{

// What an entry can actually be played with: the remixes it names, in the order it names them, that
// the pool has and could read. An entry asking for something the pool does not hold is not an error
// here -- catalogues are edited by hand and a missing remix should cost that one option rather than
// the whole entry.
//
// `variant` is the index of the chosen way of playing, or -1 for none. A variant that names its own
// remixes overrides the entry's rather than adding to them: Skulltag's Invasion takes three lives
// and its Duel does not, and "the entry's plus mine" cannot express that.
std::vector<AddonRemix> OfferedRemixes(const AddonEntry &entry, int variant,
                                       const std::vector<AddonRemix> &pool);

struct RemixPick
{
	// Index into what OfferedRemixes returned, or -1 when the entry offers none. -1 is the ordinary
	// case of a pack that plays one way, and the caller draws no button for it.
	int index;

	// What to show. Empty when there is nothing to show.
	std::string name;

	// The id actually in force, which is not always the one asked for. Kept so a caller can write
	// back what it settled on rather than leaving a stale preference in place.
	std::string id;

	// What it adds, both optional and both possibly empty: the baseline remix adds neither.
	std::string cfg;
	std::vector<AddonFileRef> files;

	// What it already contains, carried through from the remix so the caller can act on it without
	// going back to the pool for the thing it just picked.
	std::vector<std::string> provides;

	// The mode it switches to, Unknown when it is not about the mode. Carried for the same reason
	// `provides` is: the caller has the pick and should not have to go back to the pool for a fact
	// about the thing it just picked.
	HostGameMode gameMode;

	RemixPick() : index(-1), gameMode(HostGameMode::Unknown) {}
};

// The mode actually in force, given what the entry or its variant said and what the player then
// picked. A mix that names a mode WINS, because picking it is the more recent and more specific
// statement: the entry says how it plays by default, the pill says how it is being played now.
//
// Only one mix may claim it. Two pills naming modes are on the same axis by construction -- one
// lights per group -- so the last one that names a mode is the one in force, and a catalogue that
// puts two mode mixes in different groups has asked for something no server can do.
HostGameMode EffectiveGameMode(HostGameMode stated, const std::vector<RemixPick> &picks);

// `wantedId` is what the player last chose, which may be empty or name a remix this entry no longer
// offers. Either way the answer is the FIRST one offered, which is the baseline by convention: a
// remix list is written with "as it ships" at the top, so somebody who has never chosen gets the
// pack as its author meant it.
RemixPick PickRemix(const std::vector<AddonRemix> &offered, const std::string &wantedId);

// [rc4l] One axis: the remixes that are alternatives to each other, in the order the entry named
// them.
struct RemixGroup
{
	std::string id;						// the shared group name; empty is the default group
	std::vector<AddonRemix> choices;	// at least one, in entry order
};

// Split what an entry offers into its axes, keeping entry order both between groups and within them.
//
// Group order is FIRST APPEARANCE, not alphabetical, for the same reason the choices keep their
// order: the catalogue is written by hand and the author's sequence is the one that reads correctly.
// Sorting here would put "lives" above "mod" because of a letter.
//
// A group with one choice is still a group. It is nothing to decide, so a caller may well draw it
// differently or not at all, but that is a drawing question and this is not the place to answer it.
std::vector<RemixGroup> GroupRemixes(const std::vector<AddonRemix> &offered);

// What is in force on every axis at once, given what the player last chose on each.
//
// `wanted` maps group id to remix id and may be missing groups, name remixes that are no longer
// offered, or name groups that no longer exist. Every one of those resolves the same way a single
// pick does: to that group's own baseline. A stale preference costs the axis it was about and never
// the others.
std::vector<RemixPick> PickRemixes(const std::vector<AddonRemix> &offered,
                                   const std::vector<std::pair<std::string, std::string> > &wanted);

// [rc4l] The whole load, in order: what the entry and its variant bring, then what each pick adds,
// minus anything a pick already contains.
//
// One function rather than an append followed by a filter, because the filter needs to know where
// each file CAME FROM. A mix that provides a role must not suppress its own copy of that role, and
// once the lists are concatenated there is nothing left to tell them apart.
//
// Matched on AddonFileRef::provides against RemixPick::provides -- roles, never filenames. A role no
// file fills drops nothing, which is the normal case for a mix offered by several entries where only
// some of them load one.
std::vector<AddonFileRef> CombineFiles(const std::vector<AddonFileRef> &base,
                                       const std::vector<RemixPick> &picks);

} // namespace zx

#endif // ZX_REMIXPICK_COMPUTE_H
