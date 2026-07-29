// [rc4l] See wadreload_compute.h. Pure logic only -- unit-tested at 100% coverage off-engine.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/wadreload/computation/wadreload_compute.h"

#include <cctype>

namespace zx { namespace wadreload {

std::string NormalizePath(const std::string &p)
{
	size_t b = 0, e = p.size();
	while (b < e && std::isspace((unsigned char)p[b])) ++b;
	while (e > b && std::isspace((unsigned char)p[e - 1])) --e;

	std::string out;
	out.reserve(e - b);
	for (size_t i = b; i < e; ++i)
	{
		char c = p[i];
		if (c == '\\') c = '/';
		out.push_back((char)std::tolower((unsigned char)c));
	}
	return out;
}

bool WantedMatchesLoaded(const std::string &curIwad, const std::vector<std::string> &curFiles,
                         const std::string &wantIwad, const std::vector<std::string> &wantFiles)
{
	if (NormalizePath(curIwad) != NormalizePath(wantIwad))
		return false;
	if (curFiles.size() != wantFiles.size())
		return false;
	for (size_t i = 0; i < curFiles.size(); ++i)
		if (NormalizePath(curFiles[i]) != NormalizePath(wantFiles[i]))
			return false;
	return true;
}

bool IsSwitchToken(const std::string &tok)
{
	return !tok.empty() && (tok[0] == '-' || tok[0] == '+');
}

std::vector<std::string> ComputeReloadArgv(const std::vector<std::string> &argv,
                                           const std::vector<std::string> &removeSwitches,
                                           const std::vector<std::string> &appendTokens)
{
	std::vector<std::string> out;
	out.reserve(argv.size() + appendTokens.size());

	size_t i = 0;
	if (!argv.empty())      // argv[0] is the program name -- always kept, never treated as a switch
	{
		out.push_back(argv[0]);
		i = 1;
	}

	while (i < argv.size())
	{
		bool remove = false;
		for (size_t s = 0; s < removeSwitches.size(); ++s)
			if (argv[i] == removeSwitches[s]) { remove = true; break; }

		if (remove)
		{
			++i;                                              // drop the switch itself
			while (i < argv.size() && !IsSwitchToken(argv[i]))
				++i;                                          // ...and each of its values, to the next switch
		}
		else
		{
			out.push_back(argv[i]);
			++i;
		}
	}

	for (size_t a = 0; a < appendTokens.size(); ++a)
		out.push_back(appendTokens[a]);
	return out;
}

ArchiveKind ClassifyArchiveMagic(const unsigned char *bytes, size_t n)
{
	if (bytes == nullptr)
		return ArchiveKind::Unknown;

	// WAD: "IWAD" or "PWAD".
	if (n >= 4 && (bytes[1] == 'W' && bytes[2] == 'A' && bytes[3] == 'D') &&
	    (bytes[0] == 'I' || bytes[0] == 'P'))
		return ArchiveKind::Wad;

	// ZIP / PK3 / PKZip: "PK" then a valid record signature (local file 03 04, central-end 05 06,
	// or spanned 07 08). Covers .pk3 and .zip.
	if (n >= 4 && bytes[0] == 'P' && bytes[1] == 'K' &&
	    ((bytes[2] == 0x03 && bytes[3] == 0x04) ||
	     (bytes[2] == 0x05 && bytes[3] == 0x06) ||
	     (bytes[2] == 0x07 && bytes[3] == 0x08)))
		return ArchiveKind::Zip;

	// 7-Zip / PK7: "7z" BC AF 27 1C.
	if (n >= 6 && bytes[0] == '7' && bytes[1] == 'z' &&
	    bytes[2] == 0xBC && bytes[3] == 0xAF && bytes[4] == 0x27 && bytes[5] == 0x1C)
		return ArchiveKind::SevenZip;

	return ArchiveKind::Unknown;
}

std::string ParseMapAssignment(const std::string &token)
{
	const char key[] = "map=";
	const size_t klen = 4;
	if (token.size() <= klen)
		return std::string();
	for (size_t i = 0; i < klen; ++i)
		if (std::tolower((unsigned char)token[i]) != key[i])
			return std::string();
	return token.substr(klen);
}

}} // namespace zx::wadreload
