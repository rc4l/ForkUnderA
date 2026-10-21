// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See maplist_compute.h for the two rules and why there are two of them.

#include "features/server-browser/computation/maplist_compute.h"

namespace zx
{

namespace
{

char Upper(char c)
{
	return ((c >= 'a') && (c <= 'z')) ? static_cast<char>(c - 'a' + 'A') : c;
}

std::string UpperOf(const std::string &s)
{
	std::string out = s;
	for (size_t i = 0; i < out.size(); ++i)
		out[i] = Upper(out[i]);

	return out;
}

bool IsMapDataMarker(const std::string &name)
{
	const std::string up = UpperOf(name);
	return (up == "THINGS") || (up == "TEXTMAP");
}

// "maps/MAP01.wad" -> "MAP01". Empty when the path is not a map file under maps/.
std::string MapFromArchivePath(const std::string &path)
{
	const std::string up = UpperOf(path);

	// Only the top-level maps/ folder, which is where the engine looks.
	if (up.compare(0, 5, "MAPS/") != 0)
		return std::string();

	const std::string rest = up.substr(5);

	// One level down only: maps/doom2/map01.wad is not a map the engine would find.
	if (rest.find('/') != std::string::npos)
		return std::string();

	const size_t dot = rest.rfind('.');
	if (dot == std::string::npos)
		return std::string();

	// Both extensions the engine looks up by full name: .map is how a Build map is stored, and
	// P_OpenMapData checks for it beside the wad.
	const std::string ext = rest.substr(dot);
	if ((ext != ".WAD") && (ext != ".MAP"))
		return std::string();

	return rest.substr(0, dot);
}

} // namespace

bool IsMapName(const std::string &name)
{
	if (name.empty() || (name.size() > 8))
		return false;

	for (size_t i = 0; i < name.size(); ++i)
	{
		const char c = name[i];

		const bool ok = ((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')) ||
			((c >= '0') && (c <= '9')) || (c == '_');

		if (!ok)
			return false;
	}

	return true;
}

std::vector<std::string> MapsInFile(const std::vector<LumpEntry> &lumps)
{
	std::vector<std::string> out;

	for (size_t i = 0; i < lumps.size(); ++i)
	{
		// The archive rule first: an entry with a path is not a WAD lump and cannot be a header.
		if (!lumps[i].path.empty())
		{
			const std::string fromPath = MapFromArchivePath(lumps[i].path);

			if (!fromPath.empty() && IsMapName(fromPath))
				MergeMaps(out, std::vector<std::string>(1, fromPath));

			continue;
		}

		// The WAD rule: this lump is a map when the next one starts a map's data.
		if ((i + 1) >= lumps.size())
			continue;

		if (!IsMapDataMarker(lumps[i + 1].name))
			continue;

		if (!IsMapName(lumps[i].name))
			continue;

		MergeMaps(out, std::vector<std::string>(1, UpperOf(lumps[i].name)));
	}

	return out;
}

void MergeMaps(std::vector<std::string> &into, const std::vector<std::string> &incoming)
{
	for (size_t i = 0; i < incoming.size(); ++i)
	{
		bool seen = false;

		for (size_t j = 0; j < into.size(); ++j)
		{
			if (into[j] == incoming[i])
			{
				seen = true;
				break;
			}
		}

		if (!seen)
			into.push_back(incoming[i]);
	}
}

} // namespace zx
