// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/addon-catalogue/computation/compat_compute.h"

#include <algorithm>
#include <cstring>

namespace zx
{

namespace
{

struct SlotName
{
	const char *name;
	AddonSlot slot;
};

// The one place a slot name is spelled. A second copy is how the parser and the writer drift apart.
const SlotName kSlotNames[] = {
	{ "iwad",     AddonSlot::Iwad     },
	{ "maps",     AddonSlot::Maps     },
	{ "gameplay", AddonSlot::Gameplay },
	{ "patch",    AddonSlot::Patch    },
	{ "cosmetic", AddonSlot::Cosmetic },
};

// How many addons may fill a slot. Iwad and Gameplay are the exclusive ones: two gameplay mods both
// replacing the same actor is last-one-wins and silently broken, which is worse than a refusal.
int MaxOccupants(AddonSlot slot)
{
	switch (slot)
	{
	case AddonSlot::Iwad:
	case AddonSlot::Gameplay:
		return 1;
	default:
		return -1;	// unbounded
	}
}

bool Fills(const Addon &addon, AddonSlot slot)
{
	return std::find(addon.fills.begin(), addon.fills.end(), slot) != addon.fills.end();
}

bool Locks(const Addon &addon, AddonSlot slot)
{
	return std::find(addon.locks.begin(), addon.locks.end(), slot) != addon.locks.end();
}

} // namespace

bool SlotFromName(const char *name, AddonSlot &out)
{
	if (name == 0)
		return false;

	for (size_t i = 0; i < sizeof(kSlotNames) / sizeof(kSlotNames[0]); ++i)
	{
		if (strcmp(kSlotNames[i].name, name) == 0)
		{
			out = kSlotNames[i].slot;
			return true;
		}
	}
	return false;
}

const char *NameForSlot(AddonSlot slot)
{
	for (size_t i = 0; i < sizeof(kSlotNames) / sizeof(kSlotNames[0]); ++i)
	{
		if (kSlotNames[i].slot == slot)
			return kSlotNames[i].name;
	}
	return "";
}

bool ActorStyleFromName(const char *name, ActorStyle &out)
{
	if (name == 0)
		return false;
	if (strcmp(name, "vanilla") == 0) { out = ActorStyle::Vanilla; return true; }
	if (strcmp(name, "custom") == 0)  { out = ActorStyle::Custom;  return true; }
	return false;
}

CompatResult CheckSelection(const std::vector<Addon> &selected)
{
	CompatResult result;

	// Blocked beats Warned, and both are collected in full rather than returned at the first
	// finding: a picker that reveals one problem at a time makes the user play twenty questions.
	for (int s = 0; s < static_cast<int>(AddonSlot::Count); ++s)
	{
		const AddonSlot slot = static_cast<AddonSlot>(s);
		const int limit = MaxOccupants(slot);

		std::vector<std::string> occupants;
		for (size_t i = 0; i < selected.size(); ++i)
		{
			if (Fills(selected[i], slot))
				occupants.push_back(selected[i].id);
		}

		if (limit > 0 && static_cast<int>(occupants.size()) > limit)
		{
			std::string why = std::string("more than one addon fills '") + NameForSlot(slot) + "':";
			for (size_t i = 0; i < occupants.size(); ++i)
				why += " " + occupants[i];
			result.reasons.push_back(why);
			result.verdict = Verdict::Blocked;
		}

		// A lock is only violated by somebody ELSE filling the slot, so a total conversion filling
		// and locking the same slot is legal, which is the whole point of locking it.
		for (size_t i = 0; i < selected.size(); ++i)
		{
			if (!Locks(selected[i], slot))
				continue;

			for (size_t j = 0; j < selected.size(); ++j)
			{
				if (i == j || !Fills(selected[j], slot))
					continue;

				result.reasons.push_back(selected[i].id + " does not allow anything else in '" +
					NameForSlot(slot) + "', but " + selected[j].id + " fills it");
				result.verdict = Verdict::Blocked;
			}
		}
	}

	// Declared conflicts. Checked both ways so only one of the pair has to know.
	for (size_t i = 0; i < selected.size(); ++i)
	{
		for (size_t j = i + 1; j < selected.size(); ++j)
		{
			const bool declared =
				std::find(selected[i].conflictsWith.begin(), selected[i].conflictsWith.end(),
					selected[j].id) != selected[i].conflictsWith.end() ||
				std::find(selected[j].conflictsWith.begin(), selected[j].conflictsWith.end(),
					selected[i].id) != selected[j].conflictsWith.end();

			if (declared)
			{
				result.reasons.push_back(selected[i].id + " and " + selected[j].id +
					" are declared not to work together");
				if (result.verdict != Verdict::Blocked)
					result.verdict = Verdict::Warned;
			}
		}
	}

	// Custom-actor maps under a gameplay mod. A warning and never a block: it is the single most
	// common real-world combination that half works, and refusing it would be wrong far more often
	// than it would be right.
	bool hasGameplay = false;
	for (size_t i = 0; i < selected.size(); ++i)
		hasGameplay = hasGameplay || Fills(selected[i], AddonSlot::Gameplay);

	if (hasGameplay)
	{
		for (size_t i = 0; i < selected.size(); ++i)
		{
			if (!Fills(selected[i], AddonSlot::Maps) || Fills(selected[i], AddonSlot::Gameplay))
				continue;
			if (selected[i].actors == ActorStyle::Vanilla)
				continue;

			result.reasons.push_back(selected[i].id +
				" brings its own actors, which a gameplay mod may replace or fight over");
			if (result.verdict != Verdict::Blocked)
				result.verdict = Verdict::Warned;
		}
	}

	return result;
}

std::vector<std::string> LoadOrder(const std::vector<Addon> &selected)
{
	std::vector<std::string> order;

	// Slot order, then selection order within a slot, so the caller keeps control of ties without
	// the slot rule ever being up for negotiation. An addon filling several slots is placed at its
	// EARLIEST one and never repeated: a total conversion is one file, not two entries.
	for (int s = 0; s < static_cast<int>(AddonSlot::Count); ++s)
	{
		const AddonSlot slot = static_cast<AddonSlot>(s);

		for (size_t i = 0; i < selected.size(); ++i)
		{
			if (!Fills(selected[i], slot))
				continue;
			if (std::find(order.begin(), order.end(), selected[i].id) != order.end())
				continue;

			order.push_back(selected[i].id);
		}
	}

	return order;
}

} // namespace zx
