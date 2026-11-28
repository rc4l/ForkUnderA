// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/actor-flags/computation/actorflags_compute.h"

#include <cstring>

namespace zx { namespace actorflags {

FlagName ComputeFlagName(const char *name)
{
	const char *text = name != nullptr ? name : "";
	const char *dot = std::strchr(text, '.');
	if (dot == nullptr)
		return { std::string(text), std::string(), false };
	return { std::string(text, dot - text), std::string(dot + 1), true };
}

bool ComputeCountsKill(bool countsAsKill, int health)
{
	return countsAsKill && health > 0;
}

int ComputeCountDelta(bool before, bool after)
{
	if (before == after)
		return 0;
	return after ? 1 : -1;
}

}} // namespace zx::actorflags
