// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/dmflagnames_compute.h"

namespace
{

int PopCount(unsigned int v)
{
	int n = 0;
	while (v)
	{
		v &= v - 1;
		++n;
	}
	return n;
}

} // namespace

namespace zx
{

std::vector<std::string> ComputeSetFlagNames(const FlagNameEntry *entries, int entryCount,
	const std::vector<int> &words)
{
	std::vector<std::string> out;
	if ((entries == NULL) || (entryCount <= 0))
		return out;

	// What each word has already had claimed by a wider mask. Sized to the words we were actually
	// given, so a table entry naming a word the server never sent simply has nowhere to match.
	std::vector<unsigned int> consumed(words.size(), 0u);

	// Widest masks first. Order within a width does not matter -- two entries of the same width that
	// both matched would be genuinely independent flags.
	int maxBits = 0;
	for (int i = 0; i < entryCount; ++i)
	{
		const int bits = PopCount(entries[i].value);
		if (bits > maxBits)
			maxBits = bits;
	}

	std::vector<const FlagNameEntry *> matched;
	for (int width = maxBits; width >= 1; --width)
	{
		for (int i = 0; i < entryCount; ++i)
		{
			const FlagNameEntry &e = entries[i];
			if (PopCount(e.value) != width)
				continue;
			if ((e.word < 0) || (e.word >= static_cast<int>(words.size())))
				continue;			// the server sent fewer words than the table knows about

			const unsigned int w = static_cast<unsigned int>(words[e.word]);
			if ((w & e.value) != e.value)
				continue;			// not all of this mask's bits are set

			// Every bit already claimed by a wider mask -- this is the narrow half of a field that a
			// wider entry already named, not a flag of its own.
			if ((e.value & ~consumed[e.word]) == 0u)
				continue;

			consumed[e.word] |= e.value;
			matched.push_back(&e);
		}
	}

	// Emitted in table order rather than match order, so the panel lists flags the way the header
	// declares them instead of widest-first.
	for (int i = 0; i < entryCount; ++i)
	{
		for (size_t m = 0; m < matched.size(); ++m)
		{
			if (matched[m] == &entries[i])
			{
				out.push_back(entries[i].name != NULL ? entries[i].name : "");
				break;
			}
		}
	}
	return out;
}

} // namespace zx
