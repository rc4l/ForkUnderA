// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/cvar-alias/computation/cvaralias_compute.h"

namespace zx { namespace cvaralias {

namespace {

struct Alias { const char *alias, *target; };

// [rc4l] One line per alias, the fork's name first.
const Alias kAliases[] = {
	{ "fua_vid_fps", "vid_fps" },
};

char Lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

// [rc4l] Whether the first `len` characters of `name` are all of `alias`, ignoring case.
bool SameName(const char *name, size_t len, const char *alias)
{
	size_t i = 0;
	for (; i < len; i++)
		if (alias[i] == 0 || Lower(name[i]) != Lower(alias[i])) return false;
	return alias[i] == 0;
}

} // namespace

const char *ComputeCVarAliasTarget(const char *name, size_t len)
{
	if (name == NULL) return NULL;
	for (const Alias &a : kAliases)
		if (SameName(name, len, a.alias)) return a.target;
	return NULL;
}

}} // namespace zx::cvaralias
