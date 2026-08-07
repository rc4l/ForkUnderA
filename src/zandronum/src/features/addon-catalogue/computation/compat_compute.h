// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Whether a chosen set of addons can be loaded together.
//
// The rules live here; the facts about each addon live in its catalogue entry. A per-pair rule must
// never appear in this file: the moment "if brutal and duel40" is written in C++, the model has
// failed and every new mod is a code change.
//
// The question answered is "will this load and work", never "is this a good idea". A duel mappack
// under a weapons mod is legal and usually unwise, and a validator that editorialises about taste is
// one nobody trusts about correctness.

#ifndef ZX_ADDON_COMPAT_COMPUTE_H
#define ZX_ADDON_COMPAT_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// The slots an addon can occupy. Parsed by NAME from the catalogue, never by ordinal: these values
// are written into files that ship on players' disks, so renumbering silently changes what an
// unchanged entry means. See tools/wire-enum-snapshot.py for the same hazard on the wire.
enum class AddonSlot
{
	Iwad,		// the base game; exactly one
	Maps,		// levels
	Gameplay,	// weapons, monsters, rules
	Patch,		// bridges a specific gameplay+maps pair
	Cosmetic,	// HUD, music, skins, textures
	Count,
};

// Whether an addon's maps carry their own actors. A mappack using only stock monsters composes with
// any gameplay mod; one shipping its own DECORATE fights that mod for the same actor names. This one
// flag is the difference between "mappacks work with Brutal Doom" being true and being a support
// burden.
enum class ActorStyle
{
	Unknown,	// not declared; treated as Custom, because guessing wrong the safe way costs a warning
	Vanilla,
	Custom,
};

struct Addon
{
	std::string id;
	std::vector<AddonSlot> fills;	// slots this occupies
	std::vector<AddonSlot> locks;	// slots nothing else may occupy alongside it
	ActorStyle actors;
	std::vector<std::string> conflictsWith;	// ids, for the specific pairs no rule predicts

	Addon() : actors(ActorStyle::Unknown) {}
};

enum class Verdict
{
	Allowed,	// including combinations nobody has tried; silence on an untried pair is correct
	Warned,		// loads, but a declared conflict or custom actors under a gameplay mod
	Blocked,	// arity or a lock; cannot proceed
};

struct CompatResult
{
	Verdict verdict;
	std::vector<std::string> reasons;	// human-readable, one per finding, empty when Allowed

	CompatResult() : verdict(Verdict::Allowed) {}
};

// The verdict for a whole selection. Order of `selected` does not affect the result.
CompatResult CheckSelection(const std::vector<Addon> &selected);

// Load order, derived from the slot rather than typed by anyone: iwad, maps, gameplay, patch,
// cosmetic. That ordering is what makes a gameplay mod's actors win over the mappack it is layered
// on, and getting it out of the user's hands is half the value of the feature.
std::vector<std::string> LoadOrder(const std::vector<Addon> &selected);

// Name/ordinal conversion for the catalogue parser. Returns false on an unknown name rather than
// guessing, so an entry written for a newer schema is skipped instead of misread.
bool SlotFromName(const char *name, AddonSlot &out);
const char *NameForSlot(AddonSlot slot);
bool ActorStyleFromName(const char *name, ActorStyle &out);

} // namespace zx

#endif // ZX_ADDON_COMPAT_COMPUTE_H
