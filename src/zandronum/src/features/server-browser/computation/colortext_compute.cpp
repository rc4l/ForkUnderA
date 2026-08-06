// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/colortext_compute.h"

namespace zx
{

std::vector<size_t> ComputeColorSafeCutPoints(const std::string &text)
{
	std::vector<size_t> out;
	out.push_back(0);

	size_t i = 0;
	while (i < text.size())
	{
		if (text[i] == kColorEscape)
		{
			++i;								// the escape byte itself

			if ((i < text.size()) && (text[i] == '['))
			{
				// Bracketed form: everything up to and including the ']' belongs to the code. An
				// unterminated bracket runs to the end of the string, which is what the renderer does
				// with it too -- so the only safe cut is past all of it.
				while ((i < text.size()) && (text[i] != ']'))
					++i;
				if (i < text.size())
					++i;
			}
			else if (i < text.size())
			{
				++i;							// single-character form
			}
		}
		else
		{
			++i;
		}
		out.push_back(i);
	}

	return out;
}

} // namespace zx
