// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/addon-catalogue/computation/remixpick_compute.h"

namespace zx
{

std::vector<AddonRemix> OfferedRemixes(const AddonEntry &entry,
                                       const std::vector<AddonRemix> &pool)
{
	std::vector<AddonRemix> offered;

	// The ENTRY's order, not the pool's. The pool is a folder listing and comes out alphabetical;
	// the entry's list is written by hand, and "as it ships" belongs at the top of the picker
	// whatever it happens to be called.
	for (size_t i = 0; i < entry.remixes.size(); ++i)
	{
		for (size_t j = 0; j < pool.size(); ++j)
		{
			if (!pool[j].valid || (pool[j].id != entry.remixes[i]))
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
	return pick;
}

} // namespace zx
