// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/iwad-startup/computation/iwadstartup_compute.h"

#include <cctype>
#include <cstddef>

namespace zx
{

namespace
{

std::string Lower(const std::string &s)
{
	std::string out = s;
	for (size_t i = 0; i < out.size(); ++i)
		out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
	return out;
}

bool InSeenList(const std::string &seen, const std::string &base)
{
	const std::string wanted = Lower(base);
	size_t start = 0;
	while (start <= seen.size())
	{
		size_t end = seen.find(';', start);
		if (end == std::string::npos)
			end = seen.size();
		if (seen.compare(start, end - start, wanted) == 0)
			return true;
		start = end + 1;
	}
	return false;
}

int IndexOfBase(const std::vector<IwadStartupCandidate> &found, const std::string &base)
{
	if (base.empty())
		return -1;
	const std::string wanted = Lower(base);
	for (size_t i = 0; i < found.size(); ++i)
	{
		if (Lower(found[i].base) == wanted)
			return static_cast<int>(i);
	}
	return -1;
}

} // namespace

bool ComputeIsFreedoomIwad(const std::string &autoname)
{
	return Lower(autoname).compare(0, 13, "doom.freedoom") == 0;
}

IwadStartupChoice ComputeIwadStartup(const std::vector<IwadStartupCandidate> &found,
	const std::string &savedDefault, const std::string &seen, bool queryiwad)
{
	IwadStartupChoice choice = { 0, false };
	if (found.size() < 2)
		return choice;

	const int saved = IndexOfBase(found, savedDefault);
	int firstOwn = -1, newOwn = -1, phase2 = -1;
	for (size_t i = 0; i < found.size(); ++i)
	{
		const int index = static_cast<int>(i);
		if (ComputeIsFreedoomIwad(found[i].autoname))
		{
			if (phase2 < 0 && Lower(found[i].autoname) == "doom.freedoom.phase2")
				phase2 = index;
			continue;
		}
		if (firstOwn < 0)
			firstOwn = index;
		if (newOwn < 0 && !InSeenList(seen, found[i].base))
			newOwn = index;
	}

	// [rc4l] Only the shipped Freedoom: no choice worth a dialog, so Phase 2 unless the player saved another.
	if (firstOwn < 0)
	{
		choice.pick = (saved >= 0) ? saved : ((phase2 >= 0) ? phase2 : 0);
		return choice;
	}

	const bool savedOwn = (saved >= 0) && !ComputeIsFreedoomIwad(found[saved].autoname);
	if (savedOwn)
		choice.pick = saved;
	else if (newOwn >= 0)
		choice.pick = newOwn;
	else
		choice.pick = (saved >= 0) ? saved : firstOwn;
	choice.ask = queryiwad || (newOwn >= 0);
	return choice;
}

std::string ComputeIwadSeenList(const std::vector<IwadStartupCandidate> &found)
{
	std::string out;
	for (size_t i = 0; i < found.size(); ++i)
	{
		if (i > 0)
			out += ';';
		out += Lower(found[i].base);
	}
	return out;
}

std::string ComputeFindNameIgnoringCase(const std::vector<std::string> &entries, const std::string &name)
{
	const std::string wanted = Lower(name);
	for (size_t i = 0; i < entries.size(); ++i)
	{
		if (Lower(entries[i]) == wanted)
			return entries[i];
	}
	return std::string();
}

} // namespace zx
