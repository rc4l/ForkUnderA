// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/controls-search/computation/controlfilter_compute.h"

#include <cstddef>

namespace zx
{

namespace
{

char Lower(char c)
{
	return ((c >= 'A') && (c <= 'Z')) ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string Trimmed(const std::string &text)
{
	const size_t first = text.find_first_not_of(" \t");
	if (first == std::string::npos)
		return std::string();
	const size_t last = text.find_last_not_of(" \t");
	return text.substr(first, last - first + 1);
}

// [Doruk-Aksoy] The classic greedy wildcard walk, backing up to the last `*` on a mismatch.
bool GlobMatch(const std::string &pattern, const std::string &text)
{
	size_t p = 0, t = 0;
	size_t star = std::string::npos, resume = 0;

	while (t < text.size())
	{
		if ((p < pattern.size()) && ((pattern[p] == '?') || (Lower(pattern[p]) == Lower(text[t]))))
		{
			++p;
			++t;
		}
		else if ((p < pattern.size()) && (pattern[p] == '*'))
		{
			star = p++;
			resume = t;
		}
		else if (star != std::string::npos)
		{
			p = star + 1;
			t = ++resume;
		}
		else
			return false;
	}

	while ((p < pattern.size()) && (pattern[p] == '*'))
		++p;
	return p == pattern.size();
}

bool RowMatches(const ControlRow &row, const std::string &pattern)
{
	return ComputeControlPatternMatch(pattern, row.label) || ComputeControlPatternMatch(pattern, row.command);
}

} // namespace

bool ComputeControlPatternMatch(const std::string &pattern, const std::string &text)
{
	const std::string trimmed = Trimmed(pattern);
	if (trimmed.empty())
		return true;

	if (trimmed.find_first_of("*?") != std::string::npos)
		return GlobMatch(trimmed, text);

	return GlobMatch("*" + trimmed + "*", text);
}

std::vector<int> ComputeControlFilter(const std::vector<ControlRow> &rows, const std::string &pattern)
{
	std::vector<int> kept;
	const int count = static_cast<int>(rows.size());

	if (Trimmed(pattern).empty())
	{
		for (int i = 0; i < count; ++i)
			kept.push_back(i);
		return kept;
	}

	int i = 0;
	while (i < count)
	{
		// [Doruk-Aksoy] A section is its leading blank lines, an optional heading, and the items up to the next blank line or heading.
		const int start = i;
		while ((i < count) && (rows[i].kind == ControlRowKind::Spacer))
			++i;
		int heading = -1;
		if ((i < count) && (rows[i].kind == ControlRowKind::Heading))
			heading = i++;

		std::vector<int> items;
		const bool bWholeSection = (heading >= 0) && ComputeControlPatternMatch(pattern, rows[heading].label);
		while ((i < count) && (rows[i].kind == ControlRowKind::Item))
		{
			if (bWholeSection || RowMatches(rows[i], pattern))
				items.push_back(i);
			++i;
		}

		if (items.empty())
			continue;

		if (!kept.empty() && (heading > start))
			kept.push_back(heading - 1);
		if (heading >= 0)
			kept.push_back(heading);
		kept.insert(kept.end(), items.begin(), items.end());
	}

	return kept;
}

} // namespace zx
