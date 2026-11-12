// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "menu/callvotemenu_compute.h"

#include <cctype>

namespace zx {

namespace {

bool SameName(const std::string &a, const std::string &b)
{
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
	return true;
}

} // namespace

std::vector<unsigned> ComputeMissingVoteMenus(const std::vector<std::string> &itemActions, const std::vector<std::string> &voteMenus)
{
	std::vector<unsigned> missing;
	for (unsigned i = 0; i < voteMenus.size(); ++i)
	{
		if (voteMenus[i].empty()) continue;
		bool present = false;
		for (const std::string &action : itemActions)
			if (SameName(action, voteMenus[i])) { present = true; break; }
		if (!present) missing.push_back(i);
	}
	return missing;
}

} // namespace zx
