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

std::string StripColorCodes(const std::string &text)
{
	std::string out;
	out.reserve(text.size());

	// The same walk as above, keeping the letters instead of recording the offsets. Deliberately the
	// same shape: if the renderer ever accepts a third escape form, both of these have to learn it,
	// and two loops that already look alike are far likelier to be changed together.
	size_t i = 0;
	while (i < text.size())
	{
		if (text[i] == kColorEscape)
		{
			++i;								// the escape byte itself

			if ((i < text.size()) && (text[i] == '['))
			{
				// Unterminated brackets run to the end, matching the renderer -- so everything left is
				// part of the code and none of it is a name.
				while ((i < text.size()) && (text[i] != ']'))
					++i;
				if (i < text.size())
					++i;
			}
			else if (i < text.size())
			{
				++i;							// single-character form
			}
			// A trailing escape with nothing after it falls through having consumed only itself, which
			// is the point: it is dropped rather than kept as a byte nothing will ever consume.
		}
		else
		{
			out.push_back(text[i]);
			++i;
		}
	}

	return out;
}

} // namespace zx
