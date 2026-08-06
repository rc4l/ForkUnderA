// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/serversort_compute.h"

namespace zx
{

namespace
{
// Matches TEXTCOLOR_ESCAPE in v_text.h; spelled out so this unit stays engine-free, exactly as
// colortext_compute.h does.
const char kEscape = '\034';
} // namespace

std::string ServerSortKey(const std::string &name)
{
	std::string out;
	out.reserve(name.size());

	for (size_t i = 0; i < name.size(); ++i)
	{
		if (name[i] == kEscape)
		{
			// Skip the escape and the code it takes. Both forms the renderer accepts: one character,
			// or a bracketed name -- the same walk colortext_compute does, for the same reason.
			++i;
			if (i >= name.size())
				break;

			if (name[i] == '[')
			{
				while ((i < name.size()) && (name[i] != ']'))
					++i;
			}
			continue;
		}

		char c = name[i];
		if ((c >= 'A') && (c <= 'Z'))
			c = static_cast<char>(c - 'A' + 'a');
		out.push_back(c);
	}

	return out;
}

int CompareServers(int playersA, const std::string &nameA, int playersB, const std::string &nameB)
{
	// Busiest first. Full counts as busy: it is evidence about where people play, which is what the
	// list is being read for.
	if (playersA != playersB)
		return (playersB - playersA);

	const std::string keyA = ServerSortKey(nameA);
	const std::string keyB = ServerSortKey(nameB);

	if (keyA < keyB)
		return -1;
	if (keyB < keyA)
		return 1;

	return 0;
}

} // namespace zx
