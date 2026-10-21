// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/joinplan_compute.h"

namespace
{

char LowerAscii(char c)
{
	return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

std::string FoldAscii(const std::string &s)
{
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i)
		out.push_back(LowerAscii(s[i]));
	return out;
}

// A name that is empty or all spaces/tabs carries no file.
bool IsBlank(const std::string &s)
{
	for (size_t i = 0; i < s.size(); ++i)
	{
		const char c = s[i];
		if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
			return false;
	}
	return true;
}

} // namespace

namespace zx
{

std::vector<std::string> ComputeJoinWadList(const std::string &iwad,
	const std::vector<std::string> &pwads)
{
	std::vector<std::string> out;
	std::vector<std::string> seen;		// folded names already accepted

	const std::string iwadFolded = FoldAscii(iwad);
	// An empty IWAD must not suppress anything: folding "" and comparing would drop nothing anyway,
	// but being explicit keeps a future blank-name change from silently eating the whole list.
	const bool haveIwad = !IsBlank(iwad);

	for (size_t i = 0; i < pwads.size(); ++i)
	{
		const std::string &name = pwads[i];
		if (IsBlank(name))
			continue;

		const std::string folded = FoldAscii(name);
		if (haveIwad && folded == iwadFolded)
			continue;					// loaded as the IWAD, not as a mod

		bool dup = false;
		for (size_t j = 0; j < seen.size(); ++j)
		{
			if (seen[j] == folded) { dup = true; break; }
		}
		if (dup)
			continue;					// keep the first occurrence; order is semantic

		seen.push_back(folded);
		out.push_back(name);			// the server's own spelling, for the file search to resolve
	}

	return out;
}

} // namespace zx
