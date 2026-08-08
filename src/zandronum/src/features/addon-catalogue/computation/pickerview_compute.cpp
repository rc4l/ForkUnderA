// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/addon-catalogue/computation/pickerview_compute.h"

#include "features/server-browser/computation/serversearch_compute.h"

namespace zx
{

PickerView BuildPickerView(const std::vector<PickerItem> &items,
                           const std::string &query,
                           const std::string &keepId)
{
	PickerView view;

	// Folded once here rather than once per item, which is the reason SearchKey is separate from the
	// match in the first place.
	const std::string key = SearchKey(query);

	for (size_t i = 0; i < items.size(); ++i)
	{
		// Reuses the browser's matcher rather than growing a second one. It strips colour escapes
		// before comparing, which a catalogue name will not have but costs nothing, and it means the
		// two lists in this menu can never disagree about what "matches" means.
		if (ServerMatchesSearch(items[i].name, key))
			view.visible.push_back(i);
	}

	if (view.visible.empty())
		return view;	// selectedRow stays -1: there is genuinely nothing to be on

	// Follow the entry, never the row. Typing one more character must not slide the highlight onto
	// whatever moved into that position.
	if (!keepId.empty())
	{
		for (size_t row = 0; row < view.visible.size(); ++row)
		{
			if (items[view.visible[row]].id == keepId)
			{
				view.selectedRow = static_cast<int>(row);
				view.selectedId = keepId;
				return view;
			}
		}
	}

	// It filtered out, or nothing was selected. The first row is the honest fallback: it is what the
	// player is looking at, and leaving nothing selected would make the panel beside the list empty
	// for no reason a player could explain.
	view.selectedRow = 0;
	view.selectedId = items[view.visible[0]].id;
	return view;
}

} // namespace zx
