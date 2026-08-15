// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/flagset_compute.h"

#include <cstdio>

namespace zx
{

bool FlagIsOn(unsigned int value, unsigned int bit)
{
	return ( bit != 0 ) && (( value & bit ) == bit );
}

unsigned int FlagSet(unsigned int value, unsigned int bit, bool on)
{
	if (bit == 0)
		return value;

	return on ? ( value | bit ) : ( value & ~bit );
}

unsigned int KnownMask(const std::vector<FlagBit> &bits)
{
	unsigned int mask = 0;
	for (size_t i = 0; i < bits.size(); ++i)
		mask |= bits[i].bit;

	return mask;
}

unsigned int UnknownBits(unsigned int value, const std::vector<FlagBit> &bits)
{
	return value & ~KnownMask(bits);
}

int CountBits(unsigned int value)
{
	int n = 0;
	while (value != 0)
	{
		n += static_cast<int>(value & 1u);
		value >>= 1;
	}

	return n;
}

bool ParseFlagNumber(const std::string &text, unsigned int &out)
{
	out = 0;

	size_t from = 0;
	size_t to = text.size();

	while ((from < to) && ((text[from] == ' ') || (text[from] == '\t')))
		++from;
	while ((to > from) && ((text[to - 1] == ' ') || (text[to - 1] == '\t')))
		--to;

	// An empty box is zero rather than a refusal: clearing it to type a new number must not read as
	// an error on every keystroke of the way.
	if (from >= to)
		return true;

	unsigned long long acc = 0;
	for (size_t i = from; i < to; ++i)
	{
		const char c = text[i];
		if ((c < '0') || (c > '9'))
			return false;

		acc = acc * 10 + static_cast<unsigned long long>(c - '0');

		// [rc4l] Refused rather than wrapped. A number too big for the field is a mistake, and
		// taking it modulo 2^32 would turn one into a different, valid-looking setting.
		if (acc > 0xFFFFFFFFull)
			return false;
	}

	out = static_cast<unsigned int>(acc);
	return true;
}

std::string FormatFlagNumber(unsigned int value)
{
	char text[16];
	snprintf(text, sizeof(text), "%u", value);
	return std::string(text);
}

// The order these have been quoted in for twenty years, and the list of what a FIELD is. One array
// so the ranking and the membership test cannot disagree.
static const char *const kOrder[] = {
	"dmflags", "dmflags2", "dmflags3", "zadmflags", "compatflags", "compatflags2",
	"zacompatflags", "lmsspectatorsettings", "lmsallowedweapons",
};

bool IsFlagFieldName(const std::string &name)
{
	for (size_t i = 0; i < sizeof(kOrder) / sizeof(kOrder[0]); ++i)
	{
		if (name == kOrder[i])
			return true;
	}

	// [rc4l] sv_forbidvoteflags is a field too, and is NOT in the order above -- that list is the
	// preferred order and this one has always been appended by the "anything else found" rule. Named
	// here because a caller asking "is this a field" gets a wrong answer otherwise.
	return (name == "sv_forbidvoteflags");
}

std::vector<std::string> FlagFieldOrder(const std::vector<std::string> &found)
{

	std::vector<std::string> out;

	for (size_t i = 0; i < sizeof(kOrder) / sizeof(kOrder[0]); ++i)
	{
		for (size_t j = 0; j < found.size(); ++j)
		{
			if (found[j] == kOrder[i])
			{
				out.push_back(found[j]);
				break;
			}
		}
	}

	// Anything the engine has that this list does not name, in the order it was found. Appended
	// rather than dropped: a field nobody here has heard of is still a field somebody can set, and
	// a screen that hides it is a screen that lies about what it edits.
	for (size_t j = 0; j < found.size(); ++j)
	{
		bool seen = false;
		for (size_t k = 0; k < out.size(); ++k)
		{
			if (out[k] == found[j])
			{
				seen = true;
				break;
			}
		}

		if (!seen)
			out.push_back(found[j]);
	}

	return out;
}

} // namespace zx
