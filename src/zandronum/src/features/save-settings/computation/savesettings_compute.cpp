// [MobiusU9B] See savesettings_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "features/save-settings/computation/savesettings_compute.h"

namespace zx {

std::string ComputeSaveSettingsMessage(bool saved, const char *error)
{
	if (saved)
		return "Settings saved.";

	const bool hasReason = error != nullptr && error[0] != '\0';
	return std::string("Could not save settings:\n") + (hasReason ? error : "unknown error");
}

} // namespace zx
