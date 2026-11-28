// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_ACTORFLAGS_COMPUTE_H
#define ZX_ACTORFLAGS_COMPUTE_H

#include <string>

namespace zx { namespace actorflags {

// [Doruk-Aksoy] A flag's name split at its first dot, "Class.FLAG" giving the class part and the flag, a bare "FLAG" an empty class part.
struct FlagName
{
	std::string part1;
	std::string part2;
	bool dotted;
};

FlagName ComputeFlagName(const char *name);

// [Doruk-Aksoy] Whether an actor counts toward the map's kill total: a kill only while it lives, as UZDoom's ModActorFlag counts it.
bool ComputeCountsKill(bool countsAsKill, int health);

// [Doruk-Aksoy] How a map total moves when an actor stops or starts counting toward it: -1, +1 or 0.
int ComputeCountDelta(bool before, bool after);

}} // namespace zx::actorflags

#endif
