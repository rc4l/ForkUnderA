// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/iwad-registry/computation/iwadregistry_compute.h"

namespace
{

char LowerAscii(char c)
{
	return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

bool IsHexDigit(char c)
{
	return ((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f')) || ((c >= 'A') && (c <= 'F'));
}

// No trailing separator of either kind, normalised to '/' so the store has one shape everywhere.
std::string WithoutTrailingSlash(const std::string &s)
{
	std::string out = s;
	while (!out.empty() && ((out[out.size() - 1] == '/') || (out[out.size() - 1] == '\\')))
		out.erase(out.size() - 1);
	return out;
}

} // namespace

namespace zx
{

std::string NormalizeDigest(const std::string &sha256Hex)
{
	// Exactly 64, since anything else is not a SHA-256 and so not an identity to file under.
	if (sha256Hex.size() != 64)
		return std::string();

	std::string out;
	out.reserve(64);

	for (size_t i = 0; i < sha256Hex.size(); ++i)
	{
		if (!IsHexDigit(sha256Hex[i]))
			return std::string();

		out.push_back(LowerAscii(sha256Hex[i]));
	}

	return out;
}

bool IsSafeStoreName(const std::string &fileName)
{
	if (fileName.empty())
		return false;

	// "." and ".." are places rather than names, and a leaf of ".." would put the copy in the folder
	// above its digest, where every other digest's folder lives.
	if ((fileName == ".") || (fileName == ".."))
		return false;

	for (size_t i = 0; i < fileName.size(); ++i)
	{
		const char c = fileName[i];

		if ((c == '/') || (c == '\\'))
			return false;

		// A colon is a drive or an NTFS stream, and both leave the folder we meant to write in.
		if (c == ':')
			return false;

		// Control characters never appear in a name anyone typed, and do appear in one somebody
		// built to be mishandled.
		if ((unsigned char)c < 0x20)
			return false;
	}

	return true;
}

std::string IwadStoreDir(const std::string &root, const std::string &sha256Hex)
{
	const std::string digest = NormalizeDigest(sha256Hex);
	if (digest.empty() || root.empty())
		return std::string();

	return WithoutTrailingSlash(root) + "/iwads/" + digest;
}

std::string IwadStorePath(const std::string &root, const std::string &sha256Hex,
                          const std::string &fileName)
{
	if (!IsSafeStoreName(fileName))
		return std::string();

	const std::string dir = IwadStoreDir(root, sha256Hex);
	if (dir.empty())
		return std::string();

	return dir + "/" + fileName;
}

} // namespace zx
