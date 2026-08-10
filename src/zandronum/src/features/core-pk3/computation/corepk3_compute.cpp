// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "corepk3_compute.h"

namespace zx
{

const char *const kCorePk3Prefix = "fua_core_";
const char *const kCorePk3Extension = ".pk3";

namespace
{

char LowerAscii(char c)
{
	return ((c >= 'A') && (c <= 'Z')) ? static_cast<char>(c - 'A' + 'a') : c;
}

bool StartsWithNoCase(const std::string &text, const char *prefix)
{
	std::string::size_type i = 0;
	for (; prefix[i] != '\0'; ++i)
	{
		if ((i >= text.size()) || (LowerAscii(text[i]) != LowerAscii(prefix[i])))
			return false;
	}
	return true;
}

bool EndsWithNoCase(const std::string &text, const char *suffix)
{
	std::string::size_type suffixLen = 0;
	while (suffix[suffixLen] != '\0')
		++suffixLen;

	if (text.size() < suffixLen)
		return false;

	const std::string::size_type offset = text.size() - suffixLen;
	for (std::string::size_type i = 0; i < suffixLen; ++i)
	{
		if (LowerAscii(text[offset + i]) != LowerAscii(suffix[i]))
			return false;
	}
	return true;
}

} // namespace

bool IsCorePk3Name(const std::string &fileName)
{
	// Extension first, then prefix. The other order leaves the length guard inside EndsWithNoCase
	// unreachable, because nothing shorter than the prefix can ever get past it.
	if (!EndsWithNoCase(fileName, kCorePk3Extension))
		return false;
	if (!StartsWithNoCase(fileName, kCorePk3Prefix))
		return false;

	// The prefix and the extension must not be the same characters: "fua_core_.pk3" names no build.
	std::string::size_type prefixLen = 0;
	while (kCorePk3Prefix[prefixLen] != '\0')
		++prefixLen;
	std::string::size_type extLen = 0;
	while (kCorePk3Extension[extLen] != '\0')
		++extLen;

	return fileName.size() > (prefixLen + extLen);
}

std::string DescribeFoundCores(const std::string &expected, const std::vector<std::string> &found)
{
	std::vector<std::string> others;
	for (std::vector<std::string>::const_iterator it = found.begin(); it != found.end(); ++it)
	{
		// The expected one is never worth listing: if it were there we would not be here, and naming
		// it as "found" would read as a contradiction of the line above.
		if (*it != expected)
			others.push_back(*it);
	}

	if (others.empty())
		return "No other fua_core_*.pk3 is beside the executable either.";

	std::string message = "Found beside the executable: ";
	for (std::vector<std::string>::size_type i = 0; i < others.size(); ++i)
	{
		if (i > 0)
			message += ", ";
		message += others[i];
	}
	message += ".\nThat is a different build's data; the executable and its pk3 ship together.";
	return message;
}

} // namespace zx
