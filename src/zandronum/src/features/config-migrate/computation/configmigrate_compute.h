// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Saved settings a newer build puts back to their defaults once, keyed by the config's Version-fua.

#ifndef ZX_CONFIGMIGRATE_COMPUTE_H
#define ZX_CONFIGMIGRATE_COMPUTE_H

#include <cstddef>
#include <vector>

namespace zx { namespace configmigrate {

// [Doruk-Aksoy] A cvar reset to its default in configs saved before `version`.
struct ConfigReset
{
	int version;
	const char *cvar;
};

// [Doruk-Aksoy] The Version-fua a config saved by this build carries, the highest version in the table.
int ComputeConfigVersion();
int ComputeConfigVersion(const ConfigReset *table, size_t count);

// [Doruk-Aksoy] The Version-fua a config last saved, 0 when it has none or it is unreadable.
int ComputeConfigLastVersion(const char *saved);

// [Doruk-Aksoy] The cvars a config last saved at `lastVersion` resets, each once, in table order.
std::vector<const char *> ComputeConfigResets(int lastVersion);
std::vector<const char *> ComputeConfigResets(const ConfigReset *table, size_t count, int lastVersion);

}} // namespace zx::configmigrate

#endif // ZX_CONFIGMIGRATE_COMPUTE_H
