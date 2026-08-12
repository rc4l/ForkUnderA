// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/addon-catalogue/computation/maprotation_compute.h"

#include <cctype>

namespace zx
{

namespace
{

char Lower(char c)
{
	return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool IsSpace(char c)
{
	return (c == ' ') || (c == '\t') || (c == '\r');
}

// [rc4l] What may follow a map name and still leave it a map name. Anything else and the line is
// left alone: `addmapx foo` is not an addmap, and neither is a word that merely starts with one.
bool IsNameChar(char c)
{
	return !IsSpace(c) && (c != '\n') && (c != '/') && (c != '"');
}

} // namespace

std::vector<std::string> MapsInRotation(const std::string &cfgText)
{
	static const char kCommand[] = "addmap";
	const size_t kCommandLen = sizeof(kCommand) - 1;

	std::vector<std::string> maps;

	size_t at = 0;
	while (at <= cfgText.size())
	{
		size_t end = cfgText.find('\n', at);
		if (end == std::string::npos)
			end = cfgText.size();

		size_t p = at;
		at = end + 1;

		while ((p < end) && IsSpace(cfgText[p]))
			++p;

		// A comment, whatever is behind it. The catalogue's cfgs are heavily commented and several
		// of those comments talk about maps.
		if (((end - p) >= 2) && (cfgText[p] == '/') && (cfgText[p + 1] == '/'))
			continue;

		if ((end - p) <= kCommandLen)
			continue;

		bool bMatch = true;
		for (size_t i = 0; i < kCommandLen; ++i)
		{
			if (Lower(cfgText[p + i]) != kCommand[i])
			{
				bMatch = false;
				break;
			}
		}

		if (!bMatch)
			continue;

		p += kCommandLen;

		// The command has to END here. Without this, `addmapcycle` would read as `addmap cycle`.
		if (!IsSpace(cfgText[p]))
			continue;

		while ((p < end) && IsSpace(cfgText[p]))
			++p;

		const size_t nameAt = p;
		while ((p < end) && IsNameChar(cfgText[p]))
			++p;

		if (p == nameAt)
			continue;			// `addmap` with nothing after it

		const std::string name = cfgText.substr(nameAt, p - nameAt);

		bool bSeen = false;
		for (size_t i = 0; i < maps.size(); ++i)
		{
			if (maps[i] == name)
			{
				bSeen = true;
				break;
			}
		}

		if (!bSeen)
			maps.push_back(name);
	}

	return maps;
}

size_t MapRotationStart(const std::vector<std::string> &maps, const std::string &wanted)
{
	if (wanted.empty())
		return 0;

	for (size_t i = 0; i < maps.size(); ++i)
	{
		if (maps[i].size() != wanted.size())
			continue;

		bool same = true;
		for (size_t c = 0; c < wanted.size(); ++c)
		{
			if (Lower(maps[i][c]) != Lower(wanted[c]))
			{
				same = false;
				break;
			}
		}

		if (same)
			return i;
	}

	return 0;
}

} // namespace zx
