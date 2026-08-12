// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/addon-catalogue/computation/remixpick_compute.h"

namespace zx
{

namespace
{

bool Holds(const std::vector<std::string> &list, const std::string &want)
{
	for (size_t i = 0; i < list.size(); ++i)
	{
		if (list[i] == want)
			return true;
	}

	return false;
}

} // namespace

std::vector<AddonRemix> OfferedRemixes(const AddonEntry &entry, int variant,
                                       const std::vector<AddonRemix> &pool)
{
	std::vector<AddonRemix> offered;

	// The variant's list when it WROTE one, otherwise the entry's. Overriding rather than adding,
	// because the case that needs this is one way of playing taking something the others must not.
	//
	// Keyed on the key being present, not on the list being non-empty: an empty list is how a variant
	// says it takes none of what its entry offers, and reading that as silence made it unsayable.
	const std::vector<std::string> *want = &entry.remixes;

	if ((variant >= 0) && (variant < static_cast<int>(entry.variants.size())) &&
		entry.variants[variant].remixesSet)
	{
		want = &entry.variants[variant].remixes;
	}

	// Written order, not the pool's. The pool is a folder listing and comes out alphabetical; the
	// list here is written by hand, and "as it ships" belongs at the top of the picker whatever it
	// happens to be called.
	for (size_t i = 0; i < want->size(); ++i)
	{
		for (size_t j = 0; j < pool.size(); ++j)
		{
			if (!pool[j].valid || (pool[j].id != (*want)[i]))
				continue;

			offered.push_back(pool[j]);
			break;
		}
	}

	return offered;
}

RemixPick PickRemix(const std::vector<AddonRemix> &offered, const std::string &wantedId)
{
	RemixPick pick;

	if (offered.empty())
		return pick;

	// What the player asked for, if it is still on offer. Checked FIRST so a remembered choice
	// survives a catalogue update that added remixes around it.
	if (!wantedId.empty())
	{
		for (size_t i = 0; i < offered.size(); ++i)
		{
			if (offered[i].id != wantedId)
				continue;

			pick.index = static_cast<int>(i);
			pick.id = offered[i].id;
			pick.name = offered[i].name;
			pick.cfg = offered[i].cfg;
			pick.files = offered[i].files;
			pick.provides = offered[i].provides;
			pick.gameMode = offered[i].gameMode;
			return pick;
		}
	}

	// No preference, or one this entry no longer offers. Both land on the first, which is the
	// baseline: a choice that has been withdrawn is not a reason to leave the player on something
	// they never picked, and somebody who has never chosen should get the pack as it ships.
	pick.index = 0;
	pick.id = offered[0].id;
	pick.name = offered[0].name;
	pick.cfg = offered[0].cfg;
	pick.files = offered[0].files;
	pick.provides = offered[0].provides;
	pick.gameMode = offered[0].gameMode;
	return pick;
}

HostGameMode EffectiveGameMode(HostGameMode stated, const std::vector<RemixPick> &picks)
{
	// Last one wins, and in practice there is only ever one: mode mixes share a group, so the
	// grouping already guarantees a single pick can name a mode. Written as a sweep rather than a
	// find-first anyway, because relying on the grouping here would make this quietly wrong the day
	// a catalogue splits modes across two axes, and the sweep is the same length either way.
	HostGameMode out = stated;

	for (size_t i = 0; i < picks.size(); ++i)
	{
		if (picks[i].gameMode != HostGameMode::Unknown)
			out = picks[i].gameMode;
	}

	return out;
}

std::vector<RemixGroup> GroupRemixes(const std::vector<AddonRemix> &offered)
{
	std::vector<RemixGroup> groups;

	for (size_t i = 0; i < offered.size(); ++i)
	{
		size_t at = groups.size();
		for (size_t g = 0; g < groups.size(); ++g)
		{
			if (groups[g].id == offered[i].group)
			{
				at = g;
				break;
			}
		}

		// First appearance opens the group, so the order is the author's. A group whose members are
		// written apart from each other still collects into one axis; only its POSITION comes from
		// whichever of them was named first.
		if (at == groups.size())
		{
			RemixGroup fresh;
			fresh.id = offered[i].group;
			groups.push_back(fresh);
		}

		groups[at].choices.push_back(offered[i]);
	}

	return groups;
}

std::vector<RemixPick> PickRemixes(const std::vector<AddonRemix> &offered,
                                   const std::vector<std::pair<std::string, std::string> > &wanted)
{
	const std::vector<RemixGroup> groups = GroupRemixes(offered);
	std::vector<RemixPick> picks;

	for (size_t g = 0; g < groups.size(); ++g)
	{
		std::string want;
		for (size_t w = 0; w < wanted.size(); ++w)
		{
			if (wanted[w].first == groups[g].id)
			{
				want = wanted[w].second;
				break;
			}
		}

		// Resolved per group by the SAME function a single axis uses, so a stale or missing
		// preference falls back to that group's baseline and cannot reach across to another axis.
		// `index` is into this group's own choices, which is what a caller drawing one row wants.
		picks.push_back(PickRemix(groups[g].choices, want));
	}

	return picks;
}

std::vector<AddonFileRef> CombineFiles(const std::vector<AddonFileRef> &base,
                                       const std::vector<RemixPick> &picks)
{
	// -1 for the entry's own, otherwise which pick brought it. Only ever read to answer "did this
	// file come from the mix now trying to suppress it", so an index is the whole of what is needed.
	std::vector<AddonFileRef> all;
	std::vector<int> from;

	for (size_t i = 0; i < base.size(); ++i)
	{
		all.push_back(base[i]);
		from.push_back(-1);
	}

	// Every axis, in group order, each appended after the last. Load order between axes is the
	// catalogue author's: a mod named after a rules remix loads after it and so wins where they
	// overlap, which is the only way an author can express that at all.
	for (size_t p = 0; p < picks.size(); ++p)
	{
		for (size_t i = 0; i < picks[p].files.size(); ++i)
		{
			all.push_back(picks[p].files[i]);
			from.push_back(static_cast<int>(p));
		}
	}

	std::vector<AddonFileRef> kept;

	for (size_t i = 0; i < all.size(); ++i)
	{
		if (all[i].provides.empty())
		{
			kept.push_back(all[i]);
			continue;
		}

		bool drop = false;

		for (size_t p = 0; p < picks.size(); ++p)
		{
			// Its own file. A mix providing a role has to keep whatever it brought to provide it
			// with, or declaring the role would delete the thing being declared.
			if (from[i] == static_cast<int>(p))
				continue;

			if (Holds(picks[p].provides, all[i].provides))
			{
				drop = true;
				break;
			}
		}

		if (!drop)
			kept.push_back(all[i]);
	}

	return kept;
}

} // namespace zx
