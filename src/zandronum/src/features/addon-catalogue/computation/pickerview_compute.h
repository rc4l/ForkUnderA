// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Which rows a picker shows, and which one stays selected while you type.
//
// With a few entries the list is browsable and this hardly matters. With thousands the search box IS
// the interface, so the selection has to survive filtering: it follows the ENTRY, not the row index.
// Keying on the index means typing one more character silently moves the highlight onto whatever
// happens to sit at that position, and the player starts a different server than the one they were
// looking at.
//
// Deliberately knows nothing about hosting. It answers "what is on screen and what is chosen", so
// the same unit serves an offline picker later with only the panel beside it differing.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_PICKERVIEW_COMPUTE_H
#define ZX_PICKERVIEW_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

struct PickerItem
{
	std::string id;		// stable across filtering; the folder name
	std::string name;	// what is shown, and what search matches against
};

struct PickerView
{
	std::vector<size_t> visible;	// indices into the caller's items, in display order
	int selectedRow;				// index into `visible`; -1 when there is nothing to select
	std::string selectedId;			// "" when nothing is selected

	PickerView() : selectedRow(-1) {}
};

// `keepId` is what was selected before this keystroke. Empty means "no preference", which selects
// the first row so a freshly opened picker is never sitting on nothing.
PickerView BuildPickerView(const std::vector<PickerItem> &items,
                           const std::string &query,
                           const std::string &keepId);

} // namespace zx

#endif // ZX_PICKERVIEW_COMPUTE_H
