// [MobiusU9B] See bindarchive_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "features/save-settings/computation/bindarchive_compute.h"

#include <algorithm>
#include <string>

namespace zx {

namespace {

// [MobiusU9B] An ASCII letter in lower case, as the engine's stricmp folds it.
char Lower(char c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// [MobiusU9B] ASCII case-insensitive equality, as the engine's stricmp matched a binding to its action.
bool SameIgnoringCase(const std::string &a, const std::string &b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return Lower(x) == Lower(y); });
}

} // namespace

EBindArchive ComputeBindArchive(const char *binding, const char *matchcmd, bool claimed)
{
	const bool empty = binding == nullptr || *binding == '\0';
	if (matchcmd != nullptr)
	{
		return (!empty && !claimed && SameIgnoringCase(binding, matchcmd)) ? EBindArchive::Write : EBindArchive::Skip;
	}
	if (empty)
	{
		return EBindArchive::Clear;
	}
	return claimed ? EBindArchive::Skip : EBindArchive::Write;
}

} // namespace zx
