// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/wad-download/computation/iwadsubstitute_compute.h"

// [rc4l] kIwadSubstitutes, generated at build time from the repo-root iwadsubstitutes.txt by
// tools/gen-wadlists.cmake.
#include "zx_waddownload_lists.h"

namespace
{

char LowerAscii(char c)
{
	return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

std::string ToLower(const std::string &s)
{
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i)
		out.push_back(LowerAscii(s[i]));
	return out;
}

} // namespace

namespace zx
{

std::string FreeIwadSubstituteFor(const std::string &wantedIwadName)
{
	const std::string lowered = ToLower(wantedIwadName);
	for (size_t i = 0; i < sizeof kIwadSubstitutes / sizeof kIwadSubstitutes[0]; ++i)
	{
		if (lowered == kIwadSubstitutes[i][0])
			return kIwadSubstitutes[i][1];
	}
	return std::string();
}

} // namespace zx
