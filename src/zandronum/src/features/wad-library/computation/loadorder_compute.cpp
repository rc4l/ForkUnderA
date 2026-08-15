// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/wad-library/computation/loadorder_compute.h"

namespace zx
{

namespace
{

bool SameNameIgnoringCase(const std::string &a, const std::string &b)
{
	if (a.size() != b.size())
		return false;

	for (size_t i = 0; i < a.size(); ++i)
	{
		char ca = a[i];
		char cb = b[i];
		if ((ca >= 'A') && (ca <= 'Z')) ca = static_cast<char>(ca - 'A' + 'a');
		if ((cb >= 'A') && (cb <= 'Z')) cb = static_cast<char>(cb - 'A' + 'a');
		if (ca != cb)
			return false;
	}

	return true;
}

} // namespace

AddResult AddToLoadOrder(std::vector<LoadOrderEntry> &list, const LoadOrderEntry &entry)
{
	if (entry.path.empty() || entry.name.empty())
		return AddResult(AddVerdict::Empty, 0);

	for (size_t i = 0; i < list.size(); ++i)
	{
		if (!SameNameIgnoringCase(list[i].name, entry.name))
			continue;

		// [rc4l] Same path is the same file however it was reached, so that answer never depends on
		// having hashed anything. The hashes are compared only when the paths differ, and an entry
		// with no hash yet falls through to NameTaken -- which is the safe way round, since the
		// refusal is about the name in the first place.
		if (list[i].path == entry.path)
			return AddResult(AddVerdict::AlreadyThere, i);

		if (!entry.md5.empty() && (list[i].md5 == entry.md5))
			return AddResult(AddVerdict::AlreadyThere, i);

		return AddResult(AddVerdict::NameTaken, i);
	}

	list.push_back(entry);
	return AddResult(AddVerdict::Added, list.size() - 1);
}

size_t MoveInLoadOrder(std::vector<LoadOrderEntry> &list, size_t index, int step)
{
	if ((index >= list.size()) || (step == 0))
		return index;

	// Clamped rather than wrapped. A file that jumps from the bottom of the load order to the top
	// because somebody pressed down once too often is a server that loads in an order nobody chose.
	if ((step < 0) && (index == 0))
		return 0;
	if ((step > 0) && (index + 1 >= list.size()))
		return index;

	const size_t other = (step < 0) ? (index - 1) : (index + 1);

	const LoadOrderEntry tmp = list[index];
	list[index] = list[other];
	list[other] = tmp;

	return other;
}

size_t RemoveFromLoadOrder(std::vector<LoadOrderEntry> &list, size_t index)
{
	if (index >= list.size())
		return list.empty() ? 0 : (list.size() - 1);

	list.erase(list.begin() + static_cast<long>(index));

	if (list.empty())
		return 0;

	// The row that slid up into the gap, so the selection stays where the eye already is. Except at
	// the end, where there is no such row and the new last one is the nearest thing to it.
	return (index < list.size()) ? index : (list.size() - 1);
}

std::vector<std::string> LoadOrderPaths(const std::string &iwadPath,
	const std::vector<LoadOrderEntry> &list)
{
	std::vector<std::string> out;
	out.reserve(list.size() + 1);

	if (!iwadPath.empty())
		out.push_back(iwadPath);

	for (size_t i = 0; i < list.size(); ++i)
		out.push_back(list[i].path);

	return out;
}

} // namespace zx
