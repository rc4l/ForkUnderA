// SPDX-License-Identifier: GPL-3.0-or-later

#include "features/handoff/computation/namelist_compute.h"

#include <cctype>

namespace zx { namespace handoff {

namespace {

bool SameName(const std::string &a, const char *b)
{
	size_t i = 0;
	for (; i < a.size() && b[i] != '\0'; i++)
	{
		if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
	}
	return i == a.size() && b[i] == '\0';
}

} // namespace

bool ComputeNameListHas(const std::vector<std::string> &names, const char *name)
{
	if (name == nullptr) return false;
	for (const std::string &held : names)
	{
		if (SameName(held, name)) return true;
	}
	return false;
}

void ComputeNameListAdd(std::vector<std::string> &names, const char *name)
{
	if (name == nullptr || ComputeNameListHas(names, name)) return;
	names.push_back(name);
}

}} // namespace zx::handoff
