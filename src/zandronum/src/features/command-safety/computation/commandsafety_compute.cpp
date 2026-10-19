// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/command-safety/computation/commandsafety_compute.h"

#include <cctype>
#include <cstring>

namespace zx {

namespace {

bool EndsWithNoCase(const std::string &text, const std::string &end)
{
	if (end.size() > text.size())
		return false;
	const size_t from = text.size() - end.size();
	for (size_t i = 0; i < end.size(); i++)
	{
		if (std::tolower((unsigned char)text[from + i]) != std::tolower((unsigned char)end[i]))
			return false;
	}
	return true;
}

} // namespace

std::string ComputeOwnExtension(const std::string &name, std::initializer_list<const char *> exts)
{
	for (const char *ext : exts)
	{
		if (EndsWithNoCase(name, ext))
			return name;
	}
	return name + *exts.begin();
}

std::string ComputePlainFileName(const std::string &name)
{
	std::string plain = name;
	for (char &c : plain)
	{
		if ((unsigned char)c < ' ' || std::strchr(".:/\\*?\"<>|", c) != nullptr)
			c = '-';
	}
	return plain;
}

bool ComputeBareFileName(const std::string &name)
{
	return name.find_first_of("/\\:") == std::string::npos;
}

std::string ComputeFileBeside(const std::string &anchor, const std::string &name, const std::string &ext)
{
	const std::string bare = EndsWithNoCase(name, ext) ? name.substr(0, name.size() - ext.size()) : name;
	const std::string plain = ComputePlainFileName(bare);
	if (plain.empty())
		return std::string();
	const size_t slash = anchor.find_last_of("/\\");
	return anchor.substr(0, slash == std::string::npos ? 0 : slash + 1) + plain + ext;
}

bool ComputeMenuCommandUnsafe(bool confirmed, int lumpFile, int iwadFile)
{
	return !confirmed || lumpFile < 0 || lumpFile >= iwadFile;
}

} // namespace zx
