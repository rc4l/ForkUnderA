// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/addon-catalogue/computation/hostlist_compute.h"

namespace zx
{

std::vector<HostListRow> BuildHostListRows(const std::vector<int> &variantCounts,
                                           const std::vector<bool> &open)
{
	std::vector<HostListRow> rows;

	for (size_t i = 0; i < variantCounts.size(); ++i)
	{
		const int entry = static_cast<int>(i);
		rows.push_back(HostListRow(entry, -1));

		// Past the end of `open` is shut, so a caller with no state yet can pass nothing at all.
		if ((i >= open.size()) || !open[i])
			continue;

		// A count of zero or less opens to nothing, which is the same as not being open. Guarded
		// rather than assumed, because "the open entry has variants" is a fact about a catalogue
		// that can be re-read while an entry is open.
		for (int v = 0; v < variantCounts[i]; ++v)
			rows.push_back(HostListRow(entry, v));
	}

	return rows;
}

int FindHostListRow(const std::vector<HostListRow> &rows, int entry, int variant)
{
	int entryRow = -1;

	for (size_t i = 0; i < rows.size(); ++i)
	{
		if (rows[i].entry != entry)
			continue;

		if (rows[i].variant == variant)
			return static_cast<int>(i);

		// Remembered on the way past, so a variant that is not currently showing still resolves to
		// something visible instead of leaving the cursor nowhere.
		if (rows[i].variant < 0)
			entryRow = static_cast<int>(i);
	}

	return entryRow;
}

} // namespace zx
