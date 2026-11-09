// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] See configmigrate_compute.h.

#include "features/config-migrate/computation/configmigrate_compute.h"

#include <cstdlib>
#include <cstring>

namespace zx { namespace configmigrate {

namespace
{
	// [Doruk-Aksoy] One row per reset; a new row takes the next version.
	const ConfigReset kResets[] =
	{
		{ 1, "cl_fua_replay" },   // [Doruk-Aksoy] Instant replay turned default off, so configs that saved it on go back off.
	};
	const size_t kResetCount = sizeof(kResets) / sizeof(kResets[0]);
}

int ComputeConfigVersion()
{
	return ComputeConfigVersion(kResets, kResetCount);
}

int ComputeConfigVersion(const ConfigReset *table, size_t count)
{
	int version = 0;
	for (size_t i = 0; i < count; i++)
	{
		if (table[i].version > version) version = table[i].version;
	}
	return version;
}

int ComputeConfigLastVersion(const char *saved)
{
	if (saved == NULL) return 0;
	char *end = NULL;
	const long value = strtol(saved, &end, 10);
	if (end == saved || *end != '\0' || value < 0 || value > 1000000) return 0;
	return static_cast<int>(value);
}

std::vector<const char *> ComputeConfigResets(int lastVersion)
{
	return ComputeConfigResets(kResets, kResetCount, lastVersion);
}

std::vector<const char *> ComputeConfigResets(const ConfigReset *table, size_t count, int lastVersion)
{
	std::vector<const char *> out;
	for (size_t i = 0; i < count; i++)
	{
		if (table[i].version <= lastVersion) continue;
		bool seen = false;
		for (const char *name : out)
		{
			if (strcmp(name, table[i].cvar) == 0) { seen = true; break; }
		}
		if (!seen) out.push_back(table[i].cvar);
	}
	return out;
}

}} // namespace zx::configmigrate
